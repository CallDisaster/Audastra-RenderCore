// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "MdxTestModel.h"

namespace GeosetChannelFixture {
inline std::vector<std::byte> Bytes(std::uint32_t mode, bool global) {
    const auto original=MdxTest::BuildTestMdx(); MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at=4;at<original.size();) {
        std::uint32_t size=0;std::memcpy(&size,original.data()+at+4,4);
        if (std::memcmp(original.data()+at,"SEQS",4)==0 && size==2*132) {
            // 原版按动作文件顺序切 GEOA 键区间：Stand [0,1000] 须排在 Death [2000,3000] 前才拿得到键。
            out.Raw(original.data()+at,8);out.Raw(original.data()+at+8+132,132);out.Raw(original.data()+at+8,132);
        } else if (std::memcmp(original.data()+at,"GEOA",4)!=0) out.Raw(original.data()+at,size+8);
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
                if (mode>=2) {
                    // 同一 GEOA 的静态字段 RGB；KGAC 值/切线是 BGR，用非对称分量防止双错相消。
                    const float inBgr[3]{mode==2 ? -0.125f : 0.25f,mode==2 ? 0.375f : 0.25f,mode==2 ? 0.625f : 0.75f};
                    const float outBgr[3]{mode==2 ? -0.125f : 0.75f,mode==2 ? 0.375f : 0.25f,mode==2 ? 0.625f : 0.25f};
                    for (const auto v:inBgr) out.F32(v);
                    for (const auto v:outBgr) out.F32(v);
                }
            }
            out.EndInclusive(object);out.EndChunk(chunk);
        }
        at+=size+8;
    }
    if (global) { out.Tag("GLBS");const auto chunk=out.Begin();out.U32(1000);out.EndChunk(chunk); }
    return std::move(out.bytes);
}
}
