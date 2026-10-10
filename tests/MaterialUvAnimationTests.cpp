// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
// TXAN 贴图坐标动画：原创合成夹具，期望值按原版 6F77C730 的合成顺序（平移→绕中心缩放→绕中心旋转）手算。
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"
#include "War3MapCore/Render/MdxModel.h"
#include "War3RenderCore/PoseBatch.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
using namespace War3Map::Render;
using namespace War3RenderCore;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

// 条目0：KTAT 线性 0→(1,0.5,0)，帧 0..1000（Stand 区间）。
// 条目1：KTAR 单键绕 Z 转 90°，KTAS 单键 (2,2,1)。
std::vector<std::byte> Fixture(bool truncated = false) {
    auto bytes = MdxTest::BuildTestMdx({.animatedHelper = true});
    MdxTest::Writer out; out.bytes = std::move(bytes);
    out.Tag("TXAN"); const auto chunk = out.Begin();
    {
        const auto entry = out.Begin();
        out.Tag("KTAT"); out.U32(2); out.U32(1); out.U32(UINT32_MAX);
        out.U32(0); out.F32(0); out.F32(0); out.F32(0);
        out.U32(1000); out.F32(1); out.F32(0.5f); out.F32(0);
        out.EndInclusive(entry);
    }
    {
        const auto entry = out.Begin();
        const auto s = std::sqrt(0.5f);
        out.Tag("KTAR"); out.U32(1); out.U32(0); out.U32(UINT32_MAX);
        out.U32(0); out.F32(0); out.F32(0); out.F32(s); out.F32(s);
        out.Tag("KTAS"); out.U32(1); out.U32(0); out.U32(UINT32_MAX);
        out.U32(0); out.F32(2); out.F32(2); out.F32(1);
        if (truncated) out.Tag("KTAT"); // 剩余字节不足一个轨迹头
        out.EndInclusive(entry);
    }
    out.EndChunk(chunk);
    return std::move(out.bytes);
}
MdxModel Parse(bool truncated = false) {
    auto parsed = ParseMdx(Fixture(truncated));
    Check(parsed.Succeeded(), "TXAN夹具解析失败");
    auto model = std::move(*parsed.value);
    Check(model.materials.size() == 1 && model.materials[0].layers.size() == 2, "夹具材质层数变化");
    model.materials[0].layers[0].textureAnimationId = 0;
    model.materials[0].layers[1].textureAnimationId = 1;
    return model;
}
struct Prepared {
    CompiledModel asset;
    CompiledMaterialTextures textures;
    std::vector<War3Map::Diagnostic> diagnostics;
};
Prepared Prepare(const MdxModel& model) {
    const auto compiled = CompileMdxModel(model, {1, 1}, {"uv.mdx", "original", "uv"});
    Check(compiled.Succeeded(), "姿势资产编译失败");
    const auto textures = CompileMdxMaterialTextures(model, *compiled.value);
    Check(textures.Succeeded(), "TXAN sidecar编译失败");
    return {*compiled.value, *textures.value, textures.diagnostics};
}
bool HasCode(const std::vector<War3Map::Diagnostic>& diagnostics, const char* code) {
    for (const auto& d : diagnostics) if (d.code == code) return true;
    return false;
}
PoseRequest Request(std::uint32_t sequence, std::uint64_t time) {
    PoseRequest r; r.asset = {1, 1}; r.instance = {1, 1}; r.sequenceIndex = sequence; r.sequenceElapsedMs = time; return r;
}
std::vector<UvTransform> Evaluate(const Prepared& prepared, std::uint32_t sequence, std::uint64_t time) {
    const std::array assets{prepared.asset}; const std::array textureAssets{prepared.textures};
    auto made = PoseBatchEvaluator::Create(assets, 1, {}, GeosetFailurePolicy::Strict, {}, {1, 64}, textureAssets);
    Check(made.Succeeded(), "TXAN容量预热失败");
    const auto request = Request(sequence, time);
    RenderCoreAllocProbe::Begin(); const auto outcome = made.value->Evaluate(std::span(&request, 1));
    const auto allocations = RenderCoreAllocProbe::End();
    Check(outcome.error == Code::Ok && !outcome.failedInstances && !allocations, "TXAN热路径失败或分配");
    const auto values = made.value->LayerUvTransforms(0);
    Check(values.size() == 2 && made.value->LayerTextureIds(0).size() == 2, "TXAN输出与层序不同长");
    return {values.begin(), values.end()};
}
void Near(const UvTransform& actual, const UvTransform& expected, const char* message) {
    for (std::size_t i = 0; i < 6; ++i) Check(std::fabs(actual[i] - expected[i]) < 1e-5f, message);
}
void Run() {
    const auto model = Parse();
    Check(model.textureAnimations.size() == 2 && model.textureAnimationIssue.empty()
        && model.textureAnimations[0].translation && !model.textureAnimations[0].rotation
        && model.textureAnimations[1].rotation && model.textureAnimations[1].scaling, "TXAN轨迹未按标签读取");
    const auto prepared = Prepare(model);
    Check(prepared.textures.HasAnimatedUv()
        && HasCode(prepared.diagnostics, "MATERIAL_UV_ANIMATION_PREVIEW"), "TXAN动画标记/披露缺失");

    // Stand(0)：线性平移；旋转+缩放层 u'=1.5-2v，v'=2u-0.5。
    auto uv = Evaluate(prepared, 0, 500);
    Near(uv[0], {1, 0, 0.5f, 0, 1, 0.25f}, "KTAT线性平移错误");
    Near(uv[1], {0, -2, 1.5f, 2, 0, -0.5f}, "KTAR/KTAS未按原版中心与顺序合成");
    uv = Evaluate(prepared, 0, 0);
    Near(uv[0], IdentityUvTransform, "首键平移不为零");
    // Death(1)：原版逐动作区间内没有键时取默认值，不借用 Stand 的键。
    uv = Evaluate(prepared, 1, 500);
    Near(uv[0], IdentityUvTransform, "动作区间外平移键被借用");
    Near(uv[1], IdentityUvTransform, "动作区间外旋转/缩放键被借用");

    // 平移+缩放+旋转同一条目：检查合成顺序（先平移后缩放）。
    auto combined = model;
    combined.textureAnimations[1].translation = combined.textureAnimations[0].translation;
    combined.textureAnimations[1].rotation.reset();
    uv = Evaluate(Prepare(combined), 0, 500);
    // 平移(0.5,0.25)后绕中心缩放：u'=2(u+0.5-0.5)+0.5=2u+0.5，v'=2(v+0.25-0.5)+0.5=2v。
    Near(uv[1], {2, 0, 0.5f, 0, 2, 0}, "平移未先于缩放");

    // 无 TXAN 引用：单位变换，且不宣称坐标动画。
    auto plain = model;
    for (auto& layer : plain.materials[0].layers) layer.textureAnimationId = UINT32_MAX;
    const auto staticPrepared = Prepare(plain);
    Check(!staticPrepared.textures.HasAnimatedUv(), "无引用仍标记坐标动画");
    for (const auto& value : Evaluate(staticPrepared, 0, 500)) Near(value, IdentityUvTransform, "无TXAN层非单位变换");

    // 引用不存在的条目 / 坏全局序列：保持静态坐标并报告，不拒绝整个模型。
    auto missing = model; missing.materials[0].layers[0].textureAnimationId = 7;
    const auto missingPrepared = Prepare(missing);
    Check(HasCode(missingPrepared.diagnostics, "MATERIAL_UV_ANIMATION_STATIC"), "缺失TXAN引用未报告");
    Near(Evaluate(missingPrepared, 0, 500)[0], IdentityUvTransform, "缺失TXAN引用未保持静态");
    auto badGlobal = model; badGlobal.textureAnimations[0].translation->globalSequence = 42;
    const auto badPrepared = Prepare(badGlobal);
    Check(HasCode(badPrepared.diagnostics, "MATERIAL_UV_ANIMATION_STATIC"), "坏全局序列未报告");
    Near(Evaluate(badPrepared, 0, 500)[0], IdentityUvTransform, "坏全局序列TXAN仍被使用");

    // 坏 TXAN 块：模型其余部分照常读取，整块不采用并给出诊断。
    const auto truncated = ParseMdx(Fixture(true));
    Check(truncated.Succeeded() && truncated.value->textureAnimations.empty()
        && !truncated.value->textureAnimationIssue.empty() && HasCode(truncated.diagnostics, "MDX_TXAN_UNINTERPRETED")
        && !truncated.value->geosets.empty(), "坏TXAN块连带拒绝模型或静默丢弃");

    // KMTF 层带 TXAN 不再整层拒绝：贴图ID照常取键，坐标单独求值。
    auto both = model; auto& layer = both.materials[0].layers[1];
    layer.animatedTexture = true; layer.textureInterpolation = 0; layer.textureGlobalSequence = UINT32_MAX;
    layer.textureKeys = {{0, layer.textureId, 0, 0, 0}};
    const auto bothPrepared = Prepare(both);
    Check(bothPrepared.textures.HasAnimatedTexture() && bothPrepared.textures.HasAnimatedUv(), "KMTF+TXAN标记错误");
    Near(Evaluate(bothPrepared, 0, 500)[1], {0, -2, 1.5f, 2, 0, -0.5f}, "KMTF层TXAN未求值");
    const auto reference = SampleMdxLayerTextureIds(both, {0, 0, 500, 0});
    Check(reference.Succeeded(), "参考实现仍因TXAN拒绝KMTF层");
}
}
int main() {
    try { Run(); std::cout << "material_uv_animation game_order=1 game_ranges=1 static_fallback_reported=1 no_alloc=1\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
