// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3RenderCore/PoseBatch.h"
#include "War3MapCore/Render/MdxPose.h"
#include "MdxTestModel.h"
#include "RenderCoverageNodes.h"
#include "RenderCoverageTransforms.h"
#include "RenderCoverageNormals.h"
#include "RenderCoverageParticles.h"
#include "RenderCoverageScan.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace War3Map::Render;
using namespace War3RenderCore;
void Check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void SequenceIsolation() {
    MdxTest::Options options; options.animatedHelper = true;
    const auto originalBytes = MdxTest::BuildTestMdx(options);
    const auto original = ParseMdx(originalBytes);
    Check(original.Succeeded(), "原始合成模型解析失败");
    for (const auto unsupportedFlags : {false, true}) {
        auto bytes = originalBytes;
        std::size_t offset = 0;
        for (; offset + 8 <= bytes.size(); ++offset)
            if (std::memcmp(bytes.data() + offset, "SEQS", 4) == 0) break;
        Check(offset + 8 + 2 * 132 <= bytes.size(), "合成模型缺少 SEQS");
        const std::uint32_t bad = unsupportedFlags ? 2u : 1000u;
        // 夹具按时间顺序 Stand=0、Death=1；破坏排在后面的 Death，不影响 Stand 的原版键区间。
        std::memcpy(bytes.data() + offset + 8 + 132 + (unsupportedFlags ? 92 : 84), &bad, 4);
        const auto parsed = ParseMdx(bytes);
        Check(parsed.Succeeded() && parsed.value->sequences.size() == 2, "坏动作杀死整个模型");
        const auto& model = *parsed.value;
        Check(model.sequences[0].Usable() && !model.sequences[1].Usable(), "序列可用性错误");
        Check(parsed.diagnostics.size() == 1 && parsed.diagnostics[0].code == "MDX_SEQUENCE_UNAVAILABLE",
            "缺少坏序列警告");
        Check(parsed.diagnostics[0].message.find("sequence=1") != std::string::npos, "警告缺少序列号");
        const auto compiled = CompileMdxModel(model, {1, 1}, {"test.mdx", "synthetic", "sequence-isolation"});
        Check(compiled.Succeeded(), "坏动作导致整模型编译失败");
        const std::array assets{*compiled.value};
        auto prepared = PoseBatchEvaluator::Create(assets, 1, {2, 8, 8, 8, 65536});
        Check(prepared.Succeeded(), "求值器创建失败");
        auto evaluator = std::move(*prepared.value);
        std::array<PoseRequest, 2> requests{};
        for (std::uint32_t i = 0; i < requests.size(); ++i) {
            requests[i].instance = {i + 1, 1}; requests[i].asset = {1, 1};
            requests[i].sequenceIndex = i; requests[i].sequenceElapsedMs = 250;
        }
        const auto batch = evaluator.Evaluate(requests);
        Check(batch.error == Code::Ok && batch.failedInstances == 1, "坏动作未按实例隔离");
        Check(evaluator.Receipts()[1].error == Code::PoseSequenceUnsupported
            && evaluator.Nodes(1).empty() && evaluator.Groups(1).empty(), "坏动作未明确拒绝");
        Check(evaluator.Receipts()[0].error == Code::Ok, "好动作被误拒绝");
        const auto badPose = SampleMdxNodePose(model, {1, 250, 0, 0, std::nullopt});
        const auto badAlpha = SampleMdxGeosetAlpha(model, {0, 1, 250, 0});
        Check(!badPose.Succeeded() && badPose.diagnostics[0].code == "MDX_POSE_SEQUENCE_UNSUPPORTED",
            "参考姿势未拒绝坏动作");
        Check(!badAlpha.Succeeded() && badAlpha.diagnostics[0].code == "MDX_GEOA_SEQUENCE_UNSUPPORTED",
            "无透明度轨迹时也必须拒绝坏动作");
        for (std::uint32_t i = 0; i < compiled.value->NodeCount(); ++i) {
            const auto id = compiled.value->Node(i).objectId;
            const auto reference = SampleMdxNodePose(*original.value, {0, 250, 0, id, std::nullopt});
            const auto changed = SampleMdxNodePose(model, {0, 250, 0, id, std::nullopt});
            Check(reference.Succeeded() && changed.Succeeded(), "参考好动作失败");
            const auto& expected = reference.value->chain.back().worldMatrix;
            Check(std::memcmp(expected.data(), changed.value->chain.back().worldMatrix.data(), sizeof(expected)) == 0
                && std::memcmp(expected.data(), evaluator.Nodes(0)[i].worldMatrix.data(), sizeof(expected)) == 0,
                "好动作新旧逐位不一致");
        }
    }
    std::cout << "sequence_isolation=1 reversed_and_flags=1 original_indices_preserved=1 bitwise_mismatches=0\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("缺少测试组");
        if (std::string_view(argv[1]) == "sequences") { SequenceIsolation(); Coverage::ScanDetails(); }
        else if (std::string_view(argv[1]) == "nodes") { Coverage::Nodes(); Coverage::Normals(); Coverage::Particles(); }
        else if (std::string_view(argv[1]) == "transforms") Coverage::Transforms();
        else throw std::runtime_error("未知测试组");
        std::cout << "PASS " << argv[1] << '\n';
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
