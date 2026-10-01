// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace War3RenderCore {

// 只描述 GEOA 静态 RGB，保持文件通道顺序；KGAC 的 BGR 轨迹不在本接口支持范围。
// 不含指针，颜色保持原 float 位值；既不钳位，也不做伽马转换或 8 位量化。
enum class GeosetColorError : std::uint32_t { None, Uninterpreted, InvalidNumber };
struct GeosetColor {
    std::array<float, 3> rgb{1, 1, 1};
    GeosetColorError error = GeosetColorError::None;
    std::uint32_t enabled = 0;
};
static_assert(sizeof(GeosetColor) == 20);

// 独立于参考 SampleMdxGeosetColor 的核心加载校验。缺少 GEOA 时使用默认构造值。
// 未启用颜色的占位字节不参与计算；未知位、未知尾部、KGAC 仍拒绝。
[[nodiscard]] inline GeosetColor CompileGeosetColor(std::uint32_t flags,
    const std::array<float, 3>& rgb, bool hasUninterpretedData) noexcept {
    GeosetColor result;
    if ((flags & ~2u) != 0 || hasUninterpretedData) {
        result.error = GeosetColorError::Uninterpreted;
        return result;
    }
    if ((flags & 2u) == 0) return result;
    for (const float component : rgb) {
        if (!std::isfinite(component) || component < 0.0f || component > 1.0f) {
            result.error = GeosetColorError::InvalidNumber;
            return result;
        }
    }
    result.rgb = rgb;
    result.enabled = 1;
    return result;
}

} // namespace War3RenderCore
