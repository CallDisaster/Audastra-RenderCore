// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "PoseMath.h"

namespace War3RenderCore::Detail {
[[nodiscard]] bool EvaluateComputed(const ModelData& model, const PoseRequest& request,
    std::uint32_t frame, std::span<NodePose> nodes, std::span<GroupMatrix> groups, Evaluation& evaluation) noexcept;
[[nodiscard]] std::uint32_t SequenceFrame(const Sequence& sequence, const PoseRequest& request) noexcept;
} // namespace War3RenderCore::Detail
