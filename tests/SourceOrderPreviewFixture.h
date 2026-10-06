// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "MdxTestModel.h"
#include "War3MapCore/Render/MdxModel.h"
#include <array>

namespace SourceOrderTest {
// 原创数值/几何为空的四节点 MDX；仅帧序拓扑复现反馈边界，不包含游戏资产。
inline std::vector<std::byte> Bytes() {
    MdxTest::Writer w;
    w.Tag("MDLX"); w.Tag("VERS"); w.U32(4); w.U32(800);
    w.Tag("SEQS"); {
        const auto at = w.Begin();
        const auto sequence = [&](const char* name, std::uint32_t start, std::uint32_t end) {
            w.Fixed(name, 80); w.U32(start); w.U32(end); w.F32(0); w.U32(1);
            w.F32(0); w.U32(0); for (int i = 0; i < 7; ++i) w.F32(0);
        };
        sequence("Birth", 4168, 6253); sequence("Stand", 14585, 18754);
        sequence("Safe", 20836, 22919); w.EndChunk(at);
    }
    w.Tag("GLBS"); w.U32(4); w.U32(0);
    w.Tag("BONE"); {
        const auto at = w.Begin();
        for (const auto id : {0u, 19u, 22u}) {
            const auto node = w.Begin(); w.Fixed("Synthetic", 80); w.U32(id);
            w.U32(id == 0 ? 45u : 0u); w.U32(0x100);
            if (id != 0) {
                w.Tag("KGSC"); w.U32(6); w.U32(1); w.U32(UINT32_MAX);
                const std::array<std::uint32_t, 6> frames{14585, 15003, 15419, 15003, 15454, 18754};
                const std::array<float, 6> values{1, 2, 4, 3, 5, 1};
                for (std::size_t i = 0; i < frames.size(); ++i) {
                    w.U32(frames[i]); for (int c = 0; c < 3; ++c) w.F32(values[i]);
                }
            }
            w.EndInclusive(node); w.U32(UINT32_MAX); w.U32(UINT32_MAX);
        }
        w.EndChunk(at);
    }
    w.Tag("HELP"); {
        const auto at = w.Begin(); const auto node = w.Begin();
        w.Fixed("BirthOrigin", 80); w.U32(45); w.U32(UINT32_MAX); w.U32(0);
        w.Tag("KGTR"); w.U32(4); w.U32(1); w.U32(UINT32_MAX);
        const std::array<std::uint32_t, 4> frames{4168, 5419, 6253, 4168};
        const std::array<float, 4> values{-300, -31, 20, -300};
        for (std::size_t i = 0; i < frames.size(); ++i) {
            w.U32(frames[i]); w.F32(0); w.F32(0); w.F32(values[i]);
        }
        w.Tag("KGRT"); w.U32(1); w.U32(1); w.U32(0); w.U32(0);
        w.F32(0); w.F32(0); w.F32(0); w.F32(1);
        w.Tag("KGSC"); w.U32(1); w.U32(1); w.U32(0); w.U32(0);
        w.F32(1); w.F32(1); w.F32(1);
        w.EndInclusive(node); w.EndChunk(at);
    }
    w.Tag("PIVT"); w.U32(46 * 12);
    for (int i = 0; i < 46 * 3; ++i) w.F32(0);
    return std::move(w.bytes);
}
}
