// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3RenderCore/PoseBatch.h"
#include "War3MapCore/Render/MdxPose.h"
#include "MdxTestModel.h"
#include "RigidStaticPreviewFixture.h"
#include "RenderCoreAllocProbe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace War3RenderCore;
using namespace War3Map::Render;
std::uint64_t checkedRequests = 0, checkedNodes = 0, checkedVertices = 0;
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename T> bool Bits(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
AssetSource Source() { return {"Tests/Crate.mdx", "synthetic", "fixture-v1"}; }
MdxModel Model() {
    MdxTest::Options options; options.animatedHelper = true;
    auto parsed = ParseMdx(MdxTest::BuildTestMdx(options));
    Require(parsed.Succeeded(), "合成 MDX 解析失败");
    auto model = std::move(*parsed.value);
    model.pivots = {{1, -2, 0}, {2, 3, 4}};
    model.helpers[0].translation->keys.push_back({2000, {0, 20, 0}, {}, {}});
    model.helpers[0].translation->keys.push_back({3000, {0, 30, 0}, {}, {}});
    for (auto& g : model.geosets) { g.matrixGroupSizes = {2, 1}; g.matrixIndices = {1, 0, 0}; g.vertexGroups = {0, 1, 0, 1}; }
    return model;
}
CompiledModel Compile(const MdxModel& model, AssetIdentity id = {11, 1}) {
    auto compiled = CompileMdxModel(model, id, Source());
    if (!compiled.Succeeded()) throw std::runtime_error(compiled.diagnostics.front().code);
    return std::move(*compiled.value);
}
PoseBatchEvaluator Prepare(const CompiledModel& model) {
    const std::array assets{model};
    auto evaluator = PoseBatchEvaluator::Create(assets, 101, {8, 128, 128, 32, 65536});
    Require(evaluator.Succeeded(), "求值器预热失败");
    return std::move(*evaluator.value);
}
PoseRequest Request(std::uint64_t id = 1) {
    PoseRequest request; request.instance = {id, 7}; request.asset = {11, 1};
    request.sequenceIndex = 0; request.sequenceElapsedMs = 250; // 夹具按时间顺序：Stand=0、Death=1
    return request;
}
MdxPoseRequest ReferenceRequest(const PoseRequest& request, std::uint32_t node) {
    MdxPoseRequest result;
    result.sequenceIndex = request.sequenceIndex; result.sequenceElapsedMs = request.sequenceElapsedMs;
    result.globalElapsedMs = request.globalElapsedMs; result.objectId = node;
    if (request.hasCamera) result.camera = MdxPoseCamera{request.camera.eye, request.camera.up};
    return result;
}
void AgainstReference(const MdxModel& model, PoseRequest request = Request(), Code expected = Code::Ok) {
    const auto asset = Compile(model); auto evaluator = Prepare(asset);
    const std::array requests{request}; const auto outcome = evaluator.Evaluate(requests);
    Require(outcome.error == Code::Ok, "批次错误");
    const auto& receipt = evaluator.Receipts()[0];
    Require(receipt.error == expected, CodeName(receipt.error));
    Require(receipt.quality == PoseQuality::PreviewApproximate, "质量标记错误");
    using Warning = std::pair<std::string, std::string>;
    std::set<Warning> oldWarnings, newWarnings;
    std::string oldError;
    for (std::uint32_t i = 0; i < asset.NodeCount(); ++i) {
        const auto old = SampleMdxNodePose(model, ReferenceRequest(request, asset.Node(i).objectId));
        if (!old.Succeeded()) { oldError = old.diagnostics.front().code; break; }
        for (const auto& d : old.diagnostics) oldWarnings.emplace(d.code, d.message);
        if (expected != Code::Ok) continue;
        const auto& a = evaluator.Nodes(0)[i]; const auto& b = old.value->chain.back();
        Require(a.objectId == b.objectId && a.parentId == b.parentId && Bits(a.pivot, b.pivot)
            && Bits(a.translation, b.translation) && Bits(a.rotation, b.rotation) && Bits(a.scaling, b.scaling)
            && Bits(a.localMatrix, b.localMatrix) && Bits(a.worldMatrix, b.worldMatrix), "节点逐位差分失败");
        Require(receipt.sequenceFrame == old.value->sequenceFrame, "动作帧不一致"); ++checkedNodes;
    }
    for (std::uint32_t g = 0; oldError.empty() && g < asset.GeosetCount(); ++g) {
        const auto alpha = SampleMdxGeosetAlpha(model, {g, request.sequenceIndex, request.sequenceElapsedMs, request.globalElapsedMs});
        if (!alpha.Succeeded()) { oldError = alpha.diagnostics.front().code; break; }
        if (expected != Code::Ok) continue;
        Require(Bits(evaluator.Alphas(0)[g], *alpha.value), "透明度逐位差分失败");
        MdxRigidSkinRequest skinRequest{g, request.sequenceIndex, request.sequenceElapsedMs, request.globalElapsedMs, std::nullopt};
        skinRequest.camera = ReferenceRequest(request, 0).camera;
        const auto old = SampleMdxRigidSkinnedGeoset(model, skinRequest);
        Require(old.Succeeded(), "参考蒙皮失败");
        std::vector<Vec3> positions(model.geosets[g].positions.size()), normals(positions.size());
        Require(SkinPreviewGeoset(asset, g, evaluator.Groups(0), positions, normals) == Code::Ok, "新蒙皮失败");
        for (std::size_t v = 0; v < positions.size(); ++v) {
            Require(Bits(positions[v], old.value->positions[v]) && Bits(normals[v], old.value->normals[v]), "蒙皮逐位差分失败");
            ++checkedVertices;
        }
    }
    if (expected == Code::Ok) {
        Require(oldError.empty() && receipt.poseVersion != 0 && receipt.evaluationEpoch == 101, "成功回执错误");
        for (const auto& d : evaluator.Diagnostics(0)) newWarnings.emplace(CodeName(d.code), DescribeDiagnostic(d));
        Require(oldWarnings == newWarnings, "预览警告与参考不一致");
    } else {
        Require(oldError == CodeName(expected), "拒绝码与参考不一致");
        Require(receipt.poseVersion == 0 && evaluator.Nodes(0).empty() && evaluator.Groups(0).empty()
            && evaluator.Alphas(0).empty() && evaluator.Diagnostics(0).size() == 1, "失败泄漏部分结果");
    }
    ++checkedRequests;
}
void CompileError(const MdxModel& model, Code expected) {
    const auto result = CompileMdxModel(model, {11, 1}, Source());
    Require(!result.Succeeded() && result.diagnostics.front().code == CodeName(expected), "加载拒绝码错误");
}
void Oracle() {
    auto model = Model();
    for (const auto time : std::array<std::uint64_t, 8>{0, 1, 249, 500, 999, 1000, 1001, UINT64_MAX}) {
        auto r = Request(); r.sequenceElapsedMs = time; AgainstReference(model, r);
    }
    // 独立期望值覆盖跳变前、同帧最后键、跳变后和区间末端的重复键。
    for (const bool global : {false, true}) {
        auto m = model; m.sequences[0].end = 4; m.sequences[0].flags = 1; m.globalSequences = {8};
        auto& node = m.helpers[0];
        node.translation = MdxNodeTrack<3>{MdxInterpolation::Linear, global ? 0u : UINT32_MAX,
            {{0, {0, 0, 0}, {}, {}}, {2, {10, 0, 0}, {}, {}}, {2, {20, 0, 0}, {}, {}},
             {4, {30, 0, 0}, {}, {}}, {4, {40, 0, 0}, {}, {}}}};
        node.scaling = MdxNodeTrack<3>{MdxInterpolation::Linear, global ? 0u : UINT32_MAX,
            {{0, {1, 1, 1}, {}, {}}, {2, {3, 1, 1}, {}, {}}, {2, {5, 1, 1}, {}, {}},
             {4, {7, 1, 1}, {}, {}}, {4, {9, 1, 1}, {}, {}}}};
        node.rotation = MdxNodeTrack<4>{MdxInterpolation::Linear, global ? 0u : UINT32_MAX,
            {{0, {0, 0, 0, 1}, {}, {}}, {2, {0, 0, 0, 1}, {}, {}}, {2, {0, 0, 1, 0}, {}, {}},
             {4, {0, 0, 1, 0}, {}, {}}, {4, {0, 0, 0, 1}, {}, {}}}};
        const std::array<float, 4> x{5, 20, 25, 40}, scale{2, 5, 6, 9};
        for (std::uint64_t time = 1; time <= 4; ++time) {
            auto r = Request(); r.sequenceElapsedMs = time; r.globalElapsedMs = time;
            AgainstReference(m, r);
            const auto pose = *SampleMdxNodePose(m, ReferenceRequest(r, 1)).value;
            const auto rotation = time == 1 || time == 4 ? Quaternion{0, 0, 0, 1} : Quaternion{0, 0, 1, 0};
            const auto index = static_cast<std::size_t>(time - 1);
            Require(pose.chain.back().translation == Vec3{x[index], 0, 0}
                && pose.chain.back().scaling == Vec3{scale[index], 1, 1}
                && pose.chain.back().rotation == rotation, "重复 TRS 键没有按源序取值");
        }
    }
    for (std::uint32_t mode = 0; mode < 4; ++mode) {
        auto m = model; m.globalSequences = {1000};
        m.helpers[0].translation->interpolation = static_cast<MdxInterpolation>(mode);
        m.helpers[0].translation->globalSequence = 0;
        m.bones[0].rotation = MdxNodeTrack<4>{static_cast<MdxInterpolation>(mode), UINT32_MAX,
            {{0, {0, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0.3f, 0.7f}},
             {1000, {0, 0, 1, 0}, {0, 0, 0.7f, 0.3f}, {0, 0, 1, 0}}}};
        m.bones[0].scaling = MdxNodeTrack<3>{static_cast<MdxInterpolation>(mode), UINT32_MAX,
            {{0, {1, 2, 3}, {}, {1, 2, 1}}, {1000, {2, 3, 4}, {2, 1, 2}, {}}}};
        for (const auto time : {0ull, 123ull, 501ull, 999ull, 1001ull}) {
            auto r = Request(); r.sequenceElapsedMs = time; r.globalElapsedMs = time + 71; AgainstReference(m, r);
        }
        if (mode >= 2) { m.bones[0].rotation->keys[0].outTangent = {}; AgainstReference(m); }
    }
    for (const auto flags : {0x8u, 0x18u, 0x28u, 0x48u, 0x80u, 0x88u}) {
        auto m = model; m.helpers[0].flags = flags; AgainstReference(m);
        auto r = Request(); r.hasCamera = 1; r.camera.eye = {50, -30, 70}; AgainstReference(m, r);
    }
    auto r = Request(); r.hasCamera = 1; r.camera.eye = {4.5f, 3, 20};
    auto m = model; m.helpers[0].flags = 0x48; AgainstReference(m, r); // 轴奇点仍有显式预览警告
    m = model; m.sequences[0].flags = 1; r = Request(); r.sequenceElapsedMs = 2000; AgainstReference(m, r);
    m.sequences[0].start = 250; m.sequences[0].end = 250; AgainstReference(m, r);
    m = model; m.helpers[0].translation->keys.clear(); AgainstReference(m);
    m.helpers[0].translation->keys = {{500, {3, 2, 1}, {}, {}}}; AgainstReference(m);
    m = model; m.geosets[0].animation = MdxGeosetAnimation{}; m.geosets[0].animation->staticAlpha = -0.25f; AgainstReference(m);
    // 原版接缝不是钳值：首键前的分子仍以首键为基准，分母取动作长度。
    for (const bool global : {false, true}) {
        auto seam = model; seam.globalSequences = {2000};
        seam.helpers[0].translation = MdxNodeTrack<3>{MdxInterpolation::Linear, global ? 0u : UINT32_MAX,
            {{100, {10, 0, 0}, {}, {}}, {700, {70, 0, 0}, {}, {}}}};
        for (const auto time : {50ull, 800ull}) {
            auto request = Request(); request.sequenceElapsedMs = time; request.globalElapsedMs = time;
            AgainstReference(seam, request);
            const auto x = SampleMdxNodePose(seam, ReferenceRequest(request, 1)).value->chain.back().translation[0];
            Require(x == (global ? (time == 50 ? 15.0f : 40.0f) : (time == 50 ? 77.5f : 55.0f)), "节点接缝取键或周期来源错误");
        }
    }
    // 逐个破坏语义，检查动态拒绝码；不同时塞多个坏字段混淆优先级。
    m = model; m.helpers[0].translation->keys[0].frame = 100; AgainstReference(m, Request(), Code::Ok);
    r = Request(); r.sequenceElapsedMs = 50; AgainstReference(m, r);
    m = model; m.helpers[0].translation->keys[1].frame = 700;
    r.sequenceElapsedMs = 800; AgainstReference(m, r);
    m = model; m.helpers[0].translation->keys[1].frame = -1; AgainstReference(m, Request(), Code::PoseKeysInvalid);
    // 原序不变：坏区间只阻止相交动作，无关动作和没有键的动作仍可采样。
    m = model; m.helpers[0].translation->keys = {{0, {}, {}, {}}, {100, {10,0,0}, {}, {}},
        {200, {20,0,0}, {}, {}}, {150, {15,0,0}, {}, {}}, {300, {30,0,0}, {}, {}},
        {2000, {20,0,0}, {}, {}}, {3000, {30,0,0}, {}, {}}};
    r = Request(); r.sequenceIndex = 1; AgainstReference(m, r);
    r.sequenceIndex = 0; AgainstReference(m, r, Code::PoseKeysInvalid);
    m.sequences[0].end = 100; r.sequenceElapsedMs = 50; AgainstReference(m, r);
    Require(SampleMdxNodePose(m, ReferenceRequest(r, 1)).value->chain.back().translation[0] == 5, "无关动作被逆序轨迹连带拒绝");
    m.sequences[0].start = 500; m.sequences[0].end = 1000; AgainstReference(m, r);
    m.globalSequences = {4000}; m.helpers[0].translation->globalSequence = 0; AgainstReference(m, r, Code::PoseKeysInvalid);
    m = model; m.helpers[0].translation->globalSequence = 0; AgainstReference(m, Request(), Code::PoseGlobalSequenceInvalid);
    m.globalSequences = {0}; m.helpers[0].translation->keys[0].value = {7, 8, 9};
    r = Request(); r.globalElapsedMs = UINT64_MAX; AgainstReference(m, r);
    Require(SampleMdxNodePose(m, ReferenceRequest(r, 1)).value->chain.back().translation == Vec3{7, 8, 9}, "零全局周期没有停在帧零");
    m.helpers[0].translation->keys = {{333, {7, 8, 9}, {}, {}}};
    m.helpers[0].scaling = MdxNodeTrack<3>{MdxInterpolation::None, 0, {{150000, {0.25f, 0.5f, 0.75f}, {}, {}}}};
    m.bones[0].rotation = MdxNodeTrack<4>{MdxInterpolation::None, 0, {{-333, {0, 0, 1, 0}, {}, {}}}};
    for (const auto period : {0u, 100u}) { m.globalSequences[0] = period; AgainstReference(m, r);
        const auto pose = *SampleMdxNodePose(m, ReferenceRequest(r, 0)).value;
        Require(pose.chain.front().translation == Vec3{7, 8, 9} && pose.chain.front().scaling == Vec3{0.25f, 0.5f, 0.75f}
            && pose.chain.back().rotation == Quaternion{0, 0, 1, 0}, "全局单键必须返回常量TRS，不能取默认值"); }
    m.globalSequences[0] = 0; m.helpers[0].translation->keys = {{-10, {}, {}, {}}, {10, {1, 2, 3}, {}, {}}};
    AgainstReference(m, r);
    m.helpers[0].translation->keys.insert(m.helpers[0].translation->keys.begin()+1, {0, {7, 8, 9}, {}, {}}); AgainstReference(m, r);
    Require(SampleMdxNodePose(m, ReferenceRequest(r, 1)).value->chain.back().translation == Vec3{}, "零周期须取归零前首键，不能取源0帧键");
    m.globalSequences[0] = 7; r.globalElapsedMs = 5; AgainstReference(m, r);
    Require(SampleMdxNodePose(m, ReferenceRequest(r, 1)).value->chain.back().translation == Vec3{3.5f, 4, 4.5f}, "周期外的右键没有参与归零后插值");
    m.bones[0].rotation->interpolation = MdxInterpolation::Hermite;
    m.bones[0].rotation->keys = {{333, {0,0,0,1}, {}, {}}, {1333, {0,0,1,0}, {}, {}}}; AgainstReference(m, r);
    m.helpers[0].translation->keys = {{INT32_MIN, {}, {}, {}}, {0, {}, {}, {}}}; AgainstReference(m, r, Code::PoseGlobalSpanUnsupported);
    m = model; m.helpers[0].translation->interpolation = static_cast<MdxInterpolation>(99);
    AgainstReference(m, Request(), Code::PoseInterpolationUnsupported);
    m = model; m.helpers[0].translation->keys[0].value[0] = std::numeric_limits<float>::infinity();
    AgainstReference(m, Request(), Code::PoseNumberInvalid);
    r = Request(); r.sequenceElapsedMs = 1000; r.sequenceIndex = 1; AgainstReference(m, r); // 坏值不在该动作内
    m = model; m.bones[0].rotation = MdxNodeTrack<4>{MdxInterpolation::Linear, UINT32_MAX, {{0, {}, {}, {}}}};
    AgainstReference(m, Request(), Code::PoseRotationInvalid);
    m = model; m.sequences[0].flags = 7; AgainstReference(m, Request(), Code::PoseSequenceUnsupported);
    r = Request(); r.sequenceIndex = 99; AgainstReference(model, r, Code::PoseSequenceInvalid);
    r = Request(); r.hasCamera = 1; r.camera.up = {}; AgainstReference(model, r, Code::PoseCameraInvalid);
    m = model; m.bones[0].flags = 0x38; r.camera.up = {0, 0, 1}; AgainstReference(m, r, Code::PoseCameraFlagsUnsupported);
    m = model; auto& a = *m.geosets[1].animation;
    a.alphaKeys = {{100, 0.2f}, {900, 0.8f}}; AgainstReference(m);
    r = Request(); r.sequenceElapsedMs = 0; AgainstReference(m, r); // 首键前按原版接缝回绕，不再拒绝
    a.interpolation = 99; AgainstReference(m, Request(), Code::GeoaInterpolationUnsupported);
    a.interpolation = 0; a.globalSequence = 0; AgainstReference(m, Request(), Code::GeoaGlobalSequenceUnsupported);
    // 非全局同帧键按原版顺序查找（取同帧最后一个）；区间内逆序键在严格模式拒绝。
    a.globalSequence = UINT32_MAX; a.alphaKeys[1].frame = 100; AgainstReference(m, Request());
    a.alphaKeys[1].frame = 50;
    {   // 参考按原版顺序取值；严格核心拒绝该区间（原序预览的逐位差分见 GeosetChannelTests）。
        const auto asset = Compile(m); auto evaluator = Prepare(asset); const std::array requests{Request()};
        Require(evaluator.Evaluate(requests).error == Code::Ok && evaluator.Receipts()[0].error == Code::GeoaKeysInvalid
            && SampleMdxGeosetAlpha(m, {1, 0, 250, 0}).Succeeded(), "严格模式未拒绝逆序 GEOA 或参考未按原版取值");
    }
    a.alphaKeys[1].frame = 900; a.alphaKeys[1].value = std::numeric_limits<float>::quiet_NaN();
    AgainstReference(m, Request(), Code::GeoaNumberInvalid);
    a.alphaKeys.clear(); a.hasUninterpretedData = true; AgainstReference(m, Request(), Code::GeoaUninterpreted);
    m = model; m.bones[0].objectId = 1; CompileError(m, Code::PoseNodeInvalid);
    m = model; m.helpers[0].parentId = 0; CompileError(m, Code::PoseParentInvalid);
    m = model; m.helpers[0].parentId = 17; CompileError(m, Code::PoseParentUnavailable);
    m = model; m.pivots.clear(); CompileError(m, Code::PosePivotUnavailable);
    m = model; m.bones[0].flags = 0x20000000; CompileError(m, Code::PoseNodeFlagsUnsupported);
    m = model; m.geosets[0].matrixGroupSizes[0] = 99; CompileError(m, Code::SkinGeometryUnsupported);
    m = model; m.geosets[0].matrixIndices.push_back(0); CompileError(m, Code::SkinGeometryUnsupported);
    m = model; m.geosets[0].vertexGroups[0] = 99; CompileError(m, Code::SkinGeometryUnsupported);
    m = model; m.skinningTopologyComplete = false; CompileError(m, Code::SkinTopologyIncomplete);
    // 很小但非零的法线可能被放大到合法范围，不能在加载时误用变换后阈值。
    m = model; for (auto& g : m.geosets) for (auto& n : g.normals) n = {0, 0, 1e-25f};
    m.helpers[0].scaling = MdxNodeTrack<3>{MdxInterpolation::None, UINT32_MAX, {{0, {1, 1, 1e10f}, {}, {}}}};
    AgainstReference(m);
    m = model; m.helpers[0].scaling = MdxNodeTrack<3>{MdxInterpolation::None, UINT32_MAX, {{0, {}, {}, {}}}};
    const auto collapsed = Compile(m); auto collapseEvaluator = Prepare(collapsed);
    const std::array collapseRequests{Request()};
    Require(collapseEvaluator.Evaluate(collapseRequests).failedInstances == 0, "零缩放节点矩阵应可求值");
    std::vector<Vec3> cp(4), cn(4);
    const auto oldCollapsed = SampleMdxRigidSkinnedGeoset(m, {0, 0, 250, 0, std::nullopt});
    Require(!oldCollapsed.Succeeded() && oldCollapsed.diagnostics.front().code == CodeName(Code::SkinNormalInvalid)
        && SkinPreviewGeoset(collapsed, 0, collapseEvaluator.Groups(0), cp, cn) == Code::SkinNormalInvalid,
        "变换后退化法线拒绝与参考不一致");
    // 固定输入扫描，拓展浮点符号、非单位四元数与变换组合；仍由参考实现逐位判定。
    std::uint32_t random = 0x7f4a7c15u;
    for (std::uint32_t sample = 0; sample < 128; ++sample) {
        const auto number = [&]() { random = random * 1664525u + 1013904223u;
            return static_cast<float>(static_cast<std::int32_t>(random % 2001) - 1000) / 127.0f; };
        m = model; m.pivots[0] = {number(), number(), number()};
        auto& keys = m.helpers[0].translation->keys;
        keys[0].value = {number(), number(), number()}; keys[1].value = {number(), number(), number()};
        m.bones[0].rotation = MdxNodeTrack<4>{MdxInterpolation::Linear, UINT32_MAX,
            {{0, {number(), number(), number(), number()}, {}, {}}, {1000, {number(), number(), number(), number()}, {}, {}}}};
        r = Request(); r.sequenceElapsedMs = random % 1000; AgainstReference(m, r);
    }
    std::cout << "oracle requests=" << checkedRequests << " nodes=" << checkedNodes << " vertices=" << checkedVertices
              << " bitwise_mismatches=0 diagnostic_mismatches=0\n";
}
void BillboardYZQuad() {
    auto model = Model();
    model.helpers[0].translation = MdxNodeTrack<3>{MdxInterpolation::None, UINT32_MAX,
        {{0, {3, 4, 5}, {}, {}}}};
    model.bones[0].flags = 0x108;
    auto& quad = model.geosets[0];
    quad.positions = {{1, -3, -1}, {1, -1, -1}, {1, -3, 1}, {1, -1, 1}};
    quad.normals.assign(4, {1, 0, 0}); quad.vertexGroups.assign(4, 0);
    quad.matrixGroupSizes = {1}; quad.matrixIndices = {0};
    const auto original = quad.positions;
    const Vec3 anchor{4, 2, 5}; // 父平移 (3,4,5) 加本地 pivot (1,-2,0)。
    const std::array cameras{MdxPoseCamera{{24, -8, 17}, {0, 0, 1}},
        MdxPoseCamera{{4.000001f, 2, 25}, {0, 0, 1}}, MdxPoseCamera{{4, 2, 25}, {0, 0, 1}}};
    for (const auto& camera : cameras) {
        auto request = Request(); request.hasCamera = 1; request.camera = {camera.eye, camera.up};
        AgainstReference(model, request);
        const auto pose = SampleMdxNodePose(model, ReferenceRequest(request, 0));
        const auto skin = SampleMdxRigidSkinnedGeoset(model, {0, 0, 250, 0, camera});
        Require(pose.Succeeded() && skin.Succeeded(), "YZ billboard 的轴心/几何采样失败");
        const auto& matrix = pose.value->chain.back().worldMatrix;
        const double det = matrix[0] * (matrix[5] * matrix[10] - matrix[9] * matrix[6])
            - matrix[4] * (matrix[1] * matrix[10] - matrix[9] * matrix[2])
            + matrix[8] * (matrix[1] * matrix[6] - matrix[5] * matrix[2]);
        Require(det > 0.999, "无锁 billboard 交换基列后翻转 handedness");
        const Vec3 direction{camera.eye[0] - anchor[0], camera.eye[1] - anchor[1], camera.eye[2] - anchor[2]};
        const auto length = std::hypot(static_cast<double>(direction[0]), direction[1], direction[2]);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto pivot = matrix[12 + axis] + matrix[axis] * model.pivots[0][0]
                + matrix[4 + axis] * model.pivots[0][1] + matrix[8 + axis] * model.pivots[0][2];
            Require(std::abs(pivot - anchor[axis]) < 1e-5f, "billboard 改朝向时轴心脱离父链");
        }
        for (std::size_t v = 0; v < quad.positions.size(); ++v) {
            double plane = 0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                Require(std::abs(skin.value->normals[v][axis] - direction[axis] / length) < 1e-6,
                    "纯 YZ billboard 的本地 X 法线未朝向相机");
                plane += (skin.value->positions[v][axis] - anchor[axis]) * direction[axis] / length;
            }
            Require(std::abs(plane) < 1e-5, "YZ quad 未处于面向相机的平面");
        }
    }
    Require(quad.positions == original, "billboard 求值改写静态几何");
    std::cout << "billboard_yz_quads=3 handedness_positive=1 parent_pivot_retained=1 bitwise_mismatches=0 preview_approximate=1\n";
}
void Instances() {
    RenderCoreAllocProbe::Begin();
    void* scalarProbe = ::operator new(1); void* alignedProbe = ::operator new(1, std::align_val_t{64});
    const auto probeCount = RenderCoreAllocProbe::End();
    ::operator delete(scalarProbe); ::operator delete(alignedProbe, std::align_val_t{64});
    Require(probeCount == 2, "分配探针没有实际截获普通/对齐分配");
    auto original = Model(); const auto asset = Compile(original); auto evaluator = Prepare(asset);
    Require(asset.Node(0).objectId == 1 && asset.Node(1).parentIndex == 0, "父前子后拓扑未编译");
    const auto groupsBefore = asset.VertexGroups().data();
    std::array requests{Request(1), Request(2)}; requests[1].sequenceIndex = 1; requests[1].sequenceElapsedMs = 800;
    Require(evaluator.Evaluate(requests).failedInstances == 0, "双实例求值失败");
    const std::vector first(evaluator.Groups(0).begin(), evaluator.Groups(0).end());
    const std::vector second(evaluator.Groups(1).begin(), evaluator.Groups(1).end());
    Require(first != second, "不同动作实例发生串姿势");
    const auto version = evaluator.Receipts()[1].poseVersion;
    original.helpers[0].translation->keys.clear(); original.geosets.clear(); // 编译资产拥有数据，不借原模型
    Require(evaluator.Evaluate(requests).failedInstances == 0, "重复求值失败");
    for (std::size_t i = 0; i < first.size(); ++i)
        Require(Bits(first[i], evaluator.Groups(0)[i]) && Bits(second[i], evaluator.Groups(1)[i]), "同输入不确定");
    Require(evaluator.Receipts()[0].poseVersion > version && asset.VertexGroups().data() == groupsBefore, "版本/静态资产错误");
    auto other = Prepare(asset); std::swap(requests[0], requests[1]);
    Require(other.Evaluate(requests).failedInstances == 0, "第二求值器失败");
    Require(std::equal(first.begin(), first.end(), other.Groups(1).begin()), "请求排序污染实例");
    std::swap(requests[0], requests[1]); requests[0].sequenceElapsedMs = 500;
    Require(evaluator.Evaluate(requests).failedInstances == 0, "单步失败");
    Require(!std::equal(first.begin(), first.end(), evaluator.Groups(0).begin())
        && std::equal(second.begin(), second.end(), evaluator.Groups(1).begin()), "单步污染另一实例");
    requests[0].asset.generation = 999;
    Require(evaluator.Evaluate(requests).failedInstances == 1 && evaluator.Groups(0).empty()
        && !evaluator.Groups(1).empty(), "资产换代没有隔离失败");
    requests[0] = Request(1); requests[1] = Request(1);
    Require(evaluator.Evaluate(requests).error == Code::DuplicateInstance && evaluator.Receipts().empty(), "重复实例未拒绝");
    requests[1] = Request(2);
    RenderCoreAllocProbe::Begin(); bool valid = true;
    for (std::uint32_t frame = 0; frame < 500; ++frame) {
        requests[0].sequenceElapsedMs = frame; valid = valid && evaluator.Evaluate(requests).failedInstances == 0;
    }
    const auto allocations = RenderCoreAllocProbe::End(); Require(valid && allocations == 0, "热路径分配或失败");
    auto warningModel = Model(); warningModel.helpers[0].flags = 0x48;
    warningModel.helpers[0].translation->keys.clear();
    warningModel.bones[0].rotation = MdxNodeTrack<4>{MdxInterpolation::Hermite, UINT32_MAX,
        {{0, {0, 0, 0, 1}, {}, {}}, {1000, {0, 0, 1, 0}, {}, {}}}};
    auto warnings = Prepare(Compile(warningModel));
    requests[0].hasCamera = 1; requests[0].camera.eye = {2, 3, 20}; requests[0].sequenceElapsedMs = 500;
    RenderCoreAllocProbe::Begin(); const auto warned = warnings.Evaluate(requests);
    const auto warningAllocations = RenderCoreAllocProbe::End();
    Require(warned.failedInstances == 0 && warnings.Diagnostics(0).size() == 4 && warningAllocations == 0,
        "样条回退/相机奇点警告路径发生分配或丢诊断");
    const std::array assets{asset}; auto limited = PoseBatchEvaluator::Create(assets, 202, {1, 2, 4, 2, 8192});
    Require(limited.Succeeded() && limited.value->Evaluate(requests).error == Code::CapacityExceeded, "预热容量未拒绝");
    const std::array conflict{asset, Compile(Model())};
    Require(!PoseBatchEvaluator::Create(conflict, 203).Succeeded(), "同身份的不同资产存储未拒绝");
    Require(!CompileMdxModel(Model(), {0, 1}, Source()).Succeeded(), "零资产编号未拒绝");
    Require(!CompileMdxModel(Model(), {11, 1}, {}).Succeeded(), "缺来源未拒绝");
    Require(!CompileMdxModel(Model(), {11, 1}, Source(), 1).Succeeded(), "加载预算未拒绝");
    std::cout << "instances independent=" << 1 << " deterministic=1 static_storage_stable=1 frames=500 new_calls=" << allocations
              << " warning_new_calls=" << warningAllocations << '\n';
}
void External() {
    const auto asset = Compile(Model()); auto evaluator = Prepare(asset);
    std::array requests{Request(1), Request(2)};
    Require(evaluator.Evaluate(requests).failedInstances == 0, "初始求值失败");
    std::vector external(evaluator.Groups(0).begin(), evaluator.Groups(0).end());
    requests[1].source = PoseSource::ExternalGroups; requests[1].external.matrices.count = asset.GroupCount();
    Require(evaluator.Evaluate(requests, external).failedInstances == 0, "混合来源失败");
    Require(evaluator.Nodes(1).empty() && std::equal(external.begin(), external.end(), evaluator.Groups(1).begin()), "外部矩阵未保持");
    const auto good = requests[1];
    const auto reject = [&](Code code) {
        RenderCoreAllocProbe::Begin(); const auto result = evaluator.Evaluate(requests, external);
        const auto allocations = RenderCoreAllocProbe::End();
        Require(result.error == Code::Ok && result.failedInstances == 1 && evaluator.Receipts()[1].error == code
            && evaluator.Groups(1).empty() && evaluator.Receipts()[1].poseVersion == 0 && allocations == 0, "外部拒绝/零分配错误");
        requests[1] = good;
    };
    requests[1].external.matrices.count = 0; reject(Code::ExternalCountInvalid);
    requests[1].external.matrices.offset = UINT32_MAX; reject(Code::ExternalRangeInvalid);
    requests[1].external.strideBytes = 64; reject(Code::ExternalLayoutInvalid);
    requests[1].external.layout = static_cast<MatrixLayout>(9); reject(Code::ExternalLayoutInvalid);
    requests[1].external.space = CoordinateSpace::World; reject(Code::ExternalLayoutInvalid);
    requests[1].external.worldTransformApplied = 1; reject(Code::ExternalLayoutInvalid);
    requests[1].overrides.count = 1; reject(Code::OverrideUnsupported);
    const auto saved = external[0].values[0]; external[0].values[0] = std::numeric_limits<float>::quiet_NaN();
    reject(Code::PoseNumberInvalid); external[0].values[0] = saved;
    requests[1].assetIndex = 9; reject(Code::AssetIndexInvalid);
    requests[1].source = static_cast<PoseSource>(9); reject(Code::RequestInvalid);
    requests[1].instance.value = 0; reject(Code::InstanceIdentityInvalid);
    const std::array<BoneOverride, 1> overrides{};
    Require(evaluator.Evaluate(requests, external, overrides).error == Code::OverrideUnsupported, "未引用的覆盖池被静默忽略");
    Require(evaluator.Evaluate(requests, external).failedInstances == 0, "失败后无法恢复");
    const auto aliased = evaluator.Groups(0);
    Require(evaluator.Evaluate(requests, aliased).error == Code::RequestInvalid, "输出别名未拒绝");
    Require(evaluator.Evaluate(requests, external).failedInstances == 0, "别名拒绝后无法恢复");
    std::vector<Vec3> p(4), n(4);
    Require(SkinPreviewGeoset(asset, 0, external, p, n) == Code::Ok, "外部预览蒙皮失败");
    Require(SkinPreviewGeoset(asset, 0, {}, p, n) == Code::ExternalCountInvalid, "蒙皮组数拒绝错误");
    Require(SkinPreviewGeoset(asset, 0, external, {}, n) == Code::OutputRangeInvalid, "输出容量拒绝错误");
    std::cout << "external mixed_sources=1 layout_count_space_rejections=1 override_rejected=1 recovery=1\n";
}

void RigidStaticPreview() {
    const auto bytes = MdxTest::BuildRigidStaticPreviewMdx();
    const auto parsed = ParseMdxRigidStaticPreview(bytes); Require(parsed.Succeeded(), "静态预览解析失败");
    Require(!CompileMdxModel(*parsed.value, {11, 1}, Source()).Succeeded(), "普通编译仍拒绝静态来源");
    auto compiled = CompileMdxRigidStaticPreview(*parsed.value, {11, 1}, Source());
    Require(compiled.Succeeded() && compiled.value->IsRigidStaticPreview() && compiled.value->NodeCount() == 0
        && compiled.value->GroupCount() == 2, "静态身份/组号失败");
    auto evaluator = Prepare(*compiled.value); auto first = Request(1); first.sequenceIndex = 0;
    auto second = first; second.instance.value = 2; second.sequenceElapsedMs = 800;
    const std::array requests{first, second}; Require(evaluator.Evaluate(requests).failedInstances == 0, "静态双实例求值失败");
    Require(evaluator.Receipts()[0].sequenceFrame == 260 && evaluator.Receipts()[1].sequenceFrame == 810,
        "静态实例时间不能串用");
    const auto groupData = compiled.value->VertexGroups().data();
    const GroupMatrix identity{{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}};
    for (std::uint32_t instance = 0; instance < requests.size(); ++instance) {
        Require(evaluator.Nodes(instance).empty() && evaluator.Receipts()[instance].quality == PoseQuality::PreviewApproximate,
            "静态预览质量或虚构节点");
        for (const auto& matrix : evaluator.Groups(instance)) Require(Bits(matrix, identity), "局部组不是 identity");
        for (std::uint32_t g = 0; g < 2; ++g) {
            const auto reference = SampleMdxRigidStaticPreviewGeoset(*parsed.value,
                {g, 0, requests[instance].sequenceElapsedMs, 0, std::nullopt});
            Require(reference.Succeeded() && reference.value->sequenceFrame == evaluator.Receipts()[instance].sequenceFrame,
                "静态独立时钟/参考失败");
            std::vector<Vec3> positions(3), normals(3);
            Require(SkinPreviewGeoset(*compiled.value, g, evaluator.Groups(instance), positions, normals) == Code::Ok,
                "静态蒙皮失败");
            for (std::size_t v = 0; v < 3; ++v)
                Require(Bits(positions[v], reference.value->positions[v]) && Bits(normals[v], reference.value->normals[v]),
                    "静态 identity 参考逐位不符");
        }
    }
    const auto stable = evaluator.Groups(0)[0]; RenderCoreAllocProbe::Begin();
    const auto outcome = evaluator.Evaluate(requests); const auto allocations = RenderCoreAllocProbe::End();
    Require(outcome.failedInstances == 0 && allocations == 0 && Bits(stable, evaluator.Groups(0)[0])
        && compiled.value->VertexGroups().data() == groupData, "静态重复求值/资源/分配不变量");
    auto changed = *parsed.value; changed.geosets[0].vertexGroups[0] = 1;
    Require(!CompileMdxRigidStaticPreview(changed, {11, 1}, Source()).Succeeded(), "当前非零槽号未拒绝");
    changed = *parsed.value; changed.helpers.push_back({});
    Require(!CompileMdxRigidStaticPreview(changed, {11, 1}, Source()).Succeeded(), "添加控制器未拒绝");
    changed = *parsed.value; changed.geosets[0].matrixGroupSizes.push_back(1);
    Require(!CompileMdxRigidStaticPreview(changed, {11, 1}, Source()).Succeeded(), "添加非原始矩阵组未拒绝");
    changed = *parsed.value; changed.rigidStaticPreviewSource.reset();
    Require(!CompileMdxRigidStaticPreview(changed, {11, 1}, Source()).Succeeded(), "缺解析来源未拒绝");
    std::cout << "rigid_static_preview strict_rejected=1 identity_groups=2 oracle_bit_mismatch=0 independent_clocks=1 new_calls="
        << allocations << '\n';
}
}
int main(int argc, char** argv) {
    try {
        Require(argc == 2, "用法：war3-render-core-tests oracle|instances|external");
        const std::string group = argv[1];
        if (group == "oracle") { Oracle(); BillboardYZQuad(); RigidStaticPreview(); }
        else if (group == "instances") Instances(); else if (group == "external") External();
        else throw std::runtime_error("未知测试分组");
        std::cout << "PASS " << group << '\n'; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
