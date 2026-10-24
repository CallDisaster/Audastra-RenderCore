// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
// 原创字节夹具。静态资格、原位保留及矩阵的手算期望，不依赖真实游戏资源。
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"
#include "War3MapCore/Render/MdxPose.h"
#include "War3RenderCore/PoseBatch.h"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace War3Map::Render;
using namespace War3RenderCore;
using MdxTest::Writer;
constexpr std::array<std::uint32_t, 3> PivotBits{0x7FC00031u, 0xFFC00051u, 0x80000000u};
constexpr Matrix4 Identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
constexpr std::array<float, 12> GroupIdentity{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool HasCode(const std::vector<War3Map::Diagnostic>& diagnostics, const char* code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
        [&](const auto& diagnostic) { return diagnostic.code == code; });
}
struct Options {
    bool nonFinite = true, parent = false, sibling = false;
    std::uint32_t flags = 0x80000000u;
    const char* nodeTrack = nullptr;
    const char* materialTrack = nullptr;
    const char* extraChunk = nullptr;
    bool geoaTrack = false, overwrittenGeoaTrack = false, externalAnimation = false;
    std::uint32_t extraVertices = 0;
};
struct Fixture {
    std::vector<std::byte> bytes;
    std::size_t pivotOffset = 0, flagsOffset = 0, positionsOffset = 0, normalsOffset = 0, alphaOffset = 0;
};
void NodeTrack(Writer& w, const char* tag) {
    w.Tag(tag); w.U32(2); w.U32(1); w.U32(UINT32_MAX);
    const bool rotation = std::strcmp(tag, "KGRT") == 0;
    const bool scaling = std::strcmp(tag, "KGSC") == 0;
    for (std::uint32_t key = 0; key < 2; ++key) {
        const auto step = static_cast<float>(key);
        w.U32(key * 1000); w.F32(rotation ? 0.0f : scaling ? 1.0f + step : 10.0f * step);
        w.F32(scaling ? 1.0f : 0.0f); w.F32(scaling ? 1.0f : 0.0f);
        if (rotation) w.F32(1);
    }
}
void Geoa(Writer& w, bool track) {
    w.Tag("GEOA"); const auto chunk = w.Begin(); const auto item = w.Begin();
    w.F32(1); w.U32(2); w.F32(0.25f); w.F32(0.5f); w.F32(0.75f); w.U32(0);
    if (track) {
        w.Tag("KGAO"); w.U32(1); w.U32(0); w.U32(UINT32_MAX); w.U32(0); w.F32(1);
    }
    w.EndInclusive(item); w.EndChunk(chunk);
}
Fixture Make(Options options = {}) {
    Fixture result;
    Writer w; w.Tag("MDLX"); w.Tag("VERS"); w.U32(4); w.U32(800);
    w.Tag("MODL"); { const auto at = w.Begin(); w.Fixed("OriginalStatic", 80);
        w.Fixed(options.externalAnimation ? "Animations\\External.mdx" : "", 260);
        for (int i = 0; i < 7; ++i) w.F32(0);
        w.U32(0); w.EndChunk(at); }
    w.Tag("SEQS"); { const auto at = w.Begin(); w.Fixed("Stand", 80); w.U32(0); w.U32(1000);
        w.F32(0); w.U32(0); w.F32(0); w.U32(0);
        for (int i = 0; i < 7; ++i) w.F32(0);
        w.EndChunk(at); }
    w.Tag("TEXS"); w.U32(268); w.U32(0); w.Fixed("Textures\\Original.blp", 260); w.U32(0);
    w.Tag("MTLS"); { const auto chunk = w.Begin(); const auto material = w.Begin();
        w.U32(0); w.U32(0); w.Tag("LAYS"); w.U32(1); const auto layer = w.Begin();
        w.U32(0); w.U32(0); w.U32(0); w.U32(UINT32_MAX); w.U32(0);
        result.alphaOffset = w.bytes.size(); w.F32(1);
        if (options.materialTrack) {
            w.Tag(options.materialTrack); w.U32(1); w.U32(0); w.U32(UINT32_MAX); w.U32(0);
            if (std::strcmp(options.materialTrack, "KMTF") == 0) w.U32(0); else w.F32(1);
        }
        w.EndInclusive(layer); w.EndInclusive(material); w.EndChunk(chunk); }
    const auto vertices = 3 + options.extraVertices;
    w.Tag("GEOS"); { const auto chunk = w.Begin(); const auto geoset = w.Begin();
        w.Tag("VRTX"); w.U32(vertices); result.positionsOffset = w.bytes.size();
        for (const auto& p : std::array<Vec3, 3>{{{2, 3, 4}, {12, 3, 4}, {2, 13, 4}}})
            for (const auto axis : p) w.F32(axis);
        for (std::uint32_t i = 3; i < vertices; ++i) { w.F32(0); w.F32(0); w.F32(0); }
        w.Tag("NRMS"); w.U32(vertices); result.normalsOffset = w.bytes.size();
        for (std::uint32_t i = 0; i < vertices; ++i) { w.F32(0); w.F32(0); w.F32(1); }
        w.Tag("PTYP"); w.U32(1); w.U32(4); w.Tag("PCNT"); w.U32(1); w.U32(3);
        w.Tag("PVTX"); w.U32(3); w.U16(0); w.U16(1); w.U16(2);
        w.Tag("GNDX"); w.U32(vertices);
        for (std::uint32_t i = 0; i < vertices; ++i) w.Raw("\0", 1);
        w.Tag("MTGC"); w.U32(1); w.U32(1); w.Tag("MATS"); w.U32(1); w.U32(0);
        w.U32(0); w.U32(0); w.U32(0); for (int i = 0; i < 7; ++i) w.F32(0); w.U32(0);
        w.Tag("UVAS"); w.U32(1); w.Tag("UVBS"); w.U32(vertices);
        for (std::uint32_t i = 0; i < vertices; ++i) { w.F32(static_cast<float>(i)); w.F32(0); }
        w.EndInclusive(geoset); w.EndChunk(chunk); }
    Geoa(w, options.geoaTrack || options.overwrittenGeoaTrack);
    if (options.overwrittenGeoaTrack) Geoa(w, false);
    w.Tag("BONE"); { const auto chunk = w.Begin(); const auto node = w.Begin();
        w.Fixed("Geometry", 80); w.U32(0); w.U32(options.parent ? 1 : UINT32_MAX);
        result.flagsOffset = w.bytes.size(); w.U32(options.flags);
        if (!options.parent && !options.sibling && options.nodeTrack) NodeTrack(w, options.nodeTrack);
        w.EndInclusive(node); w.U32(UINT32_MAX); w.U32(UINT32_MAX); w.EndChunk(chunk); }
    if (options.parent || options.sibling) {
        w.Tag("HELP"); const auto chunk = w.Begin(); const auto node = w.Begin();
        w.Fixed(options.parent ? "Parent" : "Sibling", 80); w.U32(1); w.U32(UINT32_MAX); w.U32(0);
        if (options.nodeTrack) NodeTrack(w, options.nodeTrack);
        w.EndInclusive(node); w.EndChunk(chunk);
    }
    w.Tag("PIVT"); { const auto chunk = w.Begin(); result.pivotOffset = w.bytes.size();
        for (int n = 0; n < (options.parent || options.sibling ? 2 : 1); ++n)
            for (const auto bits : PivotBits) w.U32(options.nonFinite ? bits : 0u);
        w.EndChunk(chunk); }
    if (options.extraChunk) { w.Tag(options.extraChunk); w.U32(0); }
    result.bytes = std::move(w.bytes);
    return result;
}
void Patch(Fixture& fixture, std::size_t offset, std::uint32_t bits) {
    Check(offset + 4 <= fixture.bytes.size(), "测试补丁边界");
    std::memcpy(fixture.bytes.data() + offset, &bits, 4);
}
auto Compile(const MdxModel& model) {
    return CompileMdxModel(model, {1, 1}, {"original-static.mdx", "original-test", "fixture"});
}
void Rejected(Options options, const char* reason = nullptr) {
    const auto result = ParseMdx(Make(options).bytes);
    Check(!result.Succeeded() && HasCode(result.diagnostics, "MDX_NUMBER_INVALID"), "动态来源的非有限PIVT必须拒绝");
    if (reason) Check(result.diagnostics.front().message.find(reason) != std::string::npos, "拒绝须包含具体资格原因");
}
void RawBitsAndMatrices() {
    for (const bool parent : {false, true}) {
        const auto fixture = Make({.parent = parent}); const auto original = fixture.bytes;
        const auto parsed = ParseMdx(fixture.bytes);
        Check(parsed.Succeeded() && HasCode(parsed.diagnostics, "MDX_NO_CONTROLLER_STATIC"), "静态来源须读回并给出诊断");
        Check(fixture.bytes == original, "不得改写源字节");
        const auto& model = *parsed.value;
        Check(model.noControllerStaticSource && model.bones[0].flags == 0x80000000u, "原始flags必须保留");
        Check(model.skinningTopologyComplete && model.geosets[0].matrixGroupSizes == std::vector<std::uint32_t>{1}
            && model.geosets[0].matrixIndices == std::vector<std::uint32_t>{0}, "原矩阵组和骨骼不得删改");
        for (const auto& pivot : model.pivots)
            for (std::size_t axis = 0; axis < 3; ++axis)
                Check(std::bit_cast<std::uint32_t>(pivot[axis]) == PivotBits[axis], "PIVT NaN载荷和负零须逐位保留");
        const auto& diagnostic = parsed.diagnostics.back();
        Check(diagnostic.origin && diagnostic.origin->offset == fixture.pivotOffset, "警告应定位原PIVT");
        auto compiled = Compile(model);
        Check(compiled.Succeeded() && compiled.value->UsesNoControllerStatic() && !compiled.value->IsRigidStaticPreview()
            && HasCode(compiled.diagnostics, "MDX_NO_CONTROLLER_STATIC"), "编译资格须与缺MTGC近似分开");
        const std::array assets{*compiled.value};
        auto made = PoseBatchEvaluator::Create(assets, 1);
        Check(made.Succeeded(), "静态批量容量准备失败");
        for (const auto time : {0ull, 125ull, 999ull, 12345ull}) {
            const auto reference = SampleMdxNodePose(model, {0, time, 0, 0, std::nullopt});
            Check(reference.Succeeded() && reference.value->chain.size() == (parent ? 2u : 1u), "参考祖先链缺失");
            PoseRequest request; request.asset = {1, 1}; request.instance = {1, 1}; request.sequenceElapsedMs = time;
            RenderCoreAllocProbe::Begin(); const auto outcome = made.value->Evaluate(std::span(&request, 1));
            const auto allocations = RenderCoreAllocProbe::End();
            Check(outcome.error == Code::Ok && outcome.failedInstances == 0 && allocations == 0, "静态热路径必须成功且不分配");
            for (const auto& node : made.value->Nodes(0)) {
                Check(node.localMatrix == Identity && node.worldMatrix == Identity, "静态节点不能进行NaN轴心运算");
                for (std::size_t axis = 0; axis < 3; ++axis)
                    Check(std::bit_cast<std::uint32_t>(node.pivot[axis]) == PivotBits[axis], "姿态来源轴心不可填零");
            }
            for (const auto& node : reference.value->chain)
                Check(node.localMatrix == Identity && node.worldMatrix == Identity, "参考静态矩阵须为单位矩阵");
            Check(made.value->Groups(0).size() == 1 && made.value->Groups(0)[0].values == GroupIdentity, "原组使用有限单位矩阵");
            std::array<Vec3, 3> positions{}, normals{};
            Check(SkinPreviewGeoset(*compiled.value, 0, made.value->Groups(0), positions, normals) == Code::Ok,
                "静态CPU蒙皮失败");
            const auto skinned = SampleMdxRigidSkinnedGeoset(model, {0, 0, time, 0, std::nullopt});
            Check(skinned.Succeeded() && skinned.value->positions == model.geosets[0].positions, "静态参考必须保留原几何");
            for (std::size_t i = 0; i < positions.size(); ++i)
                Check(positions[i] == model.geosets[0].positions[i] && normals[i] == Vec3{0, 0, 1}, "静态输出不得污染顶点法线");
        }
    }
}
void SourceRejections() {
    for (const auto* track : {"KGTR", "KGRT", "KGSC"}) {
        Rejected({.nodeTrack = track}, "节点含TRS");
        Rejected({.parent = true, .nodeTrack = track}, "节点含TRS");
        // 无关兄弟节点也需要控制器，不能只检查被绘制节点的祖先链。
        Rejected({.sibling = true, .nodeTrack = track}, "节点含TRS");
    }
    for (const auto* track : {"KMTA", "KMTF"}) Rejected({.materialTrack = track}, "材质层含");
    for (const auto* chunk : {"TXAN", "GLBS", "PREM", "PRE2", "RIBB", "LITE", "CAMS", "EVTS", "UNKN"})
        Rejected({.extraChunk = chunk}, "源文件含");
    Rejected({.geoaTrack = true}, "源GEOA");
    Rejected({.overwrittenGeoaTrack = true}, "源GEOA");
    Rejected({.externalAnimation = true}, "MODL含外部动画");
    for (const auto flags : {1u, 2u, 4u, 8u, 0x10u, 0x20u, 0x40u, 0x80u, 0x20000000u})
        Rejected({.flags = flags}, "节点含TRS");
    for (const auto kind : {0, 1, 2}) {
        auto fixture = Make();
        Patch(fixture, kind == 0 ? fixture.positionsOffset : kind == 1 ? fixture.normalsOffset : fixture.alphaOffset, 0x7FC00001u);
        const auto parsed = ParseMdx(fixture.bytes);
        Check(!parsed.Succeeded() && HasCode(parsed.diagnostics, "MDX_NUMBER_INVALID"), "静态旁路不得放宽顶点/法线/材质F32");
    }
    auto fixture = Make(); fixture.bytes.pop_back();
    Check(!ParseMdx(fixture.bytes).Succeeded(), "PIVT截断必须拒绝");
    MdxLimits limits; limits.maxPivots = 0;
    Check(!ParseMdx(Make().bytes, limits).Succeeded(), "PIVT预算必须保持");
    fixture = Make(); Patch(fixture, fixture.pivotOffset, 0x7F800000u);
    const auto infinite = ParseMdx(fixture.bytes);
    Check(infinite.Succeeded() && std::isinf(infinite.value->pivots[0][0]), "静态不使用的Inf也应原样保留");
}
void ChangedModelRejections() {
    const auto parsed = ParseMdx(Make().bytes); Check(parsed.Succeeded(), "修改回归夹具失败");
    const auto rejected = [&](auto change) {
        auto model = *parsed.value; change(model);
        Check(!Compile(model).Succeeded(), "修改后的整模型不能沿用旧静态资格");
        Check(!SampleMdxNodePose(model, {}).Succeeded(), "参考不能沿用旧静态资格");
    };
    rejected([](auto& model) { model.noControllerStaticSource.reset(); });
    rejected([](auto& model) { model.pivots[0][0] = std::bit_cast<float>(0x7FC00099u); });
    rejected([](auto& model) { model.bones[0].flags = 0; });
    rejected([](auto& model) { model.bones[0].rotation.emplace(); });
    rejected([](auto& model) { model.bones[0].translation.emplace(); });
    rejected([](auto& model) { model.bones[0].scaling.emplace(); });
    rejected([](auto& model) { model.bones[0].flags |= 8; });
    rejected([](auto& model) { model.bones[0].parentId = 0; });
    rejected([](auto& model) { model.materials[0].layers[0].animatedAlpha = true; });
    rejected([](auto& model) { model.materials[0].layers[0].animatedTexture = true; });
    rejected([](auto& model) { model.materials[0].layers[0].textureAnimationId = 0; });
    rejected([](auto& model) { model.geosets[0].animation->hasAlphaTrack = true; });
    rejected([](auto& model) { model.geosets[0].animation->hasColorTrack = true; });
    rejected([](auto& model) { model.skippedChunks = 1; });
    rejected([](auto& model) { model.premEmitters.emplace_back(); });
    rejected([](auto& model) { model.lights.emplace_back(); });
    rejected([](auto& model) { model.textureAnimations.emplace_back(); });
    rejected([](auto& model) { model.globalSequences.push_back(1000); });
    Check(!CompileMdxModel(*parsed.value, {1, 1}, {"static.mdx", "original", "fixture"}, 1).Succeeded(),
        "新增静态来源仍受编译存储预算约束");
}
void OrdinaryAnimationStillWorks() {
    const auto parsed = ParseMdx(Make({.nonFinite = false, .flags = 0, .nodeTrack = "KGTR"}).bytes);
    Check(parsed.Succeeded() && !parsed.value->noControllerStaticSource, "动态有限模型不应密封静态来源");
    const auto compiled = Compile(*parsed.value);
    Check(compiled.Succeeded() && !compiled.value->UsesNoControllerStatic(), "原动画编译不能被替换");
    const auto pose = SampleMdxNodePose(*parsed.value, {0, 500, 0, 0, std::nullopt});
    Check(pose.Succeeded() && pose.value->chain[0].worldMatrix[12] == 5.0f, "原KGTR仍应随时间平移");
    const auto high = ParseMdx(Make({.nonFinite = false, .nodeTrack = "KGRT"}).bytes);
    Check(high.Succeeded() && !Compile(*high.value).Succeeded(), "未知高位不得在有动画时全局放宽");
    const auto finiteStatic = ParseMdx(Make({.nonFinite = false, .flags = 0}).bytes);
    Check(finiteStatic.Succeeded() && !HasCode(finiteStatic.diagnostics, "MDX_NO_CONTROLLER_STATIC"), "普通有限静态模型不制造兼容警告");
    auto changed = *finiteStatic.value; changed.bones[0].translation.emplace();
    const auto changedCompiled = Compile(changed);
    Check(changedCompiled.Succeeded() && !changedCompiled.value->UsesNoControllerStatic(), "有限模型修改后应按普通路径重新校验");
}
void BudgetedStaticRead() {
    constexpr std::size_t hard = 128u * 1024u * 1024u;
    const auto fixture = Make({.parent = true});
    const auto parsed = ParseMdxNoControllerStaticBudgeted(fixture.bytes, hard);
    Check(parsed.Succeeded() && parsed.value->model.noControllerStaticSource, "预算读取须封存真实静态来源");
    Check(Compile(parsed.value->model).Succeeded(), "预算读取应保持原静态编译资格");
    const auto charged = parsed.value->chargedPeakBytes;
    Check(charged > sizeof(MdxModel) && charged < hard, "须返回实际条目及临时表有效存储计量");
    const auto exact = ParseMdxNoControllerStaticBudgeted(fixture.bytes, charged);
    Check(exact.Succeeded() && exact.value->chargedPeakBytes == charged, "精确预算应成功");
    const auto shortRead = ParseMdxNoControllerStaticBudgeted(fixture.bytes, charged - 1);
    Check(!shortRead.Succeeded() && !shortRead.value && HasCode(shortRead.diagnostics, "MDX_STATIC_PARSE_BUDGET"),
        "少一字节须在分配模型前整体拒绝");
    for (const auto invalid : {std::size_t{0}, hard + 1})
        Check(!ParseMdxNoControllerStaticBudgeted(fixture.bytes, invalid).Succeeded(), "预算硬上限不能放宽");
    MdxLimits limits; limits.maxVerticesPerGeoset = 2;
    Check(!ParseMdxNoControllerStaticBudgeted(fixture.bytes, hard, limits).Succeeded(), "原条目硬上限仍生效");
    const auto dynamic = Make({.nonFinite = false, .flags = 0, .nodeTrack = "KGTR"});
    const auto rejected = ParseMdxNoControllerStaticBudgeted(dynamic.bytes, hard);
    Check(!rejected.Succeeded() && HasCode(rejected.diagnostics, "MDX_STATIC_PARSE_UNQUALIFIED"), "动态拒绝不能伪装预算耗尽");
    Check(ParseMdx(dynamic.bytes).Succeeded(), "普通ParseMdx动画行为不得改变");
    auto cycle = Make({.nonFinite = false, .flags = 0}); Patch(cycle, cycle.flagsOffset - 4, 0);
    const auto cyclic = ParseMdxNoControllerStaticBudgeted(cycle.bytes, hard);
    Check(!cyclic.Succeeded() && HasCode(cyclic.diagnostics, "MDX_REFERENCE_INVALID"), "连续拓扑表仍拒父链环");
    Writer extra; Geoa(extra, false);
    auto duplicate = Make(); duplicate.bytes.insert(duplicate.bytes.end(), extra.bytes.begin(), extra.bytes.end());
    Check(ParseMdxNoControllerStaticBudgeted(duplicate.bytes, hard).Succeeded(), "重复静态GEOA须保留已有最后值语义");
    Writer texture; texture.Tag("TEXS"); texture.U32(268); texture.U32(0); texture.Fixed("second.blp", 260); texture.U32(0);
    auto repeated = Make(); repeated.bytes.insert(repeated.bytes.end(), texture.bytes.begin(), texture.bytes.end());
    limits = {}; limits.maxTextures = 1;
    const auto cumulative = ParseMdxNoControllerStaticBudgeted(repeated.bytes, hard, limits);
    Check(!cumulative.Succeeded() && HasCode(cumulative.diagnostics, "MDX_LIMIT"), "重复块必须按累计数量收紧");
    Writer material; material.Tag("MTLS"); const auto chunk = material.Begin(); const auto item = material.Begin();
    material.U32(0); material.U32(0); material.Tag("LAYS"); material.U32(1); const auto layer = material.Begin();
    material.U32(0); material.U32(0); material.U32(0); material.U32(UINT32_MAX); material.U32(0); material.F32(1);
    material.EndInclusive(layer); material.EndInclusive(item); material.EndChunk(chunk);
    auto expanded = Make({.parent = true}); expanded.bytes.insert(expanded.bytes.end(), material.bytes.begin(), material.bytes.end());
    const auto expandedRead = ParseMdxNoControllerStaticBudgeted(expanded.bytes, hard);
    Check(expandedRead.Succeeded() && expandedRead.value->chargedPeakBytes >= charged + sizeof(MdxMaterial) + sizeof(MdxLayer),
        "新增材质与层必须增加对应条目有效存储");

    // 有效的大模型字节先在探针外备好。小预算/末尾未知控制器必须在展开
    // 顶点、法线等大数组之前拒绝；探针单次最大分配只允许有界错误诊断。
    auto large = Make({.nonFinite = false, .flags = 0, .extraVertices = 60000});
    RenderCoreAllocProbe::Begin();
    const auto tight = ParseMdxNoControllerStaticBudgeted(large.bytes, 4096);
    static_cast<void>(RenderCoreAllocProbe::End());
    Check(!tight.Succeeded() && HasCode(tight.diagnostics, "MDX_STATIC_PARSE_BUDGET")
        && RenderCoreAllocProbe::LargestAllocationBytes() < 4096, "预算失败前不得展开大模型");
    Writer tail; tail.Tag("TXAN"); tail.U32(0);
    large.bytes.insert(large.bytes.end(), tail.bytes.begin(), tail.bytes.end());
    RenderCoreAllocProbe::Begin();
    const auto lateDynamic = ParseMdxNoControllerStaticBudgeted(large.bytes, hard);
    static_cast<void>(RenderCoreAllocProbe::End());
    Check(!lateDynamic.Succeeded() && HasCode(lateDynamic.diagnostics, "MDX_STATIC_PARSE_UNQUALIFIED")
        && RenderCoreAllocProbe::LargestAllocationBytes() < 4096, "末尾动态块也须在分配大数组前拒绝");
}
} // namespace

int main() {
    try {
        RawBitsAndMatrices(); SourceRejections(); ChangedModelRejections(); OrdinaryAnimationStillWorks(); BudgetedStaticRead();
        std::cout << "no-controller-static: source qualification, raw bits, reference/batch matrices and rejections passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
