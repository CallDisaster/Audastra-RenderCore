// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "PoseMath.h"

namespace War3RenderCore::Detail {
// 预分配求值路径，不依赖参考实现。静态汇编依据及预览边界见 Inheritance.md。
// 根输入固定为模型局部单位变换；0x1/0x2/0x4 分别为不继承平移/缩放/旋转。
Matrix4 InheritedWorld(const NodePose& node, const NodePose* parent, std::uint32_t flags, Evaluation& e) noexcept {
    auto matrix = parent ? parent->worldMatrix : Identity();
    const Vec3 parentPivot = parent ? parent->pivot : Vec3{};
    // 原生遍历栈保存变换后的父轴心，而 worldMatrix 已包含末尾 T(-pivot)。
    if (parent) {
        const auto anchor = TransformPosition(matrix, parentPivot, e);
        for (std::size_t row = 0; row < 3; ++row) matrix[12 + row] = anchor[row];
    }
    const auto strip = [&](std::uint32_t mode) {
        mode &= 6u;
        if (mode == 6u) {
            for (std::size_t column = 0; column < 3; ++column)
                for (std::size_t row = 0; row < 3; ++row) matrix[column * 4 + row] = column == row ? 1.0f : 0.0f;
        } else if (mode != 0) {
            for (std::size_t column = 0; column < 3; ++column) {
                const auto at = column * 4;
                const auto length = std::hypot(static_cast<double>(matrix[at]), matrix[at + 1], matrix[at + 2]);
                if (!std::isfinite(length) || (mode == 2u && length <= 1e-20))
                    { e.Fail(Code::PoseInheritanceSingular); return; }
                for (std::size_t row = 0; row < 3; ++row)
                    matrix[at + row] = mode == 2u ? static_cast<float>(matrix[at + row] / length)
                        : (column == row ? static_cast<float>(length) : 0.0f);
            }
        }
    };
    const auto translate = [&](const Vec3& delta) {
        const auto moved = TransformPosition(matrix, delta, e);
        for (std::size_t row = 0; row < 3; ++row) matrix[12 + row] = moved[row];
    };
    if ((flags & 1u) != 0) {
        matrix[12] = matrix[13] = matrix[14] = 0.0f;
        strip(flags); // 不继承平移时，旋转/缩放剥离前移到轴心平移之前。
    }
    Vec3 delta{};
    for (std::size_t axis = 0; axis < 3; ++axis)
        delta[axis] = (node.pivot[axis] - ((flags & 1u) ? 0.0f : parentPivot[axis])) + node.translation[axis];
    translate(delta);
    if ((flags & 7u) == 4u) strip(4u);
    auto rotation = node;
    rotation.translation = {}; rotation.pivot = {}; rotation.scaling = {1, 1, 1};
    matrix = Multiply(matrix, LocalMatrix(rotation, e), e);
    // 原版此分支在本节点旋转之后执行；不是分解父矩阵后一次性重组 TRS。
    if ((flags & 3u) == 2u) strip(flags);
    for (std::size_t column = 0; column < 3; ++column)
        for (std::size_t row = 0; row < 3; ++row) matrix[column * 4 + row] *= node.scaling[column];
    translate({-node.pivot[0], -node.pivot[1], -node.pivot[2]});
    (void)CheckFinite(matrix, e);
    return matrix;
}
} // namespace War3RenderCore::Detail
