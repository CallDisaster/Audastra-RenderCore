// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3RenderCore/Result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 经典 MDX（MDLX v800）的只读模型、公共节点图及 TRS 轨迹。
// 非网格节点参与父链；其粒子/条带/事件/碰撞负载不执行、不渲染。
// 只读字节，不访问文件或 GPU；姿势仍属于通用预览。
namespace War3Map::Render {

struct MdxLimits {
    // 默认值即硬上限，只能收紧。
    std::size_t maxInputBytes = 32 * 1024 * 1024;
    std::size_t maxGeosets = 1024;
    std::size_t maxVerticesPerGeoset = 65536; // 索引为 16 位
    std::size_t maxTotalVertices = 1048576;
    std::size_t maxTotalIndices = 3145728;
    std::size_t maxTextures = 1024;
    std::size_t maxMaterials = 1024;
    std::size_t maxLayersPerMaterial = 64;
    std::size_t maxSequences = 4096;
    std::size_t maxTrackKeys = 65536;
    std::size_t maxGlobalSequences = 4096;
    std::size_t maxBones = 8192;
    std::size_t maxHelpers = 8192;
    std::size_t maxPivots = 8192;
    std::size_t maxMatrixGroupsPerGeoset = 256; // GNDX 槽号为 uint8
    std::size_t maxMatrixIndicesPerGeoset = 65536;
    std::size_t maxTotalBoneTrackKeys = 262144; // 所有公共节点轨迹共享总按键预算
    std::size_t maxNodes = 8192; // 所有节点种类共用，与 PIVT 硬上限一致
};

struct MdxTexture {
    std::uint32_t replaceableId = 0; // 0 = 使用 path；1 队伍色、2 队伍光晕、31+ 树木等由对象数据决定
    std::string path;                // 原文（反斜杠、大小写保留）
    std::uint32_t flags = 0;         // 1 横向环绕、2 纵向环绕
};

// 原版层过滤模式。
enum class MdxFilterMode : std::uint32_t {
    None = 0, Transparent = 1, Blend = 2, Additive = 3, AddAlpha = 4, Modulate = 5, Modulate2x = 6,
};

struct MdxLayer {
    MdxFilterMode filterMode = MdxFilterMode::None;
    std::uint32_t shadingFlags = 0; // 0x1 无光照、0x10 双面、0x40 不测深度、0x80 不写深度
    std::uint32_t textureId = 0;
    float alpha = 1.0f;             // 静态值；层透明度轨迹未解释
    bool animatedTexture = false;   // 有贴图 ID 轨迹（KMTF）时为真，仍取静态 textureId
    bool animatedAlpha = false;
    std::uint32_t textureAnimationId = UINT32_MAX, coordId = 0;
    bool hasUninterpretedData = false; // 元数据仅供严格粒子材质门禁；不改变现有网格绘制。
};

struct MdxMaterial {
    std::int32_t priorityPlane = 0;
    std::uint32_t flags = 0;
    std::vector<MdxLayer> layers;
    bool hasUninterpretedData = false;
};

struct MdxGeosetAlphaKey {
    std::uint32_t frame = 0;
    float value = 1.0f;
    float inTangent = 0.0f, outTangent = 0.0f;
};
struct MdxGeosetColorKey {
    std::uint32_t frame = 0;
    std::array<float,3> value{}, inTangent{}, outTangent{}; // 文件 BGR 已转 RGB
};

// GEOA 的原始透明度与 RGB 轨迹；有限通用插值预览，未知尾部保持拒绝。
struct MdxGeosetAnimation {
    float staticAlpha = 1.0f;
    std::uint32_t flags = 0;
    std::uint32_t interpolation = 0;
    std::uint32_t globalSequence = UINT32_MAX;
    std::vector<MdxGeosetAlphaKey> alphaKeys;
    bool hasAlphaTrack = false;
    bool hasUninterpretedData = false;
    // GEOA 静态颜色：文件 BGR 转成 RGB；仅 flags & 0x2 时参与着色。
    // 不重写原始标志；KGAC 与 KGAO 各有独立插值类型和全局序列。
    std::array<float, 3> staticColor{1, 1, 1};
    bool hasColorTrack = false;
    std::uint32_t colorInterpolation = 0, colorGlobalSequence = UINT32_MAX;
    std::vector<MdxGeosetColorKey> colorKeys;
};

struct MdxGeoset {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> uvs;     // 第一组 UV
    std::vector<std::uint16_t> indices;        // 仅三角形
    std::vector<std::uint8_t> vertexGroups;      // GNDX：每顶点一个 MTGC 组槽号
    std::vector<std::uint32_t> matrixGroupSizes; // MTGC：每组在 MATS 内占的索引数
    std::vector<std::uint32_t> matrixIndices;    // MATS：连续节点 objectId，不是逐顶点权重
    std::uint32_t materialId = 0;
    bool visibleInStand = true;                // geoset 动画在“站立”起点的透明度 > 0
    std::optional<MdxGeosetAnimation> animation; // 显式动作按帧求 KGAO；静态视图仍用 visibleInStand
    std::size_t skippedPrimitiveGroups = 0;    // 非三角形图元组（未显示）
    std::size_t uninterpretedTailBytes = 0;    // GEOS 的未知尾部，禁止据此声称蒙皮拓扑完整
};

struct MdxSequence {
    std::string name;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t flags = 0; // SEQS 原始 flags；求值时不得把非循环动作猜成循环
    // 坏序列保留原序号/原字段，只禁用自己；不删项、不交换起止点、不修正成循环。
    [[nodiscard]] bool Usable() const noexcept { return end >= start && flags <= 1; }
};

enum class MdxInterpolation : std::uint32_t { None = 0, Linear = 1, Hermite = 2, Bezier = 3 };

// 经典节点轨迹的原始按键。仅 Hermite/Bezier 使用入/出切线；其余插值时这两项为零。
template <std::size_t Components>
struct MdxNodeKey {
    std::int32_t frame = 0;
    std::array<float, Components> value{};
    std::array<float, Components> inTangent{};
    std::array<float, Components> outTangent{};
};

template <std::size_t Components>
struct MdxNodeTrack {
    MdxInterpolation interpolation = MdxInterpolation::None;
    std::uint32_t globalSequence = UINT32_MAX; // GLBS 索引；UINT32_MAX 表示随普通动作时钟
    std::vector<MdxNodeKey<Components>> keys;
};

// 种类只描述公共节点所属块；纳入父链不表示已渲染粒子/条带/挂点/碰撞。
enum class MdxNodeKind : std::uint32_t {
    Helper, Bone, Attachment, ParticleEmitter, ParticleEmitter2, Ribbon, Event, Light, Collision
};

struct MdxNode {
    std::string name;
    std::uint32_t objectId = 0;
    std::int32_t parentId = -1; // 对象 ID；父节点也可能来自其它 MDX 节点块
    std::uint32_t flags = 0;
    std::optional<MdxNodeTrack<3>> translation; // KGTR
    std::optional<MdxNodeTrack<4>> rotation;    // KGRT，四元数 x/y/z/w
    std::optional<MdxNodeTrack<3>> scaling;     // KGSC
    MdxNodeKind kind = MdxNodeKind::Helper;
};

// 从变换标志校验中排除已知的类型/发射器绘制位，仍保留原始 flags；未知位不放行。
[[nodiscard]] inline std::uint32_t MdxNodeNonTransformFlags(MdxNodeKind kind) noexcept {
    switch (kind) {
    case MdxNodeKind::Helper: case MdxNodeKind::Bone: return 0x100u;
    case MdxNodeKind::Attachment: return 0x800u;
    case MdxNodeKind::ParticleEmitter: return 0x19000u;
    case MdxNodeKind::ParticleEmitter2: return 0x1F9000u;
    case MdxNodeKind::Ribbon: return 0x4000u;
    case MdxNodeKind::Event: return 0x400u;
    case MdxNodeKind::Light: return 0x200u;
    case MdxNodeKind::Collision: return 0x2000u;
    }
    return 0;
}

struct MdxBone : MdxNode {
    std::uint32_t geosetId = UINT32_MAX;
    std::uint32_t geosetAnimationId = UINT32_MAX;
};

// 光源动画按键；Hermite/Bezier 的切线读取后丢弃，采样方按线性处理。
struct MdxColorKey {
    std::int32_t frame = 0;
    std::array<float, 3> rgb{}; // 文件为 BGR，已转成 RGB
};
struct MdxScalarKey {
    std::int32_t frame = 0;
    float value = 0;
};

// LITE 光源（昼夜 DNC 模型用平行光表达太阳）。朝向取节点旋转轨迹的第一个按键。
struct MdxLight {
    std::string name;
    std::uint32_t type = 0;                     // 0 点光、1 平行光、2 环境光
    std::array<float, 4> rotation{0, 0, 0, 1};  // 四元数 x, y, z, w
    std::array<float, 3> color{1, 1, 1};        // 静态值，RGB
    float intensity = 1;
    std::array<float, 3> ambientColor{1, 1, 1};
    float ambientIntensity = 0;
    std::vector<MdxColorKey> colorKeys, ambientColorKeys;       // KLAC、KLBC
    std::vector<MdxScalarKey> intensityKeys, ambientIntensityKeys; // KLAI、KLBI
};

struct MdxModel {
    std::uint32_t version = 0;
    std::string name;
    std::array<float, 3> minimum{}, maximum{};
    float boundsRadius = 0;
    std::vector<MdxSequence> sequences;
    std::vector<std::uint32_t> globalSequences; // GLBS 周期，毫秒；索引由节点轨迹引用
    std::vector<MdxTexture> textures;
    std::vector<MdxMaterial> materials;
    std::vector<MdxGeoset> geosets;
    std::vector<MdxLight> lights;
    std::vector<MdxBone> bones;
    std::vector<MdxNode> helpers; // HELP 与 BONE 共用 objectId/父链/轨迹语义
    std::vector<MdxNode> otherNodes; // ATCH/PREM/PRE2/RIBB/EVTS/LITE/CLID 公共节点
    std::vector<std::array<float, 3>> pivots; // PIVT 按 objectId 索引；其它类型节点可占额外槽
    bool skinningTopologyComplete = false; // GEOS 矩阵引用、节点父链和 pivot 的数据已闭合；不表示已实现蒙皮
    std::string skinningTopologyIssue; // 不完整时显式说明；静态几何仍可读取
    std::size_t skippedChunks = 0; // 按大小跳过的其它块（粒子等）
};

// 仅用于进程内解析数据，不是插件/IPC 布局；跨边界描述符仍用定宽计数。
[[nodiscard]] inline std::size_t MdxNodeCount(const MdxModel& model) noexcept {
    return model.bones.size() + model.helpers.size() + model.otherNodes.size();
}
template <class Visit>
void ForEachMdxNode(const MdxModel& model, Visit&& visit) {
    for (const auto& node : model.bones) visit(static_cast<const MdxNode&>(node));
    for (const auto& node : model.helpers) visit(node);
    for (const auto& node : model.otherNodes) visit(node);
}

struct MdxGeosetAlphaRequest {
    std::size_t geosetIndex = 0;
    std::size_t sequenceIndex = 0;
    std::uint64_t sequenceElapsedMs = 0;
    std::uint64_t globalElapsedMs = 0;
};

// 有界 KGAO 的阶梯/线性/Hermite/Bezier 通用预览；未知边界不合成补键。
[[nodiscard]] Result<float> SampleMdxGeosetAlpha(const MdxModel& model, const MdxGeosetAlphaRequest& request);

// GEOA RGB 乘数。flags=0 返回白色；0x2 使用静态色或 KGAC；未知位/尾部明确拒绝。
// 颜色曲线采用通用插值并夹到 0..1；与 alpha 共用请求身份，不声称原版逐帧一致。
[[nodiscard]] Result<std::array<float, 3>> SampleMdxGeosetColor(
    const MdxModel& model, const MdxGeosetAlphaRequest& request);

// 结构越界、版本不是 800、索引/材质/贴图引用无效时整体失败，不发布部分模型。
[[nodiscard]] Result<MdxModel> ParseMdx(std::span<const std::byte> bytes, const MdxLimits& limits = {});

// 由对象数据的模型字段得到实际文件：去掉 .mdl/.mdx 后缀；variationCount > 1 时追加变体编号。
[[nodiscard]] std::string MdxModelFile(std::string_view modelField, std::uint32_t variation, std::uint32_t variationCount);

} // namespace War3Map::Render
