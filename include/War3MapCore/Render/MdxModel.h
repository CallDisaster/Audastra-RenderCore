// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3RenderCore/Result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

struct MdxMaterialAlphaKey {
    std::uint32_t frame = 0;
    float value = 1.0f, inTangent = 0.0f, outTangent = 0.0f;
    std::uint64_t sourceByteOffset = 0; // 原frame字段位置；value为+4，切线为+8/+12。
};
struct MdxMaterialTextureKey {
    std::uint32_t frame = 0, textureId = 0, inTangentBits = 0, outTangentBits = 0;
    std::uint64_t sourceByteOffset = 0; // 原frame位置；贴图ID为+4，不经过float转换。
};
struct MdxMaterialTextureTrack {
    std::uint32_t interpolation = 0, globalSequence = UINT32_MAX;
    std::uint64_t sourceByteOffset = 0;
    std::vector<MdxMaterialTextureKey> keys;
};
struct MdxLayer {
    MdxFilterMode filterMode = MdxFilterMode::None;
    std::uint32_t shadingFlags = 0; // 0x1 无光照、0x10 双面、0x40 不测深度、0x80 不写深度
    std::uint32_t textureId = 0;
    float alpha = 1.0f;             // 没有KMTA时的真实静态值；不预乘GEOA。
    bool animatedTexture = false;   // 显式纹理渠道才消费KMTF；旧默认仍取静态textureId。
    bool animatedAlpha = false;
    std::uint32_t textureAnimationId = UINT32_MAX, coordId = 0;
    bool hasUninterpretedData = false; // 元数据仅供严格粒子材质门禁；不改变现有网格绘制。
    std::uint32_t alphaInterpolation = 0, alphaGlobalSequence = UINT32_MAX;
    std::uint64_t alphaSourceByteOffset = 0, staticAlphaByteOffset = 0;
    std::vector<MdxMaterialAlphaKey> alphaKeys;
    std::uint32_t textureInterpolation = 0, textureGlobalSequence = UINT32_MAX;
    std::uint64_t textureSourceByteOffset = 0, staticTextureIdByteOffset = 0, duplicateTextureByteOffset = 0;
    std::vector<MdxMaterialTextureKey> textureKeys;
    std::vector<MdxMaterialTextureTrack> duplicateTextureTracks; // 原轨迹完整保留；新渠道拒绝冲突。
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
    // GEOA 静态颜色保持文件 RGB 顺序；仅 flags & 0x2 时参与着色。
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
    std::uint32_t selectionGroup = 0, selectionFlags = 0; // GEOS 原值；未知标志不推定为可选
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

// 布局名描述本读取器实际消费的文件字节；type1/3 尚不能等同于原版运行时形状类型。
enum class MdxCollisionLayout : std::uint32_t {
    BoxVertices24, SphereCenterRadius16,
    UnverifiedType1Vertices24, UnverifiedType3VerticesRadius28
};
enum class MdxCollisionSupport : std::uint32_t {
    Supported, LayoutAmbiguous, NonFinite, InvalidRadius
};
struct MdxCollisionShape {
    std::uint32_t sourceIndex = 0; // 跨 CLID 块的原始顺序，包括无法用于点选的形状
    std::uint32_t sourceType = 0, nodeId = 0, vertexCount = 0, payloadBytes = 0;
    std::array<std::array<float, 3>, 2> vertices{};
    float radius = 0;
    bool hasRadius = false;
    std::array<std::uint32_t, 7> rawWords{}; // 原始浮点位；未使用槽为零，不修复 NaN/-0
    MdxCollisionLayout layout = MdxCollisionLayout::BoxVertices24;
    // Supported 仅表示已证的 type0/2 布局与有限几何输入，不表示原版游戏点选已验收。
    MdxCollisionSupport support = MdxCollisionSupport::LayoutAmbiguous;
};

// PREM 旧式模型发射器的只读负载；不执行发射、不访问资源、不赋予保存或原版等价资格。
enum class MdxPremParameter : std::uint32_t {
    EmissionRate, Gravity, Longitude, Latitude, LifeSpan, Speed, Visibility, Count
};
struct MdxPremScalarKey {
    std::int32_t frame = 0;
    float value = 0, inTangent = 0, outTangent = 0;
    std::uint64_t sourceByteOffset = 0; // frame 的原物理位置；三个 float 为 +4/+8/+12。
    std::array<std::uint32_t, 3> rawValueBits{}; // 原始 value/in/out 位，不修复 NaN/-0。
};
struct MdxPremScalarTrack {
    std::uint32_t interpolation = 0, globalSequence = UINT32_MAX, sourceKeyCount = 0;
    std::uint64_t sourceByteOffset = 0, sourceByteSize = 0; // tag 起点及已证结构长度。
    std::vector<MdxPremScalarKey> keys; // 原始次序；不排序、不合并、不给空轨迹补键。
};
enum class MdxPremSupport : std::uint32_t {
    Supported, UnsupportedFlags, InvalidPath, InvalidParameters, UnknownTrack,
    DuplicateTrack, InvalidTrack, TrackKeyLimit, InvalidGlobalSequence
};
struct MdxPremEmitter {
    std::uint32_t sourceIndex = 0, otherNodeIndex = 0, nodeId = 0, flags = 0;
    std::uint64_t recordByteOffset = 0, recordByteSize = 0, payloadByteOffset = 0;
    std::uint64_t pathByteOffset = 0;
    std::array<std::byte, 260> rawPath{};
    std::string path; // NUL 前原字节；保留大小写/反斜杠，不猜 .mdl/.mdx 资源别名。
    bool pathTerminated = false;
    float emissionRate = 0, gravity = 0, longitude = 0, latitude = 0, lifeSpan = 0, speed = 0;
    // 上述六字段的文件顺序及逐位原值；角度保留原弧度，未套用 PRE2 度单位。
    std::array<std::uint32_t, 6> staticValueBits{};
    std::array<std::optional<MdxPremScalarTrack>, static_cast<std::size_t>(MdxPremParameter::Count)> tracks;
    // Supported 仅表示 typed 结构/有限字段/已知模型标志可表达，不表示发射或原版时序已经验收。
    MdxPremSupport support = MdxPremSupport::Supported;
    std::string issueCode;
    std::uint64_t issueByteOffset = 0, issueByteLength = 4;
    std::uint32_t issueValueBits = 0;
    // 安全外层记录内无法解释的尾部仍有准确物理边界，未将其宣称为已解析轨迹。
    std::uint64_t uninterpretedTailByteOffset = 0, uninterpretedTailBytes = 0;
};

struct MdxModel;

// 缺少矩阵组的无控制器静态模型，仅供显式“预览近似”入口。
// 偏移指向原始文件；零 MTGC/MATS 不改写成虚构骨骼或源矩阵组。
struct MdxRigidStaticGeosetOrigin {
    std::size_t geosetBegin = 0, geosetEnd = 0;
    std::size_t positionsOffset = 0, normalsOffset = 0, indicesOffset = 0, uvsOffset = 0;
    std::size_t vertexGroupsCountOffset = 0, matrixGroupsCountOffset = 0, matrixIndicesCountOffset = 0;
    std::uint32_t vertexCount = 0, indexCount = 0, uvCount = 0;
};

class MdxRigidStaticPreviewSource {
public:
    [[nodiscard]] std::span<const std::byte> Bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::span<const MdxRigidStaticGeosetOrigin> Geosets() const noexcept { return geosets_; }
private:
    MdxRigidStaticPreviewSource(std::span<const std::byte> bytes,
        std::vector<MdxRigidStaticGeosetOrigin> geosets) : bytes_(bytes.begin(), bytes.end()), geosets_(std::move(geosets)) {}
    std::vector<std::byte> bytes_;
    std::vector<MdxRigidStaticGeosetOrigin> geosets_;
    friend Result<MdxModel> ParseMdxRigidStaticPreview(std::span<const std::byte>, const MdxLimits&);
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
    std::vector<MdxCollisionShape> collisionShapes; // 只读 CLID 来源，不提供写入/保存能力
    std::vector<MdxPremEmitter> premEmitters; // PREM 来源顺序，包含局部 unsupported；nodeId 对应 PIVT。
    // 不可伪造构造的解析来源；消费者仍重新检查当前公开几何与原字节一致。
    std::shared_ptr<const MdxRigidStaticPreviewSource> rigidStaticPreviewSource;
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
// 独立原始键参考；按material/layer唯一顺序，request.geosetIndex不使用。
// 不调用RenderCore，不声称与原版逐帧等价；未知/坏层不回退static成功。
[[nodiscard]] Result<std::vector<float>> SampleMdxLayerAlphas(
    const MdxModel& model, const MdxGeosetAlphaRequest& request);
// KMTF整数None(0)离散通用预览参考；不是原版选键等价证明。
[[nodiscard]] Result<std::vector<std::uint32_t>> SampleMdxLayerTextureIds(
    const MdxModel& model, const MdxGeosetAlphaRequest& request);

// GEOA RGB 乘数。flags=0 返回白色；0x2 使用静态色或 KGAC；未知位/尾部明确拒绝。
// 颜色曲线采用通用插值并夹到 0..1；与 alpha 共用请求身份，不声称原版逐帧一致。
[[nodiscard]] Result<std::array<float, 3>> SampleMdxGeosetColor(
    const MdxModel& model, const MdxGeosetAlphaRequest& request);

// 结构越界、版本不是 800、索引/材质/贴图引用无效时整体失败，不发布部分模型。
[[nodiscard]] Result<MdxModel> ParseMdx(std::span<const std::byte> bytes, const MdxLimits& limits = {});

// 明确申请无控制器静态预览；不放宽 ParseMdx、不改源文件、不提供保存资格。
[[nodiscard]] Result<MdxModel> ParseMdxRigidStaticPreview(
    std::span<const std::byte> bytes, const MdxLimits& limits = {});

// 由对象数据的模型字段得到实际文件：去掉 .mdl/.mdx 后缀；variationCount > 1 时追加变体编号。
[[nodiscard]] std::string MdxModelFile(std::string_view modelField, std::uint32_t variation, std::uint32_t variationCount);

} // namespace War3Map::Render
