// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3MapCore/Render/MdxModel.h"
#include <bit>
#include <cmath>

namespace War3Map::Render::MdxNoControllerStaticInternal {

// 保守子集：1.27a先判断整模型是否需要控制器，再决定是否读取节点轴心。
// 不接纳当前解析器无法完整保留的控制器块，即使其中的数组恰好为空。
inline bool AllowedChunk(std::string_view tag) noexcept {
    return tag == "VERS" || tag == "MODL" || tag == "SEQS" || tag == "MTLS"
        || tag == "TEXS" || tag == "GEOS" || tag == "GEOA" || tag == "BONE"
        || tag == "HELP" || tag == "PIVT";
}

inline bool StaticAnimation(const MdxGeosetAnimation& animation) noexcept {
    return !animation.hasAlphaTrack && !animation.hasColorTrack && !animation.hasUninterpretedData
        && animation.alphaKeys.empty() && animation.colorKeys.empty() && (animation.flags & ~2u) == 0;
}

// 不把单个节点无TRS作为资格。这里每次重新检查整个可变公开模型。
inline const char* FieldIssue(const MdxModel& model) noexcept {
    const MdxLimits hard;
    if (model.version != 800 || model.rigidStaticPreviewSource || !model.skinningTopologyComplete
        || model.bones.empty() || model.bones.size() > hard.maxBones || model.helpers.size() > hard.maxHelpers
        || MdxNodeCount(model) > hard.maxNodes || model.pivots.size() > hard.maxPivots
        || model.geosets.empty() || model.geosets.size() > hard.maxGeosets
        || model.materials.size() > hard.maxMaterials || model.sequences.size() > hard.maxSequences
        || !model.otherNodes.empty() || !model.lights.empty() || !model.collisionShapes.empty()
        || !model.premEmitters.empty() || !model.globalSequences.empty() || !model.textureAnimations.empty()
        || !model.textureAnimationIssue.empty() || model.skippedChunks != 0)
        return "整模型包含其它控制器对象、未解释块，或拓扑/数量资格不完整";
    bool nodesQualified = true;
    ForEachMdxNode(model, [&](const MdxNode& node) {
        // 已取证的高位仅在此静态资格中保留；普通姿态的flags门禁不放宽。
        if ((node.kind != MdxNodeKind::Bone && node.kind != MdxNodeKind::Helper)
            || node.objectId == UINT32_MAX || node.objectId >= model.pivots.size() || node.parentId < -1
            || node.translation || node.rotation || node.scaling || (node.flags & ~0x80000100u) != 0)
            nodesQualified = false;
    });
    if (!nodesQualified) return "节点含TRS轨迹、继承/朝向/未验证标志，或轴心索引不完整";
    for (const auto& sequence : model.sequences) if (!sequence.Usable()) return "动作区间或标志不可用";
    for (const auto& material : model.materials) {
        if (material.hasUninterpretedData || material.layers.size() > hard.maxLayersPerMaterial)
            return "材质含未解释数据或超出数量上限";
        for (const auto& layer : material.layers) {
            if (layer.animatedAlpha || layer.animatedTexture || layer.hasUninterpretedData
                || !layer.alphaKeys.empty() || !layer.textureKeys.empty() || !layer.duplicateTextureTracks.empty()
                || layer.textureAnimationId != UINT32_MAX || layer.coordId != 0)
                return "材质层含透明度/贴图/坐标动画，或未解释数据";
        }
    }
    for (const auto& geoset : model.geosets) {
        if (geoset.uninterpretedTailBytes != 0 || geoset.skippedPrimitiveGroups != 0
            || (geoset.animation && !StaticAnimation(*geoset.animation)))
            return "几何体含GEOA轨迹、未验证标志或未解释图元数据";
    }
    return nullptr;
}

inline bool FieldsQualified(const MdxModel& model) noexcept { return FieldIssue(model) == nullptr; }

inline bool Qualified(const MdxModel& model) noexcept {
    if (!model.noControllerStaticSource || !FieldsQualified(model)) return false;
    const auto& source = *model.noControllerStaticSource;
    if (source.Nodes().size() != MdxNodeCount(model) || source.PivotBits().size() != model.pivots.size()) return false;
    bool matches = true;
    std::size_t index = 0;
    ForEachMdxNode(model, [&](const MdxNode& node) {
        const auto& original = source.Nodes()[index++];
        if (node.objectId != original.objectId || node.parentId != original.parentId
            || node.flags != original.flags || node.kind != original.kind) matches = false;
    });
    for (std::size_t pivot = 0; pivot < model.pivots.size(); ++pivot)
        for (std::size_t axis = 0; axis < 3; ++axis)
            if (std::bit_cast<std::uint32_t>(model.pivots[pivot][axis]) != source.PivotBits()[pivot][axis]) return false;
    return matches;
}

inline bool NeedsCompatibility(const MdxModel& model) noexcept {
    for (const auto& pivot : model.pivots)
        for (const auto axis : pivot) if (!std::isfinite(axis)) return true;
    bool highFlag = false;
    ForEachMdxNode(model, [&](const MdxNode& node) { highFlag = highFlag || (node.flags & 0x80000000u) != 0; });
    return highFlag;
}

inline Diagnostic Warning(const MdxModel& model) {
    std::size_t nonFinite = 0, highFlags = 0;
    for (const auto& pivot : model.pivots)
        for (const auto axis : pivot) if (!std::isfinite(axis)) ++nonFinite;
    ForEachMdxNode(model, [&](const MdxNode& node) { if ((node.flags & 0x80000000u) != 0) ++highFlags; });
    return {DiagnosticSeverity::Warning, "Mdx.Static", "MDX_NO_CONTROLLER_STATIC",
        "整模型符合原版无动画控制器静态路径；矩阵不使用PIVT，保留原始轴心与节点标志。非有限分量="
            + std::to_string(nonFinite) + "，高位标志节点=" + std::to_string(highFlags)
            + "；新增动画或动态对象后必须重新校验。",
        OriginSpan{{}, model.noControllerStaticSource->PivotByteOffset(), model.pivots.size() * 12, {}}};
}

} // namespace War3Map::Render::MdxNoControllerStaticInternal
