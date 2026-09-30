// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
// 原创双实例平面，不依赖任何游戏资产；同时供入口校验和 WARP 测试使用。
#include "War3MapCore/RenderPluginV2.h"
#include <array>
struct RenderSkinFixture {
    std::array<War3RenderSkinnedModelVertexV2, 4> vertices{{
        {{-200,-200,20,0,0,1,0,1},0}, {{200,-200,20,0,0,1,1,1},0},
        {{-200,200,20,0,0,1,0,0},0}, {{200,200,20,0,0,1,1,0},0}}};
    std::array<std::uint16_t, 6> indices{0,1,2,1,3,2};
    War3RenderModelLayerV2 layer{WAR3_RENDER_MODEL_NO_TEXTURE,0,1,1};
    War3RenderSkinnedModelMeshV2 mesh{{0,6,0,0,1},0,1};
    War3RenderSkinnedModelV2 model{{0,1},1};
    std::array<War3RenderSkinnedModelInstanceV2, 2> instances{{
        {{0,{1,0,0,0,0,1,0,0,0,0,1,0},{1,0,0}},0,0},
        {{0,{1,0,0,0,0,1,0,0,0,0,1,0},{0,1,0}},1,1}}};
    std::array<War3RenderGroupMatrixV2, 2> groups{{
        {{1,0,0,0,1,0,0,0,1,-500,0,0}}, {{1,0,0,0,1,0,0,0,1,500,0,0}}}};
    std::array<float, 2> alphas{1,1};
    War3RenderSkinUpdateV2 Update(std::uint64_t version = 1) const {
        return {sizeof(War3RenderSkinUpdateV2),2,2,2,1,version,
            WAR3_RENDER_GROUP_LOCAL_COLUMN_3X4,48,0,0,instances.data(),groups.data(),alphas.data()};
    }
    War3RenderSkinnedModelSceneV2 Scene(const War3RenderSkinUpdateV2& pose) const {
        return {sizeof(War3RenderSkinnedModelSceneV2),4,6,1,1,1,0,
            vertices.data(),indices.data(),&layer,&mesh,&model,nullptr,&pose};
    }
};
