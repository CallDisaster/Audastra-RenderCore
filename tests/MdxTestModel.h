// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

// 测试共用：原创合成 MDX v800 模型（两 geoset、一材质两层、队伍色替换贴图），不来自任何游戏文件。
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace MdxTest {

// 原创合成 MDX 写入器：只用于测试解析器，不来自任何游戏文件。
class Writer {
public:
    std::vector<std::byte> bytes;
    void Raw(const void* data, std::size_t size) {
        const auto* begin = static_cast<const std::byte*>(data);
        bytes.insert(bytes.end(), begin, begin + size);
    }
    void Tag(const char* tag) { Raw(tag, 4); }
    void U32(std::uint32_t value) { Raw(&value, 4); }
    void F32(float value) { Raw(&value, 4); }
    void U16(std::uint16_t value) { Raw(&value, 2); }
    void Fixed(const std::string& text, std::size_t length) {
        std::vector<char> buffer(length, 0);
        std::memcpy(buffer.data(), text.data(), std::min(text.size(), length));
        Raw(buffer.data(), length);
    }
    std::size_t Begin() { const auto at = bytes.size(); U32(0); return at; }
    // 回填从 at 起（含 4 字节自身）的“包含式”大小。
    void EndInclusive(std::size_t at) { const auto size = static_cast<std::uint32_t>(bytes.size() - at); std::memcpy(bytes.data() + at, &size, 4); }
    // 回填块大小（不含 4 字节自身）。
    void EndChunk(std::size_t at) { const auto size = static_cast<std::uint32_t>(bytes.size() - at - 4); std::memcpy(bytes.data() + at, &size, 4); }
};

struct Options {
    std::uint32_t version = 800;
    std::uint16_t badIndex = 0;       // 非 0 时写入越界索引
    std::uint32_t geosetMaterial = 0;
    std::uint32_t layerTexture = 1;
    bool animatedHelper = false;
    bool truncate = false;
    std::uint32_t selectionGroup = 0, selectionFlags = 0;
};

inline std::vector<std::byte> BuildTestMdx(const Options& options = {}) {
    Writer w;
    w.Tag("MDLX");
    w.Tag("VERS"); w.U32(4); w.U32(options.version);
    w.Tag("MODL"); { const auto at = w.Begin(); w.Fixed("Crate", 80); w.Fixed("", 260); w.F32(50);
        w.F32(-10); w.F32(-10); w.F32(0); w.F32(10); w.F32(10); w.F32(20); w.U32(150); w.EndChunk(at); }
    w.Tag("SEQS"); { const auto at = w.Begin();
        w.Fixed("Death", 80); w.U32(2000); w.U32(3000); w.F32(0); w.U32(0); w.F32(0); w.U32(0); w.F32(0);
        for (int i = 0; i < 6; ++i) w.F32(0);
        w.Fixed("Stand - 1", 80); w.U32(0); w.U32(1000); w.F32(0); w.U32(0); w.F32(0); w.U32(0); w.F32(0);
        for (int i = 0; i < 6; ++i) w.F32(0);
        w.EndChunk(at); }
    w.Tag("TEXS"); { const auto at = w.Begin();
        w.U32(0); w.Fixed("Textures\\Crate.blp", 260); w.U32(3);
        w.U32(1); w.Fixed("", 260); w.U32(0);
        w.EndChunk(at); }
    w.Tag("MTLS"); { const auto at = w.Begin();
        const auto material = w.Begin(); w.U32(0); w.U32(0); w.Tag("LAYS"); w.U32(2);
        { const auto layer = w.Begin(); w.U32(0); w.U32(0x10); w.U32(options.layerTexture); w.U32(0xFFFFFFFF); w.U32(0); w.F32(1); w.EndInclusive(layer); }
        { const auto layer = w.Begin(); w.U32(1); w.U32(0); w.U32(0); w.U32(0xFFFFFFFF); w.U32(0); w.F32(0.5f);
          w.Tag("KMTF"); w.U32(1); w.U32(0); w.U32(0xFFFFFFFF); w.U32(0); w.U32(0); w.EndInclusive(layer); }
        w.EndInclusive(material);
        w.EndChunk(at); }
    w.Tag("GEOS"); { const auto at = w.Begin();
        for (int g = 0; g < 2; ++g) {
            const auto geoset = w.Begin();
            w.Tag("VRTX"); w.U32(4);
            const float positions[4][3]{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {10, 10, 5}};
            for (const auto& p : positions) { w.F32(p[0]); w.F32(p[1]); w.F32(p[2] + static_cast<float>(g)); }
            w.Tag("NRMS"); w.U32(4); for (int i = 0; i < 4; ++i) { w.F32(0); w.F32(0); w.F32(1); }
            // geoset 1 额外带一个三角带图元组（不显示，只计数）。
            w.Tag("PTYP"); w.U32(g == 0 ? 1 : 2); w.U32(4); if (g == 1) w.U32(5);
            w.Tag("PCNT"); w.U32(g == 0 ? 1 : 2); w.U32(6); if (g == 1) w.U32(3);
            w.Tag("PVTX"); w.U32(g == 0 ? 6 : 9);
            const std::uint16_t indices[6]{0, 1, 2, 1, 3, 2};
            for (auto index : indices) w.U16(options.badIndex != 0 && g == 0 ? options.badIndex : index);
            if (g == 1) { w.U16(0); w.U16(1); w.U16(2); }
            w.Tag("GNDX"); w.U32(4); for (int i = 0; i < 4; ++i) w.Raw("\0", 1);
            w.Tag("MTGC"); w.U32(1); w.U32(1);
            w.Tag("MATS"); w.U32(1); w.U32(0);
            w.U32(options.geosetMaterial); w.U32(options.selectionGroup); w.U32(options.selectionFlags);
            for (int i = 0; i < 7; ++i) w.F32(0);
            w.U32(1); for (int i = 0; i < 7; ++i) w.F32(0);
            w.Tag("UVAS"); w.U32(1);
            w.Tag("UVBS"); w.U32(4);
            const float uvs[4][2]{{0, 1}, {1, 1}, {0, 0}, {1, 0}};
            for (const auto& uv : uvs) { w.F32(uv[0]); w.F32(uv[1]); }
            w.EndInclusive(geoset);
        }
        w.EndChunk(at); }
    // geoset 1 在 Stand 起点透明度为 0（死亡时才出现）；geoset 0 无动画。
    w.Tag("GEOA"); { const auto at = w.Begin();
        const auto animation = w.Begin(); w.F32(1); w.U32(0); w.F32(1); w.F32(1); w.F32(1); w.U32(1);
        w.Tag("KGAO"); w.U32(2); w.U32(0); w.U32(0xFFFFFFFF); w.U32(0); w.F32(0); w.U32(2000); w.F32(1);
        w.EndInclusive(animation);
        w.EndChunk(at); }
    w.Tag("BONE"); { const auto at = w.Begin();
        const auto node = w.Begin(); w.Fixed("Geometry", 80); w.U32(0);
        w.U32(options.animatedHelper ? 1 : 0xFFFFFFFFu); w.U32(0);
        w.EndInclusive(node); w.U32(0xFFFFFFFFu); w.U32(0xFFFFFFFFu);
        w.EndChunk(at); }
    if (options.animatedHelper) {
        w.Tag("HELP"); const auto at = w.Begin();
        const auto node = w.Begin(); w.Fixed("Motion", 80); w.U32(1); w.U32(0xFFFFFFFFu); w.U32(0);
        w.Tag("KGTR"); w.U32(2); w.U32(1); w.U32(0xFFFFFFFFu);
        w.U32(0); w.F32(0); w.F32(0); w.F32(0);
        w.U32(1000); w.F32(10); w.F32(0); w.F32(0);
        w.EndInclusive(node); w.EndChunk(at);
    }
    w.Tag("PIVT"); { const auto at = w.Begin();
        for (int node = 0; node < (options.animatedHelper ? 2 : 1); ++node)
            for (int axis = 0; axis < 3; ++axis) w.F32(0);
        w.EndChunk(at); }
    w.Tag("UNKN"); w.U32(8); w.U32(0); w.U32(0); // 未解释的块按大小跳过
    if (options.truncate) w.bytes.resize(w.bytes.size() - 6);
    return std::move(w.bytes);
}

} // namespace MdxTest
