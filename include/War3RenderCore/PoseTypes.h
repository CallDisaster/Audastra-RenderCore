// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace War3RenderCore {

using Vec3 = std::array<float, 3>;
using Quaternion = std::array<float, 4>;
using Matrix4 = std::array<float, 16>;
constexpr std::uint32_t NoIndex = UINT32_MAX;

// 所有跨边界的编号/计数均为定宽值；不得用原生指针充当身份。
// 这些描述符还不是 IPC 协议，传输时仍需显式编码与大小端/版本检查。
struct alignas(8) AssetIdentity {
    std::uint64_t value = 0;
    std::uint64_t generation = 0;
    bool operator==(const AssetIdentity&) const noexcept = default;
};
struct alignas(8) InstanceIdentity {
    std::uint64_t value = 0;
    std::uint64_t generation = 0;
    bool operator==(const InstanceIdentity&) const noexcept = default;
};

// 只用于加载阶段的进程内元数据，不可 memcpy 到插件或共享内存。
// 由宿主规范化虚拟路径、判定来源层并计算内容摘要；核心不自行访问文件。
struct AssetSource {
    std::string virtualPath;
    std::string sourceLayer;
    std::string contentDigest;
    bool operator==(const AssetSource&) const = default;
};

struct Range32 {
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
    bool operator==(const Range32&) const noexcept = default;
};

enum class PoseSource : std::uint32_t { Computed = 0, ExternalGroups = 1 };
enum class PoseQuality : std::uint32_t { PreviewApproximate = 0 };
enum class MatrixLayout : std::uint32_t { ColumnMajor3x4 = 0 };
enum class CoordinateSpace : std::uint32_t { ModelLocal = 0, NodeLocal = 1, World = 2 };

// 列向量、四列各三个 float：X 基列、Y 基列、Z 基列、平移列。
// m[column*3+row]；隐含最后一行为 0,0,0,1；48 字节紧密步长。
// 已完成组内平均。Computed/旧 ExternalGroups 是模型局部且未乘实例世界矩阵；
// 新 ExternalFrame 的空间和世界变换状态以输入及回执为准，不能重复乘世界矩阵。
// 原版 48 字节数据必须先证明布局、组映射和空间，不能只凭大小直接拷贝。
struct GroupMatrix {
    std::array<float, 12> values{};
    bool operator==(const GroupMatrix&) const noexcept = default;
};
struct ExternalGroupSlice {
    Range32 matrices;
    std::uint32_t strideBytes = 48;
    MatrixLayout layout = MatrixLayout::ColumnMajor3x4;
    CoordinateSpace space = CoordinateSpace::ModelLocal;
    std::uint32_t worldTransformApplied = 0;
};
struct LocalCamera {
    Vec3 eye{};
    Vec3 up{0, 0, 1};
};

// W1 全部拒绝。目标为 MDX objectId，不是数组序号；坐标空间与作用阶段不可省略。
// 将来支持时：Replace 替换该阶段的分量，Additive 在该阶段按明示空间叠加。
enum class OverrideKind : std::uint32_t { Translation = 0, Rotation = 1, Scale = 2, Matrix = 3 };
enum class OverrideMode : std::uint32_t { Replace = 0, Additive = 1 };
enum class OverrideStage : std::uint32_t { AfterTrackSampling = 0, AfterHierarchy = 1 };
struct BoneOverride {
    std::uint32_t objectId = NoIndex;
    OverrideKind kind = OverrideKind::Rotation;
    CoordinateSpace space = CoordinateSpace::NodeLocal;
    OverrideMode mode = OverrideMode::Replace;
    OverrideStage stage = OverrideStage::AfterTrackSampling;
    Matrix4 value{};
};

struct alignas(8) PoseRequest {
    InstanceIdentity instance;
    AssetIdentity asset;
    std::uint32_t assetIndex = 0; // 本次批量求值器持有的不可变资产表
    std::uint32_t sequenceIndex = 0;
    std::uint64_t sequenceElapsedMs = 0;
    std::uint64_t globalElapsedMs = 0;
    PoseSource source = PoseSource::Computed;
    std::uint32_t hasCamera = 0;
    LocalCamera camera;
    ExternalGroupSlice external;
    Range32 overrides;
};

// 内部保持与冻结参考实现相同的 4x4 计算顺序。worldMatrix 仍在模型局部空间，
// 其中 world 仅指“节点的父链已合成”，绝不是游戏/编辑器的实例世界变换。
struct NodePose {
    std::uint32_t objectId = NoIndex;
    std::int32_t parentId = -1;
    Vec3 pivot{};
    Vec3 translation{};
    Quaternion rotation{0, 0, 0, 1};
    Vec3 scaling{1, 1, 1};
    Matrix4 localMatrix{};
    Matrix4 worldMatrix{};
};
struct NodeLayout {
    std::uint32_t objectId = NoIndex;
    std::int32_t parentId = -1;
    std::uint32_t parentIndex = NoIndex; // 拓扑序号，父节点严格在前
    std::uint32_t flags = 0;
};
struct GeosetLayout {
    Range32 groups;   // 资产全部 geoset 按原顺序拼接后的矩阵片段
    Range32 vertices;
};

// 不用动态字符串报告热路径错误；CodeName/CodeMessage 为静态文本。
enum class Code : std::uint32_t {
    Ok = 0, PoseModelUnsupported, PoseSequenceInvalid, PoseSequenceUnsupported,
    PoseNodeInvalid, PoseNodeUnavailable, PoseParentInvalid, PoseParentUnavailable,
    PosePivotUnavailable, PoseNodeFlagsUnsupported, PoseLimit, PoseInterpolationUnsupported,
    PoseGlobalSequenceInvalid, PoseKeysInvalid, PoseBoundaryUnsupported, PoseNumberInvalid,
    PoseRotationInvalid, PoseCameraInvalid, PoseCameraFlagsUnsupported,
    GenericRotationPreview, RotationControlFallback, GenericCameraPreview,
    CameraStaticPreview, CameraAxisSingular,
    SkinTopologyIncomplete, SkinModelUnsupported, SkinGeosetInvalid,
    SkinGeometryUnsupported, SkinMatrixUnsupported, SkinNormalInvalid,
    GeoaReferenceInvalid, GeoaUninterpreted, GeoaNumberInvalid,
    GeoaInterpolationUnsupported, GeoaGlobalSequenceUnsupported, GeoaLimit,
    GeoaSequenceUnsupported, GeoaKeysInvalid, GeoaBoundaryUnsupported,
    AssetIdentityInvalid, AssetIdentityConflict, AssetStale, AssetIndexInvalid,
    InstanceIdentityInvalid, DuplicateInstance, CapacityExceeded, AllocationFailed,
    RequestInvalid, OverrideUnsupported, ExternalLayoutInvalid, ExternalRangeInvalid,
    ExternalCountInvalid, OutputRangeInvalid, VersionExhausted, PoseInheritanceSingular, GeoaColorInvalid,
    PoseZeroGlobalKeysUnsupported, GeoaZeroGlobalKeysUnsupported,
    PoseGlobalSpanUnsupported, GeoaGlobalSpanUnsupported,
    ExternalFrameContextInvalid, ExternalFrameVersionInvalid, ExternalFrameSourceInvalid,
    ExternalFrameStale, ExternalFrameLayoutInvalid, ExternalChannelCountInvalid, ExternalChannelRangeInvalid,
    MaterialAlphaUninterpreted, MaterialAlphaNumberInvalid, MaterialAlphaInterpolationUnsupported,
    MaterialAlphaGlobalSequenceUnsupported, MaterialAlphaKeysInvalid, MaterialAlphaBoundaryUnsupported,
    MaterialAlphaGlobalSpanUnsupported, MaterialAlphaLimit,
    MaterialTextureUninterpreted, MaterialTextureInterpolationUnsupported,
    MaterialTextureGlobalSequenceUnsupported, MaterialTextureKeysInvalid,
    MaterialTextureReferenceInvalid, MaterialTextureGlobalSpanUnsupported, MaterialTextureLimit,
};
enum class Severity : std::uint32_t { Warning = 0, Error = 1 };
enum class TrackKind : std::uint32_t { None = 0, Translation = 1, Rotation = 2, Scale = 3, MaterialAlpha = 4, MaterialTexture = 5 };
struct alignas(8) PoseDiagnostic {
    Code code = Code::Ok;
    Severity severity = Severity::Error;
    std::uint32_t objectId = NoIndex;
    TrackKind track = TrackKind::None;
    std::int64_t leftFrame = 0;
    std::int64_t rightFrame = 0;
};
[[nodiscard]] const char* CodeName(Code code) noexcept;
[[nodiscard]] const char* CodeMessage(Code code) noexcept;
// 按需在 UI/日志线程展开；它允许分配，不属于逐帧求值路径。
[[nodiscard]] std::string DescribeDiagnostic(const PoseDiagnostic& diagnostic);

struct alignas(8) PoseReceipt {
    InstanceIdentity instance;
    AssetIdentity asset;
    std::uint64_t evaluationEpoch = 0; // 宿主分配；重建求值器时必须换代
    std::uint64_t poseVersion = 0;     // 本求值器内单调递增；失败为 0
    std::uint32_t sequenceFrame = 0;
    PoseSource source = PoseSource::Computed;
    PoseQuality quality = PoseQuality::PreviewApproximate;
    Code error = Code::Ok;
    Range32 nodes;
    Range32 groups;
    Range32 alphas;
    Range32 diagnostics;
};
struct BatchOutcome {
    Code error = Code::Ok; // 批次级错误；实例级错误在各自回执中
    std::uint32_t instanceCount = 0;
    std::uint32_t failedInstances = 0;
};
// GroupsAndAlphas 不发布节点；完整 NodePose 仅作为单实例复用的计算暂存。
// maxNodes 仍限制整批计算节点总数（也决定最坏诊断容量），不是持久实例数。
enum class PoseOutputMode : std::uint32_t { FullNodes = 0, GroupsAndAlphas = 1 };
struct alignas(8) BatchLimits {
    std::uint32_t maxInstances = 64;
    std::uint32_t maxNodes = 65536;
    std::uint32_t maxGroups = 65536;
    std::uint32_t maxGeosets = 8192;
    std::uint64_t maxBytes = 64ull * 1024 * 1024;
    PoseOutputMode outputMode = PoseOutputMode::FullNodes;
    std::uint32_t reserved = 0;
};

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(sizeof(AssetIdentity) == 16 && sizeof(InstanceIdentity) == 16);
static_assert(sizeof(GroupMatrix) == 48 && sizeof(ExternalGroupSlice) == 24);
static_assert(sizeof(BatchLimits) == 32);
static_assert(sizeof(PoseRequest) == 120 && sizeof(PoseReceipt) == 96);
static_assert(sizeof(PoseDiagnostic) == 32 && std::is_trivially_copyable_v<PoseRequest>);

} // namespace War3RenderCore
