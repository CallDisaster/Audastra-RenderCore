// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "RenderCoverageNodes.h"
#include "War3RenderCore/ModelContent.h"

namespace Coverage {
// 原创 PREM/PRE2 公共节点模型，删除网格/骨骼，不生成或模拟任何粒子。
inline std::vector<std::byte> ParticleOnlyBytes(std::string_view kind, bool sequences = true) {
    const auto source = OtherParent(kind);
    MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at = 4; at < source.size();) {
        std::uint32_t size = 0; std::memcpy(&size, source.data() + at + 4, 4);
        const std::string_view tag(reinterpret_cast<const char*>(source.data() + at), 4);
        if (tag != "GEOS" && tag != "GEOA" && tag != "BONE" && (sequences || tag != "SEQS"))
            out.Raw(source.data() + at, size + 8);
        at += size + 8;
    }
    return std::move(out.bytes);
}
inline void Particles() {
    using namespace War3Map::Render;
    using namespace War3RenderCore;
    for (const auto kind : {"PREM", "PRE2"}) for (const bool sequences : {true, false}) {
        const auto parsed = ParseMdx(ParticleOnlyBytes(kind, sequences));
        Require(parsed.Succeeded(), "纯粒子公共节点解析失败");
        const auto& model = *parsed.value;
        Require(ClassifyMdxContent(model) == ModelContentKind::ParticleOnlyUnimplemented,
            "纯粒子没有单独分类");
        const auto compiled = CompileMdxModel(model, {1,1}, {"particle.mdx","synthetic",kind});
        Require(compiled.Succeeded() && compiled.value->GeosetCount() == 0
            && compiled.value->GroupCount() == 0 && compiled.value->NodeCount() == 1,
            "纯粒子被整模型拒绝或伪造网格");
        if (sequences) {
            const std::array assets{*compiled.value};
            auto made = PoseBatchEvaluator::Create(assets, 1, {1,8,8,8,65536});
            Require(made.Succeeded(), "纯粒子节点求值器失败");
            auto evaluator = std::move(*made.value);
            PoseRequest request; request.asset = {1,1}; request.instance = {1,1};
            request.sequenceIndex = 1; request.sequenceElapsedMs = 250;
            const auto outcome = evaluator.Evaluate(std::span(&request,1));
            Require(outcome.error == Code::Ok && outcome.failedInstances == 0
                && evaluator.Groups(0).empty() && evaluator.Alphas(0).empty(), "空几何求值错误");
            const auto ref = SampleMdxNodePose(model, {1,250,0,model.otherNodes[0].objectId,std::nullopt});
            Require(ref.Succeeded() && std::memcmp(ref.value->chain.back().worldMatrix.data(),
                evaluator.Nodes(0)[0].worldMatrix.data(), sizeof(Matrix4)) == 0, "粒子节点新旧逐位不一致");
        }
        auto bad = model; bad.otherNodes[0].flags |= 0x20000000u;
        Require(!CompileMdxModel(bad,{1,1},{"bad","synthetic","flags"}).Succeeded(), "粒子分类绕过未知位拒绝");
        bad = model; bad.pivots.clear();
        Require(!CompileMdxModel(bad,{1,1},{"bad","synthetic","pivot"}).Succeeded(), "粒子分类绕过轴心检查");
    }
    auto light = ParseMdx(ParticleOnlyBytes("LITE"));
    Require(light.Succeeded() && ClassifyMdxContent(*light.value) == ModelContentKind::OtherNodeOnly,
        "只有光源被误标纯粒子");
    auto mesh = ParseMdx(OtherParent("PRE2"));
    Require(mesh.Succeeded() && ClassifyMdxContent(*mesh.value) == ModelContentKind::Mesh, "混合模型被误标纯粒子");
    MdxModel empty; empty.version = 800;
    Require(ClassifyMdxContent(empty) == ModelContentKind::OtherNodeOnly
        && !CompileMdxModel(empty,{1,1},{"empty","synthetic","empty"}).Succeeded(), "空模型被伪装成可用粒子");
    std::cout << "particle_models=4 empty_meshes=1 node_bitwise_mismatches=0 invalid_still_rejected=1\n";
}
} // namespace Coverage
