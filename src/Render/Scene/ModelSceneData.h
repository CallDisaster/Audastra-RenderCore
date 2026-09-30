// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/RenderPluginModelColor.h"
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
    std::vector<War3RenderSkinnedModelInstanceV2> instances;
    std::vector<War3RenderGroupMatrixV2> groups;
    std::vector<float> alphas;
    std::vector<War3RenderMeshColorV2> colors; // 空=静态颜色；否则与alphas平行，内容属于contentVersion
};
// 模型场景的 CPU 副本（已由插件入口校验）。
struct ModelScene {
    std::vector<War3RenderModelVertexV2> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<War3RenderModelLayerV2> layers;
    std::vector<War3RenderModelMeshV2> meshes;
    std::vector<War3RenderModelV2> models;
    std::vector<War3RenderModelInstanceV2> instances;
    std::vector<TextureRgba> textures;
    // 蒙皮模式的额外静态索引；顶点位置/法线/UV仍只保留一份。
    std::vector<std::uint32_t> vertexGroups;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> meshGroups;
    std::vector<std::uint32_t> modelGroupCounts;
    std::vector<War3RenderMeshColorV2> meshColors; // 空=旧白色路径，否则与meshes等长
    std::shared_ptr<const SkinPose> initialSkin; // 非空即为蒙皮模型槽

};
}
