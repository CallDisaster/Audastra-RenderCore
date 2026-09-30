// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3RenderCore/Result.h"
#include <array>
#include <cstddef>
#include <span>
#include <string>

namespace War3Map::Render {
// 只识别输入中精确的零向量（包括带符号零）；很小的非零法线不在降级范围。
[[nodiscard]] inline std::size_t MdxZeroNormalCount(std::span<const std::array<float, 3>> normals) noexcept {
    std::size_t count = 0;
    for (const auto& n : normals) count += n[0] == 0 && n[1] == 0 && n[2] == 0;
    return count;
}
[[nodiscard]] inline Diagnostic MdxZeroNormalWarning(std::size_t geoset, std::size_t count) {
    return {DiagnosticSeverity::Warning, "Mdx.Skin", "MDX_SKIN_ZERO_NORMAL_PREVIEW",
        "geoset=" + std::to_string(geoset) + " vertices=" + std::to_string(count)
            + "：输入零法线保留为零；着色器使用安全预览法线，非原版一致性证明。", std::nullopt};
}
} // namespace War3Map::Render
