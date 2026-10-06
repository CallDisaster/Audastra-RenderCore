// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace War3RenderCore::Detail {
inline constexpr std::uint64_t MaxSourceOrderKeyVisits = 1048576;
enum class SourceOrderWindowKind { Empty, Value, Segment };
struct SourceOrderWindow {
    SourceOrderWindowKind kind = SourceOrderWindowKind::Empty;
    std::size_t left = 0, right = 0;
    std::int64_t offset = 0, span = 0;
    std::int64_t reverseLeft = 0, reverseRight = 0;
    bool seam = false, descending = false;
};

// 核心与legacy参考共用此唯一选择实现。只处理普通动作中的原序键；不分配、不改源。
// 显式契约：最后源序exact优先，否则首个右邻及其原序前邻；无右邻才走首尾接缝。
// 不能用末源帧预判接缝：逆序末键可能回到动作首帧（例如Birth重复起点）。
template<class Keys>
SourceOrderWindow SelectSourceOrderNodeWindow(const Keys& keys, std::int64_t start,
    std::int64_t end, std::int64_t time) noexcept {
    constexpr auto none = std::numeric_limits<std::size_t>::max();
    std::size_t first = none, last = none, exact = none, right = none, left = none;
    SourceOrderWindow result;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto frame = static_cast<std::int64_t>(keys[i].frame);
        if (i != 0) {
            const auto previous = static_cast<std::int64_t>(keys[i - 1].frame);
            if (!result.descending && frame < previous && frame <= end && previous >= start) {
                result.descending = true; result.reverseLeft = previous; result.reverseRight = frame;
            }
        }
        if (frame < start || frame > end) continue;
        if (first == none) first = i;
        if (frame == time) exact = i;
        if (right == none && frame > time) { right = i; left = last; }
        last = i;
    }
    if (first == none) return result;
    if (exact != none || first == last) {
        result.kind = SourceOrderWindowKind::Value;
        result.left = result.right = exact != none ? exact : first;
        return result;
    }
    result.kind = SourceOrderWindowKind::Segment;
    result.seam = right == none || left == none;
    result.left = result.seam ? last : left;
    result.right = result.seam ? first : right;
    const auto leftFrame = static_cast<std::int64_t>(keys[result.left].frame);
    const auto rightFrame = static_cast<std::int64_t>(keys[result.right].frame);
    result.offset = time - (left == none && right != none ? rightFrame : leftFrame);
    result.span = rightFrame - leftFrame;
    if (result.span < 0) result.span += end - start;
    return result;
}

// 仅UI/参考结果展开时分配；批量热路径仍写预热的PoseDiagnostic。
inline std::string DescribeSourceOrderNodePreview(const char* track, std::uint32_t objectId,
    std::int64_t left, std::int64_t right) {
    return std::string(track) + " objectId=" + std::to_string(objectId) + " 在源帧 "
        + std::to_string(left) + "→" + std::to_string(right)
        + " 含逆序键；采用原序兼容预览，未确认原版缓存等价。";
}

inline std::string DescribeSourceOrderNodeKeyLimit(const char* track, std::uint32_t objectId,
    std::int64_t requested, std::int64_t remaining) {
    return std::string(track) + " objectId=" + std::to_string(objectId)
        + " 原序预览按键访问预算不足：本轨迹需要 " + std::to_string(requested)
        + " 次，当前批次剩余 " + std::to_string(remaining) + " 次；每批上限 "
        + std::to_string(MaxSourceOrderKeyVisits) + " 次。该实例仍被容量门禁拒绝。";
}
}
