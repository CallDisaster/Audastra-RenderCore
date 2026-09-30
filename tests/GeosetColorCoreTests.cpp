// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3RenderCore/GeosetColor.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}
int main() {
    using namespace War3RenderCore;
    try {
        const std::array<float, 3> rgb{1.0f, 0.211765f, 0.0f};
        const auto colored = CompileGeosetColor(2, rgb, false);
        Require(colored.error == GeosetColorError::None && colored.enabled == 1 &&
            std::memcmp(colored.rgb.data(), rgb.data(), sizeof(rgb)) == 0, "静态 RGB 位值被改写");
        auto values = rgb;
        values[1] = std::numeric_limits<float>::quiet_NaN();
        const auto inactive = CompileGeosetColor(0, values, false);
        Require(inactive.error == GeosetColorError::None && !inactive.enabled &&
            inactive.rgb == std::array<float, 3>{1, 1, 1}, "未启用颜色不是白色单位元");
        for (const auto flags : {1u, 3u, 4u, 0x80000000u})
            Require(CompileGeosetColor(flags, rgb, false).error == GeosetColorError::Uninterpreted,
                "未知 GEOA 位被放行");
        Require(CompileGeosetColor(2, rgb, true).error == GeosetColorError::Uninterpreted,
            "KGAC 或未知尾部被放行");
        for (const float bad : {-0.1f, 1.1f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()}) {
            values[0] = bad;
            Require(CompileGeosetColor(2, values, false).error == GeosetColorError::InvalidNumber,
                "无效活动颜色被钳位或放行");
        }
        std::cout << "PASS geoa-core-color rgb_bits=1 inactive_white=1 unknown_data_rejected=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
