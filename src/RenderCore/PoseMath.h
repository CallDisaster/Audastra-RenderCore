// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "ModelData.h"

#include <algorithm>
#include <cmath>

namespace War3RenderCore::Detail {

template <std::size_t N>
bool Finite(const std::array<float, N>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](float x) { return std::isfinite(x); });
}
template <std::size_t N>
bool CheckFinite(const std::array<float, N>& value, Evaluation& evaluation) noexcept {
    if (!Finite(value)) evaluation.Fail(Code::PoseNumberInvalid);
    return evaluation.Good();
}
[[nodiscard]] Quaternion Normalize(Quaternion value, Evaluation& evaluation) noexcept;
[[nodiscard]] Quaternion Slerp(Quaternion first, Quaternion second, double factor, Evaluation& evaluation) noexcept;
[[nodiscard]] Quaternion Squad(const Quaternion& first, const Quaternion& outControl,
    const Quaternion& inControl, const Quaternion& second, double factor, Evaluation& evaluation) noexcept;
[[nodiscard]] Matrix4 Identity() noexcept;
[[nodiscard]] Matrix4 LocalMatrix(const NodePose& node, Evaluation& evaluation) noexcept;
[[nodiscard]] Matrix4 InheritedWorld(const NodePose& node, const NodePose* parent,
    std::uint32_t flags, Evaluation& evaluation) noexcept;
[[nodiscard]] Matrix4 Multiply(const Matrix4& first, const Matrix4& second, Evaluation& evaluation) noexcept;
[[nodiscard]] bool CheckCamera(const LocalCamera& camera, Evaluation& evaluation) noexcept;
[[nodiscard]] Matrix4 CameraFacingWorld(const NodePose& node, const Matrix4& staticWorld,
    const LocalCamera& camera, std::uint32_t flags, Evaluation& evaluation) noexcept;
[[nodiscard]] Vec3 TransformPosition(const Matrix4& matrix, const Vec3& point, Evaluation& evaluation) noexcept;
[[nodiscard]] Vec3 TransformNormal(const Matrix4& matrix, const Vec3& normal, Evaluation& evaluation) noexcept;
[[nodiscard]] GroupMatrix Pack(const Matrix4& matrix) noexcept;
[[nodiscard]] Matrix4 Unpack(const GroupMatrix& matrix) noexcept;

} // namespace War3RenderCore::Detail
