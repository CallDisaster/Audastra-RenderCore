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
    bool seam = false;
};

// 1.27a Game.dll sub_6F78A090：非全局轨迹按动作的文件顺序共享一个只进游标，把源键切成每个动作的
// 连续区间——先跳过起点前的键（遇到超过终点的键即停），再连续收下所有不超过终点的键（不再检查起点）。
// 源键不排序；逆序键留在所在动作的区间里，重叠或倒序排列的动作可能拿不到自己时间内的键（空区间取默认值）。
// emit(动作序号, 区间首键, 键数) 返回 false 时停止。核心编译与参考求值共用这一份实现。
template<class Keys, class Sequences, class Emit>
void ForEachGameSequenceRange(const Keys& keys, const Sequences& sequences, Emit&& emit) {
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < sequences.size(); ++index) {
        const auto start = static_cast<std::int64_t>(sequences[index].start);
        const auto end = static_cast<std::int64_t>(sequences[index].end);
        while (cursor < keys.size() && static_cast<std::int64_t>(keys[cursor].frame) <= end
            && static_cast<std::int64_t>(keys[cursor].frame) < start) ++cursor;
        const auto first = cursor;
        while (cursor < keys.size() && static_cast<std::int64_t>(keys[cursor].frame) <= end) ++cursor;
        if (!emit(index, first, cursor - first)) return;
    }
}

// 区间内第一条逆序边；没有时返回 false。
template<class Keys>
bool FirstDescendingEdge(const Keys& keys, std::size_t offset, std::size_t count,
    std::int64_t& left, std::int64_t& right) noexcept {
    for (std::size_t i = offset + 1; i < offset + count; ++i)
        if (keys[i].frame < keys[i - 1].frame) {
            left = static_cast<std::int64_t>(keys[i - 1].frame); right = static_cast<std::int64_t>(keys[i].frame);
            return true;
        }
    return false;
}

// 1.27a 区间内查找（sub_6F789830）与取值（sub_6F77AAD0 位置/缩放、sub_6F77AE30 旋转）：
// 从区间首键向后线性扫描，停在下一键帧大于时间处（同帧取连续的最后一个）。时间早于首键时按动作终点重查，
// 时间差仍以首键计（原版写法，插值比例为负）；末键之后接回首键，跨度为负时加动作长度。
// 原版每帧从上次结果继续扫描；动作内时间单调增加时与每次从首键扫描结果相同，回绕时原版也从首键重扫。
template<class Keys>
SourceOrderWindow SelectGameKeyWindow(const Keys& keys, std::size_t offset, std::size_t count,
    std::int64_t start, std::int64_t end, std::int64_t time) noexcept {
    SourceOrderWindow result;
    if (count == 0) return result;
    result.kind = SourceOrderWindowKind::Value;
    result.left = result.right = offset;
    if (count == 1) return result;
    const auto frame = [&](std::size_t i) { return static_cast<std::int64_t>(keys[i].frame); };
    const auto last = offset + count - 1;
    const auto scan = [&](std::int64_t at) {
        auto i = offset;
        while (i < last && at >= frame(i + 1)) ++i;
        return i;
    };
    auto left = scan(time);
    const bool beforeFirst = left == offset && frame(offset) > time;
    if (beforeFirst) left = scan(end);
    else if (frame(left) == time) { result.left = result.right = left; return result; }
    result.kind = SourceOrderWindowKind::Segment;
    result.left = left;
    result.right = left == last ? offset : left + 1;
    result.seam = beforeFirst || left == last;
    result.offset = time - frame(beforeFirst ? offset : left);
    result.span = frame(result.right) - frame(left);
    if (result.span < 0) result.span += end - start;
    return result;
}

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
