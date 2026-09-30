// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3RenderCore/PoseTypes.h"

namespace War3RenderCore {

constexpr std::uint32_t ExternalFrameContractVersion = 1;

// 来源是宿主声明；接收成功本身不构成原版采集或运行时等价证明。
enum class ExternalFrameSource : std::uint32_t { Unknown = 0, NativeEvaluated = 1, OfflineComputed = 2 };

// 由宿主在输入包之外提供当前帧身份，不能从待校验的请求反向填充。
struct alignas(8) ExternalFrameContext {
    std::uint64_t sourceEpoch = 0;
    std::uint64_t frameVersion = 0;
    ExternalFrameSource source = ExternalFrameSource::Unknown;
    std::uint32_t contractVersion = ExternalFrameContractVersion;
};

// 没有动作编号或时钟。矩阵已平均；通道按原 geoset 顺序，必须全部提供。
// ModelLocal/0 与 World/1 明示世界变换是否已应用；核心只复制，不转换空间。
struct alignas(8) ExternalFrameRequest {
    InstanceIdentity instance;
    AssetIdentity asset;
    std::uint64_t sourceEpoch = 0;
    std::uint64_t frameVersion = 0;
    std::uint32_t assetIndex = 0;
    std::uint32_t contractVersion = ExternalFrameContractVersion;
    ExternalFrameSource source = ExternalFrameSource::Unknown;
    std::uint32_t reserved = 0;
    ExternalGroupSlice groups;
    Range32 alphas;
    Range32 colors;
};

struct alignas(8) ExternalFrameReceipt {
    InstanceIdentity instance;
    AssetIdentity asset;
    std::uint64_t evaluationEpoch = 0;
    std::uint64_t poseVersion = 0; // 成功输出版本；失败为 0。
    std::uint64_t sourceEpoch = 0;
    std::uint64_t frameVersion = 0;
    std::uint32_t contractVersion = ExternalFrameContractVersion;
    ExternalFrameSource source = ExternalFrameSource::Unknown;
    CoordinateSpace space = CoordinateSpace::ModelLocal;
    std::uint32_t worldTransformApplied = 0;
    Code error = Code::Ok;
    Range32 groups;
    Range32 alphas;
    Range32 colors;
    Range32 diagnostics;
};

static_assert(sizeof(ExternalFrameContext) == 24 && sizeof(ExternalFrameRequest) == 104);
static_assert(sizeof(ExternalFrameReceipt) == 120);
static_assert(std::is_trivially_copyable_v<ExternalFrameRequest> && std::is_trivially_copyable_v<ExternalFrameReceipt>);

} // namespace War3RenderCore
