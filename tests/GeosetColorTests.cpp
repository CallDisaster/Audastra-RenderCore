// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "MdxTestModel.h"
#include "War3MapCore/Render/MdxModel.h"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace War3Map::Render;
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::vector<std::byte> ColorModel(std::uint32_t flags, const std::array<float, 3>& bgr) {
    auto bytes = MdxTest::BuildTestMdx();
    for (std::size_t at = 4; at + 8 <= bytes.size();) {
        std::uint32_t size = 0; std::memcpy(&size, bytes.data() + at + 4, 4);
        if (std::memcmp(bytes.data() + at, "GEOA", 4) == 0) {
            Require(size >= 28, "fixture GEOA too short");
            std::memcpy(bytes.data() + at + 16, &flags, 4);
            std::memcpy(bytes.data() + at + 20, bgr.data(), 12);
            return bytes;
        }
        at += 8 + size;
    }
    throw std::runtime_error("fixture missing GEOA");
}
void ParseColor() {
    const auto bytes = ColorModel(2, {1.0f, 0.211765f, 0.0f});
    const auto parsed = ParseMdx(bytes);
    Require(parsed.Succeeded(), "color fixture rejected by parser");
    const auto& model = *parsed.value;
    const std::array<float, 3> rgb{0.0f, 0.211765f, 1.0f};
    const auto read = SampleMdxGeosetColor(model, {1, 1, 250, 0});
    Require(read.Succeeded() && std::memcmp(read.value->data(), rgb.data(), sizeof(rgb)) == 0,
        "GEOA BGR to RGB mismatch");
    Require(model.geosets[1].animation->flags == 2 &&
        std::memcmp(model.geosets[1].animation->staticColor.data(), rgb.data(), sizeof(rgb)) == 0,
        "parser discarded static color or flag");
    const auto plain = SampleMdxGeosetColor(model, {0, 1, 250, 0});
    Require(plain.Succeeded() && *plain.value == std::array<float,3>{1,1,1}, "absent GEOA not identity");
    auto inactive = ParseMdx(ColorModel(0, {0.1f,0.2f,0.3f}));
    Require(inactive.Succeeded(), "inactive fixture parse failed");
    const auto white = SampleMdxGeosetColor(*inactive.value, {1,1,0,0});
    Require(white.Succeeded() && *white.value == std::array<float,3>{1,1,1}, "inactive color applied");
    for (const auto flags : {1u,3u,4u,0x80000000u}) {
        auto bad = model; bad.geosets[1].animation->flags = flags;
        const auto result = SampleMdxGeosetColor(bad, {1,1,0,0});
        Require(!result.Succeeded() && result.diagnostics[0].code == "MDX_GEOA_UNINTERPRETED", "unknown flag accepted");
    }
    for (const float invalid : {-0.1f,1.1f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        auto bad = model; bad.geosets[1].animation->staticColor[1] = invalid;
        const auto result = SampleMdxGeosetColor(bad, {1,1,0,0});
        Require(!result.Succeeded() && result.diagnostics[0].code == "MDX_GEOA_COLOR_INVALID", "invalid color accepted");
    }
    auto unknown = model; unknown.geosets[1].animation->hasUninterpretedData = true;
    Require(!SampleMdxGeosetColor(unknown,{1,1,0,0}).Succeeded(), "uninterpreted KGAC accepted");
    unknown = model; unknown.sequences[1].end = 0; unknown.sequences[1].start = 10;
    Require(!SampleMdxGeosetColor(unknown,{1,1,0,0}).Succeeded(), "invalid sequence accepted");
    Require(!SampleMdxGeosetColor(model,{9,1,0,0}).Succeeded(), "invalid geoset accepted");
    // A 链路完成后，静态 RGB 不再封住 alpha；未知位和未知尾部的拒绝仍在上方覆盖。
    const auto blocked = SampleMdxGeosetAlpha(model,{1,1,250,0});
    Require(blocked.Succeeded() && *blocked.value == 0.0f, "静态 RGB 的 KGAO 未正确求值");
    std::cout << "geoa_static_color_bgr=1 flags_retained=1 inactive_identity=1 strict_rejections=1 static_rgb_alpha_enabled=1\n";
}
}
int main() {
    try { ParseColor(); std::cout << "PASS geoa-color-parse\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
