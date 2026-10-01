// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginEditorVisibility.h"
#include "War3MapCore/RenderPluginModelColor.h"

extern "C" {
// 静态资产驻留的完整实例重绑定。旧结构flags必须为0。
// 所有指针仅借到submit返回；插件深拷贝，queued不是GPU或完成帧签收。
struct War3RenderEditorInstanceRosterV2 {
    std::uint32_t structSize, flags, reserved[2];
    War3RenderEditorPresentationTargetV2 expected;
    std::uint64_t expectedPresentationRevision, expectedAssetIdentity, expectedLayersRevision;
    const War3RenderSkinUpdateV2* candidatePose;
    // 驻留粒子模式必须提供（包括空帧）；否则必须为空。modelPose须等于candidatePose。
    const War3RenderParticleUpdateV2* candidateParticles;
    // 完整新roster的掩码。expected与本请求expected相同；其中三个动态指针须为空。
    const War3RenderEditorVisibilityUpdateV2* visibility;
};
enum War3RenderInstanceRosterFlagsV2 : std::uint32_t {
    WAR3_RENDER_ROSTER_APPEND_ASSETS = 1u,
};
// 计数绑定已拥有的静态前缀。不是由调用方再次提交旧几何或纹理。
struct War3RenderAssetPrefixV2 {
    std::uint32_t vertexCount, indexCount, layerCount, meshCount, modelCount;
    std::uint32_t modelTextureCount, particleTextureCount;
};
struct War3RenderAssetAppendV2 {
    std::uint32_t structSize, reserved;
    War3RenderAssetPrefixV2 prefix;
    std::uint32_t vertexCount, indexCount, layerCount, meshCount, modelCount;
    std::uint32_t modelTextureCount, particleTextureCount, colorCount;
    const War3RenderSkinnedModelVertexV2* vertices;
    const std::uint16_t* indices;
    const War3RenderModelLayerV2* layers;
    const War3RenderSkinnedModelMeshV2* meshes;
    const War3RenderSkinnedModelV2* models;
    // 所有first/base/texture/model序号都属于追加后的全局编号空间。
    // 网格不得引用旧几何；材质可以引用旧纹理。颜色空=新增网格白色。
    const War3RenderMeshColorV2* colors;
    const War3RenderTextureMipChainV2* modelTextures;
    const War3RenderTextureMipChainV2* particleTextures;
};
// 只有明确探测APPEND_ASSETS能力后才使用此尾部；旧base的ABI不变。
// 新颜色/首个粒子模式可以显式提升，已有模式不能降级。
struct War3RenderEditorAssetRosterV2 {
    War3RenderEditorInstanceRosterV2 base;
    const War3RenderAssetAppendV2* assets;
};
enum War3RenderInstanceRosterStateV2 : std::uint32_t {
    WAR3_RENDER_ROSTER_TARGET = 0,
    WAR3_RENDER_ROSTER_QUEUED = 1,
    WAR3_RENDER_ROSTER_REJECTED = 2,
    WAR3_RENDER_ROSTER_COMPLETED = 3,
    WAR3_RENDER_ROSTER_SUPERSEDED = 4,
};
struct War3RenderInstanceRosterResultV2 {
    std::uint32_t structSize, state;
    std::int32_t result;
    std::uint32_t reserved;
    std::uint64_t ticket, assetIdentity, layersRevision, presentationRevision;
    War3RenderEditorPresentationTargetV2 target;
    // 仅completed填写。同次实际Draw/Present的身份和上传计数，其他状态保持零。
    War3RenderEditorCompletedFrameV2 completed;
};
// getResult(ticket=0)查询当前CPU基准，state=TARGET；非0查询最近64张票据，不等待。
// 旧API和结构保持原状，调用者须探測新尾部尺寸与非空指针。
}
