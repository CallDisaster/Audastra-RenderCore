// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "RenderCoverageNodes.h"
#include <cmath>

namespace Coverage {
template <std::size_t N>
War3Map::Render::MdxNodeTrack<N> Constant(const std::array<float, N>& value) {
    War3Map::Render::MdxNodeTrack<N> track;
    War3Map::Render::MdxNodeKey<N> key; key.frame = 0; key.value = value;
    track.keys.push_back(key); return track;
}
inline void Transforms() {
    using namespace War3Map::Render;
    using namespace War3RenderCore;
    MdxTest::Options options; options.animatedHelper = true;
    auto parsed = ParseMdx(MdxTest::BuildTestMdx(options));
    Require(parsed.Succeeded(), "继承样本解析失败");
    auto base = *parsed.value;
    auto& parent = base.helpers[0]; auto& child = base.bones[0];
    base.pivots[parent.objectId] = {10,20,0}; base.pivots[child.objectId] = {13,24,0};
    parent.translation = Constant<3>({5,7,11}); parent.rotation = Constant<4>({0,0,1,0});
    parent.scaling = Constant<3>({2,3,4});
    child.translation = Constant<3>({1,2,0}); child.rotation.reset(); child.scaling.reset();
    const std::array<std::array<float, 3>, 8> anchors{{{7,9,11},{-28,-78,0},{7,9,11},{-14,-26,0},
        {7,9,11},{28,78,0},{7,9,11},{14,26,0}}};
    const std::array<std::array<float, 3>, 8> diagonal{{{-2,-3,4},{-2,-3,4},{-1,-1,1},{-1,-1,1},
        {2,3,4},{2,3,4},{1,1,1},{1,1,1}}};
    std::uint32_t samples = 0;
    for (const auto extra : {0u,0x40u,0x100u}) for (std::uint32_t flags = 0; flags < 8; ++flags)
        for (const bool camera : {false,true}) {
            auto model = base; model.bones[0].flags = flags | extra;
            const auto compiled = CompileMdxModel(model,{1,1},{"inherit.mdx","synthetic","flags"});
            Require(compiled.Succeeded(), "已核实继承位被拒绝");
            const std::array assets{*compiled.value};
            auto made = PoseBatchEvaluator::Create(assets,1,{1,8,8,8,65536});
            Require(made.Succeeded(), "继承求值器创建失败");
            auto evaluator = std::move(*made.value);
            PoseRequest request; request.asset = {1,1}; request.instance = {1,1};
            request.sequenceIndex = 1; request.hasCamera = camera ? 1u : 0u; request.camera.eye = {200,300,400};
            const auto batch = evaluator.Evaluate(std::span(&request,1));
            Require(batch.error == Code::Ok && batch.failedInstances == 0, "继承求值失败");
            const auto view = camera ? std::optional<MdxPoseCamera>{{{200,300,400},{0,0,1}}} : std::nullopt;
            for (std::uint32_t n = 0; n < compiled.value->NodeCount(); ++n) {
                const auto id = compiled.value->Node(n).objectId;
                const auto ref = SampleMdxNodePose(model,{1,0,0,id,view});
                Require(ref.Succeeded(), "继承参考失败");
                const auto& actual = evaluator.Nodes(0)[n].worldMatrix;
                Require(std::memcmp(ref.value->chain.back().worldMatrix.data(),actual.data(),sizeof(actual)) == 0,
                    "继承矩阵新旧逐位不一致");
                if (id == child.objectId && !(camera && extra == 0x40u)) {
                    // 手算的父轴心位置及基列；独立于两个实现，防止复制同一个错误。
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        Require(actual[axis*5] == diagonal[flags][axis], "继承基列不符合手算");
                        const auto atPivot = actual[12+axis] + actual[axis*5] * model.pivots[id][axis];
                        Require(atPivot == anchors[flags][axis], "继承轴心不符合手算");
                    }
                }
            }
            ++samples;
        }
    // 不继承缩放在本节点旋转之后去掉基列长度，非均匀父缩放不能整体清零。
    auto spun = base; spun.bones[0].flags = 2; spun.bones[0].rotation = Constant<4>({0,0,0.6f,0.8f});
    auto direct = SampleMdxNodePose(spun,{1,0,0,child.objectId,std::nullopt});
    Require(direct.Succeeded(), "非均匀缩放旋转样本失败");
    const auto& m = direct.value->chain.back().worldMatrix;
    for (std::size_t c = 0; c < 3; ++c)
        Require(std::abs(std::hypot(m[c*4],m[c*4+1],m[c*4+2])-1.0f) < 1e-6f, "缩放剥离没有归一化基列");
    auto bad = base; bad.helpers[0].scaling = Constant<3>({0,3,4}); bad.bones[0].flags = 2;
    const auto rejection = SampleMdxNodePose(bad,{1,0,0,child.objectId,std::nullopt});
    Require(!rejection.Succeeded() && rejection.diagnostics[0].code == "MDX_POSE_INHERITANCE_SINGULAR", "退化继承未拒绝");
    std::cout << "inheritance_samples=" << samples << " bitwise_mismatches=0 analytic_pivots=1 singular_rejected=1\n";
}
} // namespace Coverage
