// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "MdxTestModel.h"
#include "War3MapCore/Render/MdxPose.h"
#include "War3RenderCore/PoseBatch.h"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace Coverage {
inline void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
// 把原创合成模型的 Motion HELP 改为其他种类；公共 Node 原字节不动。
inline std::vector<std::byte> OtherParent(std::string_view kind) {
    MdxTest::Options options; options.animatedHelper = true;
    const auto original = MdxTest::BuildTestMdx(options);
    MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at = 4; at < original.size();) {
        std::uint32_t size = 0; std::memcpy(&size, original.data() + at + 4, 4);
        if (std::memcmp(original.data() + at, "HELP", 4) != 0) out.Raw(original.data() + at, size + 8);
        else {
            const std::uint32_t flags = kind == "ATCH" ? 0x800u : kind == "PREM" || kind == "PRE2" ? 0x1000u
                : kind == "RIBB" ? 0x4000u : kind == "EVTS" ? 0x400u : kind == "LITE" ? 0x200u : 0x2000u;
            out.Raw(kind.data(), 4); const auto chunk = out.Begin();
            const bool sized = kind != "EVTS" && kind != "CLID";
            const auto object = sized ? out.Begin() : out.bytes.size();
            const auto node = out.bytes.size(); out.Raw(original.data() + at + 8, size);
            std::memcpy(out.bytes.data() + node + 92, &flags, 4);
            if (kind == "EVTS") { out.Tag("KEVT"); out.U32(2); out.U32(UINT32_MAX); out.U32(10); out.U32(20); }
            else if (kind == "CLID") { out.U32(2); out.F32(0); out.F32(0); out.F32(0); out.F32(10); }
            else {
                const auto tail = kind == "ATCH" ? 264u : kind == "PREM" ? 284u : kind == "PRE2" ? 171u
                    : kind == "RIBB" ? 52u : 44u;
                out.bytes.resize(out.bytes.size() + tail);
            }
            if (sized) out.EndInclusive(object);
            out.EndChunk(chunk);
        }
        at += size + 8;
    }
    return std::move(out.bytes);
}
inline void Nodes() {
    using namespace War3Map::Render;
    using namespace War3RenderCore;
    std::uint32_t samples = 0;
    for (const auto kind : {"ATCH", "PREM", "PRE2", "RIBB", "EVTS", "LITE", "CLID"}) {
        const auto bytes = OtherParent(kind);
        const auto parsed = ParseMdx(bytes);
        Require(parsed.Succeeded(), "其他节点父链解析失败");
        const auto& model = *parsed.value;
        Require(model.skinningTopologyComplete && model.otherNodes.size() == 1 && model.helpers.empty(), "公共节点未纳入图");
        Require(MdxNodeCount(model) == 2 && model.otherNodes[0].translation.has_value(), "丢失公共轨迹");
        const auto compiled = CompileMdxModel(model, {1, 1}, {"nodes.mdx", "synthetic", kind});
        Require(compiled.Succeeded() && compiled.value->NodeCount() == 2, "编译父链失败");
        const std::array assets{*compiled.value};
        auto created = PoseBatchEvaluator::Create(assets, 1, {1, 8, 8, 8, 65536});
        Require(created.Succeeded(), "节点求值器创建失败");
        auto evaluator = std::move(*created.value);
        for (const auto time : {0u, 250u, 500u, 999u}) {
            PoseRequest request; request.instance = {1, 1}; request.asset = {1, 1};
            request.sequenceIndex = 0; request.sequenceElapsedMs = time;
            Require(evaluator.Evaluate(std::span(&request, 1)).failedInstances == 0, "公共节点求值失败");
            for (std::uint32_t n = 0; n < 2; ++n) {
                const auto ref = SampleMdxNodePose(model, {0, time, 0, compiled.value->Node(n).objectId, std::nullopt});
                Require(ref.Succeeded(), "参考公共节点失败");
                Require(std::memcmp(ref.value->chain.back().worldMatrix.data(), evaluator.Nodes(0)[n].worldMatrix.data(),
                    sizeof(std::array<float, 16>)) == 0, "公共节点新旧差分");
            }
            ++samples;
        }
        auto invalid = bytes; invalid.pop_back();
        Require(!ParseMdx(invalid).Succeeded(), "截断未拒绝");
        MdxLimits tight; tight.maxNodes = 1;
        Require(!ParseMdx(bytes, tight).Succeeded(), "公共节点总预算未生效");
        auto duplicate = model; duplicate.otherNodes[0].objectId = 0;
        Require(!CompileMdxModel(duplicate,{1,1},{"x","test","dup"}).Succeeded(), "跨类型重复ID未拒绝");
    }
    std::cout << "node_kinds=7 parent_samples=" << samples << " bitwise_mismatches=0 bounds_and_duplicates=1\n";
}
} // namespace Coverage
