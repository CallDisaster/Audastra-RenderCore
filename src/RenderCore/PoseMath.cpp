// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "PoseMath.h"

namespace War3RenderCore::Detail {

// 保持 FORGE 冻结参考的 float 存储、double 中间量和运算顺序。
// 原 MdxPose.cpp 不改动，用作独立差分裁判；本模块不包含 WarVK 或第三方实现。
Quaternion Normalize(Quaternion value, Evaluation& e) noexcept {
    if (!CheckFinite(value, e)) return {};
    double squared = 0;
    for (const auto component : value) squared += static_cast<double>(component) * component;
    if (!std::isfinite(squared) || squared < 1e-20) { e.Fail(Code::PoseRotationInvalid); return {}; }
    const auto factor = 1.0 / std::sqrt(squared);
    for (auto& component : value) component = static_cast<float>(component * factor);
    (void)CheckFinite(value, e);
    return value;
}
Quaternion Slerp(Quaternion first, Quaternion second, double factor, Evaluation& e) noexcept {
    first = Normalize(first, e); second = Normalize(second, e);
    if (!e.Good()) return {};
    double cosine = 0;
    for (std::size_t i = 0; i < 4; ++i) cosine += static_cast<double>(first[i]) * second[i];
    if (cosine < 0) {
        for (auto& component : second) component = -component;
        cosine = -cosine;
    }
    cosine = std::clamp(cosine, 0.0, 1.0);
    double a = 1.0 - factor, b = factor;
    if (cosine < 0.9995) {
        const auto angle = std::acos(cosine), denominator = std::sin(angle);
        if (!(denominator > 0)) { e.Fail(Code::PoseRotationInvalid); return {}; }
        a = std::sin((1.0 - factor) * angle) / denominator;
        b = std::sin(factor * angle) / denominator;
    }
    Quaternion result{};
    for (std::size_t i = 0; i < 4; ++i) result[i] = static_cast<float>(a * first[i] + b * second[i]);
    return Normalize(result, e);
}
Quaternion Squad(const Quaternion& first, const Quaternion& outControl, const Quaternion& inControl,
    const Quaternion& second, double factor, Evaluation& e) noexcept {
    const auto endpoints = Slerp(first, second, factor, e);
    const auto controls = Slerp(outControl, inControl, factor, e);
    return Slerp(endpoints, controls, 2.0 * factor * (1.0 - factor), e);
}
Matrix4 Identity() noexcept { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Matrix4 LocalMatrix(const NodePose& node, Evaluation& e) noexcept {
    if (!e.Good()) return {};
    const auto& q = node.rotation;
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    auto matrix = Identity();
    matrix[0] = static_cast<float>((1 - 2 * (y * y + z * z)) * node.scaling[0]);
    matrix[1] = static_cast<float>((2 * (x * y + z * w)) * node.scaling[0]);
    matrix[2] = static_cast<float>((2 * (x * z - y * w)) * node.scaling[0]);
    matrix[4] = static_cast<float>((2 * (x * y - z * w)) * node.scaling[1]);
    matrix[5] = static_cast<float>((1 - 2 * (x * x + z * z)) * node.scaling[1]);
    matrix[6] = static_cast<float>((2 * (y * z + x * w)) * node.scaling[1]);
    matrix[8] = static_cast<float>((2 * (x * z + y * w)) * node.scaling[2]);
    matrix[9] = static_cast<float>((2 * (y * z - x * w)) * node.scaling[2]);
    matrix[10] = static_cast<float>((1 - 2 * (x * x + y * y)) * node.scaling[2]);
    for (std::size_t row = 0; row < 3; ++row) {
        double moved = node.translation[row] + node.pivot[row];
        for (std::size_t column = 0; column < 3; ++column)
            moved -= static_cast<double>(matrix[column * 4 + row]) * node.pivot[column];
        matrix[12 + row] = static_cast<float>(moved);
    }
    (void)CheckFinite(matrix, e);
    return matrix;
}
Matrix4 Multiply(const Matrix4& first, const Matrix4& second, Evaluation& e) noexcept {
    Matrix4 result{};
    if (!e.Good()) return result;
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            double value = 0;
            for (std::size_t inner = 0; inner < 4; ++inner)
                value += static_cast<double>(first[inner * 4 + row]) * second[column * 4 + inner];
            result[column * 4 + row] = static_cast<float>(value);
        }
    }
    (void)CheckFinite(result, e);
    return result;
}
namespace {
Vec3 Subtract(const Vec3& a, const Vec3& b) noexcept { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec3 Cross(const Vec3& a, const Vec3& b) noexcept {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
Vec3 UnitVector(const Vec3& value, Code code, Evaluation& e) noexcept {
    if (!CheckFinite(value, e)) return {};
    const auto length = std::hypot(static_cast<double>(value[0]), value[1], value[2]);
    if (!std::isfinite(length) || length < 1e-8) { e.Fail(code); return {}; }
    Vec3 result{};
    for (std::size_t i = 0; i < 3; ++i) result[i] = static_cast<float>(value[i] / length);
    (void)CheckFinite(result, e);
    return result;
}
Matrix4 AxisRotation(std::size_t axis, double cosine, double sine) noexcept {
    auto matrix = Identity();
    const auto a = (axis + 1) % 3, b = (axis + 2) % 3;
    matrix[a * 4 + a] = static_cast<float>(cosine); matrix[b * 4 + a] = static_cast<float>(-sine);
    matrix[a * 4 + b] = static_cast<float>(sine); matrix[b * 4 + b] = static_cast<float>(cosine);
    return matrix;
}
Matrix4 FacingBasis(const Vec3& towardEye, const Vec3& cameraUp, std::uint32_t flags, Evaluation& e) noexcept {
    if (!e.Good()) return {};
    const auto lock = flags & 0x70u;
    if (lock != 0 && lock != 0x10u && lock != 0x20u && lock != 0x40u) {
        e.Fail(Code::PoseCameraFlagsUnsupported); return {};
    }
    if (lock != 0) {
        const std::size_t axis = lock == 0x10u ? 0 : lock == 0x20u ? 1 : 2;
        const std::size_t a = axis == 1 ? 2 : 1, b = 3 - axis - a;
        const double towardA = towardEye[a], towardB = towardEye[b];
        const auto length = std::hypot(towardA, towardB);
        if (length < 1e-8) { e.Warn(Code::CameraAxisSingular); return Identity(); }
        const Vec3 reference{a == 0 ? 1.0f : 0.0f, a == 1 ? 1.0f : 0.0f, a == 2 ? 1.0f : 0.0f};
        const auto rotated = Cross(Vec3{axis == 0 ? 1.0f : 0.0f, axis == 1 ? 1.0f : 0.0f,
            axis == 2 ? 1.0f : 0.0f}, reference);
        return AxisRotation(axis, towardA / length, towardEye[b] / (length * rotated[b]));
    }
    const auto facing = UnitVector(towardEye, Code::PoseCameraInvalid, e);
    if (!e.Good()) return {};
    auto right = Cross(facing, cameraUp);
    if (std::hypot(static_cast<double>(right[0]), right[1], right[2]) < 1e-8)
        right = Cross(facing, std::abs(facing[2]) < 0.9f ? Vec3{0, 0, 1} : Vec3{0, 1, 0});
    right = UnitVector(right, Code::PoseCameraInvalid, e);
    const auto up = UnitVector(Cross(right, facing), Code::PoseCameraInvalid, e);
    auto matrix = Identity();
    for (std::size_t row = 0; row < 3; ++row) {
        matrix[row] = right[row]; matrix[4 + row] = facing[row]; matrix[8 + row] = up[row];
    }
    return matrix;
}
} // namespace

bool CheckCamera(const LocalCamera& camera, Evaluation& e) noexcept {
    if (!CheckFinite(camera.eye, e) || !CheckFinite(camera.up, e)) return false;
    for (const auto coordinate : camera.eye)
        if (std::abs(coordinate) > 400000.0f) { e.Fail(Code::PoseCameraInvalid); return false; }
    for (const auto coordinate : camera.up)
        if (std::abs(coordinate) > 10000.0f) { e.Fail(Code::PoseCameraInvalid); return false; }
    (void)UnitVector(camera.up, Code::PoseCameraInvalid, e);
    return e.Good();
}
Vec3 TransformPosition(const Matrix4& matrix, const Vec3& point, Evaluation& e) noexcept {
    if (!CheckFinite(point, e)) return {};
    Vec3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        const double value = static_cast<double>(matrix[12 + row])
            + static_cast<double>(matrix[row]) * point[0]
            + static_cast<double>(matrix[4 + row]) * point[1]
            + static_cast<double>(matrix[8 + row]) * point[2];
        result[row] = static_cast<float>(value);
    }
    (void)CheckFinite(result, e);
    return result;
}
Vec3 TransformNormal(const Matrix4& matrix, const Vec3& normal, Evaluation& e) noexcept {
    if (!CheckFinite(normal, e)) return {};
    // 仅原始零法线降级。非零法线被姿势压成零时，继续保留旧的退化拒绝。
    if (normal[0] == 0 && normal[1] == 0 && normal[2] == 0) return {};
    std::array<double, 3> result{};
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            result[row] += static_cast<double>(matrix[column * 4 + row]) * normal[column];
    const auto magnitude = std::hypot(result[0], result[1], result[2]);
    if (!std::isfinite(magnitude) || magnitude <= 1e-20) { e.Fail(Code::SkinNormalInvalid); return {}; }
    Vec3 normalized{};
    for (std::size_t row = 0; row < 3; ++row) normalized[row] = static_cast<float>(result[row] / magnitude);
    (void)CheckFinite(normalized, e);
    return normalized;
}
Matrix4 CameraFacingWorld(const NodePose& node, const Matrix4& staticWorld,
    const LocalCamera& camera, std::uint32_t flags, Evaluation& e) noexcept {
    const auto anchor = TransformPosition(staticWorld, node.pivot, e);
    const auto towardEye = Subtract(camera.eye, anchor);
    const auto basis = FacingBasis(towardEye, UnitVector(camera.up, Code::PoseCameraInvalid, e), flags, e);
    auto spin = node; spin.pivot = {}; spin.translation = {};
    const auto oriented = Multiply(basis, LocalMatrix(spin, e), e);
    if (!e.Good()) return {};
    auto world = oriented;
    for (std::size_t row = 0; row < 3; ++row) {
        double moved = anchor[row];
        for (std::size_t column = 0; column < 3; ++column)
            moved -= static_cast<double>(oriented[column * 4 + row]) * node.pivot[column];
        world[12 + row] = static_cast<float>(moved);
    }
    (void)CheckFinite(world, e);
    return world;
}
GroupMatrix Pack(const Matrix4& matrix) noexcept {
    GroupMatrix result;
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 3; ++row) result.values[column * 3 + row] = matrix[column * 4 + row];
    return result;
}
Matrix4 Unpack(const GroupMatrix& matrix) noexcept {
    auto result = Identity();
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 3; ++row) result[column * 4 + row] = matrix.values[column * 3 + row];
    return result;
}
} // namespace War3RenderCore::Detail
