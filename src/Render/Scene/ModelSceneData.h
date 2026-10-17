// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/RenderPluginModelColor.h"
#include "War3MapCore/RenderPluginMaterialAlpha.h"
#include "War3MapCore/RenderPluginMaterialState.h"
#include "War3MapCore/RenderPluginStaticSamplers.h"
#include <memory>
#include <utility>
#include <vector>

namespace War3Map::Render {
struct TextureMipRgba {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
struct TextureRgba {
    std::vector<TextureMipRgba> mips;
    std::uint32_t maxSampleMip = 0, atlasColumns = 1, atlasRows = 1;
};
struct SkinPose {
    std::uint64_t sceneVersion = 0, contentVersion = 0;
    std::uint32_t matrixFormat = WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4;
    std::vector<War3RenderSkinnedModelInstanceV2> instances;
    std::vector<War3RenderGroupMatrixV2> groups;
    std::vector<float> alphas;
    std::vector<War3RenderMeshColorV2> colors; // 空=静态颜色；否则与alphas平行，内容属于contentVersion
    std::vector<float> layerAlphas;
    std::vector<War3RenderMaterialLayerSliceV2> layerSlices;
    bool dynamicMaterialAlphas = false; // 空roster仍保留格式；不是根据vector是否空推导。
    std::vector<War3RenderMaterialStateV2> materialStates;
    std::vector<War3RenderMaterialLayerSliceV2> materialStateSlices;
    bool dynamicMaterialStates = false;
};
struct ModelStaticSuffix {
    std::vector<War3RenderModelVertexV2> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<std::uint32_t> vertexGroups;
    std::vector<TextureRgba> textures;
};
// 模型场景的 CPU 副本（已由插件入口校验）。
struct ModelScene {
    std::vector<War3RenderModelVertexV2> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<War3RenderModelLayerV2> layers;
    std::vector<std::uint32_t> staticSamplerFlags; // 显式静态模式，与layers绝对层同序；legacy不分配。
    std::vector<War3RenderModelMeshV2> meshes;
    std::vector<War3RenderModelV2> models;
    std::vector<War3RenderModelInstanceV2> instances;
    std::vector<TextureRgba> textures;
    // 蒙皮模式的额外静态索引；顶点位置/法线/UV仍只保留一份。
    std::vector<std::uint32_t> vertexGroups;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> meshGroups;
    std::vector<std::uint32_t> modelGroupCounts;
    std::vector<std::uint32_t> modelLayerCounts; // 显式材质模式下实际draw层数，与models同序。
    std::vector<War3RenderMeshColorV2> meshColors; // 空=旧白色路径，否则与meshes等长
    std::shared_ptr<const SkinPose> initialSkin; // 非空即为蒙皮模型槽
    // 追加场景的layout元数据完整，几何/纹理只拥有本次suffix；旧字节由不可变前缀拥有。
    std::shared_ptr<const ModelScene> staticBase;
    std::vector<std::shared_ptr<const ModelStaticSuffix>> staticPrefixes;
    std::shared_ptr<const ModelStaticSuffix> staticSuffix;
    std::uint32_t prefixVertices = 0, prefixIndices = 0, prefixTextures = 0;
    std::uint32_t prefixLayers = 0, prefixMeshes = 0, prefixModels = 0;
    bool dynamicColors = false; // 由请求格式绑定，空roster也不能丢掉动态颜色模式。
    bool explicitStaticSamplers = false; // 空静态场景也保留模式；不能按vector是否为空推导。
    // provider实际载荷计费后的余额；backend另建flags须按实际capacity复核再发布。
    // 只约束本次静态提交的新增载荷，不代表全进程RSS；legacy保持0。
    std::uint64_t staticSamplerBackendBudget = 0;
    bool dynamicMaterialAlphas = false;
    bool dynamicMaterialStates = false;
    std::uint32_t materialStateTextureCount = 0; // CPU初始候选纹理尚未读入时绑定已验证声明数量。
    const auto& StaticVertices() const noexcept { return staticSuffix ? staticSuffix->vertices : vertices; }
    const auto& StaticIndices() const noexcept { return staticSuffix ? staticSuffix->indices : indices; }
    const auto& StaticVertexGroups() const noexcept { return staticSuffix ? staticSuffix->vertexGroups : vertexGroups; }
    const auto& StaticTextures() const noexcept { return staticSuffix ? staticSuffix->textures : textures; }
    std::uint32_t VertexCount() const noexcept { return prefixVertices + static_cast<std::uint32_t>(StaticVertices().size()); }
    std::uint32_t IndexCount() const noexcept { return prefixIndices + static_cast<std::uint32_t>(StaticIndices().size()); }
    std::uint32_t TextureCount() const noexcept { return prefixTextures + static_cast<std::uint32_t>(StaticTextures().size()); }
    std::uint64_t StaticSamplerBytes() const noexcept {
        return static_cast<std::uint64_t>(staticSamplerFlags.capacity()) * sizeof(std::uint32_t);
    }
    // 仅在候选拥有者发布前调用；转移新suffix，不复制已驻留字节。
    void SealStaticSuffix() {
        if (staticSuffix) return;
        auto suffix = std::make_shared<ModelStaticSuffix>();
        suffix->vertices = std::move(vertices); suffix->indices = std::move(indices);
        suffix->vertexGroups = std::move(vertexGroups); suffix->textures = std::move(textures);
        staticSuffix = std::move(suffix);
    }
    std::uint64_t TextureBytes() const noexcept {
        const auto bytes = [](const auto& values) {
            std::uint64_t total = 0;
            for (const auto& texture : values) for (const auto& mip : texture.mips) total += mip.rgba.size();
            return total;
        };
        std::uint64_t total = bytes(StaticTextures());
        if (staticBase) total += bytes(staticBase->StaticTextures());
        for (const auto& page : staticPrefixes) total += bytes(page->textures);
        return total;
    }
};
}
