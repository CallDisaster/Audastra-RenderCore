// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/RenderPluginEditorVisibility.h"

// 附加模型层与主场景共用相机、深度和设备；不拥有地图对象或保存事务。
// 32 是独立编辑层数量预算，不是地图实例数量限制。旧 ABI 的所有前缀保持不变。
inline constexpr std::uint32_t WAR3_RENDER_MAX_MODEL_SCENE_LAYERS = 32;
extern "C" {
struct War3RenderModelSceneLayerV2 {
    std::uint32_t structSize, reserved;
    std::uint64_t layerId, parentModelEpoch, revision, expectedRevision;
    const War3RenderSkinnedModelSceneV2* models;
    const War3RenderParticleSceneV2* particles; // models/particles 恰好一个
};
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
