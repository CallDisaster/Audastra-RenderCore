// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3MapCore/Render/MdxModel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace War3Map::Render {

// 只求一条 BONE/HELP 祖先链，不读取 GPU。序列时间从区间起点计，
// 全局序列使用独立时钟。样条和相机节点仅供“通用算法预览”，不声称原版等价。
struct MdxPoseCamera {
    // 视点与向上方向均在模型局部空间；调用方负责逆变换模型实例的世界相机。
    std::array<float, 3> eye{};
    std::array<float, 3> up{0, 0, 1};
};

struct MdxPoseRequest {
    std::size_t sequenceIndex = 0;
    std::uint64_t sequenceElapsedMs = 0;
    std::uint64_t globalElapsedMs = 0;
    std::uint32_t objectId = 0;
    std::optional<MdxPoseCamera> camera;
};

struct MdxPoseNode {
    std::uint32_t objectId = 0;
    std::int32_t parentId = -1;
    std::array<float, 3> pivot{};
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    std::array<float, 3> scaling{1, 1, 1};
    // 列主序、列向量；普通节点 world = parentWorld * T(translation+pivot) * R * S * T(-pivot)。
    // 相机节点的 worldMatrix 是覆盖父链朝向后的通用预览矩阵，localMatrix 仍保留静态分解。
    std::array<float, 16> localMatrix{};
    std::array<float, 16> worldMatrix{};
};

struct MdxPoseSample {
    std::uint32_t sequenceFrame = 0; // 含 SEQS 起点的绝对帧（毫秒）
    std::vector<MdxPoseNode> chain; // 根节点到请求 objectId，最多 BONE+HELP 总数
};

[[nodiscard]] Result<MdxPoseSample> SampleMdxNodePose(const MdxModel& model, const MdxPoseRequest& request);

// CPU 组矩阵蒙皮；函数名保留初版刚性接口的兼容性。每个 MTGC 组把 MATS 引用的
// BONE/HELP 仿射矩阵逐分量平均，GNDX 的每个顶点只引用一条组矩阵。法线使用同一矩阵的
// 3x3 部分并归一化（原版 CPU 内核不做归一化，故不宣称逐字节相同）。
// 返回模型局部空间姿态几何，复用 SampleMdxNodePose 的轴心、父链与时钟；GEOA 可见度
// 动画仍须调用方明确处理；通用算法预览诊断从 Result::diagnostics 返回。
struct MdxRigidSkinRequest {
    std::size_t geosetIndex = 0;
    std::size_t sequenceIndex = 0;
    std::uint64_t sequenceElapsedMs = 0;
    std::uint64_t globalElapsedMs = 0;
    std::optional<MdxPoseCamera> camera;
};

struct MdxRigidSkinnedGeoset {
    std::uint32_t sequenceFrame = 0;
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
};

[[nodiscard]] Result<MdxRigidSkinnedGeoset> SampleMdxRigidSkinnedGeoset(
    const MdxModel& model, const MdxRigidSkinRequest& request);

// 独立参考实现：重新核对无控制器来源，使用局部 identity；不改变普通骨骼采样规则。
[[nodiscard]] Result<MdxRigidSkinnedGeoset> SampleMdxRigidStaticPreviewGeoset(
    const MdxModel& model, const MdxRigidSkinRequest& request);

} // namespace War3Map::Render
