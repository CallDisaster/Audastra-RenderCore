// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "MdxTestModel.h"

namespace GeosetChannelFixture {
inline std::vector<std::byte> Bytes(std::uint32_t mode, bool global) {
    const auto original=MdxTest::BuildTestMdx(); MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at=4;at<original.size();) {
        std::uint32_t size=0;std::memcpy(&size,original.data()+at+4,4);
        if (std::memcmp(original.data()+at,"GEOA",4)!=0) out.Raw(original.data()+at,size+8);
        else {
            out.Tag("GEOA");const auto chunk=out.Begin(),object=out.Begin();
            out.F32(1);out.U32(2);out.F32(1);out.F32(0.211765f);out.F32(0);out.U32(1);
            out.Tag("KGAO");out.U32(2);out.U32(mode);out.U32(global ? 0 : UINT32_MAX);
            for (const auto time:{0u,1000u}) {
                out.U32(time);out.F32(time ? 1.0f : 0.0f);
                if (mode>=2) { out.F32(mode==2 ? 0.0f : 0.25f);out.F32(mode==2 ? 0.0f : 0.75f); }
            }
            out.Tag("KGAC");out.U32(2);out.U32(mode);out.U32(global ? 0 : UINT32_MAX);
            for (const auto time:{0u,1000u}) {
                out.U32(time);out.F32(time ? 0.0f : 1.0f);out.F32(0.25f);out.F32(time ? 1.0f : 0.0f);
                if (mode>=2) for (int i=0;i<6;++i) out.F32(mode==2 ? 0 : 0.5f);
            }
            out.EndInclusive(object);out.EndChunk(chunk);
        }
        at+=size+8;
    }
    if (global) { out.Tag("GLBS");const auto chunk=out.Begin();out.U32(1000);out.EndChunk(chunk); }
    return std::move(out.bytes);
}
}
