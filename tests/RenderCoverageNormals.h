// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "RenderCoverageNodes.h"
#include <limits>

namespace Coverage {
inline void Normals() {
    using namespace War3Map::Render;
    using namespace War3RenderCore;
    const auto parsed = ParseMdx(MdxTest::BuildTestMdx());
    Require(parsed.Succeeded(), "零法线样本解析失败");
    auto model = *parsed.value;
    model.geosets[0].normals[0] = {0.0f, -0.0f, 0.0f};
    model.geosets[0].normals[1] = {};
    model.geosets[1].normals[0] = {};
    const auto compiled = CompileMdxModel(model, {1,1}, {"normal.mdx","synthetic","zero"});
    Require(compiled.Succeeded() && compiled.diagnostics.size() == 2, "零法线整模型拒绝或警告计数错误");
    for (std::size_t g = 0; g < 2; ++g) {
        const auto& warning = compiled.diagnostics[g];
        Require(warning.severity == War3Map::DiagnosticSeverity::Warning
            && warning.code == "MDX_SKIN_ZERO_NORMAL_PREVIEW", "零法线没有降为警告");
        Require(warning.message.find("geoset=" + std::to_string(g)) != std::string::npos
            && warning.message.find(g == 0 ? "vertices=2" : "vertices=1") != std::string::npos,
            "警告缺少 geoset 或受影响顶点数");
    }
    const std::array assets{*compiled.value};
    auto made = PoseBatchEvaluator::Create(assets, 1, {1,8,8,8,65536});
    Require(made.Succeeded(), "零法线求值器失败");
    auto evaluator = std::move(*made.value);
    PoseRequest request; request.asset = {1,1}; request.instance = {1,1}; request.sequenceIndex = 1;
    for (const auto time : {0u,250u,500u,999u}) {
        request.sequenceElapsedMs = time;
        const auto batch = evaluator.Evaluate(std::span(&request,1));
        Require(batch.error == Code::Ok && batch.failedInstances == 0, "零法线妨碍姿势求值");
        for (std::uint32_t g = 0; g < 2; ++g) {
            const auto old = SampleMdxRigidSkinnedGeoset(model, {g,1,time,0,std::nullopt});
            Require(old.Succeeded(), "参考零法线未降级");
            Require(old.diagnostics.size() == 1 && old.diagnostics[0].message == compiled.diagnostics[g].message,
                "新旧零法线诊断不一致");
            std::vector<Vec3> positions(model.geosets[g].positions.size()), normals(positions.size());
            Require(SkinPreviewGeoset(*compiled.value,g,evaluator.Groups(0),positions,normals) == Code::Ok,
                "核心零法线蒙皮失败");
            Require(std::memcmp(normals.data(),old.value->normals.data(),normals.size()*sizeof(Vec3)) == 0
                && std::memcmp(positions.data(),old.value->positions.data(),positions.size()*sizeof(Vec3)) == 0,
                "零法线新旧逐位不一致");
            Require(normals[0] == Vec3{}, "零法线未保持为零");
        }
    }
    auto invalid = model; invalid.geosets[0].normals[0][1] = std::numeric_limits<float>::infinity();
    const auto bad = CompileMdxModel(invalid,{1,1},{"normal.mdx","synthetic","nonfinite"});
    Require(!bad.Succeeded() && bad.diagnostics[0].code == "MDX_POSE_NUMBER_INVALID", "非有限法线被放行");
    Require(!SampleMdxRigidSkinnedGeoset(invalid,{0,1,0,0,std::nullopt}).Succeeded(), "参考非有限法线被放行");
    std::cout << "zero_normal_samples=8 affected_geosets=2 vertices=3 bitwise_mismatches=0 nonfinite_rejected=1\n";
}
} // namespace Coverage
