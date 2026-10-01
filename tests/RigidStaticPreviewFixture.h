// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "MdxTestModel.h"

namespace MdxTest {
// 原创无控制器双 geoset；原始 MTGC/MATS 为空，不来自游戏资源。
inline std::vector<std::byte> BuildRigidStaticPreviewMdx() {
    Writer w; w.Tag("MDLX"); w.Tag("VERS"); w.U32(4); w.U32(800);
    w.Tag("MODL"); { const auto at = w.Begin(); w.Fixed("Static", 80); w.Fixed("", 260); w.F32(20);
        for (int i = 0; i < 6; ++i) w.F32(0); w.U32(0); w.EndChunk(at); }
    w.Tag("SEQS"); { const auto at = w.Begin(); w.Fixed("Stand", 80); w.U32(10); w.U32(1010);
        w.F32(0); w.U32(0); w.F32(0); w.U32(0); w.F32(0);
        for (int i = 0; i < 6; ++i) w.F32(0); w.EndChunk(at); }
    w.Tag("TEXS"); w.U32(268); w.U32(0); w.Fixed("Textures\\Static.blp", 260); w.U32(0);
    w.Tag("MTLS"); { const auto at = w.Begin(); const auto material = w.Begin();
        w.U32(0); w.U32(0); w.Tag("LAYS"); w.U32(1); const auto layer = w.Begin();
        w.U32(0); w.U32(0); w.U32(0); w.U32(UINT32_MAX); w.U32(0); w.F32(1);
        w.EndInclusive(layer); w.EndInclusive(material); w.EndChunk(at); }
    w.Tag("GEOS"); { const auto at = w.Begin();
        for (int g = 0; g < 2; ++g) {
            const auto geoset = w.Begin(); w.Tag("VRTX"); w.U32(3);
            const float p[3][3]{{0, 0, 0}, {10, 0, 0}, {0, 10, 5}};
            for (const auto& vertex : p) { w.F32(vertex[0]); w.F32(vertex[1]); w.F32(vertex[2] + static_cast<float>(g)); }
            w.Tag("NRMS"); w.U32(3);
            for (int i = 0; i < 3; ++i) { w.F32(0); w.F32(0); w.F32(i == 2 ? 0.0f : 2.0f); }
            w.Tag("PTYP"); w.U32(1); w.U32(4); w.Tag("PCNT"); w.U32(1); w.U32(3);
            w.Tag("PVTX"); w.U32(3); w.U16(0); w.U16(1); w.U16(2);
            w.Tag("GNDX"); w.U32(3); w.Raw("\0\0\0", 3);
            w.Tag("MTGC"); w.U32(0); w.Tag("MATS"); w.U32(0);
            w.U32(0); w.U32(0); w.U32(0); for (int i = 0; i < 7; ++i) w.F32(0); w.U32(0);
            w.Tag("UVAS"); w.U32(1); w.Tag("UVBS"); w.U32(3);
            for (int i = 0; i < 3; ++i) { w.F32(static_cast<float>(i)); w.F32(0); }
            w.EndInclusive(geoset);
        }
        w.EndChunk(at);
    }
    return std::move(w.bytes);
}
} // namespace MdxTest
