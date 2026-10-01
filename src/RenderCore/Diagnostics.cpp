// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3RenderCore/PoseTypes.h"

namespace War3RenderCore {
namespace {
struct DiagnosticText { const char* code; const char* message; };
DiagnosticText Text(Code code) noexcept {
    switch (code) {
    case Code::Ok: return {"OK", "成功。"};
    case Code::PoseModelUnsupported: return {"MDX_POSE_MODEL_UNSUPPORTED", "姿态求值只接受有界的经典 MDX 800 模型。"};
    case Code::PoseSequenceInvalid: return {"MDX_POSE_SEQUENCE_INVALID", "请求的动作序号不存在。"};
    case Code::PoseSequenceUnsupported: return {"MDX_POSE_SEQUENCE_UNSUPPORTED", "动作区间或循环标志尚不支持。"};
    case Code::PoseNodeInvalid: return {"MDX_POSE_NODE_INVALID", "BONE/HELP 节点 ID 重复或为无效哨兵。"};
    case Code::PoseNodeUnavailable: return {"MDX_POSE_NODE_UNAVAILABLE", "请求的 BONE/HELP 节点不存在。"};
    case Code::PoseParentInvalid: return {"MDX_POSE_PARENT_INVALID", "节点父 ID 无效或父链形成环。"};
    case Code::PoseParentUnavailable: return {"MDX_POSE_PARENT_UNAVAILABLE", "节点父 ID 指向未解析的节点类型。"};
    case Code::PosePivotUnavailable: return {"MDX_POSE_PIVOT_UNAVAILABLE", "节点缺少按 objectId 对齐的 PIVT 轴心。"};
    case Code::PoseInheritanceSingular: return {"MDX_POSE_INHERITANCE_SINGULAR", "剥离继承变换时基列退化，无法确定有限姿势。"};
    case Code::PoseNodeFlagsUnsupported: return {"MDX_POSE_NODE_FLAGS_UNSUPPORTED", "节点含未实现的继承或变换标志。"};
    case Code::PoseLimit: return {"MDX_POSE_LIMIT", "节点轨迹按键超过读取器硬上限。"};
    case Code::PoseInterpolationUnsupported: return {"MDX_POSE_INTERPOLATION_UNSUPPORTED", "未知节点轨迹插值类型。"};
    case Code::PoseGlobalSequenceInvalid: return {"MDX_POSE_GLOBAL_SEQUENCE_INVALID", "节点轨迹引用不存在的全局序列。"};
    case Code::PoseZeroGlobalKeysUnsupported: return {"MDX_POSE_ZERO_GLOBAL_KEYS_UNSUPPORTED", "零周期全局轨迹有多个键但没有0帧键，边界取键规则尚未支持。"};
    case Code::PoseGlobalSpanUnsupported: return {"MDX_POSE_GLOBAL_SPAN_UNSUPPORTED", "全局轨迹归零后的帧跨度超出已确认的有符号范围。"};
    case Code::PoseKeysInvalid: return {"MDX_POSE_KEYS_INVALID", "本次节点采样触及逆序帧区间；未实现原版逆序缓存语义，同帧按源序保留。"};
    case Code::PoseBoundaryUnsupported: return {"MDX_POSE_BOUNDARY_UNSUPPORTED", "轨迹接缝超出已确认的有符号时间范围，尚未模拟原版整数回绕。"};
    case Code::PoseNumberInvalid: return {"MDX_POSE_NUMBER_INVALID", "节点轨迹或矩阵产生非有限数值。"};
    case Code::PoseRotationInvalid: return {"MDX_POSE_ROTATION_INVALID", "节点四元数长度为零或超出可求值范围。"};
    case Code::PoseCameraInvalid: return {"MDX_POSE_CAMERA_INVALID", "模型局部相机超出有界预览范围，或方向退化。"};
    case Code::PoseCameraFlagsUnsupported: return {"MDX_POSE_CAMERA_FLAGS_UNSUPPORTED", "相机节点同时锁定多个轴，通用预览没有确定的自由度。"};
    case Code::GenericRotationPreview: return {"MDX_POSE_GENERIC_ROTATION_PREVIEW", "使用通用四元数 squad 预览，非原版等价证明。"};
    case Code::RotationControlFallback: return {"MDX_POSE_ROTATION_CONTROL_FALLBACK", "曲线控制四元数无效；仅该段退为端点球面插值预览。"};
    case Code::GenericCameraPreview: return {"MDX_POSE_GENERIC_CAMERA_PREVIEW", "使用有界相机朝向通用算法预览，非原版等价证明。"};
    case Code::CameraStaticPreview: return {"MDX_POSE_CAMERA_STATIC_PREVIEW", "无相机输入，暂以静态节点姿态预览。"};
    case Code::CameraAxisSingular: return {"MDX_POSE_CAMERA_AXIS_SINGULAR", "视线平行锁定轴，保留零角度通用预览。"};
    case Code::SkinTopologyIncomplete: return {"MDX_SKIN_TOPOLOGY_INCOMPLETE", "模型骨骼/矩阵拓扑未闭合。"};
    case Code::SkinModelUnsupported: return {"MDX_SKIN_MODEL_UNSUPPORTED", "只接受有界的经典 MDX 800 模型。"};
    case Code::SkinGeosetInvalid: return {"MDX_SKIN_GEOSET_INVALID", "请求的 geoset 序号不存在。"};
    case Code::SkinGeometryUnsupported: return {"MDX_SKIN_GEOMETRY_UNSUPPORTED", "geoset 顶点、法线或矩阵组数据不完整，或组引用越界。"};
    case Code::SkinMatrixUnsupported: return {"MDX_SKIN_MATRIX_UNSUPPORTED", "节点姿态矩阵不是可支持的仿射矩阵。"};
    case Code::SkinNormalInvalid: return {"MDX_SKIN_NORMAL_INVALID", "输入法线为零或姿态法线无法归一化。"};
    case Code::GeoaReferenceInvalid: return {"MDX_GEOA_REFERENCE_INVALID", "geoset 或动作序号不存在。"};
    case Code::GeoaUninterpreted: return {"MDX_GEOA_UNINTERPRETED", "GEOA 含未支持的标志、轨迹或尾部数据。"};
    case Code::GeoaColorInvalid: return {"MDX_GEOA_COLOR_INVALID", "GEOA 活动颜色必须为有限的 0..1 RGB 分量。"};
    case Code::GeoaNumberInvalid: return {"MDX_GEOA_NUMBER_INVALID", "GEOA 静态透明度或按键透明度不是有限数值。"};
    case Code::GeoaInterpolationUnsupported: return {"MDX_GEOA_INTERPOLATION_UNSUPPORTED", "GEOA 插值类型未知。"};
    case Code::GeoaGlobalSequenceUnsupported: return {"MDX_GEOA_GLOBAL_SEQUENCE_UNSUPPORTED", "GEOA 引用的全局序列不存在。"};
    case Code::GeoaZeroGlobalKeysUnsupported: return {"MDX_GEOA_ZERO_GLOBAL_KEYS_UNSUPPORTED", "零周期 GEOA 全局轨迹有多个键但没有0帧键，边界取键规则尚未支持。"};
    case Code::GeoaGlobalSpanUnsupported: return {"MDX_GEOA_GLOBAL_SPAN_UNSUPPORTED", "GEOA 全局轨迹归零后的帧跨度超出已确认的有符号范围。"};
    case Code::GeoaLimit: return {"MDX_GEOA_LIMIT", "KGAO 按键超过读取器硬上限。"};
    case Code::GeoaSequenceUnsupported: return {"MDX_GEOA_SEQUENCE_UNSUPPORTED", "动作区间或循环标志尚不支持。"};
    case Code::GeoaKeysInvalid: return {"MDX_GEOA_KEYS_INVALID", "KGAO 按键帧必须严格递增。"};
    case Code::GeoaBoundaryUnsupported: return {"MDX_GEOA_BOUNDARY_UNSUPPORTED", "GEOA 当前帧超出可确定的真实按键区间。"};
    case Code::AssetIdentityInvalid: return {"RENDER_CORE_ASSET_IDENTITY_INVALID", "资产编号或来源身份缺失/超限，不能仅按路径识别资产。"};
    case Code::AssetIdentityConflict: return {"RENDER_CORE_ASSET_IDENTITY_CONFLICT", "同一资产编号与代次绑定了不同编译资产；请更新代次。"};
    case Code::AssetStale: return {"RENDER_CORE_ASSET_STALE", "请求的资产身份或代次与求值器持有的资产不符。"};
    case Code::AssetIndexInvalid: return {"RENDER_CORE_ASSET_INDEX_INVALID", "请求的资产表序号不存在。"};
    case Code::InstanceIdentityInvalid: return {"RENDER_CORE_INSTANCE_IDENTITY_INVALID", "实例编号为无效零值。"};
    case Code::DuplicateInstance: return {"RENDER_CORE_DUPLICATE_INSTANCE", "同一批中出现重复的实例身份与代次。"};
    case Code::CapacityExceeded: return {"RENDER_CORE_CAPACITY_EXCEEDED", "请求超过预热容量或有界存储预算；不能逐帧自动扩容。"};
    case Code::AllocationFailed: return {"RENDER_CORE_ALLOCATION", "加载或预热分配失败，没有发布部分资产/求值器。"};
    case Code::RequestInvalid: return {"RENDER_CORE_REQUEST_INVALID", "求值器、请求标志、来源类型或输入/输出别名无效。"};
    case Code::OverrideUnsupported: return {"RENDER_CORE_BONE_OVERRIDE_UNSUPPORTED", "W1 不支持骨骼覆盖；请求已明确拒绝。"};
    case Code::ExternalLayoutInvalid: return {"RENDER_CORE_EXTERNAL_LAYOUT_INVALID", "外部矩阵必须为模型局部、48 字节列主序 3x4，且未乘世界变换。"};
    case Code::ExternalRangeInvalid: return {"RENDER_CORE_EXTERNAL_RANGE_INVALID", "外部矩阵片段超出输入跨度。"};
    case Code::ExternalCountInvalid: return {"RENDER_CORE_EXTERNAL_COUNT_INVALID", "外部矩阵数量与编译资产的全部矩阵组数量不符。"};
    case Code::OutputRangeInvalid: return {"RENDER_CORE_OUTPUT_RANGE_INVALID", "CPU 预览输出跨度与 geoset 顶点数量不符。"};
    case Code::VersionExhausted: return {"RENDER_CORE_VERSION_EXHAUSTED", "姿势版本号已耗尽；宿主须换 evaluationEpoch 并重建求值器。"};
    case Code::ExternalFrameContextInvalid: return {"RENDER_CORE_EXTERNAL_FRAME_CONTEXT_INVALID", "宿主必须独立提供非零来源 epoch、帧版本和已支持的来源/合同版本。"};
    case Code::ExternalFrameVersionInvalid: return {"RENDER_CORE_EXTERNAL_FRAME_VERSION_INVALID", "外部已求值帧的合同版本不受支持。"};
    case Code::ExternalFrameSourceInvalid: return {"RENDER_CORE_EXTERNAL_FRAME_SOURCE_INVALID", "外部已求值帧的来源未知或与宿主指定来源不符。"};
    case Code::ExternalFrameStale: return {"RENDER_CORE_EXTERNAL_FRAME_STALE", "外部已求值帧的来源 epoch 或帧版本与宿主当前帧不符。"};
    case Code::ExternalFrameLayoutInvalid: return {"RENDER_CORE_EXTERNAL_FRAME_LAYOUT_INVALID", "已求值帧必须为48字节列主序3x4，并明确使用ModelLocal/未乘世界矩阵或World/已乘世界矩阵。"};
    case Code::ExternalChannelCountInvalid: return {"RENDER_CORE_EXTERNAL_CHANNEL_COUNT_INVALID", "外部alpha或RGB数量与编译资产的全部geoset数量不符，不允许补默认值。"};
    case Code::ExternalChannelRangeInvalid: return {"RENDER_CORE_EXTERNAL_CHANNEL_RANGE_INVALID", "外部alpha或RGB片段超出输入跨度。"};
    case Code::MaterialAlphaUninterpreted: return {"MDX_MATERIAL_ALPHA_UNINTERPRETED", "材质层含未知或重复轨迹；未使用静态alpha假成功。"};
    case Code::MaterialAlphaNumberInvalid: return {"MDX_MATERIAL_ALPHA_NUMBER_INVALID", "KMTA静态值、键、切线或求值结果不是有限数。"};
    case Code::MaterialAlphaInterpolationUnsupported: return {"MDX_MATERIAL_ALPHA_INTERPOLATION_UNSUPPORTED", "KMTA插值类型不支持。"};
    case Code::MaterialAlphaGlobalSequenceUnsupported: return {"MDX_MATERIAL_ALPHA_GLOBAL_SEQUENCE_UNSUPPORTED", "KMTA全局序列引用不存在。"};
    case Code::MaterialAlphaKeysInvalid: return {"MDX_MATERIAL_ALPHA_KEYS_INVALID", "KMTA键未严格递增；不排序或删除原键。"};
    case Code::MaterialAlphaBoundaryUnsupported: return {"MDX_MATERIAL_ALPHA_BOUNDARY_UNSUPPORTED", "KMTA采样边界尚不支持，不补默认透明度。"};
    case Code::MaterialAlphaGlobalSpanUnsupported: return {"MDX_MATERIAL_ALPHA_GLOBAL_SPAN_UNSUPPORTED", "KMTA归零帧跨度超过已界定有符号范围。"};
    case Code::MaterialAlphaLimit: return {"MDX_MATERIAL_ALPHA_LIMIT", "KMTA轨迹或层数量超过有界预算。"};
    }
    return {"RENDER_CORE_UNKNOWN_DIAGNOSTIC", "未知诊断编号。"};
}
} // namespace

const char* CodeName(Code code) noexcept { return Text(code).code; }
const char* CodeMessage(Code code) noexcept { return Text(code).message; }

std::string DescribeDiagnostic(const PoseDiagnostic& diagnostic) {
    const auto object = "objectId=" + std::to_string(diagnostic.objectId);
    const char* track = diagnostic.track == TrackKind::Translation ? "KGTR"
        : diagnostic.track == TrackKind::Scale ? "KGSC" : "KGRT";
    switch (diagnostic.code) {
    case Code::GenericRotationPreview:
        return std::string(track) + " " + object + " 在 " + std::to_string(diagnostic.leftFrame) + ".."
            + std::to_string(diagnostic.rightFrame) + " 帧之间使用通用四元数 squad 预览，非原版等价证明。";
    case Code::RotationControlFallback:
        return std::string(track) + " " + object + " 的曲线控制四元数无效；仅该段退为端点球面插值预览。";
    case Code::GenericCameraPreview:
        return object + " 使用有界相机朝向通用算法预览，非原版等价证明。";
    case Code::CameraStaticPreview:
        return object + " 无相机输入，暂以静态节点姿态预览。";
    case Code::CameraAxisSingular:
        return object + " 视线平行锁定轴，保留零角度通用预览。";
    default: return CodeMessage(diagnostic.code);
    }
}
} // namespace War3RenderCore
