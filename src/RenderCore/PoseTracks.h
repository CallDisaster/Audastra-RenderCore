// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "PoseMath.h"

namespace War3RenderCore::Detail {
[[nodiscard]] bool EvaluateComputed(const ModelData& model, const PoseRequest& request,
    std::uint32_t frame, std::span<NodePose> nodes, std::span<GroupMatrix> groups, Evaluation& evaluation) noexcept;
[[nodiscard]] std::uint32_t SequenceFrame(const Sequence& sequence, const PoseRequest& request) noexcept;
// 与节点同一取键/插值实现；TXAN 坐标动画复用。旋转返回原始插值结果，不另行归一化。
[[nodiscard]] std::array<float, 3> SampleVec3Track(const Track<3>& track, std::array<float, 3> fallback,
    const Sequence& sequence, std::uint32_t sequenceIndex, std::uint32_t frame, std::uint64_t globalTime,
    Evaluation& evaluation) noexcept;
[[nodiscard]] Quaternion SampleQuaternionTrack(const Track<4>& track, Quaternion fallback,
    const Sequence& sequence, std::uint32_t sequenceIndex, std::uint32_t frame, std::uint64_t globalTime,
    Evaluation& evaluation) noexcept;
} // namespace War3RenderCore::Detail
