// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
// 节点轨迹编译（原版 KGTR/KGRT/KGSC 编译器 6F780970/6F780770/6F780870）。TXAN 的 KTAT/KTAR/KTAS
// 在原版走同一组编译器，因此节点与贴图坐标动画共用这里的唯一实现。Budget 需提供 Charge(count, stride)，
// 超限时由调用方的 Charge 抛出各自的编译失败。
#include "ModelData.h"
#include "SourceOrderNodeTrack.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace War3RenderCore::Detail {
using War3Map::Render::MdxNodeTrack;
using War3Map::Render::MdxSequence;

template <std::size_t N>
inline bool ControlValid(const std::array<float, N>& values) {
    if (!Finite(values)) return false;
    double squared = 0;
    for (const auto x : values) squared += static_cast<double>(x) * x;
    return std::isfinite(squared) && squared >= 1e-20;
}

template <std::size_t N, class Budget>
void CompileSequenceKeys(Track<N>& track, const std::vector<MdxSequence>& sequences, Budget& budget) {
    // 原版按动作文件顺序切连续区间；只有与按时间二分不同（逆序键、重叠或倒序排列的动作）时才保存。
    const bool sorted = std::is_sorted(track.keys.begin(), track.keys.end(),
        [](const auto& left, const auto& right) { return left.frame < right.frame; });
    bool needed = !sorted;
    if (sorted) ForEachGameSequenceRange(track.keys, sequences, [&](std::size_t index, std::size_t first, std::size_t count) {
        const auto& sequence = sequences[index];
        const auto begin = std::lower_bound(track.keys.begin(), track.keys.end(), static_cast<std::int64_t>(sequence.start),
            [](const auto& key, auto value) { return key.frame < value; });
        const auto end = std::upper_bound(begin, track.keys.end(), static_cast<std::int64_t>(sequence.end),
            [](auto value, const auto& key) { return value < key.frame; });
        if (end != begin && (static_cast<std::size_t>(begin - track.keys.begin()) != first
            || static_cast<std::size_t>(end - begin) != count)) needed = true;
        if (end == begin && count != 0) needed = true;
        return !needed;
    });
    if (!needed) return;
    budget.Charge(sequences.size(), sizeof(Range32) + sizeof(std::uint8_t));
    track.sequenceKeys.reserve(sequences.size());
    track.sequenceDescending.reserve(sequences.size());
    ForEachGameSequenceRange(track.keys, sequences, [&](std::size_t, std::size_t first, std::size_t count) {
        std::int64_t left = 0, right = 0;
        track.sequenceKeys.push_back({static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(count)});
        track.sequenceDescending.push_back(FirstDescendingEdge(track.keys, first, count, left, right) ? 1 : 0);
        return true;
    });
}

template <std::size_t N, class Budget>
Track<N> CompileTrack(const std::optional<MdxNodeTrack<N>>& source,
    const std::vector<std::uint32_t>& periods, const std::vector<MdxSequence>& sequences, Budget& budget) {
    Track<N> track;
    if (!source) return track;
    track.present = true;
    track.interpolation = source->interpolation;
    if (static_cast<std::uint32_t>(track.interpolation) > 3) {
        track.error = Code::PoseInterpolationUnsupported;
        return track;
    }
    track.global = source->globalSequence != NoIndex;
    if (track.global) {
        if (source->globalSequence >= periods.size()) {
            track.error = Code::PoseGlobalSequenceInvalid;
            return track;
        }
        track.period = periods[source->globalSequence];
    }
    track.keys.reserve(source->keys.size());
    if (track.global && !source->keys.empty()) track.origin = source->keys.front().frame;
    std::int64_t previous = std::numeric_limits<std::int64_t>::min();
    for (const auto& key : source->keys) {
        if (key.frame < previous) {
            if (track.global) {
                track.error = Code::PoseKeysInvalid;
                track.keys.clear();
                return track;
            }
        }
        previous = key.frame;
        const auto frame = static_cast<std::int64_t>(key.frame) - track.origin;
        if (track.global && frame > INT32_MAX) {
            track.error = Code::PoseGlobalSpanUnsupported;
            track.keys.clear();
            return track;
        }
        track.keys.push_back({static_cast<std::int32_t>(frame), key.value, key.inTangent, key.outTangent,
            Finite(key.value), ControlValid(key.inTangent), ControlValid(key.outTangent)});
    }
    if (!track.global) CompileSequenceKeys(track, sequences, budget);
    return track;
}
} // namespace War3RenderCore::Detail
