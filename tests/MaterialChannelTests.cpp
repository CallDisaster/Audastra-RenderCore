// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"
#include "War3MapCore/Render/MdxModel.h"
#include "War3RenderCore/PoseBatch.h"
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
using namespace War3Map::Render;
using namespace War3RenderCore;
void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
std::vector<std::byte> Fixture(std::uint32_t mode, bool global) {
    const auto input = MdxTest::BuildTestMdx(); MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at = 4; at < input.size();) {
        std::uint32_t size = 0; std::memcpy(&size, input.data() + at + 4, 4);
        // SEQS 按时间顺序写（Stand 0..1000 在前）：原版逐动作区间共享只进游标，排在 Death 之后的 Stand 拿不到键。
        if (!std::memcmp(input.data() + at, "SEQS", 4)) {
            Check(size == 264, "夹具SEQS形状变化");
            out.Raw(input.data() + at, 8); out.Raw(input.data() + at + 8 + 132, 132); out.Raw(input.data() + at + 8, 132);
        } else if (std::memcmp(input.data() + at, "MTLS", 4)) out.Raw(input.data() + at, size + 8);
        else {
            out.Tag("MTLS"); const auto chunk = out.Begin(), material = out.Begin();
            out.U32(0); out.U32(0); out.Tag("LAYS"); out.U32(2);
            for (const auto animated : {false, true}) {
                const auto layer = out.Begin(); out.U32(0); out.U32(0); out.U32(0);
                out.U32(UINT32_MAX); out.U32(0); out.F32(animated ? .75f : .25f);
                if (animated) {
                    out.Tag("KMTA"); out.U32(2); out.U32(mode); out.U32(global ? 0 : UINT32_MAX);
                    for (auto frame : {0u, 1000u}) {
                        out.U32(frame); out.F32(frame ? 0.0f : 1.0f);
                        if (mode >= 2) { out.F32(mode == 2 ? 0 : .25f); out.F32(mode == 2 ? 0 : .75f); }
                    }
                }
                out.EndInclusive(layer);
            }
            out.EndInclusive(material); out.EndChunk(chunk);
        }
        at += size + 8;
    }
    if (global) { out.Tag("GLBS"); const auto at = out.Begin(); out.U32(1000); out.EndChunk(at); }
    return std::move(out.bytes);
}
PoseBatchEvaluator Evaluator(const MdxModel& model, bool enabled = true) {
    const auto compiled = CompileMdxModel(model, {1, 1}, {"fixture.mdx", "original", "material"});
    Check(compiled.Succeeded(), "材质资产编译失败"); const std::array assets{*compiled.value};
    const auto layout = assets[0].MaterialLayerLayouts();
    Check(layout.size() == assets[0].MaterialLayerCount(), "材质目录数量错误");
    auto made = PoseBatchEvaluator::Create(assets, 1, {}, GeosetFailurePolicy::Strict, {enabled ? 1u : 0u, 64});
    Check(made.Succeeded(), "材质容量预热失败"); return std::move(*made.value);
}
PoseRequest Request(std::uint64_t time, std::uint64_t global, std::uint32_t sequence = 0) {
    PoseRequest request; request.asset = {1, 1}; request.instance = {1, 1};
    request.sequenceIndex = sequence; request.sequenceElapsedMs = time; request.globalElapsedMs = global; return request;
}
Code Compare(const MdxModel& model, std::uint64_t time, std::uint64_t global, std::uint32_t sequence = 0) {
    auto evaluator = Evaluator(model); const auto request = Request(time, global, sequence);
    RenderCoreAllocProbe::Begin(); const auto outcome = evaluator.Evaluate(std::span(&request, 1));
    const auto allocated = RenderCoreAllocProbe::End(); Check(outcome.error == Code::Ok && !allocated, "材质热路径分配");
    const auto reference = SampleMdxLayerAlphas(model, {0, sequence, time, global});
    const auto code = evaluator.Receipts()[0].error;
    Check(reference.Succeeded() == (code == Code::Ok), "KMTA参考与核心接受差分");
    if (reference.Succeeded()) {
        const auto values = evaluator.LayerAlphas(0);
        Check(values.size() == reference.value->size() && !std::memcmp(values.data(), reference.value->data(), values.size_bytes()), "KMTA逐位差分");
    } else {
        Check(reference.diagnostics[0].code == CodeName(code) && evaluator.LayerAlphas(0).empty(), "KMTA拒绝码/部分输出差分");
    }
    return code;
}
void Run() {
    for (std::uint32_t mode = 0; mode < 4; ++mode) for (bool global : {false, true}) {
        const auto parsed = ParseMdx(Fixture(mode, global)); Check(parsed.Succeeded(), "KMTA原始字节解析失败");
        const auto& layer = parsed.value->materials[0].layers[1];
        Check(layer.alphaKeys.size() == 2 && layer.alphaKeys[0].sourceByteOffset == layer.alphaSourceByteOffset + 16, "KMTA物理来源丢失");
        for (auto time : {0u, 1u, 123u, 500u, 999u, 1000u, 1521u}) Check(Compare(*parsed.value, time, time) == Code::Ok, "KMTA合法轨迹拒绝");
        const auto midpoint = SampleMdxLayerAlphas(*parsed.value, {0, 0, 500, 500});
        Check((*midpoint.value)[0] == .25f && (*midpoint.value)[1] == (mode ? .5f : 1), "KMTA独立中点/静态层错误");
    }
    auto model = *ParseMdx(Fixture(1, false)).value; auto evaluator = Evaluator(model);
    std::array requests{Request(0, 0), Request(500, 0)}; requests[1].instance = {2, 1};
    Check(!evaluator.Evaluate(requests).failedInstances && evaluator.LayerAlphas(0)[1] == 1 && evaluator.LayerAlphas(1)[1] == .5f, "同资产实例材质串时钟");
    auto disabled = Evaluator(model, false); Check(!disabled.MaterialChannelsEnabled() && disabled.LayerAlphas(0).empty(), "默认路径误启材质");
    const auto compiled = *CompileMdxModel(model, {1, 1}, {"fixture.mdx", "original", "material"}).value;
    const std::array assets{compiled}; const auto original = PoseBatchEvaluator::Create(assets, 1);
    Check(original.value->StorageBytes() == disabled.StorageBytes(), "未启材质改变旧预算");
    auto tight = BatchLimits{}; tight.maxBytes = original.value->StorageBytes();
    Check(!PoseBatchEvaluator::Create(assets, 1, tight, GeosetFailurePolicy::Strict, {1, 64}).Succeeded(), "材质缓冲未计预算");
    auto zero = *ParseMdx(Fixture(1, true)).value; zero.globalSequences[0] = 0;
    Check(Compare(zero, 500, UINT64_MAX) == Code::Ok && (*SampleMdxLayerAlphas(zero, {0, 0, 500, UINT64_MAX}).value)[1] == 1, "KMTA零周期未取归零首键");
    auto rawInvalid = Fixture(1, false); const auto valueOffset = model.materials[0].layers[1].alphaKeys[0].sourceByteOffset + 4;
    const std::uint32_t nanBits = 0x7fc01234u; std::memcpy(rawInvalid.data() + static_cast<std::size_t>(valueOffset), &nanBits, 4);
    const auto parsedInvalid = ParseMdx(rawInvalid); Check(parsedInvalid.Succeeded(), "KMTA坏值不能丢原始来源"); model = *parsedInvalid.value;
    auto& bad = model.materials[0].layers[1];
    auto invalid = Evaluator(model); const auto request = Request(0, 0);
    Check(invalid.Evaluate(std::span(&request, 1)).failedInstances == 1 && Compare(model, 0, 0) == Code::MaterialAlphaNumberInvalid, "KMTA非有限未拒绝");
    const auto issue = invalid.MaterialIssues(0)[0];
    Check(issue.materialIndex == 0 && issue.layerIndex == 1 && issue.sourceByteOffset == bad.alphaKeys[0].sourceByteOffset + 4 && issue.sourceValueBits == 0x7fc01234u, "KMTA坏值原偏移/位丢失");
    auto legacy = Evaluator(model, false); Check(!legacy.Evaluate(std::span(&request, 1)).failedInstances, "未启材质误拒旧姿势");
    bad.alphaKeys[0].value = 1; bad.alphaInterpolation = 4; Check(Compare(model, 0, 0) == Code::MaterialAlphaInterpolationUnsupported, "未知KMTA插值未拒绝");
    bad.alphaInterpolation = 1; bad.alphaGlobalSequence = 99; Check(Compare(model, 0, 0) == Code::MaterialAlphaGlobalSequenceUnsupported, "坏KMTA全局序列未拒绝");
    // 非全局同帧键在原版逐动作区间内合法（逆序与全局轨迹见 MaterialAlphaDuplicatePreviewTests）。
    bad.alphaGlobalSequence = UINT32_MAX; bad.alphaKeys[1].frame = 0; Check(Compare(model, 0, 0) == Code::Ok, "同帧KMTA键未按原版区间接受");
    bad.alphaKeys[1].frame = 1000; bad.hasUninterpretedData = true; Check(Compare(model, 0, 0) == Code::MaterialAlphaUninterpreted, "未知KMTA尾部静态放行");
}
void BoneGuard(const char* path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate); Check(bool(input), "实际BoneGuard打不开");
    const auto length = input.tellg(); Check(length > 0 && length <= 64 * 1024 * 1024, "实际模型输入预算");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length)); input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Check(bool(input), "实际模型读取失败"); const auto parsed = ParseMdx(bytes); Check(parsed.Succeeded(), "实际BoneGuard解析失败");
    const auto& model = *parsed.value; Check(model.materials.size() == 2 && model.materials[0].layers.size() == 2 && model.materials[1].layers.size() == 1, "不是已盘点BoneGuard布局");
    std::uint32_t death = UINT32_MAX; for (std::uint32_t i = 0; i < model.sequences.size(); ++i) if (model.sequences[i].name == "Death") death = i;
    Check(death != UINT32_MAX && model.sequences[death].start == 3863, "实际Death身份不同");
    for (auto time : {0u, 1664u, 3328u}) {
        Check(Compare(model, time, 0, death) == Code::Ok, "实际BoneGuard逐位差分");
        const auto values = *SampleMdxLayerAlphas(model, {0, death, time, 0}).value;
        const float expected = time == 0 ? 1 : time == 1664 ? .5f : 0;
        Check(values[0] == 1 && values[1] == expected && values[2] == expected, "实际BoneGuard不等于1/.5/0");
        std::cout << "boneguard_death relative_ms=" << time << " body=" << values[0] << " glow=" << values[1] << " team_glow=" << values[2] << '\n';
    }
}
}
int main(int argc, char** argv) {
    try { Run(); if (argc == 2) BoneGuard(argv[1]); std::cout << "material_channels bitwise=1 no_alloc=1 budgets=1 source_issue=1 legacy_default=1\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
