// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/RenderPluginEditorVisibility.h"
#include <cstddef>

// 附加模型层与主场景共用相机、深度和设备；不拥有地图对象或保存事务。
// 32 是独立编辑层数量预算，不是地图实例数量限制。旧 ABI 的所有前缀保持不变。
inline constexpr std::uint32_t WAR3_RENDER_MAX_MODEL_SCENE_LAYERS = 32;
// 显式编辑器展示角色；不从模型路径、层ID或材质flags猜测角色。
inline constexpr std::uint32_t WAR3_RENDER_MODEL_LAYER_MODEL = 0;
inline constexpr std::uint32_t WAR3_RENDER_MODEL_LAYER_GROUND_DECAL_PREVIEW = 1;
inline constexpr std::uint32_t WAR3_RENDER_MODEL_LAYER_CAP_GROUND_DECAL_PREVIEW = 1;
extern "C" {
struct War3RenderModelSceneLayerV2 {
    std::uint32_t structSize, reserved;
    std::uint64_t layerId, parentModelEpoch, revision, expectedRevision;
    const War3RenderSkinnedModelSceneV2* models;
    const War3RenderParticleSceneV2* particles; // models/particles 恰好一个
    // 可选尾部。旧prefix (offsetof(role))仍为Model；宿主先查询新能力。
    // GroundDecalPreview只接受混合、不写深度的模型层，使用编辑器小幅深度偏移。
    // 这是预览近似，不是原版投影/生命周期；更新和同ID替换不能改变角色。
    std::uint32_t role, roleReserved;
};
inline constexpr std::uint32_t WAR3_RENDER_MODEL_SCENE_LAYER_PREFIX_SIZE =
    static_cast<std::uint32_t>(offsetof(War3RenderModelSceneLayerV2, role));
static_assert(WAR3_RENDER_MODEL_SCENE_LAYER_PREFIX_SIZE == (sizeof(void*) == 8 ? 56 : 48));
static_assert(sizeof(War3RenderModelSceneLayerV2) == (sizeof(void*) == 8 ? 64 : 56));
struct War3RenderModelLayerUpdateV2 {
    std::uint32_t structSize, reserved;
    std::uint64_t layerId, parentModelEpoch, revision, expectedRevision;
    const War3RenderSkinUpdateV2* pose;
    const War3RenderParticleUpdateV2* particles;
};
struct War3RenderModelLayerStatusV2 {
    std::uint32_t structSize;
    std::int32_t result;
    std::uint64_t layerId, parentModelEpoch, revision;
    War3RenderSkinStatusV2 skin;
    War3RenderParticleStatusV2 particles;
};
struct War3RenderModelLayersTargetV2 {
    std::uint32_t structSize, layerCount;
    std::uint64_t parentModelEpoch, layersRevision;
};
struct War3RenderLayeredCapturedFrameV2 {
    std::uint32_t structSize, layerCount;
    std::uint64_t layersRevision;
    War3RenderEditorCapturedFrameV2 world;
    War3RenderModelLayerStatusV2 layers[WAR3_RENDER_MAX_MODEL_SCENE_LAYERS];
};
}
