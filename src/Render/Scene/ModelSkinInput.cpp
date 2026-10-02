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
bool SkinModes(const War3RenderSkinUpdateV2& input,
    const War3RenderDynamicColoredSkinUpdateV2*& colored,
    const War3RenderMaterialSkinUpdateV2*& material,
    const War3RenderMaterialStateSkinUpdateV2*& state) {
    colored = nullptr; material = nullptr; state = nullptr;
    if (input.structSize == sizeof(input)) return true;
    if (input.structSize == sizeof(War3RenderMaterialStateSkinUpdateV2)) {
        state = reinterpret_cast<const War3RenderMaterialStateSkinUpdateV2*>(&input);
        if (state->materialStateFormat != WAR3_RENDER_DYNAMIC_MATERIAL_STATE_V1 || state->reserved
            || state->materialStateCount > 1048576 || state->materialInstanceSliceCount != input.instanceCount
            || (state->materialStateCount && !state->materialStates)
            || (state->materialInstanceSliceCount && !state->materialInstanceSlices)) return false;
        material = &state->base;
    } else if (input.structSize == sizeof(War3RenderMaterialSkinUpdateV2)) {
        material = reinterpret_cast<const War3RenderMaterialSkinUpdateV2*>(&input);
    } else if (input.structSize == sizeof(War3RenderDynamicColoredSkinUpdateV2)) {
        colored = reinterpret_cast<const War3RenderDynamicColoredSkinUpdateV2*>(&input);
    } else return false;
    if (material) {
        if (material->layerAlphaFormat != WAR3_RENDER_DYNAMIC_LAYER_ALPHA_V1 || material->reserved
            || material->layerAlphaCount > 1048576 || material->instanceSliceCount != input.instanceCount
            || (material->layerAlphaCount && !material->layerAlphas)
            || (material->instanceSliceCount && !material->instanceSlices)) return false;
        if (!material->base.colorFormat)
            return !material->base.colorCount && !material->base.colors;
        colored = &material->base;
    }
    return colored->colorFormat == WAR3_RENDER_DYNAMIC_MESH_RGB_V1
        && colored->colorCount == input.alphaCount && (!colored->colorCount || colored->colors);
}
bool BindModelLayerCounts(ModelScene& scene, bool enabled) {
    scene.dynamicMaterialAlphas = enabled; scene.modelLayerCounts.clear();
    if (!enabled) return true;
    scene.modelLayerCounts.reserve(scene.models.size());
    for (const auto& model : scene.models) {
        if (model.firstMesh > scene.meshes.size() || model.meshCount > scene.meshes.size() - model.firstMesh) return false;
        std::uint64_t count = 0;
        for (auto i = model.firstMesh; i < model.firstMesh + model.meshCount; ++i) count += scene.meshes[i].layerCount;
        if (!count || count > 131072) return false;
        scene.modelLayerCounts.push_back(static_cast<std::uint32_t>(count));
    }
    return true;
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
    const War3RenderMaterialSkinUpdateV2* material = nullptr;
    const War3RenderMaterialStateSkinUpdateV2* state = nullptr;
    if (!SkinModes(input, colored, material, state) || bool(material) != scene.dynamicMaterialAlphas
        || bool(state) != scene.dynamicMaterialStates
        || (material ? scene.modelLayerCounts.size() != scene.models.size() : !scene.modelLayerCounts.empty())) return false;
    const std::uint64_t bytes = static_cast<std::uint64_t>(input.instanceCount) * sizeof(War3RenderSkinnedModelInstanceV2)
        + static_cast<std::uint64_t>(input.groupCount) * sizeof(War3RenderGroupMatrixV2)
        + static_cast<std::uint64_t>(input.alphaCount) * (sizeof(float) + (colored ? sizeof(War3RenderMeshColorV2) : 0))
        + (material ? static_cast<std::uint64_t>(material->layerAlphaCount) * sizeof(float)
            + static_cast<std::uint64_t>(material->instanceSliceCount) * sizeof(War3RenderMaterialLayerSliceV2) : 0)
        + (state ? std::uint64_t(state->materialStateCount) * sizeof(War3RenderMaterialStateV2)
            + std::uint64_t(state->materialInstanceSliceCount) * sizeof(War3RenderMaterialLayerSliceV2) : 0);
    if (bytes > 64ull * 1024 * 1024 || scene.modelGroupCounts.size() != scene.models.size()) return false;
    Copy(pose.instances, input.instances, input.instanceCount);
    Copy(pose.groups, input.groups, input.groupCount); Copy(pose.alphas, input.alphas, input.alphaCount);
    if (colored) {
        for (std::uint32_t i=0;i<colored->colorCount;++i) for (const auto v:colored->colors[i].rgb) if (!Unit(v)) return false;
        Copy(pose.colors,colored->colors,colored->colorCount);
    } else pose.colors.clear();
    pose.dynamicMaterialAlphas = material != nullptr;
    if (material) {
        pose.layerAlphas.clear(); pose.layerSlices.clear();
        Copy(pose.layerAlphas, material->layerAlphas, material->layerAlphaCount);
        Copy(pose.layerSlices, material->instanceSlices, material->instanceSliceCount);
    } else { pose.layerAlphas.clear(); pose.layerSlices.clear(); }
    pose.dynamicMaterialStates = state != nullptr;
    pose.materialStates.clear(); pose.materialStateSlices.clear();
    if (state) {
        Copy(pose.materialStates, state->materialStates, state->materialStateCount);
        Copy(pose.materialStateSlices, state->materialInstanceSlices, state->materialInstanceSliceCount);
    }
    pose.sceneVersion = input.sceneVersion; pose.contentVersion = input.contentVersion;
    pose.matrixFormat = input.matrixFormat;
    std::uint64_t groupEnd = 0, alphaEnd = 0, layerEnd = 0, stateEnd = 0;
    // 只允许同模型完整片段别名；跨模型和部分重叠都不是合法共享。
    struct Slice { std::uint32_t end, model; };
    std::map<std::uint32_t, Slice> groupSlices, alphaSlices, layerSlices, stateSlices;
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
    for (std::size_t i = 0; i < pose.instances.size(); ++i) {
        const auto& instance = pose.instances[i];
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
        if (material) {
            const auto& slice = pose.layerSlices[i];
            if (slice.count != scene.modelLayerCounts[model]) return false;
            if (shared) {
                if (!bindSlice(layerSlices, slice.first, slice.count, pose.layerAlphas.size(), model)) return false;
            } else {
                if (slice.first != layerEnd) return false;
                layerEnd += slice.count;
                if (layerEnd > pose.layerAlphas.size()) return false;
            }
        }
        if (state) {
            const auto& slice = pose.materialStateSlices[i];
            if (slice.count != scene.modelLayerCounts[model]) return false;
            if (shared) {
                if (!bindSlice(stateSlices, slice.first, slice.count, pose.materialStates.size(), model)) return false;
            } else {
                if (slice.first != stateEnd) return false;
                stateEnd += slice.count;
                if (stateEnd > pose.materialStates.size()) return false;
            }
        }
        for (const auto value : instance.instance.transform) if (!Coordinate(value)) return false;
        for (const auto value : instance.instance.color) if (!Unit(value)) return false;
    }
    if (!shared && (groupEnd != pose.groups.size() || alphaEnd != pose.alphas.size())) return false;
    if (material && ((!shared && layerEnd != pose.layerAlphas.size())
        || (pose.instances.empty() && !pose.layerAlphas.empty()))) return false;
    if (state && ((!shared && stateEnd != pose.materialStates.size())
        || (pose.instances.empty() && !pose.materialStates.empty()))) return false;
    for (const auto& value : pose.materialStates) {
        if ((value.textureIndex != WAR3_RENDER_MODEL_NO_TEXTURE && value.textureIndex >= scene.materialStateTextureCount)
            || (value.samplerFlags & ~3u)) return false;
        for (const auto uv : value.uvTransform) if (!std::isfinite(uv)) return false;
    }
    for (const auto& group : pose.groups) for (const auto value : group.values) if (!Coordinate(value)) return false;
    for (const auto alpha : pose.alphas) if (!std::isfinite(alpha)) return false;
    for (const auto alpha : pose.layerAlphas) if (!std::isfinite(alpha)) return false;
    if (scene.initialSkin) {
        const auto& initial = *scene.initialSkin;
        if (pose.sceneVersion != initial.sceneVersion || pose.matrixFormat != initial.matrixFormat || pose.instances.size() != initial.instances.size()
            || pose.groups.size() != initial.groups.size() || pose.alphas.size() != initial.alphas.size() || pose.colors.size() != initial.colors.size()
            || pose.dynamicMaterialAlphas != initial.dynamicMaterialAlphas || pose.layerAlphas.size() != initial.layerAlphas.size()
            || pose.layerSlices.size() != initial.layerSlices.size()
            || pose.dynamicMaterialStates != initial.dynamicMaterialStates
            || pose.materialStates.size() != initial.materialStates.size() || pose.materialStateSlices.size() != initial.materialStateSlices.size()) return false;
        for (std::size_t i = 0; i < pose.instances.size(); ++i) {
            const auto& a = pose.instances[i]; const auto& b = initial.instances[i];
            if (a.instance.model != b.instance.model || (!shared && (a.firstGroup != b.firstGroup || a.firstAlpha != b.firstAlpha))) return false;
            if (material && (pose.layerSlices[i].count != initial.layerSlices[i].count
                || (!shared && pose.layerSlices[i].first != initial.layerSlices[i].first))) return false;
            if (state && (pose.materialStateSlices[i].count != initial.materialStateSlices[i].count
                || (!shared && pose.materialStateSlices[i].first != initial.materialStateSlices[i].first))) return false;
        }
    }
    return true;
}
bool CopySkinnedModelInput(const War3RenderSkinnedModelSceneV2& input, ModelScene& scene, std::uint64_t& payloadBytes) {
    if (input.structSize < sizeof(input) || !input.initialPose || input.vertexCount > 2097152 || input.indexCount > 6291456
        || input.layerCount > 131072 || input.meshCount > 65536 || input.modelCount > 16384 || input.textureCount > 4096
        || (input.vertexCount && !input.vertices) || (input.indexCount && !input.indices)
        || (input.layerCount && !input.layers) || (input.meshCount && !input.meshes) || (input.modelCount && !input.models)) return false;
    const War3RenderDynamicColoredSkinUpdateV2* dynamicColored = nullptr;
    const War3RenderMaterialSkinUpdateV2* material = nullptr;
    const War3RenderMaterialStateSkinUpdateV2* state = nullptr;
    if (!SkinModes(*input.initialPose, dynamicColored, material, state)) return false;
    scene.dynamicMaterialStates = state != nullptr; scene.materialStateTextureCount = input.textureCount;
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
    if (!BindModelLayerCounts(scene, material != nullptr)) return false;
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
        + pose->groups.size() * sizeof(War3RenderGroupMatrixV2) + pose->alphas.size() * sizeof(float) + pose->colors.size() * sizeof(War3RenderMeshColorV2)
        + pose->layerAlphas.size() * sizeof(float) + pose->layerSlices.size() * sizeof(War3RenderMaterialLayerSliceV2)
        + pose->materialStates.size() * sizeof(War3RenderMaterialStateV2) + pose->materialStateSlices.size() * sizeof(War3RenderMaterialLayerSliceV2)
        + scene.modelLayerCounts.size() * sizeof(std::uint32_t);
    scene.initialSkin = std::move(pose);
    scene.dynamicColors = dynamicColored != nullptr;
    return payloadBytes <= 320ull * 1024 * 1024;
}
bool CopySkinRoster(const War3RenderSkinUpdateV2& input, const ModelScene& residentStatic, ModelScene& output) {
    const War3RenderDynamicColoredSkinUpdateV2* colored = nullptr;
    const War3RenderMaterialSkinUpdateV2* material = nullptr;
    const War3RenderMaterialStateSkinUpdateV2* state = nullptr;
    if (!SkinModes(input, colored, material, state)) return false;
    if (!residentStatic.initialSkin || !input.sceneVersion || !input.contentVersion
        || input.sceneVersion <= residentStatic.initialSkin->sceneVersion
        || input.matrixFormat != residentStatic.initialSkin->matrixFormat
        || bool(colored) != (residentStatic.dynamicColors || !residentStatic.initialSkin->colors.empty())
        || bool(material) != residentStatic.dynamicMaterialAlphas || bool(state) != residentStatic.dynamicMaterialStates) return false;
    ModelScene candidate;
    candidate.models = residentStatic.models; candidate.modelGroupCounts = residentStatic.modelGroupCounts;
    candidate.meshes = residentStatic.meshes; candidate.layers = residentStatic.layers;
    candidate.meshGroups = residentStatic.meshGroups;
    candidate.modelLayerCounts = residentStatic.modelLayerCounts;
    candidate.dynamicColors = residentStatic.dynamicColors || !residentStatic.initialSkin->colors.empty();
    candidate.dynamicMaterialAlphas = residentStatic.dynamicMaterialAlphas;
    candidate.dynamicMaterialStates = residentStatic.dynamicMaterialStates;
    candidate.materialStateTextureCount = residentStatic.materialStateTextureCount;
    // 新槽的初始化沿用首次CopySkinnedModelInput的完整数值/片段校验，绝不修改旧initialSkin。
    auto initial = std::make_shared<SkinPose>();
    if (!CopySkinUpdate(input, candidate, *initial)) return false;
    candidate.instances.reserve(initial->instances.size());
    for (const auto& instance : initial->instances) candidate.instances.push_back(instance.instance);
    candidate.initialSkin = std::move(initial);
    output = std::move(candidate);
    return true;
}
bool CopySkinAssetAppend(const War3RenderAssetAppendV2& assets, const War3RenderSkinUpdateV2& input,
    std::shared_ptr<const ModelScene> residentStatic, ModelScene& output, std::uint64_t& payloadBytes) {
    const War3RenderDynamicColoredSkinUpdateV2* colored = nullptr;
    const War3RenderMaterialSkinUpdateV2* material = nullptr;
    const War3RenderMaterialStateSkinUpdateV2* state = nullptr;
    if (!SkinModes(input, colored, material, state)) return false;
    if (!residentStatic || !residentStatic->initialSkin || assets.structSize != sizeof(assets) || assets.reserved
        || assets.prefix.vertexCount != residentStatic->VertexCount()
        || assets.prefix.indexCount != residentStatic->IndexCount()
        || assets.prefix.layerCount != residentStatic->layers.size()
        || assets.prefix.meshCount != residentStatic->meshes.size()
        || assets.prefix.modelCount != residentStatic->models.size()
        || assets.prefix.modelTextureCount != residentStatic->TextureCount()
        || (!assets.vertexCount && !assets.indexCount && !assets.layerCount && !assets.meshCount
            && !assets.modelCount && !assets.modelTextureCount && !assets.particleTextureCount)
        || (assets.vertexCount && !assets.vertices) || (assets.indexCount && !assets.indices)
        || (assets.layerCount && !assets.layers) || (assets.meshCount && !assets.meshes)
        || (assets.modelCount && !assets.models) || (assets.modelTextureCount && !assets.modelTextures)
        || (assets.particleTextureCount && !assets.particleTextures)
        || (assets.colorCount != 0 && (assets.colorCount != assets.meshCount || !assets.colors))) return false;
    const auto sum = [](std::uint32_t prefix, std::uint32_t added) { return std::uint64_t(prefix) + added; };
    const auto vertices = sum(assets.prefix.vertexCount, assets.vertexCount);
    const auto indices = sum(assets.prefix.indexCount, assets.indexCount);
    const auto layers = sum(assets.prefix.layerCount, assets.layerCount);
    const auto meshes = sum(assets.prefix.meshCount, assets.meshCount);
    const auto models = sum(assets.prefix.modelCount, assets.modelCount);
    const auto textures = sum(assets.prefix.modelTextureCount, assets.modelTextureCount);
    if (vertices > 2097152 || indices > 6291456 || layers > 131072 || meshes > 65536 || models > 16384
        || textures > 4096 || sum(assets.prefix.particleTextureCount, assets.particleTextureCount) > 256
        || input.sceneVersion <= residentStatic->initialSkin->sceneVersion
        || input.matrixFormat != residentStatic->initialSkin->matrixFormat
        || ((residentStatic->dynamicColors || !residentStatic->initialSkin->colors.empty()) && !colored)
        || (residentStatic->dynamicMaterialAlphas && !material) || (residentStatic->dynamicMaterialStates && !state)) return false;
    const auto bytes = vertices * sizeof(War3RenderSkinnedModelVertexV2) + indices * sizeof(std::uint16_t)
        + layers * sizeof(War3RenderModelLayerV2) + meshes * (sizeof(War3RenderSkinnedModelMeshV2) + sizeof(War3RenderMeshColorV2))
        + models * sizeof(War3RenderSkinnedModelV2);
    if (bytes > 320ull * 1024 * 1024) return false;
    ModelScene candidate;
    // 首份平坦资产只保持一个原拥有者；后续页面列表只分享字节，既不留动态历史也不递归析构。
    if (residentStatic->staticSuffix) {
        candidate.staticBase = residentStatic->staticBase;
        candidate.staticPrefixes = residentStatic->staticPrefixes;
        candidate.staticPrefixes.push_back(residentStatic->staticSuffix);
    } else candidate.staticBase = residentStatic;
    candidate.dynamicColors = colored != nullptr;
    candidate.dynamicMaterialStates = state != nullptr; candidate.materialStateTextureCount = static_cast<std::uint32_t>(textures);
    candidate.prefixVertices = assets.prefix.vertexCount; candidate.prefixIndices = assets.prefix.indexCount;
    candidate.prefixTextures = assets.prefix.modelTextureCount;
    candidate.prefixLayers = assets.prefix.layerCount; candidate.prefixMeshes = assets.prefix.meshCount;
    candidate.prefixModels = assets.prefix.modelCount;
    candidate.layers = residentStatic->layers; candidate.meshes = residentStatic->meshes;
    candidate.models = residentStatic->models; candidate.meshGroups = residentStatic->meshGroups;
    candidate.modelGroupCounts = residentStatic->modelGroupCounts;
    candidate.meshColors = residentStatic->meshColors;
    if (!candidate.meshColors.empty() || assets.colorCount) {
        if (candidate.meshColors.empty()) candidate.meshColors.resize(assets.prefix.meshCount, {{1, 1, 1}});
        candidate.meshColors.resize(static_cast<std::size_t>(meshes), {{1, 1, 1}});
    }
    candidate.vertices.reserve(assets.vertexCount); candidate.vertexGroups.reserve(assets.vertexCount);
    for (std::uint32_t i = 0; i < assets.vertexCount; ++i) {
        const auto& vertex = assets.vertices[i].vertex;
        const float values[]{vertex.x, vertex.y, vertex.z, vertex.nx, vertex.ny, vertex.nz, vertex.u, vertex.v};
        for (const auto value : values) if (!Coordinate(value)) return false;
        candidate.vertices.push_back(vertex); candidate.vertexGroups.push_back(assets.vertices[i].group);
    }
    Copy(candidate.indices, assets.indices, assets.indexCount);
    for (std::uint32_t i = 0; i < assets.layerCount; ++i) {
        const auto& layer = assets.layers[i];
        if ((layer.textureIndex != WAR3_RENDER_MODEL_NO_TEXTURE && layer.textureIndex >= textures)
            || layer.filterMode > 6 || layer.flags > 7 || !Unit(layer.alpha)) return false;
        candidate.layers.push_back(layer);
    }
    std::uint64_t indexEnd = assets.prefix.indexCount, drawnLayers = 0;
    for (const auto& mesh : candidate.meshes) drawnLayers += mesh.layerCount;
    for (std::uint32_t i = 0; i < assets.meshCount; ++i) {
        const auto& value = assets.meshes[i]; const auto& mesh = value.mesh;
        if (!value.groupCount || !mesh.layerCount || !mesh.indexCount || mesh.indexCount % 3
            || mesh.firstIndex != indexEnd || mesh.baseVertex < assets.prefix.vertexCount || mesh.baseVertex >= vertices
            || mesh.firstLayer < assets.prefix.layerCount || std::uint64_t(mesh.firstLayer) + mesh.layerCount > layers) return false;
        indexEnd += mesh.indexCount; drawnLayers += mesh.layerCount;
        if (indexEnd > indices || drawnLayers > 131072) return false;
        for (std::uint32_t j = 0; j < mesh.indexCount; ++j) {
            const auto index = candidate.indices[mesh.firstIndex - assets.prefix.indexCount + j];
            const auto vertex = std::uint64_t(mesh.baseVertex) + index;
            if (vertex >= vertices || candidate.vertexGroups[static_cast<std::size_t>(vertex) - assets.prefix.vertexCount] >= value.groupCount) return false;
        }
        candidate.meshes.push_back(mesh); candidate.meshGroups.emplace_back(value.firstGroup, value.groupCount);
        if (assets.colorCount) {
            for (const auto component : assets.colors[i].rgb) if (!Unit(component)) return false;
            candidate.meshColors[assets.prefix.meshCount + i] = assets.colors[i];
        }
    }
    if (indexEnd != indices) return false;
    std::uint64_t meshEnd = assets.prefix.meshCount;
    for (std::uint32_t i = 0; i < assets.modelCount; ++i) {
        const auto& value = assets.models[i];
        if (!value.groupCount || !value.model.meshCount || value.model.firstMesh != meshEnd) return false;
        meshEnd += value.model.meshCount;
        if (meshEnd > meshes) return false;
        for (auto j = value.model.firstMesh; j < meshEnd; ++j) {
            const auto [first, count] = candidate.meshGroups[j];
            if (first > value.groupCount || count > value.groupCount - first) return false;
        }
        candidate.models.push_back(value.model); candidate.modelGroupCounts.push_back(value.groupCount);
    }
    if (meshEnd != meshes) return false;
    if (!BindModelLayerCounts(candidate, material != nullptr)) return false;
    auto pose = std::make_shared<SkinPose>();
    if (!CopySkinUpdate(input, candidate, *pose)) return false;
    candidate.instances.reserve(pose->instances.size());
    for (const auto& instance : pose->instances) candidate.instances.push_back(instance.instance);
    candidate.initialSkin = std::move(pose);
    const auto totalBytes = bytes + candidate.initialSkin->instances.size() * sizeof(War3RenderSkinnedModelInstanceV2)
        + candidate.initialSkin->groups.size() * sizeof(War3RenderGroupMatrixV2)
        + candidate.initialSkin->alphas.size() * sizeof(float) + candidate.initialSkin->colors.size() * sizeof(War3RenderMeshColorV2)
        + candidate.initialSkin->layerAlphas.size() * sizeof(float)
        + candidate.initialSkin->layerSlices.size() * sizeof(War3RenderMaterialLayerSliceV2)
        + candidate.initialSkin->materialStates.size() * sizeof(War3RenderMaterialStateV2)
        + candidate.initialSkin->materialStateSlices.size() * sizeof(War3RenderMaterialLayerSliceV2)
        + candidate.modelLayerCounts.size() * sizeof(std::uint32_t);
    if (totalBytes > 320ull * 1024 * 1024) return false;
    payloadBytes = totalBytes; output = std::move(candidate);
    return true;
}
static_assert(sizeof(War3RenderSkinnedModelVertexV2) == 36);
static_assert(sizeof(War3RenderSkinnedModelInstanceV2) == 72);
static_assert(sizeof(War3RenderGroupMatrixV2) == 48);
}
