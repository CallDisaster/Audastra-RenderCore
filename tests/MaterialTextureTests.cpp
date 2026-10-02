// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"
#include "War3MapCore/Render/MdxModel.h"
#include "War3RenderCore/PoseBatch.h"
#include <array>
#include <iostream>
#include <stdexcept>
namespace {
using namespace War3Map::Render;
using namespace War3RenderCore;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::byte> Fixture(std::uint32_t mode = 0, bool global = false, bool duplicate = false) {
    const auto input = MdxTest::BuildTestMdx(); MdxTest::Writer out; out.Tag("MDLX");
    for (std::size_t at = 4; at < input.size();) {
        std::uint32_t size = 0; std::memcpy(&size, input.data() + at + 4, 4);
        if (std::memcmp(input.data() + at, "MTLS", 4)) out.Raw(input.data() + at, size + 8);
        else {
            out.Tag("MTLS"); const auto chunk = out.Begin(), material = out.Begin();
            out.U32(0); out.U32(0); out.Tag("LAYS"); out.U32(2);
            for (const auto animated : {false, true}) {
                const auto layer = out.Begin(); out.U32(0); out.U32(0); out.U32(animated ? 0u : 1u);
                out.U32(UINT32_MAX); out.U32(0); out.F32(1);
                if (animated) for (std::uint32_t copy = 0; copy < (duplicate ? 2u : 1u); ++copy) {
                    out.Tag("KMTF"); out.U32(2); out.U32(mode); out.U32(global ? 0u : UINT32_MAX);
                    for (const auto frame : {100u, 500u}) {
                        out.U32(frame); out.U32(frame == 100 ? 1u : 0u);
                        if (mode > 1) { out.U32(0xf1234567u); out.U32(0x87654321u); }
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
CompiledModel Asset(const MdxModel& model) {
    const auto compiled = CompileMdxModel(model, {1, 1}, {"fixture.mdx", "original", "texture"});
    Check(compiled.Succeeded(), "KMTF姿势资产编译失败"); return *compiled.value;
}
PoseBatchEvaluator Evaluator(const MdxModel& model) {
    const std::array assets{Asset(model)}; const auto textures = CompileMdxMaterialTextures(model, assets[0]);
    Check(textures.Succeeded(), "KMTF拥有元数据编译失败");
    const std::array textureAssets{*textures.value};
    auto made = PoseBatchEvaluator::Create(assets, 1, {}, GeosetFailurePolicy::Strict, {}, {1, 64}, textureAssets);
    Check(made.Succeeded(), "KMTF容量预热失败"); return std::move(*made.value);
}
PoseRequest Request(std::uint64_t time, std::uint64_t global = 0) {
    PoseRequest result; result.asset = {1, 1}; result.instance = {1, 1}; result.sequenceIndex = 1;
    result.sequenceElapsedMs = time; result.globalElapsedMs = global; return result;
}
Code Compare(const MdxModel& model, std::uint64_t time, std::uint64_t global = 0) {
    auto evaluator = Evaluator(model); const auto request = Request(time, global);
    RenderCoreAllocProbe::Begin(); const auto result = evaluator.Evaluate(std::span(&request, 1));
    const auto allocations = RenderCoreAllocProbe::End(); Check(result.error == Code::Ok && !allocations, "KMTF热路径分配");
    const auto reference = SampleMdxLayerTextureIds(model, {0, 1, time, global});
    const auto error = evaluator.Receipts()[0].error;
    Check(reference.Succeeded() == (error == Code::Ok), "KMTF接受差分");
    const auto values = evaluator.LayerTextureIds(0);
    if (reference.Succeeded()) Check(values.size() == reference.value->size()
        && !std::memcmp(values.data(), reference.value->data(), values.size_bytes()), "KMTF uint32逐位差分");
    else Check(reference.diagnostics[0].code == CodeName(error) && values.empty()
        && evaluator.Groups(0).empty(), "KMTF失败发布部分帧");
    return error;
}
void Run() {
    auto model = *ParseMdx(Fixture()).value;
    const auto& layer = model.materials[0].layers[1];
    Check(layer.textureKeys.size() == 2 && layer.textureKeys[0].sourceByteOffset == layer.textureSourceByteOffset + 16,
        "KMTF原始字段/偏移丢失");
    for (const auto time : std::array<std::uint64_t, 8>{0, 99, 100, 499, 500, 999, 1000, UINT64_MAX})
        Check(Compare(model, time) == Code::Ok, "KMTF正常阶梯拒绝");
    Check((*SampleMdxLayerTextureIds(model, {0, 1, 99, 0}).value)[1] == 0
        && (*SampleMdxLayerTextureIds(model, {0, 1, 100, 0}).value)[1] == 1, "首键前/键时刻非真实静态阶梯");
    auto evaluator = Evaluator(model); std::array requests{Request(100), Request(500)}; requests[1].instance = {2, 1};
    RenderCoreAllocProbe::Begin(); const auto two = evaluator.Evaluate(requests); const auto allocated = RenderCoreAllocProbe::End();
    Check(!two.failedInstances && !allocated && evaluator.LayerTextureIds(0)[1] == 1
        && evaluator.LayerTextureIds(1)[1] == 0, "两实例贴图串时钟/分配");
    Check(evaluator.EvaluateExternalFrames({}, {}, {}, {}, {}).error == Code::RequestInvalid
        && evaluator.LayerTextureIds(0).empty(), "纹理模式误接外部帧/旧输出仍有效");
    auto global = *ParseMdx(Fixture(0, true)).value;
    for (auto time : std::array<std::uint64_t, 6>{0, 399, 400, 999, 1000, UINT64_MAX}) Check(Compare(global, time, time) == Code::Ok, "全局KMTF差分");
    global.globalSequences[0] = 0;
    Check(Compare(global, 500, UINT64_MAX) == Code::Ok
        && (*SampleMdxLayerTextureIds(global, {0, 1, 500, UINT64_MAX}).value)[1] == 1, "KMTF零周期未取归零首键");
    const auto asset = Asset(model); const std::array assets{asset};
    const auto metadata = *CompileMdxMaterialTextures(model, asset).value;
    Check(metadata.ReachableTextureIds(1).size() == 2 && metadata.ReachableTextureIds(1)[0] == 0
        && metadata.ReachableTextureIds(1)[1] == 1, "可达TEXS集合遗漏静态/原键");
    const auto original = PoseBatchEvaluator::Create(assets, 1);
    auto disabled = PoseBatchEvaluator::Create(assets, 1, {}, GeosetFailurePolicy::Strict, {}, {}, {});
    Check(disabled.Succeeded() && disabled.value->StorageBytes() == original.value->StorageBytes()
        && !disabled.value->TextureChannelsEnabled(), "默认预算/渠道被改变");
    const std::array textureAssets{metadata}; auto tight = BatchLimits{}; tight.maxBytes = original.value->StorageBytes();
    Check(!PoseBatchEvaluator::Create(assets, 1, tight, GeosetFailurePolicy::Strict, {}, {1, 64}, textureAssets).Succeeded(), "纹理缓冲未计预算");
    const std::array unrelated{Asset(model)};
    Check(!PoseBatchEvaluator::Create(unrelated, 1, {}, GeosetFailurePolicy::Strict, {}, {1, 64}, textureAssets).Succeeded(), "不同compiled owner借用sidecar");
    auto invalid = model; auto& bad = invalid.materials[0].layers[1]; bad.textureKeys[0].textureId = 0xf1234567u;
    Check(Compare(invalid, 0) == Code::MaterialTextureReferenceInvalid, "uint32坏ID未拒绝");
    auto broken = Evaluator(invalid); const auto request = Request(0); (void)broken.Evaluate(std::span(&request, 1));
    Check(broken.TextureIssues(0)[0].sourceValueBits == 0xf1234567u
        && broken.TextureIssues(0)[0].sourceByteOffset == bad.textureKeys[0].sourceByteOffset + 4, "坏ID原值/offset被float损坏");
    invalid = model; invalid.materials[0].layers[1].textureKeys[1].frame = 100;
    Check(Compare(invalid, 0) == Code::MaterialTextureKeysInvalid, "重复帧未拒绝");
    invalid = model; invalid.materials[0].layers[1].textureGlobalSequence = 99;
    Check(Compare(invalid, 0) == Code::MaterialTextureGlobalSequenceUnsupported, "不存在GLBS引用未拒绝");
    global.materials[0].layers[1].textureKeys[1].frame = 0x80000100u;
    Check(Compare(global, 0) == Code::MaterialTextureGlobalSpanUnsupported, "全局跨度越界未拒绝");
    invalid = model; invalid.materials[0].layers[1].textureKeys.clear();
    Check(Compare(invalid, 500) == Code::Ok
        && (*SampleMdxLayerTextureIds(invalid, {0, 1, 500, 0}).value)[1] == 0, "空轨迹未使用真实静态ID");
    invalid = model; invalid.sequences[1].flags = 1;
    Check(Compare(invalid, UINT64_MAX) == Code::Ok
        && (*SampleMdxLayerTextureIds(invalid, {0, 1, UINT64_MAX, 0}).value)[1] == 0, "尾后未保持真实末键");
    auto cubic = *ParseMdx(Fixture(2)).value;
    Check(cubic.materials[0].layers[1].textureKeys[0].inTangentBits == 0xf1234567u
        && Compare(cubic, 0) == Code::MaterialTextureInterpolationUnsupported, "未证插值静默执行/原位丢失");
    const auto duplicate = *ParseMdx(Fixture(0, false, true)).value;
    Check(duplicate.materials[0].layers[1].duplicateTextureTracks.size() == 1
        && duplicate.materials[0].layers[1].duplicateTextureTracks[0].keys.size() == 2
        && Compare(duplicate, 0) == Code::MaterialTextureUninterpreted, "重复tag未owned/未拒绝");
    auto external = PoseBatchEvaluator::CreateExternalFrames(assets, 2);
    Check(external.Succeeded() && !external.value->TextureChannelsEnabled()
        && external.value->Evaluate(std::span(&request, 1)).error == Code::RequestInvalid, "外部帧误启新渠道");
}
}
int main() {
    try { Run(); std::cout << "material_textures generic_preview=1 native_exact=0 uint32_bitwise=1 no_alloc=1 legacy_default=1\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
