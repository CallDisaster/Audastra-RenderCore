// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
// KMTA 非全局轨迹按 1.27a 逐动作区间取键（IDA：材质层 6F785260 → 6F780270 → 6F78A090，与 GEOA 同一编译器）。
// 同帧键在原版区间内本就合法，原“同帧同值预览”不再触发：两种材质键策略都直接成功且没有预览警告。
// 区间内逆序边与节点/GEOA 同一策略：严格模式拒绝，原序预览按原版顺序查找；全局轨迹仍须严格递增。
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"
#include "War3RenderCore/PoseBatch.h"
#include "../src/RenderCore/ModelData.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace War3RenderCore;
using namespace War3Map::Render;
using Policy = MaterialAlphaKeySamplingPolicy;
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }

constexpr std::uint32_t kStand = 0, kDeath = 1;
constexpr std::size_t kSequenceRecord = 132;

// 四层 KMTA：0,250,250(重复次数可调),1000,2000,2500,3000。SEQS 按时间顺序写（Stand 0..1000 在前，Death 2000..3000 在后），
// 原版共享只进游标因此给 Stand 前四键、Death 后三键。
std::vector<std::byte> Fixture(std::uint32_t mode = 1, bool global = false, std::uint32_t repeats = 1) {
    const auto base = MdxTest::BuildTestMdx(); MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at = 4; at < base.size();) {
        std::uint32_t size = 0; std::memcpy(&size, base.data() + at + 4, 4);
        if (!std::memcmp(base.data() + at, "SEQS", 4)) {
            Require(size == 2 * kSequenceRecord, "fixture SEQS shape changed");
            out.Raw(base.data() + at, 8);
            out.Raw(base.data() + at + 8 + kSequenceRecord, kSequenceRecord);
            out.Raw(base.data() + at + 8, kSequenceRecord);
        } else if (std::memcmp(base.data() + at, "MTLS", 4)) out.Raw(base.data() + at, size + 8);
        else {
            out.Tag("MTLS"); const auto chunk = out.Begin(), material = out.Begin();
            out.U32(0); out.U32(0); out.Tag("LAYS"); out.U32(4);
            for (std::uint32_t layerIndex = 0; layerIndex < 4; ++layerIndex) {
                const auto layer = out.Begin(); out.U32(layerIndex % 2); out.U32(0); out.U32(0);
                out.U32(UINT32_MAX); out.U32(0); out.F32(1);
                out.Tag("KMTA"); out.U32(6 + repeats); out.U32(mode); out.U32(global ? 0 : UINT32_MAX);
                std::vector<std::uint32_t> frames{0, 250};
                for (std::uint32_t i = 0; i < repeats; ++i) frames.push_back(250);
                for (const auto frame : {1000u, 2000u, 2500u, 3000u}) frames.push_back(frame);
                for (const auto frame : frames) {
                    const float upper = frame <= 2000 ? 1.0f : frame == 2500 ? .5f : 0.0f;
                    out.U32(frame); out.F32(layerIndex < 2 ? upper : 1.0f - upper);
                    if (mode >= 2) { out.F32(0); out.F32(0); }
                }
                out.EndInclusive(layer);
            }
            out.EndInclusive(material); out.EndChunk(chunk);
        }
        at += size + 8;
    }
    if (global) { out.Tag("GLBS"); const auto chunk = out.Begin(); out.U32(3000); out.EndChunk(chunk); }
    return std::move(out.bytes);
}
MdxModel Parse(const std::vector<std::byte>& bytes) {
    auto result = ParseMdx(bytes); Require(result.Succeeded(), "MDX KMTA fixture parse failed");
    auto model = std::move(*result.value);
    Require(model.sequences.size() == 2 && model.sequences[kStand].start == 0 && model.sequences[kDeath].start == 2000,
        "fixture SEQS not in time order");
    model.sequences[kDeath].flags = 1; return model;
}
CompiledModel Compile(const MdxModel& model) {
    auto result = CompileMdxModel(model, {23, 5}, {"Tests/KmtaGameRanges.mdx", "synthetic-or-local", "original"});
    Require(result.Succeeded(), "KMTA compile failed"); return std::move(*result.value);
}
BatchLimits Limits() { return {2, 2, 4, 4, 65536}; }
auto Prepared(const CompiledModel& asset, Policy policy, BatchLimits limits = Limits(),
    NodeKeySamplingPolicy nodes = NodeKeySamplingPolicy::Strict, GeosetFailurePolicy geosets = GeosetFailurePolicy::Strict) {
    const std::array assets{asset};
    return PoseBatchEvaluator::Create(assets, 31, limits, geosets, {1, 64}, {}, {}, nodes, policy);
}
PoseBatchEvaluator Prepare(const CompiledModel& asset, Policy policy, BatchLimits limits = Limits(),
    NodeKeySamplingPolicy nodes = NodeKeySamplingPolicy::Strict, GeosetFailurePolicy geosets = GeosetFailurePolicy::Strict) {
    auto result = Prepared(asset, policy, limits, nodes, geosets);
    Require(result.Succeeded(), "KMTA prewarm failed"); return std::move(*result.value);
}
PoseRequest Request(std::uint32_t sequence = kStand, std::uint64_t elapsed = 0) {
    PoseRequest request; request.asset = {23, 5}; request.instance = {1, 7};
    request.sequenceIndex = sequence; request.sequenceElapsedMs = elapsed; request.globalElapsedMs = UINT64_MAX;
    return request;
}
void Evaluate(PoseBatchEvaluator& evaluator, std::span<const PoseRequest> requests) {
    RenderCoreAllocProbe::Begin(); const auto outcome = evaluator.Evaluate(requests);
    const auto allocations = RenderCoreAllocProbe::End();
    Require(outcome.error == Code::Ok && allocations == 0, "KMTA hot path allocated or failed batch");
}
void Evaluate(PoseBatchEvaluator& evaluator, const PoseRequest& request) { Evaluate(evaluator, std::span(&request, 1)); }
void Failed(PoseBatchEvaluator& evaluator, const PoseRequest& request, Code code) {
    Evaluate(evaluator, request);
    const auto& receipt = evaluator.Receipts()[0]; const auto diagnostics = evaluator.Diagnostics(0);
    Require(receipt.error == code && receipt.poseVersion == 0 && receipt.groups.count == 0 && receipt.nodes.count == 0
        && evaluator.LayerAlphas(0).empty() && evaluator.Groups(0).empty() && evaluator.MaterialAlphaPreviewWarnings(0).empty(),
        "KMTA failure published partial pose/warnings");
    Require(diagnostics.size() == 1 && diagnostics[0].code == code && diagnostics[0].severity == Severity::Error,
        "KMTA failure retained warning or changed first Error contract");
}
void NoPreviewWarnings(PoseBatchEvaluator& evaluator, std::uint32_t instance = 0) {
    for (const auto& diagnostic : evaluator.Diagnostics(instance))
        Require(diagnostic.code != Code::MaterialAlphaIdenticalDuplicatesPreview, "game-range KMTA still issued duplicate preview warning");
    Require(evaluator.MaterialAlphaPreviewWarnings(instance).empty(), "game-range KMTA still published typed duplicate sources");
}
void Values(PoseBatchEvaluator& evaluator, float upper, std::uint32_t instance = 0) {
    const auto values = evaluator.LayerAlphas(instance);
    Require(evaluator.Receipts()[instance].error == Code::Ok && evaluator.Receipts()[instance].poseVersion != 0
        && values.size() == 4, "KMTA success missing owned layer outputs");
    for (std::uint32_t i = 0; i < 4; ++i)
        Require(std::abs(values[i] - (i < 2 ? upper : 1.0f - upper)) <= 0.000001f, "KMTA static fallback or wrong material alpha");
    for (const auto& group : evaluator.Groups(instance)) for (const auto value : group.values)
        Require(std::isfinite(value), "KMTA pose nonfinite");
    NoPreviewWarnings(evaluator, instance);
}
float DeathUpper(std::uint64_t elapsed) { return 1.0f - static_cast<float>(elapsed) / 1000.0f; }

void PolicyDefaults() {
    const auto bytes = Fixture(); const auto before = bytes; const auto model = Parse(bytes); const auto asset = Compile(model);
    const std::array assets{asset};
    const auto* compiled = Detail::ModelAccess::Get(asset);
    Require(compiled->materialDuplicateWarnings == 0, "game-range KMTA still prepared duplicate warnings");
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& channel = compiled->materialAlphas[i];
        Require(channel.alpha.error == Code::Ok && !channel.identicalDuplicates && channel.alpha.ranges.size() == 2
            && channel.alpha.ranges[kStand].offset == 0 && channel.alpha.ranges[kStand].count == 4
            && channel.alpha.ranges[kDeath].offset == 4 && channel.alpha.ranges[kDeath].count == 3
            && channel.sources.size() == model.materials[0].layers[i].alphaKeys.size(),
            "KMTA per-sequence ranges or raw-key ownership wrong");
    }
    auto legacy = PoseBatchEvaluator::Create(assets, 31, Limits(), GeosetFailurePolicy::Strict, {1, 64});
    Require(legacy.Succeeded(), "old material Create failed");
    auto strict = Prepare(asset, Policy::Strict);
    auto preview = Prepare(asset, Policy::IdenticalDuplicatesPreview);
    Require(strict.StorageBytes() == legacy.value->StorageBytes(), "explicit Strict changed old Create storage");
    for (const auto time : {0u, 249u, 250u, 251u, 999u, 1000u}) {
        Evaluate(*legacy.value, Request(kStand, time)); Values(*legacy.value, 1);
        Evaluate(strict, Request(kStand, time)); Values(strict, 1);
        Evaluate(preview, Request(kStand, time)); Values(preview, 1);
    }
    for (const auto time : {0u, 250u, 500u, 750u, 1000u}) {
        Evaluate(strict, Request(kDeath, time)); Values(strict, DeathUpper(time));
        const auto reference = SampleMdxLayerAlphas(model, {0, kDeath, time, 0});
        const auto values = strict.LayerAlphas(0);
        Require(reference.Succeeded() && reference.value->size() == values.size()
            && std::equal(values.begin(), values.end(), reference.value->begin()), "shared reference disagrees with KMTA ranges");
    }
    Require(!Prepared(asset, static_cast<Policy>(99)).Succeeded(), "unknown material key policy accepted");
    Require(!PoseBatchEvaluator::Create(assets, 31, Limits(), GeosetFailurePolicy::Strict, {}, {}, {},
        NodeKeySamplingPolicy::Strict, Policy::IdenticalDuplicatesPreview).Succeeded(), "preview enabled without material outputs");
    auto disabled = PoseBatchEvaluator::Create(assets, 31, Limits()); Require(disabled.Succeeded(), "disabled material prepare");
    Evaluate(*disabled.value, Request(kDeath, 500));
    Require(disabled.value->Receipts()[0].error == Code::Ok && disabled.value->LayerAlphas(0).empty(), "disabled/default path was changed");
    Require(bytes == before && model.materials[0].layers[0].alphaKeys.size() == 7, "source keys or bytes changed");
}
void SamplingAndProvenance() {
    const auto model = Parse(Fixture()); auto evaluator = Prepare(Compile(model), Policy::Strict);
    std::array requests{Request(kDeath, 250), Request(kDeath, 750)}; requests[1].instance = {2, 7};
    Evaluate(evaluator, requests); Values(evaluator, .75f, 0); Values(evaluator, .25f, 1);
    Require(evaluator.Receipts()[0].instance != evaluator.Receipts()[1].instance
        && evaluator.Receipts()[0].poseVersion != evaluator.Receipts()[1].poseVersion, "instance clock/version crossed");
    // Stand 区间 0,250,100,1000 含逆序边：严格拒绝并指向该区间首个原键；原序预览按原版顺序查找；Death 区间不受影响。
    auto descending = Parse(Fixture()); descending.materials[0].layers[0].alphaKeys[2].frame = 100;
    const auto asset = Compile(descending);
    auto strict = Prepare(asset, Policy::Strict);
    Failed(strict, Request(kStand, 500), Code::MaterialAlphaKeysInvalid);
    const auto issue = strict.MaterialIssues(0); const auto& first = descending.materials[0].layers[0].alphaKeys[0];
    Require(issue.size() == 1 && issue[0].materialIndex == 0 && issue[0].layerIndex == 0 && issue[0].sourceFrame == 0
        && issue[0].sourceByteOffset == first.sourceByteOffset + 4 && issue[0].sourceValueBits == std::bit_cast<std::uint32_t>(first.value),
        "descending KMTA range lost its source location");
    Evaluate(strict, Request(kDeath, 500)); Values(strict, DeathUpper(500));
    auto preview = Prepare(asset, Policy::Strict, Limits(), NodeKeySamplingPolicy::SourceOrderPreview);
    for (const auto time : {0u, 250u, 500u, 1000u}) { Evaluate(preview, Request(kStand, time)); Values(preview, 1); }
    Evaluate(preview, Request(kDeath, 500)); Values(preview, DeathUpper(500));
    const auto reference = SampleMdxLayerAlphas(descending, {0, kStand, 500, 0});
    Require(reference.Succeeded() && std::abs((*reference.value)[0] - 1.0f) <= 0.000001f, "source-order reference rejected descending KMTA");
}
void NegativeGates() {
    const auto check = [](MdxModel model, Code code) {
        auto evaluator = Prepare(Compile(model), Policy::IdenticalDuplicatesPreview);
        Failed(evaluator, Request(kDeath, 500), code);
    };
    for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto model = Parse(Fixture()); model.materials[0].layers[0].alphaKeys.front().value = invalid;
        check(model, Code::MaterialAlphaNumberInvalid);
        model = Parse(Fixture(2)); model.materials[0].layers[0].alphaKeys.back().inTangent = invalid;
        check(model, Code::MaterialAlphaNumberInvalid);
    }
    auto model = Parse(Fixture()); model.materials[0].layers[0].alphaInterpolation = 4;
    check(model, Code::MaterialAlphaInterpolationUnsupported);
    model = Parse(Fixture()); model.materials[0].layers[0].alphaGlobalSequence = 99;
    check(model, Code::MaterialAlphaGlobalSequenceUnsupported);
    // 全局轨迹（6F77B1E0 全键查找）不切动作区间，同帧键仍不合法。
    model = Parse(Fixture(1, true)); check(model, Code::MaterialAlphaKeysInvalid);
    model = Parse(Fixture()); model.materials[0].layers[0].hasUninterpretedData = true;
    check(model, Code::MaterialAlphaUninterpreted);
    model = Parse(Fixture()); model.materials[0].hasUninterpretedData = true;
    check(model, Code::MaterialAlphaUninterpreted);
    auto evaluator = Prepare(Compile(Parse(Fixture())), Policy::IdenticalDuplicatesPreview);
    auto stale = Request(); stale.asset.generation = 6; Failed(evaluator, stale, Code::AssetStale);
    Failed(evaluator, Request(99), Code::PoseSequenceInvalid);
}
void CurvesAndBudgets() {
    for (const auto mode : {0u, 1u, 2u, 3u}) {
        const auto model = Parse(Fixture(mode)); auto evaluator = Prepare(Compile(model), Policy::IdenticalDuplicatesPreview);
        Evaluate(evaluator, Request(kStand, 250)); Values(evaluator, 1);
        auto soleTime = model;
        for (auto& layer : soleTime.materials[0].layers) {
            layer.alphaKeys.resize(3);
            for (auto& key : layer.alphaKeys) key.frame = 250;
        }
        auto soleEvaluator = Prepare(Compile(soleTime), Policy::IdenticalDuplicatesPreview);
        for (const auto elapsed : {0u, 250u, 999u}) { Evaluate(soleEvaluator, Request(kStand, elapsed)); Values(soleEvaluator, 1); }
    }
    const auto asset = Compile(Parse(Fixture(1, false, 12)));
    auto strict = Prepared(asset, Policy::Strict); auto preview = Prepared(asset, Policy::IdenticalDuplicatesPreview);
    Require(strict.Succeeded() && preview.Succeeded(), "budget prewarm baseline failed");
    Evaluate(*strict.value, Request()); Values(*strict.value, 1);
    Evaluate(*preview.value, Request()); Values(*preview.value, 1);
    // 逐动作区间计入编译预算；上限 1 字节必须拒绝。
    Require(!CompileMdxModel(Parse(Fixture()), {23, 5}, {"fixture", "original", "bounded"}, 1).Succeeded(), "compiled KMTA ranges ignored byte cap");
}
std::string EnvironmentPath() {
#if defined(_WIN32) && defined(_MSC_VER)
    std::size_t required = 0;
    Require(getenv_s(&required, nullptr, 0, "FORGE_KMTA_MODEL") == 0, "real KMTA environment query failed");
    if (required <= 1) return {};
    std::string path(required, '\0'); std::size_t copied = 0;
    Require(getenv_s(&copied, path.data(), path.size(), "FORGE_KMTA_MODEL") == 0 && copied == required,
        "real KMTA path changed during read"); path.resize(required - 1); return path;
#else
    const auto* path = std::getenv("FORGE_KMTA_MODEL"); return path ? path : "";
#endif
}
// 真实模型：源形状不变；两种材质键策略输出一致、无预览警告，且与共用原版区间参考逐层相等。
bool RealModel() {
    const auto path = EnvironmentPath(); if (path.empty()) { std::cout << "SKIP: FORGE_KMTA_MODEL unset\n"; return false; }
    std::ifstream input(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    Require(input.good(), "real KMTA model open failed"); const auto length = input.tellg();
    Require(length == 1579976, "different real KMTA model size");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length)); input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Require(input.good(), "real KMTA model read failed"); const auto original = bytes;
    auto parsed = ParseMdx(bytes); Require(parsed.Succeeded(), "real KMTA model parse failed"); const auto& model = *parsed.value;
    Require(model.materials.size() == 14 && model.materials[3].layers.size() == 4 && model.sequences.size() == 24
        && model.sequences[6].start == 81841 && model.sequences[6].end == 86010 && model.sequences[17].start == 159238,
        "real KMTA source shape changed; root wrapper must also bind SHA");
    const auto asset = Compile(model); const BatchLimits limits{1, asset.NodeCount(), asset.GroupCount(), asset.GeosetCount(), 2 * 1024 * 1024};
    auto strict = Prepare(asset, Policy::Strict, limits, NodeKeySamplingPolicy::SourceOrderPreview, GeosetFailurePolicy::PreviewHideInvalidGeosets);
    auto preview = Prepare(asset, Policy::IdenticalDuplicatesPreview, limits, NodeKeySamplingPolicy::SourceOrderPreview,
        GeosetFailurePolicy::PreviewHideInvalidGeosets);
    std::uint32_t cases = 0;
    for (const auto sequence : {6u, 17u, 21u}) for (const auto elapsed : {0u, 1u, 408u, 815u, 4168u}) {
        const auto request = Request(sequence, elapsed);
        Evaluate(strict, request); Evaluate(preview, request);
        if (strict.Receipts()[0].error != Code::Ok) continue; // 节点/几何体自身问题不属于本组。
        const auto a = strict.LayerAlphas(0), b = preview.LayerAlphas(0);
        Require(preview.Receipts()[0].error == Code::Ok && std::equal(a.begin(), a.end(), b.begin(), b.end()),
            "real KMTA policies diverged");
        NoPreviewWarnings(strict); NoPreviewWarnings(preview);
        const auto reference = SampleMdxLayerAlphas(model, {0, sequence, strict.Receipts()[0].sequenceFrame
            - model.sequences[sequence].start, 0});
        Require(reference.Succeeded() && std::equal(a.begin(), a.end(), reference.value->begin(), reference.value->end()),
            "real KMTA differs from shared reference");
        ++cases;
    }
    Require(cases > 0 && bytes == original, "real KMTA no comparable case or source changed");
    std::cout << "REAL_KMTA_CPU: " << cases << " cases match the shared game-range reference\n";
    return true;
}
}
int main(int argc, char** argv) {
    try {
        Require(argc == 2, "group required"); const std::string group = argv[1];
        if (group == "policy") PolicyDefaults();
        else if (group == "sampling") SamplingAndProvenance();
        else if (group == "negative") NegativeGates();
        else if (group == "budgets") CurvesAndBudgets();
        else if (group == "real-model") { if (!RealModel()) return 77; }
        else throw std::runtime_error("unknown group");
        std::cout << "PASS " << group << '\n'; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
