// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3MapCore/Render/MdxModel.h"
#include "War3MapCore/Render/MdxPose.h"
#include "MdxTestModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace War3Map::Render;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

using MdxTest::Options;
std::vector<std::byte> Build(const Options& options = {}) { return MdxTest::BuildTestMdx(options); }

void Rejected(const std::vector<std::byte>& bytes, const std::string& code) {
    const auto result = ParseMdx(bytes);
    Require(!result.Succeeded() && !result.value, "无效 MDX 不能发布部分模型");
    if (result.diagnostics.front().code != code)
        throw std::runtime_error("错误类别不符：期望 " + code + "，实际 " + result.diagnostics.front().code);
}

void TestParse() {
    const auto result = ParseMdx(Build());
    if (!result.Succeeded()) throw std::runtime_error("合成模型解析失败：" + result.diagnostics.front().message);
    const auto& model = *result.value;
    Require(model.version == 800 && model.name == "Crate" && model.boundsRadius == 50 && model.maximum[2] == 20, "模型头");
    Require(model.sequences.size() == 2 && model.sequences[1].name == "Stand - 1", "动作");
    Require(model.textures.size() == 2 && model.textures[0].path == "Textures\\Crate.blp" && model.textures[0].flags == 3
        && model.textures[1].replaceableId == 1, "贴图");
    Require(model.materials.size() == 1 && model.materials[0].layers.size() == 2, "材质");
    const auto& layers = model.materials[0].layers;
    Require(layers[0].filterMode == MdxFilterMode::None && layers[0].shadingFlags == 0x10 && layers[0].textureId == 1
        && !layers[0].animatedTexture, "第一层");
    Require(layers[1].filterMode == MdxFilterMode::Transparent && layers[1].alpha == 0.5f && layers[1].animatedTexture, "第二层");
    Require(model.geosets.size() == 2, "geoset 数量");
    const auto& first = model.geosets[0];
    Require(first.positions.size() == 4 && first.positions[3][2] == 5 && first.normals[0][2] == 1
        && first.uvs[1][0] == 1 && first.uvs[1][1] == 1, "顶点/法线/UV");
    Require(first.indices == std::vector<std::uint16_t>{0, 1, 2, 1, 3, 2} && first.skippedPrimitiveGroups == 0, "三角形");
    Require(first.vertexGroups == std::vector<std::uint8_t>(4, 0)
        && first.matrixGroupSizes == std::vector<std::uint32_t>{1}
        && first.matrixIndices == std::vector<std::uint32_t>{0}
        && first.uninterpretedTailBytes == 0, "原生 GNDX/MTGC/MATS 结构");
    Require(first.visibleInStand, "无动画的 geoset 可见");
    const auto& second = model.geosets[1];
    Require(second.indices.size() == 6 && second.skippedPrimitiveGroups == 1, "非三角形图元组只计数不显示");
    Require(!second.visibleInStand, "Stand 起点透明度为 0 的 geoset 隐藏");
    Require(second.animation && second.animation->hasAlphaTrack && second.animation->interpolation == 0
        && second.animation->alphaKeys.size() == 2, "GEOA/KGAO 阶梯轨迹保留供显式帧求值");
    const auto standAlpha = SampleMdxGeosetAlpha(model, {1, 1, 500, 0});
    const auto deathAlpha = SampleMdxGeosetAlpha(model, {1, 0, 0, 0});
    Require(standAlpha.Succeeded() && *standAlpha.value == 0.0f
        && deathAlpha.Succeeded() && *deathAlpha.value == 1.0f, "GEOA 按动作帧决定可见度");
    Require(model.bones.size() == 1 && model.pivots.size() == 1 && model.helpers.empty()
        && model.skinningTopologyComplete && model.skinningTopologyIssue.empty(), "静态模型的节点与轴心可解析");
    Require(model.skippedChunks == 1, "未解释块按大小跳过");
}

void TestRejections() {
    auto bytes = Build(); bytes[0] = std::byte{'X'};
    Rejected(bytes, "MDX_MAGIC");
    Options options; options.version = 900;
    Rejected(Build(options), "MDX_VERSION_UNSUPPORTED");
    options = {}; options.truncate = true;
    Rejected(Build(options), "MDX_TRUNCATED");
    options = {}; options.badIndex = 9;
    Rejected(Build(options), "MDX_VALUE_INVALID");
    options = {}; options.geosetMaterial = 3;
    Rejected(Build(options), "MDX_REFERENCE_INVALID");
    options = {}; options.layerTexture = 7;
    Rejected(Build(options), "MDX_REFERENCE_INVALID");
    MdxLimits looser; looser.maxGeosets += 1;
    const auto loose = ParseMdx(Build(), looser);
    Require(!loose.Succeeded() && loose.diagnostics.front().code == "MDX_LIMITS", "不能放宽上限");
    MdxLimits tight; tight.maxGeosets = 1;
    const auto limited = ParseMdx(Build(), tight);
    Require(!limited.Succeeded() && limited.diagnostics.front().code == "MDX_LIMIT", "收紧的上限生效");
    // 伪造巨大顶点数不能触发分配：先核对剩余字节。
    auto forged = Build();
    const char vrtx[] = "VRTX";
    for (std::size_t i = 0; i + 8 <= forged.size(); ++i) {
        if (std::memcmp(forged.data() + i, vrtx, 4) == 0) {
            const std::uint32_t huge = 60000;
            std::memcpy(forged.data() + i + 4, &huge, 4);
            break;
        }
    }
    Rejected(forged, "MDX_TRUNCATED");
}

// 两个原创经典 BONE 节点：第一根包含三种轨迹，第二根验证记录边界与父 ID。
std::vector<std::byte> BuildBones() {
    MdxTest::Writer w;
    w.Tag("MDLX");
    w.Tag("VERS"); w.U32(4); w.U32(800);
    w.Tag("BONE"); const auto chunk = w.Begin();
    const auto root = w.Begin();
    w.Fixed("Root", 80); w.U32(7); w.U32(0xFFFFFFFFu); w.U32(0x100);
    w.Tag("KGTR"); w.U32(2); w.U32(1); w.U32(0);
    w.U32(0); w.F32(1); w.F32(2); w.F32(3);
    w.U32(1200); w.F32(10); w.F32(20); w.F32(30);
    w.Tag("KGRT"); w.U32(1); w.U32(2); w.U32(0xFFFFFFFFu);
    w.U32(300); w.F32(0); w.F32(0); w.F32(0); w.F32(1);
    w.F32(0.1f); w.F32(0.2f); w.F32(0.3f); w.F32(0.4f);
    w.F32(0.5f); w.F32(0.6f); w.F32(0.7f); w.F32(0.8f);
    w.Tag("KGSC"); w.U32(1); w.U32(3); w.U32(0xFFFFFFFFu);
    w.U32(600); w.F32(1); w.F32(2); w.F32(3);
    w.F32(4); w.F32(5); w.F32(6);
    w.F32(7); w.F32(8); w.F32(9);
    w.EndInclusive(root); w.U32(0xFFFFFFFFu); w.U32(0xFFFFFFFFu);
    const auto child = w.Begin();
    w.Fixed("Child", 80); w.U32(8); w.U32(7); w.U32(0x200);
    w.EndInclusive(child); w.U32(0xFFFFFFFFu); w.U32(0xFFFFFFFFu);
    w.EndChunk(chunk);
    // 故意放在 BONE 之后，解析器需在整份文件读取后再验证全局序列引用。
    w.Tag("GLBS"); w.U32(4); w.U32(1200);
    return std::move(w.bytes);
}

void PatchAfterTag(std::vector<std::byte>& bytes, const char* tag, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i + offset + 4 <= bytes.size(); ++i) {
        if (std::memcmp(bytes.data() + i, tag, 4) != 0) continue;
        std::memcpy(bytes.data() + i + offset, &value, 4);
        return;
    }
    throw std::runtime_error("测试样本缺少轨迹标记");
}

void TestBones() {
    const auto result = ParseMdx(BuildBones());
    if (!result.Succeeded()) throw std::runtime_error("骨骼解析失败：" + result.diagnostics.front().message);
    const auto& model = *result.value;
    Require(model.bones.size() == 2 && model.globalSequences == std::vector<std::uint32_t>{1200}
        && model.skippedChunks == 0, "骨骼与全局序列");
    const auto& root = model.bones[0];
    Require(root.name == "Root" && root.objectId == 7 && root.parentId == -1 && root.flags == 0x100
        && root.geosetId == 0xFFFFFFFFu && root.geosetAnimationId == 0xFFFFFFFFu, "骨骼节点头与尾部");
    Require(root.translation && root.translation->interpolation == MdxInterpolation::Linear
        && root.translation->globalSequence == 0 && root.translation->keys.size() == 2
        && root.translation->keys[1].frame == 1200 && root.translation->keys[1].value[2] == 30,
        "KGTR 原始按键与全局序列");
    Require(root.rotation && root.rotation->interpolation == MdxInterpolation::Hermite
        && root.rotation->keys[0].frame == 300 && root.rotation->keys[0].value[3] == 1
        && root.rotation->keys[0].inTangent[2] == 0.3f && root.rotation->keys[0].outTangent[3] == 0.8f,
        "KGRT 四元数及 Hermite 切线");
    Require(root.scaling && root.scaling->interpolation == MdxInterpolation::Bezier
        && root.scaling->keys[0].value[1] == 2 && root.scaling->keys[0].inTangent[0] == 4
        && root.scaling->keys[0].outTangent[2] == 9, "KGSC 缩放及 Bezier 切线");
    const auto& child = model.bones[1];
    Require(child.name == "Child" && child.parentId == 7 && !child.translation && !child.rotation && !child.scaling,
        "第二根骨骼及无轨迹状态");
}

void TestBoneRejections() {
    auto bytes = BuildBones();
    PatchAfterTag(bytes, "BONE", 8, 95); // 包含式 Node 大小低于 96 字节。
    Rejected(bytes, "MDX_SIZE_INVALID");
    bytes = BuildBones(); PatchAfterTag(bytes, "KGTR", 8, 4);
    Rejected(bytes, "MDX_VALUE_INVALID");
    bytes = BuildBones(); PatchAfterTag(bytes, "KGTR", 12, 1);
    Rejected(bytes, "MDX_REFERENCE_INVALID");
    bytes = BuildBones(); PatchAfterTag(bytes, "KGRT", 4, 3);
    Rejected(bytes, "MDX_TRUNCATED"); // 第二个按键不得读到 Node 后面的 geoset ID。
    bytes = BuildBones(); PatchAfterTag(bytes, "KGRT", 36, 0x7FC00000u);
    Rejected(bytes, "MDX_NUMBER_INVALID"); // 切线里的 NaN。
    MdxLimits tight; tight.maxBones = 1;
    const auto limited = ParseMdx(BuildBones(), tight);
    Require(!limited.Succeeded() && limited.diagnostics.front().code == "MDX_LIMIT", "骨骼数量上限");
    tight = {}; tight.maxTotalBoneTrackKeys = 3;
    const auto keyLimit = ParseMdx(BuildBones(), tight);
    Require(!keyLimit.Succeeded() && keyLimit.diagnostics.front().code == "MDX_LIMIT", "骨骼总按键上限");
}

void TestHelperAndSkinningTopology() {
    Options options; options.animatedHelper = true;
    const auto parsed = ParseMdx(Build(options));
    if (!parsed.Succeeded()) throw std::runtime_error("HELP/矩阵组解析失败：" + parsed.diagnostics.front().message);
    const auto& model = *parsed.value;
    Require(model.skinningTopologyComplete && model.bones[0].parentId == 1 && model.helpers.size() == 1
        && model.helpers[0].objectId == 1 && model.helpers[0].parentId == -1 && model.pivots.size() == 2,
        "HELP 父链与 PIVT objectId 对齐");
    Require(model.helpers[0].translation && model.helpers[0].translation->keys.size() == 2
        && model.helpers[0].translation->keys[1].value[0] == 10,
        "HELP 轨迹保留而不是按未知块跳过");

    auto linearBytes = Build(options); PatchAfterTag(linearBytes, "KGAO", 8, 1);
    const auto unbracketedAlpha = ParseMdx(linearBytes);
    Require(unbracketedAlpha.Succeeded() && unbracketedAlpha.value, "线性 KGAO 模型可解析");
    // 原键为 (0, 0)/(2000, 1)；Stand 只有 0..1000，按原版单键常量，不借 Death 键。
    const auto constantAlpha = SampleMdxGeosetAlpha(*unbracketedAlpha.value, {1, 1, 500, 0});
    Require(constantAlpha.Succeeded() && constantAlpha.value && *constantAlpha.value == 0,
        "普通动作单键 KGAO 必须取当前动作的键值");
    // 第二键移到 Stand 末端：alpha = (t - 0) / (1000 - 0)，结果可精确表示。
    PatchAfterTag(linearBytes, "KGAO", 24, 1000);
    const auto linearAlpha = ParseMdx(linearBytes);
    Require(linearAlpha.Succeeded() && linearAlpha.value, "动作内双键线性 KGAO 可解析");
    const auto quarterAlpha = SampleMdxGeosetAlpha(*linearAlpha.value, {1, 1, 250, 0});
    const auto midpointAlpha = SampleMdxGeosetAlpha(*linearAlpha.value, {1, 1, 500, 0});
    Require(quarterAlpha.Succeeded() && *quarterAlpha.value == 0.25f
        && midpointAlpha.Succeeded() && *midpointAlpha.value == 0.5f,
        "线性 KGAO 按动作内两键插值，不能退化为阶梯值");
    auto invalidAlpha = linearBytes; PatchAfterTag(invalidAlpha, "KGAO", 8, 4);
    Rejected(invalidAlpha, "MDX_GEOA_INTERPOLATION_UNSUPPORTED");
    invalidAlpha = linearBytes; PatchAfterTag(invalidAlpha, "KGAO", 28, 0x7FC00000u);
    Rejected(invalidAlpha, "MDX_NUMBER_INVALID"); // alpha 按键里的 NaN。

    auto bytes = Build(options);
    PatchAfterTag(bytes, "GNDX", 4, 3);
    Rejected(bytes, "MDX_VALUE_INVALID"); // 每顶点须恰好一个槽号。
    bytes = Build(options); PatchAfterTag(bytes, "GNDX", 8, 1);
    Rejected(bytes, "MDX_REFERENCE_INVALID"); // 槽号不能越过 MTGC。
    bytes = Build(options); PatchAfterTag(bytes, "MTGC", 8, 2);
    Rejected(bytes, "MDX_VALUE_INVALID"); // 组大小之和必须等于 MATS 长度。
    bytes = Build(options); PatchAfterTag(bytes, "PIVT", 4, 10);
    Rejected(bytes, "MDX_SIZE_INVALID");
    bytes = Build(options); PatchAfterTag(bytes, "BONE", 96, 0);
    Rejected(bytes, "MDX_REFERENCE_INVALID"); // 节点不能以自身为父。
    bytes = Build(options); PatchAfterTag(bytes, "KGTR", 12, 0);
    Rejected(bytes, "MDX_REFERENCE_INVALID"); // HELP 也要验证 GLBS 索引。
    bytes = Build(options); PatchAfterTag(bytes, "HELP", 92, 0);
    Rejected(bytes, "MDX_REFERENCE_INVALID"); // BONE/HELP 的 objectId 必须唯一。
    bytes = Build(options); PatchAfterTag(bytes, "MATS", 8, 99);
    const auto missingMatrixNode = ParseMdx(bytes);
    Require(missingMatrixNode.Succeeded() && !missingMatrixNode.value->skinningTopologyComplete
        && missingMatrixNode.value->skinningTopologyIssue.find("MATS") != std::string::npos,
        "未知矩阵节点可供静态显示，但不能声称蒙皮拓扑闭合");
    bytes = Build(options); PatchAfterTag(bytes, "BONE", 96, 99);
    const auto missingParent = ParseMdx(bytes);
    Require(missingParent.Succeeded() && !missingParent.value->skinningTopologyComplete
        && missingParent.value->skinningTopologyIssue.find("父 ID") != std::string::npos,
        "未知父节点可供静态显示，但不能声称姿态闭合");
}

void Near(float actual, float expected, const char* message) {
    if (std::abs(actual - expected) > 0.0001f)
        throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected)
            + ", actual " + std::to_string(actual));
}

void PoseRejected(const MdxModel& model, const MdxPoseRequest& request, const std::string& code) {
    const auto result = SampleMdxNodePose(model, request);
    Require(!result.Succeeded() && !result.value, "不支持的姿态不能发布部分节点链");
    if (result.diagnostics.front().code != code)
        throw std::runtime_error("姿态错误类别不符：期望 " + code + "，实际 " + result.diagnostics.front().code);
}

template <typename T>
bool HasWarning(const War3Map::Result<T>& result, const std::string& code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(), [&](const War3Map::Diagnostic& diagnostic) {
        return diagnostic.severity == War3Map::DiagnosticSeverity::Warning && diagnostic.code == code;
    });
}

void TestParsedPoseChain() {
    Options options; options.animatedHelper = true;
    const auto parsed = ParseMdx(Build(options));
    Require(parsed.Succeeded(), "合成动画模型解析");
    const auto sampled = SampleMdxNodePose(*parsed.value, {1, 500, 0, 0, {}});
    if (!sampled.Succeeded()) throw std::runtime_error("已解析节点姿态失败：" + sampled.diagnostics.front().message);
    Require(sampled.value->sequenceFrame == 500 && sampled.value->chain.size() == 2
        && sampled.value->chain[0].objectId == 1 && sampled.value->chain[1].objectId == 0,
        "HELP 父链必须按根到 BONE 返回");
    Near(sampled.value->chain[0].worldMatrix[12], 5, "HELP 平移插值");
    Near(sampled.value->chain[1].worldMatrix[12], 5, "BONE 继承 HELP 平移");
}

MdxModel BuildPoseModel() {
    MdxModel model;
    model.version = 800;
    model.sequences.push_back({"Walk", 1000, 2000, 0});
    model.globalSequences.push_back(1000);
    model.pivots = {{1, 0, 0}, {0, 0, 0}};

    MdxNode root;
    root.objectId = 0;
    root.translation = MdxNodeTrack<3>{MdxInterpolation::Linear, UINT32_MAX,
        {{1000, {0, 0, 0}, {}, {}}, {2000, {10, 0, 0}, {}, {}}}};
    root.rotation = MdxNodeTrack<4>{MdxInterpolation::Linear, UINT32_MAX,
        {{1000, {0, 0, 0, 1}, {}, {}}, {2000, {0, 0, 1, 0}, {}, {}}}};
    model.helpers.push_back(root);

    MdxBone child;
    child.objectId = 1;
    child.parentId = 0;
    child.flags = 0x100;
    child.translation = MdxNodeTrack<3>{MdxInterpolation::Linear, 0,
        {{0, {0, 2, 0}, {}, {}}, {1000, {0, 4, 0}, {}, {}}}};
    model.bones.push_back(child);
    return model;
}

void TestPoseTimeAndMatrices() {
    auto model = BuildPoseModel();
    const auto halfway = SampleMdxNodePose(model, {0, 500, 500, 1, {}});
    if (!halfway.Succeeded()) throw std::runtime_error("父子矩阵求值失败：" + halfway.diagnostics.front().message);
    Require(halfway.value->sequenceFrame == 1500 && halfway.value->chain.size() == 2,
        "局部时钟须映射到 SEQS 绝对区间");
    const auto& root = halfway.value->chain[0];
    const auto& child = halfway.value->chain[1];
    Near(root.translation[0], 5, "序列平移线性插值");
    Near(root.worldMatrix[12], 6, "轴心旋转后的根 X");
    Near(root.worldMatrix[13], -1, "轴心旋转后的根 Y");
    Near(child.translation[1], 3, "独立全局序列插值");
    Near(child.worldMatrix[12], 3, "父子矩阵组合 X");
    Near(child.worldMatrix[13], -1, "父子矩阵组合 Y");

    const auto looped = SampleMdxNodePose(model, {0, 1500, 500, 1, {}});
    Require(looped.Succeeded() && looped.value->sequenceFrame == 1500, "循环动作按区间长度回绕");
    const auto independent = SampleMdxNodePose(model, {0, 500, 0, 1, {}});
    Require(independent.Succeeded(), "全局序列从独立时钟取样");
    Near(independent.value->chain[1].worldMatrix[12], 4, "全局时钟不随动作时钟变化");

    model.sequences[0].flags = 1;
    const auto clamped = SampleMdxNodePose(model, {0, 1500, 500, 1, {}});
    Require(clamped.Succeeded() && clamped.value->sequenceFrame == 2000, "非循环动作固定终点");
    Near(clamped.value->chain[1].worldMatrix[12], 12, "非循环终点父链 X");
    Near(clamped.value->chain[1].worldMatrix[13], -3, "非循环终点父链 Y");

    model = BuildPoseModel();
    model.helpers[0].translation->interpolation = MdxInterpolation::Hermite;
    model.bones[0].scaling = MdxNodeTrack<3>{MdxInterpolation::Bezier, 0,
        {{0, {1, 1, 1}, {}, {1, 1, 1}}, {1000, {3, 3, 3}, {3, 3, 3}, {}}}};
    const auto curves = SampleMdxNodePose(model, {0, 250, 250, 1, {}});
    Require(curves.Succeeded(), "向量 Hermite 与 Bezier 求值");
    Near(curves.value->chain[0].translation[0], 1.5625f, "KGTR Hermite 曲线");
    Near(curves.value->chain[1].scaling[0], 1.3125f, "KGSC Bezier 曲线");
}

void TestPosePreviewSemantics() {
    auto model = BuildPoseModel();
    model.helpers[0].rotation->interpolation = MdxInterpolation::Hermite;
    model.helpers[0].rotation->keys[1].value = {0, 0, 0.70710678f, 0.70710678f};
    model.helpers[0].rotation->keys[0].outTangent = {0, 0, 0, 1};
    model.helpers[0].rotation->keys[1].inTangent = {0, 0, 0, 1};
    const auto hermite = SampleMdxNodePose(model, {0, 500, 500, 1, {}});
    Require(hermite.Succeeded() && HasWarning(hermite, "MDX_POSE_GENERIC_ROTATION_PREVIEW")
        && hermite.diagnostics.front().message.find("KGRT objectId=0") != std::string::npos
        && hermite.diagnostics.front().message.find("1000..2000") != std::string::npos,
        "KGRT Hermite squad 仅作为带定位诊断的通用预览");
    Near(hermite.value->chain[0].rotation[2], 0.19509032f, "Hermite squad 半程受控制四元数影响");
    model.helpers[0].rotation->interpolation = MdxInterpolation::Bezier;
    model.helpers[0].rotation->keys[0].outTangent = {0, 0, 0.70710678f, 0.70710678f};
    model.helpers[0].rotation->keys[1].inTangent = {0, 0, 0.70710678f, 0.70710678f};
    const auto bezier = SampleMdxNodePose(model, {0, 500, 500, 1, {}});
    Require(bezier.Succeeded() && HasWarning(bezier, "MDX_POSE_GENERIC_ROTATION_PREVIEW"),
        "KGRT Bezier squad 也须标为通用预览");
    Near(bezier.value->chain[0].rotation[2], 0.55557023f, "Bezier squad 半程受控制四元数影响");
    model.helpers[0].rotation->keys[0].outTangent = {0, 0, 0, 0};
    const auto fallback = SampleMdxNodePose(model, {0, 500, 500, 1, {}});
    Require(fallback.Succeeded() && HasWarning(fallback, "MDX_POSE_ROTATION_CONTROL_FALLBACK"),
        "单条零控制切线须显式退为球面插值，不得拒绝整张预览场景");
    Near(fallback.value->chain[0].rotation[2], 0.38268343f, "无效控制切线仅回退该段端点插值");

    model = BuildPoseModel();
    model.helpers[0].flags = 0x8;
    model.helpers[0].rotation = MdxNodeTrack<4>{MdxInterpolation::None, UINT32_MAX,
        {{1000, {0, 0, 0, 1}, {}, {}}}};
    const auto staticPose = SampleMdxNodePose(model, {0, 500, 500, 0, {}});
    Require(staticPose.Succeeded() && HasWarning(staticPose, "MDX_POSE_CAMERA_STATIC_PREVIEW"),
        "缺相机时 billboard 保留静态姿态并明确诊断");
    Near(staticPose.value->chain[0].worldMatrix[12], 5, "静态预览保留原始轴心平移");
    const MdxPoseCamera front{{6, 10, 0}, {0, 0, 1}};
    const auto frontPose = SampleMdxNodePose(model, {0, 500, 500, 0, front});
    Require(frontPose.Succeeded() && HasWarning(frontPose, "MDX_POSE_GENERIC_CAMERA_PREVIEW"),
        "有相机时 billboard 返回通用预览诊断");
    Near(frontPose.value->chain[0].worldMatrix[4], 0, "正面相机不改变朝向 X");
    Near(frontPose.value->chain[0].worldMatrix[5], 1, "正面相机指向 +Y");
    const MdxPoseCamera side{{16, 0, 0}, {0, 0, 1}};
    const auto sidePose = SampleMdxNodePose(model, {0, 500, 500, 0, side});
    Require(sidePose.Succeeded(), "侧面相机可求 billboard 预览");
    Near(sidePose.value->chain[0].worldMatrix[4], 1, "billboard 的 +Y 朝向相机 +X");
    Near(sidePose.value->chain[0].worldMatrix[13] + sidePose.value->chain[0].worldMatrix[1], 0,
        "billboard 后轴心世界位置不漂移");
    model.helpers[0].flags = 0x40;
    const auto lockZ = SampleMdxNodePose(model, {0, 500, 500, 0, side});
    Require(lockZ.Succeeded(), "锁 Z 的相机预览");
    Near(lockZ.value->chain[0].worldMatrix[4], 1, "锁 Z 只绕 Z 朝向相机");
    model.helpers[0].flags = 0x10;
    const auto lockX = SampleMdxNodePose(model, {0, 500, 500, 0,
        MdxPoseCamera{{6, 0, 10}, {0, 0, 1}}});
    Require(lockX.Succeeded(), "锁 X 的相机预览");
    Near(lockX.value->chain[0].worldMatrix[6], 1, "锁 X 的 +Y 朝向 +Z");
    model.helpers[0].flags = 0x20;
    const auto lockY = SampleMdxNodePose(model, {0, 500, 500, 0, side});
    Require(lockY.Succeeded(), "锁 Y 的相机预览");
    Near(lockY.value->chain[0].worldMatrix[8], 1, "锁 Y 的 +Z 朝向 +X");
    model.helpers[0].flags = 0x80;
    Require(SampleMdxNodePose(model, {0, 500, 500, 0, side}).Succeeded(),
        "CameraAnchored 的相机朝向通用预览");
    model.helpers[0].flags = 0x10 | 0x40;
    PoseRejected(model, {0, 500, 500, 0, side}, "MDX_POSE_CAMERA_FLAGS_UNSUPPORTED");
    model.helpers[0].flags = 0x8;
    PoseRejected(model, {0, 500, 500, 0, MdxPoseCamera{{500000, 0, 0}, {0, 0, 1}}},
        "MDX_POSE_CAMERA_INVALID");
    model = BuildPoseModel();
    model.helpers[0].translation->keys[0].frame = 1100;
    const auto seamPose = SampleMdxNodePose(model, {0, 0, 500, 1, {}});
    Require(seamPose.Succeeded(), "普通动作首键前应按原版接缝取值");
    Near(seamPose.value->chain.front().translation[0], 20, "首键前保留原首键的时间偏移");
}

void SkinRejected(const MdxModel& model, const MdxRigidSkinRequest& request, const std::string& code) {
    const auto result = SampleMdxRigidSkinnedGeoset(model, request);
    Require(!result.Succeeded() && !result.value, "不支持的蒙皮不能发布部分几何");
    if (result.diagnostics.front().code != code)
        throw std::runtime_error("蒙皮错误类别不符：期望 " + code + "，实际 " + result.diagnostics.front().code);
}

void TestParsedRigidSkinning() {
    Options options; options.animatedHelper = true;
    const auto parsed = ParseMdx(Build(options));
    Require(parsed.Succeeded(), "合成动画模型解析");
    const auto& model = *parsed.value;
    const auto first = SampleMdxRigidSkinnedGeoset(model, {0, 1, 0, 0, {}});
    const auto middle = SampleMdxRigidSkinnedGeoset(model, {0, 1, 500, 0, {}});
    Require(first.Succeeded() && middle.Succeeded(), "单节点矩阵组应能求出 CPU 顶点姿态");
    Require(first.value->positions.size() == 4 && middle.value->normals.size() == 4
        && middle.value->sequenceFrame == 500, "蒙皮结果与原始顶点一一对应");
    Near(first.value->positions[0][0], 0, "起点顶点仍在绑定位置");
    Near(middle.value->positions[0][0], 5, "HELP 父链动画作用于顶点");
    Near(middle.value->positions[3][0], 15, "所有同组顶点共享骨骼姿态");
    Near(middle.value->positions[3][2], 5, "原始 Z 坐标保留");
    Near(middle.value->normals[0][2], 1, "平移不改变法线方向");

    auto multi = model;
    multi.bones[0].translation = MdxNodeTrack<3>{MdxInterpolation::None, UINT32_MAX,
        {{0, {2, 0, 0}, {}, {}}}};
    multi.geosets[0].matrixGroupSizes = {2};
    multi.geosets[0].matrixIndices = {0, 1};
    const auto blended = SampleMdxRigidSkinnedGeoset(multi, {0, 1, 500, 0, {}});
    Require(blended.Succeeded() && blended.value->positions.size() == 4,
        "多节点矩阵组按原版组矩阵平均后求值");
    Near(blended.value->positions[0][0], 6, "骨骼移动 7、父 HELP 移动 5，组矩阵平均后为 6");
    auto unsupported = model;
    unsupported.skinningTopologyComplete = false;
    unsupported.skinningTopologyIssue = "测试缺口";
    SkinRejected(unsupported, {0, 1, 500, 0, {}}, "MDX_SKIN_TOPOLOGY_INCOMPLETE");
    unsupported = model;
    unsupported.helpers[0].flags = 0x8;
    const auto staticBillboard = SampleMdxRigidSkinnedGeoset(unsupported, {0, 1, 500, 0, {}});
    Require(staticBillboard.Succeeded() && HasWarning(staticBillboard, "MDX_POSE_CAMERA_STATIC_PREVIEW"),
        "蒙皮缺相机时不丢顶点，且透传静态预览诊断");
}

void TestRigidSkinningPivotAndNormals() {
    auto model = BuildPoseModel();
    model.skinningTopologyComplete = true;
    MdxGeoset geoset;
    geoset.positions = {{2, 0, 0}};
    geoset.normals = {{1, 1, 0}};
    geoset.vertexGroups = {0};
    geoset.matrixGroupSizes = {1};
    geoset.matrixIndices = {0};
    model.geosets.push_back(geoset);
    auto& root = model.helpers[0];
    root.translation = MdxNodeTrack<3>{MdxInterpolation::None, UINT32_MAX,
        {{1000, {3, 4, 0}, {}, {}}}};
    root.rotation = MdxNodeTrack<4>{MdxInterpolation::None, UINT32_MAX,
        {{1000, {0, 0, 0.70710678f, 0.70710678f}, {}, {}}}};
    root.scaling = MdxNodeTrack<3>{MdxInterpolation::None, UINT32_MAX,
        {{1000, {2, 1, 1}, {}, {}}}};
    const auto sample = SampleMdxRigidSkinnedGeoset(model, {0, 0, 500, 500, {}});
    Require(sample.Succeeded(), "轴心、旋转、非均匀缩放可求单节点蒙皮");
    Near(sample.value->positions[0][0], 4, "按轴心旋转后的 X");
    Near(sample.value->positions[0][1], 6, "按轴心旋转后的 Y");
    Near(sample.value->normals[0][0], -0.4472136f, "法线使用同一组矩阵的 3x3 部分 X");
    Near(sample.value->normals[0][1], 0.8944272f, "法线用组矩阵变换后归一化 Y");
    root.scaling->keys[0].value[0] = 0;
    Require(SampleMdxRigidSkinnedGeoset(model, {0, 0, 500, 500, {}}).Succeeded(),
        "组矩阵奇异但法线仍有方向时可显示");
    model.geosets[0].normals[0] = {1, 0, 0};
    SkinRejected(model, {0, 0, 500, 500, {}}, "MDX_SKIN_NORMAL_INVALID");
}

void TestCollisionMetadata() {
    MdxTest::Writer w;
    w.bytes = Build({.selectionGroup = 7, .selectionFlags = 4});
    w.Tag("CLID"); const auto chunk = w.Begin();
    for (std::uint32_t type = 0; type < 4; ++type) {
        const auto node = w.Begin(); w.Fixed("Collision", 80);
        w.U32(type + 1); w.U32(UINT32_MAX); w.U32(0x2000); w.EndInclusive(node);
        w.U32(type);
        for (std::uint32_t word = 0; word < (type == 2 ? 3u : 6u); ++word) w.F32(static_cast<float>(word + 1));
        if (type == 2 || type == 3) w.F32(7);
    }
    w.EndChunk(chunk);
    const auto parsed = ParseMdx(w.bytes);
    Require(parsed.Succeeded() && parsed.value->collisionShapes.size() == 4, "CLID 保留旧四种边界");
    Require(parsed.value->geosets[0].selectionGroup == 7 && parsed.value->geosets[1].selectionFlags == 4, "GEOS 选择原值");
    for (std::uint32_t type = 0; type < 4; ++type) {
        const auto& shape = parsed.value->collisionShapes[type];
        Require(shape.sourceIndex == type && shape.sourceType == type && shape.nodeId == type + 1
            && shape.vertices[0][2] == 3 && shape.rawWords[0] == 0x3f800000, "碰撞源顺序、节点和原始位");
        Require(shape.support == (type == 0 || type == 2 ? MdxCollisionSupport::Supported : MdxCollisionSupport::LayoutAmbiguous)
            && shape.payloadBytes == (type == 2 ? 16u : type == 3 ? 28u : 24u), "已证布局与歧义分开");
    }
    Require(parsed.value->collisionShapes[0].layout == MdxCollisionLayout::BoxVertices24
        && parsed.value->collisionShapes[2].layout == MdxCollisionLayout::SphereCenterRadius16
        && parsed.value->collisionShapes[2].radius == 7, "Box 与 Sphere 原字段");
    auto nonfinite = w.bytes; const std::uint32_t rawNaN = 0x7fc01234;
    std::memcpy(nonfinite.data() + chunk + 4 + 96 + 4, &rawNaN, 4);
    const auto preserved = ParseMdx(nonfinite);
    Require(preserved.Succeeded() && preserved.value->collisionShapes[0].support == MdxCollisionSupport::NonFinite
        && preserved.value->collisionShapes[0].rawWords[0] == rawNaN, "非有限旧数据不丢弃或新增拒绝");
    auto unknown = w.bytes; const std::uint32_t rawType = 4;
    std::memcpy(unknown.data() + chunk + 4 + 96, &rawType, 4); Rejected(unknown, "MDX_COLLISION_TYPE_UNSUPPORTED");
    w.bytes.pop_back(); Rejected(w.bytes, "MDX_TRUNCATED");
}

void TestModelFile() {
    Require(MdxModelFile("Doodads\\Terrain\\LordaeronTree\\LordaeronTree", 3, 10) == "Doodads\\Terrain\\LordaeronTree\\LordaeronTree3.mdx",
        "多变体追加编号");
    Require(MdxModelFile("units\\human\\Footman\\Footman.mdl", 0, 1) == "units\\human\\Footman\\Footman.mdx", "单变体替换后缀");
    Require(MdxModelFile("Doodads\\Cage\\Cage.MDX", 5, 1) == "Doodads\\Cage\\Cage.mdx", "大写后缀");
}

} // namespace

int main() {
    try {
        TestParse();
        TestRejections();
        TestBones();
        TestBoneRejections();
        TestHelperAndSkinningTopology();
        TestParsedPoseChain();
        TestPoseTimeAndMatrices();
        TestPosePreviewSemantics();
        TestParsedRigidSkinning();
        TestRigidSkinningPivotAndNormals();
        TestCollisionMetadata();
        TestModelFile();
        std::cout << "MdxModelTests: all tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MdxModelTests: " << exception.what() << '\n';
        return 1;
    }
}
