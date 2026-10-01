// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/RenderPluginEditorVisibility.h"

extern "C" {
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_DRAW_COMPLETED = 1u << 0;
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_UPLOAD_VALID = 1u << 1;
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_WINDOW_ATTACHED = 1u << 2;
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_PRESENT_OCCLUDED = 1u << 3;
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_READBACK = 1u << 4;
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_GPU_TIME_AVAILABLE = 1u << 5;
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_RING_CAPACITY = 1024;
constexpr std::uint32_t WAR3_RENDER_FRAME_METRICS_PAGE_LIMIT = 256;
// 同次成功 Draw/Present 的不可变记录。计时均为 CPU steady_clock 纳秒，
// draw 不包含读回和 Present；upload 包含目标/资源/呈现状态安装，不能当作 GPU 时长。
// completedSteadyNs 在 Present 返回后取得；它的时钟原点只供本进程内比较。
// 无 GPU timestamp 查询时 GPU_TIME_AVAILABLE 必须为0，gpuDrawNs 为0。
struct War3RenderFrameMetricsV2 {
    std::uint32_t structSize, flags;
    std::int32_t result, presentResult;
    std::uint64_t completedSteadyNs, uploadCpuNs, drawCpuNs, presentCpuNs, readbackCpuNs, gpuDrawNs;
    std::uint32_t modelInstanceCount, visibleModelInstanceCount, particleDrawCount, visibleParticleDrawCount;
    War3RenderEditorCompletedFrameV2 completed;
};
struct War3RenderFrameMetricsPageV2 {
    std::uint32_t structSize, count, capacity, software;
    std::uint64_t oldestFrame, latestFrame, lostBeforeCount;
    std::uint32_t featureLevel, reserved;
    char adapterName[256]; // 实际 D3D11 设备的 DXGI adapter Description，UTF-8；未知时空串。
};
// 无窗口帧仍记录，但 WINDOW_ATTACHED 为0；DXGI_STATUS_OCCLUDED 原值保留，
// 不能作为可见动画验收。失败上传后重画旧内容：DRAW_COMPLETED 可为1、UPLOAD_VALID 为0，
// result 保留首个上传 HRESULT，不能把旧姿势记为新姿势成功。
// 有界分页只读，不等待 GPU、不请求新帧。afterFrame 为本 renderer 的排他游标；
// 返回最多256条，环形历史1024条。lostBeforeCount 是游标之后已丢失的帧数。
// capacity=0 且 records=nullptr 可只查元数据。成功才写输出；更大的 page 保留未知尾部。
}
