// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
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

std::vector<std::byte> Fixture(std::uint32_t mode = 1, bool global = false, std::uint32_t repeats = 1) {
    const auto base = MdxTest::BuildTestMdx(); MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at = 4; at < base.size();) {
        std::uint32_t size = 0; std::memcpy(&size, base.data() + at + 4, 4);
        if (std::memcmp(base.data() + at, "MTLS", 4)) out.Raw(base.data() + at, size + 8);
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
    auto result = ParseMdx(bytes); Require(result.Succeeded(), "MDX duplicate fixture parse failed");
    auto model = std::move(*result.value); model.sequences[0].flags = 1; return model;
}
CompiledModel Compile(const MdxModel& model) {
    auto result = CompileMdxModel(model, {23, 5}, {"Tests/KmtaDuplicates.mdx", "synthetic-or-local", "original"});
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
    Require(result.Succeeded(), "KMTA duplicate prewarm failed"); return std::move(*result.value);
}
PoseRequest Request(std::uint32_t sequence = 1, std::uint64_t elapsed = 0) {
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
std::uint32_t WarningCount(PoseBatchEvaluator& evaluator, std::uint32_t instance = 0) {
    std::uint32_t count = 0;
    for (const auto& warning : evaluator.Diagnostics(instance)) {
        if (warning.code != Code::MaterialAlphaIdenticalDuplicatesPreview) continue;
        Require(warning.severity == Severity::Warning && warning.track == TrackKind::MaterialAlpha && warning.objectId == NoIndex,
            "KMTA warning posed as node or Error");
        ++count;
    }
    const auto sources = evaluator.MaterialAlphaPreviewWarnings(instance);
    const auto diagnostics = evaluator.Diagnostics(instance);
    Require(sources.size() == count, "typed material source count differs from successful diagnostics");
    for (const auto& warning : sources) {
        Require(warning.diagnosticIndex < diagnostics.size(), "typed material diagnostic index outside owner span");
        const auto& diagnostic = diagnostics[warning.diagnosticIndex];
        Require(diagnostic.code == Code::MaterialAlphaIdenticalDuplicatesPreview && diagnostic.severity == Severity::Warning
            && diagnostic.track == TrackKind::MaterialAlpha && diagnostic.objectId == NoIndex
            && diagnostic.leftFrame == warning.sourceFrame && diagnostic.rightFrame == warning.sourceFrame,
            "typed material source bound to wrong diagnostic");
    }
    return count;
}
void Values(PoseBatchEvaluator& evaluator, float upper, std::uint32_t instance = 0) {
    const auto values = evaluator.LayerAlphas(instance);
    Require(evaluator.Receipts()[instance].error == Code::Ok && evaluator.Receipts()[instance].poseVersion != 0
        && values.size() == 4, "KMTA success missing owned layer outputs");
    for (std::uint32_t i = 0; i < 4; ++i)
        Require(values[i] == (i < 2 ? upper : 1.0f - upper), "KMTA static fallback or wrong material alpha");
    for (const auto& group : evaluator.Groups(instance)) for (const auto value : group.values)
        Require(std::isfinite(value), "KMTA compatible pose nonfinite");
}
void PolicyDefaults() {
    const auto bytes = Fixture(); const auto before = bytes; const auto model = Parse(bytes); const auto asset = Compile(model);
    const std::array assets{asset};
    auto legacy = PoseBatchEvaluator::Create(assets, 31, Limits(), GeosetFailurePolicy::Strict, {1, 64});
    Require(legacy.Succeeded(), "old material Create failed");
    auto strict = Prepare(asset, Policy::Strict);
    Require(strict.StorageBytes() == legacy.value->StorageBytes(), "explicit Strict changed old Create storage");
    const auto request = Request(1, 500);
    Failed(*legacy.value, request, Code::MaterialAlphaKeysInvalid); Failed(strict, request, Code::MaterialAlphaKeysInvalid);
    const auto issue = strict.MaterialIssues(0);
    const auto& duplicate = model.materials[0].layers[0].alphaKeys[2];
    Require(issue.size() == 1 && issue[0].materialIndex == 0 && issue[0].layerIndex == 0 && issue[0].sourceFrame == 250
        && issue[0].sourceByteOffset == duplicate.sourceByteOffset && issue[0].sourceValueBits == 250, "Strict original source issue changed");
    const auto reference = SampleMdxLayerAlphas(model, {0, 1, 500, 0});
    Require(!reference.Succeeded() && reference.diagnostics.front().code == CodeName(Code::MaterialAlphaKeysInvalid),
        "old reference default was relaxed");
    auto nodeOnly = PoseBatchEvaluator::Create(assets, 31, Limits(), GeosetFailurePolicy::Strict, {1, 64}, {}, {},
        NodeKeySamplingPolicy::SourceOrderPreview);
    Require(nodeOnly.Succeeded(), "node-only prewarm failed");
    Failed(*nodeOnly.value, request, Code::MaterialAlphaKeysInvalid);
    Require(!Prepared(asset, static_cast<Policy>(99)).Succeeded(), "unknown material key policy accepted");
    Require(!PoseBatchEvaluator::Create(assets, 31, Limits(), GeosetFailurePolicy::Strict, {}, {}, {},
        NodeKeySamplingPolicy::Strict, Policy::IdenticalDuplicatesPreview).Succeeded(), "preview enabled without material outputs");
    auto disabled = PoseBatchEvaluator::Create(assets, 31, Limits()); Require(disabled.Succeeded(), "disabled material prepare");
    Evaluate(*disabled.value, request);
    Require(disabled.value->Receipts()[0].error == Code::Ok && disabled.value->LayerAlphas(0).empty()
        && WarningCount(*disabled.value) == 0, "disabled/default path was changed");
    Require(bytes == before && model.materials[0].layers[0].alphaKeys.size() == 7, "source keys or bytes changed");
}
void SamplingAndProvenance() {
    const auto bytes = Fixture(); const auto before = bytes; const auto model = Parse(bytes); const auto asset = Compile(model);
    const auto* compiled = Detail::ModelAccess::Get(asset);
    Require(compiled->materialDuplicateWarnings == 4, "qualified warning ownership count");
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& channel = compiled->materialAlphas[i]; const auto& raw = model.materials[0].layers[i];
        Require(channel.alpha.error == Code::MaterialAlphaKeysInvalid && channel.identicalDuplicates
            && channel.identicalDuplicates->alpha.keys.size() == raw.alphaKeys.size() && channel.sources.size() == raw.alphaKeys.size(),
            "strict error lost or compatible raw-key owner truncated");
        for (std::size_t k = 0; k < raw.alphaKeys.size(); ++k)
            Require(channel.identicalDuplicates->alpha.keys[k].frame == raw.alphaKeys[k].frame
                && std::bit_cast<std::uint32_t>(channel.identicalDuplicates->alpha.keys[k].value[0]) == std::bit_cast<std::uint32_t>(raw.alphaKeys[k].value),
                "compatible prepared key sorted, removed, or changed");
    }
    auto evaluator = Prepare(asset, Policy::IdenticalDuplicatesPreview);
    for (const auto time : {0u, 249u, 250u, 251u, 999u, 1000u}) {
        Evaluate(evaluator, Request(1, time)); Values(evaluator, 1); Require(WarningCount(evaluator) == 4, "Stand warning count");
        for (const auto& warning : evaluator.MaterialAlphaPreviewWarnings(0)) {
            const auto& raw = model.materials[0].layers[warning.layerIndex];
            Require(warning.materialIndex == 0 && warning.sourceFrame == 250
                && warning.leftSourceByteOffset == raw.alphaKeys[1].sourceByteOffset
                && warning.rightSourceByteOffset == raw.alphaKeys[2].sourceByteOffset
                && warning.sourceValueBits == std::bit_cast<std::uint32_t>(raw.alphaKeys[2].value)
                && warning.interpolation == 1 && warning.inTangentBits == 0 && warning.outTangentBits == 0,
                "warning physical source or original bits missing");
            Require(DescribeDiagnostic(evaluator.Diagnostics(0)[warning.diagnosticIndex]).find("同帧同值") != std::string::npos,
                "material warning description missing");
        }
    }
    for (const auto time : {0u, 250u, 500u, 750u, 1000u}) {
        Evaluate(evaluator, Request(0, time)); Values(evaluator, 1.0f - static_cast<float>(time) / 1000.0f);
    }
    std::array requests{Request(0, 250), Request(0, 750)}; requests[1].instance = {2, 7};
    Evaluate(evaluator, requests); Values(evaluator, .75f, 0); Values(evaluator, .25f, 1);
    Require(WarningCount(evaluator, 0) == 4 && WarningCount(evaluator, 1) == 4
        && evaluator.Receipts()[0].instance != evaluator.Receipts()[1].instance
        && evaluator.Receipts()[0].poseVersion != evaluator.Receipts()[1].poseVersion, "instance warning/clock/version crossed");
    Evaluate(evaluator, Request(1, 0)); Values(evaluator, 1);
    Require(bytes == before && model.materials[0].layers[0].alphaKeys.size() == 7, "raw original changed");
}
void NegativeGates() {
    const auto check = [](MdxModel model, Code code = Code::MaterialAlphaKeysInvalid) {
        const auto asset = Compile(model); auto evaluator = Prepare(asset, Policy::IdenticalDuplicatesPreview);
        Failed(evaluator, Request(1, 500), code);
        Require(!Detail::ModelAccess::Get(asset)->materialAlphas[0].identicalDuplicates, "invalid complete track prequalified");
    };
    auto model = Parse(Fixture()); model.materials[0].layers[0].alphaKeys[2].value = .25f; check(model);
    model = Parse(Fixture()); model.materials[0].layers[0].alphaKeys[1].value = 0;
    model.materials[0].layers[0].alphaKeys[2].value = std::bit_cast<float>(0x80000000u); check(model);
    model = Parse(Fixture()); model.materials[0].layers[0].alphaKeys[2].frame = 249; check(model);
    for (const auto mode : {2u, 3u}) {
        model = Parse(Fixture(mode)); model.materials[0].layers[0].alphaKeys[2].outTangent = .25f; check(model);
        model = Parse(Fixture(mode)); model.materials[0].layers[0].alphaKeys[2].inTangent = .25f; check(model);
    }
    for (const auto invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        model = Parse(Fixture()); model.materials[0].layers[0].alphaKeys.back().value = invalid; check(model);
        model = Parse(Fixture(2)); model.materials[0].layers[0].alphaKeys.back().inTangent = invalid; check(model);
        model = Parse(Fixture()); model.materials[0].layers[0].alphaKeys.front().value = invalid;
        check(model, Code::MaterialAlphaNumberInvalid);
    }
    model = Parse(Fixture()); model.materials[0].layers[0].alphaInterpolation = 4;
    check(model, Code::MaterialAlphaInterpolationUnsupported);
    model = Parse(Fixture()); model.materials[0].layers[0].alphaGlobalSequence = 99;
    check(model, Code::MaterialAlphaGlobalSequenceUnsupported);
    model = Parse(Fixture(1, true)); check(model); model.globalSequences[0] = 0; check(model);
    model = Parse(Fixture()); model.materials[0].layers[0].hasUninterpretedData = true;
    check(model, Code::MaterialAlphaUninterpreted);
    model = Parse(Fixture()); model.materials[0].hasUninterpretedData = true;
    check(model, Code::MaterialAlphaUninterpreted);
    // layer0已发成功警告，layer1在动作起点前仍须失败；整失败只发布第一Error。
    model = Parse(Fixture()); model.materials[0].layers[1].alphaKeys[4].frame = 2001;
    auto evaluator = Prepare(Compile(model), Policy::IdenticalDuplicatesPreview);
    Failed(evaluator, Request(0, 0), Code::MaterialAlphaBoundaryUnsupported);
    const auto issue = evaluator.MaterialIssues(0);
    Require(issue.size() == 1 && issue[0].materialIndex == 0 && issue[0].layerIndex == 1 && issue[0].sourceFrame == 2001
        && issue[0].sourceByteOffset == model.materials[0].layers[1].alphaKeys[4].sourceByteOffset + 4,
        "compatible runtime boundary source mismatch");
    Evaluate(evaluator, Request(0, 1));
    const auto recovered = evaluator.LayerAlphas(0);
    Require(evaluator.Receipts()[0].error == Code::Ok && recovered.size() == 4 && recovered[1] == 1
        && std::abs(recovered[0] - .999f) <= .0000001f && std::abs(recovered[2] - .001f) <= .0000001f
        && std::abs(recovered[3] - .001f) <= .0000001f && WarningCount(evaluator) == 4, "boundary recovery stale Error or wrong clocks");
    auto stale = Request(); stale.asset.generation = 6; Failed(evaluator, stale, Code::AssetStale);
    Failed(evaluator, Request(99), Code::PoseSequenceInvalid);
}
void CurvesAndBudgets() {
    for (const auto mode : {0u, 1u, 2u, 3u}) {
        const auto model = Parse(Fixture(mode)); auto evaluator = Prepare(Compile(model), Policy::IdenticalDuplicatesPreview);
        Evaluate(evaluator, Request(1, 250)); Values(evaluator, 1); Require(WarningCount(evaluator) == 4, "curve duplicates missing");
        for (const auto& warning : evaluator.MaterialAlphaPreviewWarnings(0))
            Require(warning.interpolation == mode && warning.inTangentBits == 0 && warning.outTangentBits == 0, "curve raw controls missing");
        auto soleTime = model;
        for (auto& layer : soleTime.materials[0].layers) {
            layer.alphaKeys.resize(3);
            for (auto& key : layer.alphaKeys) key.frame = 250;
        }
        auto soleEvaluator = Prepare(Compile(soleTime), Policy::IdenticalDuplicatesPreview);
        for (const auto elapsed : {0u, 250u, 999u}) {
            Evaluate(soleEvaluator, Request(1, elapsed)); Values(soleEvaluator, 1);
            Require(WarningCount(soleEvaluator) == 8, "same-frame sole-time group changed constant semantics");
        }
    }
    const auto asset = Compile(Parse(Fixture(1, false, 12)));
    auto strict = Prepared(asset, Policy::Strict); auto preview = Prepared(asset, Policy::IdenticalDuplicatesPreview);
    Require(strict.Succeeded() && preview.Succeeded(), "budget prewarm baseline failed");
    const auto strictBytes = strict.value->StorageBytes(), previewBytes = preview.value->StorageBytes();
    // 另含独立typed来源所有者及每实例range，默认工作区保持不变。
    const auto warningPayload = 48ull * Limits().maxInstances * (sizeof(PoseDiagnostic) + sizeof(MaterialAlphaPreviewWarning));
    Require(previewBytes > strictBytes + warningPayload, "duplicate diagnostics/typed sources not charged");
    auto tight = Limits(); tight.maxBytes = previewBytes;
    Require(Prepared(asset, Policy::IdenticalDuplicatesPreview, tight).Succeeded(), "exact duplicate batch budget rejected");
    --tight.maxBytes; Require(!Prepared(asset, Policy::IdenticalDuplicatesPreview, tight).Succeeded(), "one-byte short duplicate budget accepted");
    Evaluate(*preview.value, Request()); Values(*preview.value, 1); Require(WarningCount(*preview.value) == 48, "many duplicate diagnostics truncated by node capacity");
    auto combined = Prepared(asset, Policy::IdenticalDuplicatesPreview, Limits(), NodeKeySamplingPolicy::SourceOrderPreview);
    Require(combined.Succeeded() && combined.value->StorageBytes() == previewBytes + 3ull * Limits().maxNodes * sizeof(PoseDiagnostic),
        "node and material diagnostic budgets coupled");
    const auto big = Compile(Parse(Fixture(1, false, 512)));
    Require(!Prepared(big, Policy::IdenticalDuplicatesPreview).Succeeded(), "oversized warning workspace ignored hard byte cap");
    Require(!CompileMdxModel(Parse(Fixture()), {23, 5}, {"fixture", "original", "bounded"}, 1).Succeeded(), "compiled compatibility ignored byte cap");
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
    for (std::uint32_t layer = 0; layer < 4; ++layer) {
        const auto& source = model.materials[3].layers[layer];
        Require(source.alphaKeys.size() == 48 && source.alphaInterpolation == 1 && source.alphaGlobalSequence == NoIndex
            && source.alphaKeys[3].frame == 11530 && source.alphaKeys[4].frame == 11530
            && source.alphaKeys[3].sourceByteOffset == 3888 + 428ull * layer
            && source.alphaKeys[4].sourceByteOffset == 3896 + 428ull * layer
            && std::bit_cast<std::uint32_t>(source.alphaKeys[3].value) == (layer < 2 ? 0x3f800000u : 0u)
            && std::bit_cast<std::uint32_t>(source.alphaKeys[4].value) == (layer < 2 ? 0x3f800000u : 0u), "real exact duplicate source changed");
    }
    const auto asset = Compile(model); const BatchLimits limits{1, asset.NodeCount(), asset.GroupCount(), asset.GeosetCount(), 2 * 1024 * 1024};
    auto strict = Prepare(asset, Policy::Strict, limits, NodeKeySamplingPolicy::SourceOrderPreview, GeosetFailurePolicy::PreviewHideInvalidGeosets);
    auto preview = Prepare(asset, Policy::IdenticalDuplicatesPreview, limits, NodeKeySamplingPolicy::SourceOrderPreview,
        GeosetFailurePolicy::PreviewHideInvalidGeosets);
    const auto check = [&](std::uint32_t sequence, std::uint64_t elapsed, float upper) {
        const auto request = Request(sequence, elapsed); Failed(strict, request, Code::MaterialAlphaKeysInvalid);
        const auto issue = strict.MaterialIssues(0);
        Require(issue.size() == 1 && issue[0].materialIndex == 3 && issue[0].layerIndex == 0
            && issue[0].sourceFrame == 11530 && issue[0].sourceByteOffset == 3896 && issue[0].sourceValueBits == 11530,
            "real Strict first source changed");
        Evaluate(preview, request); const auto values = preview.LayerAlphas(0);
        Require(preview.Receipts()[0].error == Code::Ok && preview.Receipts()[0].poseVersion != 0 && values.size() == 19
            && WarningCount(preview) == 4, "real compatible pose failed or wrong warnings");
        for (std::size_t i = 0; i < asset.MaterialLayerLayouts().size(); ++i) {
            const auto layout = asset.MaterialLayerLayouts()[i];
            if (layout.materialIndex != 3) continue;
            const float expected = layout.layerIndex < 2 ? upper : 1.0f - upper;
            Require(std::abs(values[i] - expected) <= 0.0000001f, "real Stand/Death wrong actual alpha");
        }
        for (const auto& warning : preview.MaterialAlphaPreviewWarnings(0))
            Require(warning.materialIndex == 3 && warning.layerIndex < 4 && warning.sourceFrame == 11530
                && warning.leftSourceByteOffset == 3888 + 428ull * warning.layerIndex
                && warning.rightSourceByteOffset == 3896 + 428ull * warning.layerIndex
                && warning.sourceValueBits == (warning.layerIndex < 2 ? 0x3f800000u : 0u), "real warning physical owner mismatch");
    };
    for (const auto elapsed : {0u, 1u, 838u, 4168u, 4169u}) check(6, elapsed, 1);
    for (const auto elapsed : {0u, 407u, 408u, 814u, 815u, 816u, 8352u})
        check(17, elapsed, elapsed >= 815 ? 0.0f : static_cast<float>(1.0 - static_cast<double>(elapsed) / 815.0));
    Failed(preview, Request(21, 0), Code::MaterialAlphaBoundaryUnsupported); check(21, 1, 1);
    Require(bytes == original && model.materials[3].layers[0].alphaKeys.size() == 48, "real source bytes/keys changed");
    std::cout << "REAL_KMTA_CPU: source shape/offsets; 13 Stand/Death/Sleep valid cases, Sleep boundary retained; root SHA binding required\n";
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
