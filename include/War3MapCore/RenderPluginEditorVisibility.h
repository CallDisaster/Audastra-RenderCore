// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/RenderPluginV2.h"

extern "C" {
// 编辑器呈现状态与模型/姿势版本独立；它不改变资产、姿势、粒子检查点或对象身份。
enum War3RenderEditorPresentationModeV2 : std::uint32_t {
    WAR3_EDITOR_PRESENTATION_STATIC = 0,
    WAR3_EDITOR_PRESENTATION_SKINNED = 1,
    WAR3_EDITOR_PRESENTATION_PARTICLES = 2
};
struct War3RenderEditorPresentationTargetV2 {
    std::uint32_t structSize, mode, instanceCount, particleDrawCount;
    // 每次完整模型槽替换或旧静态实例覆盖均递增，包括同数量、同模型内容的重新提交。
    std::uint64_t modelEpoch, sceneVersion, contentVersion;
};
struct War3RenderEditorVisibilityUpdateV2 {
    std::uint32_t structSize, instanceCount, particleDrawCount, reserved;
    War3RenderEditorPresentationTargetV2 expected; // 当前已接收 CPU 槽，不是新姿势的版本
    std::uint64_t presentationRevision; // 同一 renderer 内非0且严格递增，不能回退旧掩码
    const std::uint8_t* instanceVisible; // 与原实例顺序对应，每字节只允许0/1
    const std::uint8_t* particleDrawVisible; // 与本次粒子draw顺序对应，每字节只允许0/1
    // 均为空=仅改显隐，不上传任何资产/姿势。二者最多一个非空；
    // particleFrame 自带同版本 modelPose。新帧和新掩码在同一锁中发布。
    const War3RenderSkinUpdateV2* modelPose;
    const War3RenderParticleUpdateV2* particleFrame;
    // 可选静态实例覆盖：数量=instanceCount，布局/model序号不变；与掩码同次发布并递增epoch。
    // 与以上两个动态指针互斥。不重传几何/贴图；空指针保持原静态变换。
    const War3RenderModelInstanceV2* staticInstances;
};
struct War3RenderEditorModelSceneV2 {
    std::uint32_t structSize, reserved;
    // 三者恰好一个非空。沿用现有完整场景的校验/预算，在同一次CPU发布中带上显隐。
    const War3RenderModelSceneV2* staticModels;
    const War3RenderSkinnedModelSceneV2* models;
    const War3RenderParticleSceneV2* particles;
    // expected绑定替换前的target；mask对应新场景实例与初始粒子draw。
    // modelPose/particleFrame须为空（新场景本身携带初始帧），modelEpoch自动递增。
    const War3RenderEditorVisibilityUpdateV2* visibility;
};
struct War3RenderEditorPresentationStatusV2 {
    std::uint32_t structSize, mode, instanceCount, particleDrawCount;
    std::uint32_t visibleInstanceCount, visibleParticleDrawCount, drawCallCount, reserved;
    std::uint64_t modelEpoch, sceneVersion, contentVersion, presentationRevision;
};
struct War3RenderEditorCapturedFrameV2 {
    std::uint32_t structSize;
    War3RenderCapturedFrameV2 frame;
    War3RenderSkinStatusV2 skin;
    War3RenderParticleStatusV2 particles;
    War3RenderEditorPresentationStatusV2 presentation; // 像素同次Draw完成身份，禁止事后拼接
};
// 最近成功 Draw/Present 的同帧身份；不含像素，不等待 GPU，不等同截图读回。
struct War3RenderEditorCompletedFrameV2 {
    std::uint32_t structSize;
    War3RenderViewInfoV2 view;
    War3RenderCameraV2 camera;
    War3RenderSkinStatusV2 skin;
    War3RenderParticleStatusV2 particles;
    War3RenderEditorPresentationStatusV2 presentation;
};
// 显隐拆分后的实际模型layer draw+可见粒子draw累计最多262144，可见模型累计mesh遍历同限。
// 超预算、旧epoch/姿势/修订、长度或字节值无效均拒绝并保留旧状态。
// 旧模型/姿势写入口使掩码失效；再次显隐须按新target重绑，绝不沿用同数量旧掩码。
}
