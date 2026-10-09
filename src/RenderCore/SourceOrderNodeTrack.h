// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include "War3RenderCore/GameKeyRanges.h"

namespace War3RenderCore::Detail {
inline constexpr std::uint64_t MaxSourceOrderKeyVisits = 1048576;
using War3RenderCore::SourceOrderWindowKind;
using War3RenderCore::SourceOrderWindow;
using War3RenderCore::ForEachGameSequenceRange;
using War3RenderCore::FirstDescendingEdge;
using War3RenderCore::SelectGameKeyWindow;

// 仅UI/参考结果展开时分配；批量热路径仍写预热的PoseDiagnostic。
inline std::string DescribeSourceOrderNodePreview(const char* track, std::uint32_t objectId,
    std::int64_t left, std::int64_t right) {
    return std::string(track) + " objectId=" + std::to_string(objectId) + " 在源帧 "
        + std::to_string(left) + "→" + std::to_string(right)
        + " 含逆序键；按 1.27a 原版区间与顺序查找取值（插值浮点仍为预览实现）。";
}

inline std::string DescribeSourceOrderNodeKeyLimit(const char* track, std::uint32_t objectId,
    std::int64_t requested, std::int64_t remaining) {
    return std::string(track) + " objectId=" + std::to_string(objectId)
        + " 原序预览按键访问预算不足：本轨迹需要 " + std::to_string(requested)
        + " 次，当前批次剩余 " + std::to_string(remaining) + " 次；每批上限 "
        + std::to_string(MaxSourceOrderKeyVisits) + " 次。该实例仍被容量门禁拒绝。";
}
}
