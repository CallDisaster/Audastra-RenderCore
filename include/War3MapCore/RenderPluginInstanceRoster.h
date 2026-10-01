// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginEditorVisibility.h"

extern "C" {
// 静态资产驻留的完整实例重绑定。flags目前必须为0：陌生资产页明确不支持。
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
