// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "ModelSkinInput.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <iterator>
#include <map>

namespace War3Map::Render {
namespace {
bool Coordinate(float v) { return std::isfinite(v) && std::abs(v) <= 1.0e7f; }
bool Unit(float v) { return std::isfinite(v) && v >= 0 && v <= 1; }
template<class T> void Copy(std::vector<T>& target, const T* source, std::uint32_t count) {
    if (count) target.assign(source, source + count);
}
}
bool ValidateModelGeometry(const ModelScene& scene, std::uint32_t textureCount) {
    for (const auto& vertex : scene.vertices) {
        const float values[]{vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v};
        for (const auto value : values) if (!Coordinate(value)) return false;
    }
    for (const auto& layer : scene.layers) {
        if ((layer.textureIndex != WAR3_RENDER_MODEL_NO_TEXTURE && layer.textureIndex >= textureCount)
            || layer.filterMode > 6 || layer.flags > 7 || !Unit(layer.alpha)) return false;
    }
    for (const auto& mesh : scene.meshes) {
        if (mesh.indexCount == 0 || mesh.indexCount % 3 != 0 || mesh.firstIndex > scene.indices.size()
            || mesh.indexCount > scene.indices.size() - mesh.firstIndex || mesh.baseVertex >= std::max<std::size_t>(scene.vertices.size(), 1)
            || mesh.firstLayer > scene.layers.size() || mesh.layerCount > scene.layers.size() - mesh.firstLayer) return false;
        for (auto index = mesh.firstIndex; index < mesh.firstIndex + mesh.indexCount; ++index)
            if (static_cast<std::uint64_t>(mesh.baseVertex) + scene.indices[index] >= scene.vertices.size()) return false;
    }
    for (const auto& model : scene.models)
        if (model.firstMesh > scene.meshes.size() || model.meshCount > scene.meshes.size() - model.firstMesh) return false;
    std::uint32_t previous = 0;
    for (const auto& instance : scene.instances) {
        if (instance.model >= scene.models.size() || instance.model < previous) return false;
        previous = instance.model;
        for (const auto value : instance.transform) if (!Coordinate(value)) return false;
        for (const auto value : instance.color) if (!Unit(value)) return false;
    }
    return true;
}
bool CopySkinUpdate(const War3RenderSkinUpdateV2& input, const ModelScene& scene, SkinPose& pose) {
    const bool shared = input.matrixFormat == WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4_SHARED_RANGES;
    if (input.structSize < sizeof(input) || !input.sceneVersion || !input.contentVersion || input.reserved
        || input.groupCount > 1048576 || input.alphaCount > 1048576
        || (!shared && input.matrixFormat != WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4)
        || input.matrixStrideBytes != sizeof(War3RenderGroupMatrixV2) || input.worldTransformApplied
        || (input.instanceCount && !input.instances) || (input.groupCount && !input.groups)
        || (input.alphaCount && !input.alphas)) return false;
    const War3RenderDynamicColoredSkinUpdateV2* colored = nullptr;
    if (input.structSize != sizeof(input)) {
        if (input.structSize != sizeof(War3RenderDynamicColoredSkinUpdateV2)) return false;
        colored = reinterpret_cast<const War3RenderDynamicColoredSkinUpdateV2*>(&input);
        if (colored->colorFormat != WAR3_RENDER_DYNAMIC_MESH_RGB_V1 || colored->colorCount != input.alphaCount ||
            (colored->colorCount != 0 && !colored->colors)) return false;
    }
    const std::uint64_t bytes = static_cast<std::uint64_t>(input.instanceCount) * sizeof(War3RenderSkinnedModelInstanceV2)
        + static_cast<std::uint64_t>(input.groupCount) * sizeof(War3RenderGroupMatrixV2)
        + static_cast<std::uint64_t>(input.alphaCount) * (sizeof(float) + (colored ? sizeof(War3RenderMeshColorV2) : 0));
    if (bytes > 64ull * 1024 * 1024 || scene.modelGroupCounts.size() != scene.models.size()) return false;
    Copy(pose.instances, input.instances, input.instanceCount);
    Copy(pose.groups, input.groups, input.groupCount); Copy(pose.alphas, input.alphas, input.alphaCount);
    if (colored) {
        for (std::uint32_t i=0;i<colored->colorCount;++i) for (const auto v:colored->colors[i].rgb) if (!Unit(v)) return false;
        Copy(pose.colors,colored->colors,colored->colorCount);
    }
    pose.sceneVersion = input.sceneVersion; pose.contentVersion = input.contentVersion;
    pose.matrixFormat = input.matrixFormat;
    std::uint64_t groupEnd = 0, alphaEnd = 0;
    // 只允许同模型完整片段别名；跨模型和部分重叠都不是合法共享。
    struct Slice { std::uint32_t end, model; };
    std::map<std::uint32_t, Slice> groupSlices, alphaSlices;
    const auto bindSlice = [](auto& slices, std::uint32_t first, std::uint32_t count,
        std::size_t capacity, std::uint32_t model) {
        if (!count || first > capacity || count > capacity - first) return false;
        const auto end = first + count;
        const auto next = slices.lower_bound(first);
        if (next != slices.end() && next->first == first)
            return next->second.end == end && next->second.model == model;
        if ((next != slices.end() && next->first < end)
            || (next != slices.begin() && std::prev(next)->second.end > first)) return false;
        slices.emplace_hint(next, first, Slice{end, model});
        return true;
    };
    std::uint32_t previous = 0;
    for (const auto& instance : pose.instances) {
        const auto model = instance.instance.model;
        if (model >= scene.models.size() || model < previous)
            return false;
        previous = model;
        if (shared) {
            if (!bindSlice(groupSlices, instance.firstGroup, scene.modelGroupCounts[model], pose.groups.size(), model)
                || !bindSlice(alphaSlices, instance.firstAlpha, scene.models[model].meshCount, pose.alphas.size(), model)) return false;
        } else {
            if (instance.firstGroup != groupEnd || instance.firstAlpha != alphaEnd) return false;
            groupEnd += scene.modelGroupCounts[model]; alphaEnd += scene.models[model].meshCount;
            if (groupEnd > pose.groups.size() || alphaEnd > pose.alphas.size()) return false;
        }
        for (const auto value : instance.instance.transform) if (!Coordinate(value)) return false;
        for (const auto value : instance.instance.color) if (!Unit(value)) return false;
    }
    if (!shared && (groupEnd != pose.groups.size() || alphaEnd != pose.alphas.size())) return false;
    for (const auto& group : pose.groups) for (const auto value : group.values) if (!Coordinate(value)) return false;
    for (const auto alpha : pose.alphas) if (!std::isfinite(alpha)) return false;
    if (scene.initialSkin) {
        const auto& initial = *scene.initialSkin;
        if (pose.sceneVersion != initial.sceneVersion || pose.matrixFormat != initial.matrixFormat || pose.instances.size() != initial.instances.size()
            || pose.groups.size() != initial.groups.size() || pose.alphas.size() != initial.alphas.size() || pose.colors.size() != initial.colors.size()) return false;
        for (std::size_t i = 0; i < pose.instances.size(); ++i) {
            const auto& a = pose.instances[i]; const auto& b = initial.instances[i];
            if (a.instance.model != b.instance.model || (!shared && (a.firstGroup != b.firstGroup || a.firstAlpha != b.firstAlpha))) return false;
        }
    }
    return true;
}
bool CopySkinnedModelInput(const War3RenderSkinnedModelSceneV2& input, ModelScene& scene, std::uint64_t& payloadBytes) {
    if (input.structSize < sizeof(input) || !input.initialPose || input.vertexCount > 2097152 || input.indexCount > 6291456
        || input.layerCount > 131072 || input.meshCount > 65536 || input.modelCount > 16384 || input.textureCount > 4096
        || (input.vertexCount && !input.vertices) || (input.indexCount && !input.indices)
        || (input.layerCount && !input.layers) || (input.meshCount && !input.meshes) || (input.modelCount && !input.models)) return false;
    // 只有完整的新尾部才读取颜色；未知短尺寸拒绝，不猜布局。
    if (input.structSize != sizeof(input)) {
        if (input.structSize < sizeof(War3RenderColoredSkinnedSceneV2)) return false;
        const auto& colored = *reinterpret_cast<const War3RenderColoredSkinnedSceneV2*>(&input);
        if (colored.colorFormat != WAR3_RENDER_STATIC_MESH_RGB_V1 || colored.colorCount != input.meshCount ||
            (colored.colorCount != 0 && !colored.colors)) return false;
        for (std::uint32_t i = 0; i < colored.colorCount; ++i)
            for (const auto v : colored.colors[i].rgb) if (!Unit(v)) return false;
        Copy(scene.meshColors, colored.colors, colored.colorCount);
    }
    scene.vertices.reserve(input.vertexCount); scene.vertexGroups.reserve(input.vertexCount);
    for (std::uint32_t i = 0; i < input.vertexCount; ++i) {
        scene.vertices.push_back(input.vertices[i].vertex); scene.vertexGroups.push_back(input.vertices[i].group);
    }
    Copy(scene.indices, input.indices, input.indexCount); Copy(scene.layers, input.layers, input.layerCount);
    scene.meshes.reserve(input.meshCount); scene.meshGroups.reserve(input.meshCount);
    std::uint64_t indexEnd = 0, drawnLayers = 0;
    for (std::uint32_t i = 0; i < input.meshCount; ++i) {
        const auto& mesh = input.meshes[i];
        // 限制校验/绘制工作量：网格按索引连续覆盖，无重叠；重复材质使用 layer 而非重扫网格。
        if (!mesh.groupCount || !mesh.mesh.layerCount || mesh.mesh.firstIndex != indexEnd) return false;
        indexEnd += mesh.mesh.indexCount; drawnLayers += mesh.mesh.layerCount;
        if (indexEnd > input.indexCount || drawnLayers > 131072) return false;
        scene.meshes.push_back(mesh.mesh); scene.meshGroups.emplace_back(mesh.firstGroup, mesh.groupCount);
    }
    if (indexEnd != input.indexCount) return false;
    std::uint64_t meshEnd = 0;
    for (std::uint32_t i = 0; i < input.modelCount; ++i) {
        const auto& model = input.models[i];
        if (!model.groupCount || !model.model.meshCount || model.model.firstMesh != meshEnd) return false;
        meshEnd += model.model.meshCount;
        if (meshEnd > scene.meshes.size()) return false;
        scene.models.push_back(model.model); scene.modelGroupCounts.push_back(model.groupCount);
    }
    if (meshEnd != input.meshCount) return false;
    auto pose = std::make_shared<SkinPose>();
    if (!CopySkinUpdate(*input.initialPose, scene, *pose)) return false;
    if (pose->instances.empty() && (input.vertexCount || input.indexCount || input.layerCount || input.meshCount
        || input.modelCount || input.textureCount)) return false;
    for (const auto& instance : pose->instances) scene.instances.push_back(instance.instance);
    if (!ValidateModelGeometry(scene, input.textureCount)) return false;
    for (std::size_t m = 0; m < scene.models.size(); ++m) {
        const auto& range = scene.models[m];
        for (auto i = range.firstMesh; i < range.firstMesh + range.meshCount; ++i) {
            const auto [first, count] = scene.meshGroups[i];
            if (first > scene.modelGroupCounts[m] || count > scene.modelGroupCounts[m] - first) return false;
            const auto& mesh = scene.meshes[i];
            for (auto j = mesh.firstIndex; j < mesh.firstIndex + mesh.indexCount; ++j)
                if (scene.vertexGroups[mesh.baseVertex + scene.indices[j]] >= count) return false;
        }
    }
    payloadBytes = static_cast<std::uint64_t>(input.vertexCount) * sizeof(War3RenderSkinnedModelVertexV2)
        + static_cast<std::uint64_t>(input.indexCount) * sizeof(std::uint16_t)
        + static_cast<std::uint64_t>(input.layerCount) * sizeof(War3RenderModelLayerV2)
        + static_cast<std::uint64_t>(input.meshCount) * sizeof(War3RenderSkinnedModelMeshV2)
        + static_cast<std::uint64_t>(input.modelCount) * sizeof(War3RenderSkinnedModelV2)
        + scene.meshColors.size() * sizeof(War3RenderMeshColorV2)
        + pose->instances.size() * (sizeof(War3RenderSkinnedModelInstanceV2) + sizeof(War3RenderModelInstanceV2))
        + pose->groups.size() * sizeof(War3RenderGroupMatrixV2) + pose->alphas.size() * sizeof(float) + pose->colors.size() * sizeof(War3RenderMeshColorV2);
    scene.initialSkin = std::move(pose);
    return payloadBytes <= 320ull * 1024 * 1024;
}
bool CopySkinRoster(const War3RenderSkinUpdateV2& input, const ModelScene& residentStatic, ModelScene& output) {
    if (!residentStatic.initialSkin || !input.sceneVersion || !input.contentVersion
        || input.sceneVersion <= residentStatic.initialSkin->sceneVersion
        || input.matrixFormat != residentStatic.initialSkin->matrixFormat
        || (input.structSize == sizeof(War3RenderDynamicColoredSkinUpdateV2)) != !residentStatic.initialSkin->colors.empty()) return false;
    ModelScene candidate;
    candidate.models = residentStatic.models; candidate.modelGroupCounts = residentStatic.modelGroupCounts;
    candidate.meshes = residentStatic.meshes; candidate.layers = residentStatic.layers;
    candidate.meshGroups = residentStatic.meshGroups;
    // 新槽的初始化沿用首次CopySkinnedModelInput的完整数值/片段校验，绝不修改旧initialSkin。
    auto initial = std::make_shared<SkinPose>();
    if (!CopySkinUpdate(input, candidate, *initial)) return false;
    candidate.instances.reserve(initial->instances.size());
    for (const auto& instance : initial->instances) candidate.instances.push_back(instance.instance);
    candidate.initialSkin = std::move(initial);
    output = std::move(candidate);
    return true;
}
static_assert(sizeof(War3RenderSkinnedModelVertexV2) == 36);
static_assert(sizeof(War3RenderSkinnedModelInstanceV2) == 72);
static_assert(sizeof(War3RenderGroupMatrixV2) == 48);
}
