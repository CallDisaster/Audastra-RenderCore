// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/Render/MdxModel.h"
#include "MdxReadInternal.h"

// 二进制边界参考 GhostWolf 的 MDX Specifications（Node 与各类对象大小），不是移植实现：
// https://www.hiveworkshop.com/threads/mdx-specifications.240487/
namespace War3Map::Render::MdxReadInternal {
inline void CheckNodeBudget(const MdxModel& model, const MdxLimits& limits) {
    if (MdxNodeCount(model) >= limits.maxNodes) Fail("MDX_LIMIT", "公共节点总数超过上限。");
}
template <std::size_t Components>
MdxNodeTrack<Components> ReadNodeTrack(Reader& reader, const MdxLimits& limits,
    std::size_t& totalKeys, const char* what) {
    const auto count = reader.U32(what);
    const auto interpolation = reader.U32(what);
    const auto globalSequence = reader.U32(what);
    if (interpolation > static_cast<std::uint32_t>(MdxInterpolation::Bezier))
        Fail("MDX_VALUE_INVALID", std::string(what) + " 插值类型未知。");
    if (count > limits.maxTrackKeys || totalKeys > limits.maxTotalBoneTrackKeys
        || count > limits.maxTotalBoneTrackKeys - totalKeys)
        Fail("MDX_LIMIT", std::string(what) + " 按键数量超过上限。");
    const bool tangents = interpolation >= static_cast<std::uint32_t>(MdxInterpolation::Hermite);
    reader.Need(static_cast<std::size_t>(count) * (4 + 4 * Components * (tangents ? 3 : 1)), what);
    MdxNodeTrack<Components> track;
    track.interpolation = static_cast<MdxInterpolation>(interpolation);
    track.globalSequence = globalSequence;
    track.keys.reserve(count);
    for (std::uint32_t k = 0; k < count; ++k) {
        MdxNodeKey<Components> key;
        key.frame = reader.I32(what);
        for (auto& component : key.value) component = reader.F32(what);
        if (tangents) {
            for (auto& component : key.inTangent) component = reader.F32(what);
            for (auto& component : key.outTangent) component = reader.F32(what);
        }
        track.keys.push_back(key);
    }
    totalKeys += count;
    return track;
}

inline MdxNode ParseNode(Reader& reader, const MdxLimits& limits, std::size_t& totalKeys,
    std::size_t tailBytes, const char* what) {
    const auto nodeStart = reader.Position();
    const auto nodeBytes = reader.U32(what);
    // Node = 包含式大小 + 80 字节名称 + 3 个整数 + 可选轨迹；BONE 后另有两个 geoset 引用。
    if (nodeBytes < 96 || nodeBytes - 4 > reader.Remaining()
        || reader.Remaining() - (nodeBytes - 4) < tailBytes)
        Fail("MDX_SIZE_INVALID", std::string(what) + " 大小无效。");
    auto payload = reader.Until(nodeStart + nodeBytes, what);
    MdxNode node;
    node.name = payload.FixedString(80, "节点名称");
    node.objectId = payload.U32("节点对象 ID");
    node.parentId = payload.I32("节点父 ID");
    node.flags = payload.U32("节点标志");
    while (payload.Remaining() != 0) {
        if (payload.Remaining() < 16) Fail("MDX_SIZE_INVALID", "节点轨迹头不完整。");
        const auto tag = payload.Tag("节点轨迹");
        if (tag == "KGTR") {
            if (node.translation) Fail("MDX_VALUE_INVALID", "节点含重复 KGTR 轨迹。");
            node.translation = ReadNodeTrack<3>(payload, limits, totalKeys, "节点平移轨迹");
        } else if (tag == "KGRT") {
            if (node.rotation) Fail("MDX_VALUE_INVALID", "节点含重复 KGRT 轨迹。");
            node.rotation = ReadNodeTrack<4>(payload, limits, totalKeys, "节点旋转轨迹");
        } else if (tag == "KGSC") {
            if (node.scaling) Fail("MDX_VALUE_INVALID", "节点含重复 KGSC 轨迹。");
            node.scaling = ReadNodeTrack<3>(payload, limits, totalKeys, "节点缩放轨迹");
        } else Fail("MDX_TAG_MISMATCH", "节点含未知轨迹 " + tag + "。");
    }
    reader.Skip(nodeBytes - 4, what);
    return node;
}

inline void ParseBones(Reader reader, MdxModel& model, const MdxLimits& limits, std::size_t& totalKeys) {
    while (reader.Remaining() != 0) {
        if (model.bones.size() >= limits.maxBones) Fail("MDX_LIMIT", "骨骼数量超过上限。");
        CheckNodeBudget(model, limits);
        MdxBone bone;
        static_cast<MdxNode&>(bone) = ParseNode(reader, limits, totalKeys, 8, "骨骼节点");
        bone.kind = MdxNodeKind::Bone;
        bone.geosetId = reader.U32("骨骼 geoset ID");
        bone.geosetAnimationId = reader.U32("骨骼 geoset 动画 ID");
        model.bones.push_back(std::move(bone));
    }
}

inline void ParseHelpers(Reader reader, MdxModel& model, const MdxLimits& limits, std::size_t& totalKeys) {
    while (reader.Remaining() != 0) {
        if (model.helpers.size() >= limits.maxHelpers) Fail("MDX_LIMIT", "辅助节点数量超过上限。");
        CheckNodeBudget(model, limits);
        model.helpers.push_back(ParseNode(reader, limits, totalKeys, 0, "辅助节点"));
    }
}

inline void ParsePivots(Reader reader, MdxModel& model, const MdxLimits& limits) {
    if (reader.Remaining() % 12 != 0) Fail("MDX_SIZE_INVALID", "PIVT 大小不是 12 的整数倍。");
    const auto count = reader.Remaining() / 12;
    if (model.pivots.size() > limits.maxPivots || count > limits.maxPivots - model.pivots.size())
        Fail("MDX_LIMIT", "节点轴心数量超过上限。");
    for (std::size_t i = 0; i < count; ++i) {
        std::array<float, 3> pivot{};
        for (auto& axis : pivot) axis = reader.F32("节点轴心");
        model.pivots.push_back(pivot);
    }
}


// 有外层包含式长度的节点：负载只检查最小固定长度，之后按外层大小跳过。
// 公共 Node 由独立子 reader 限定，坏大小不能读进下一对象或下一个顶层块。
inline void ParseSizedNodes(Reader reader, MdxModel& model, const MdxLimits& limits,
    std::size_t& totalKeys, MdxNodeKind kind, std::size_t fixedTail, const char* tag) {
    while (reader.Remaining() != 0) {
        CheckNodeBudget(model, limits);
        const auto start = reader.Position();
        const auto size = reader.U32(tag);
        if (size < 4 + 96 + fixedTail || size - 4 > reader.Remaining())
            Fail("MDX_SIZE_INVALID", std::string(tag) + " 对象大小无效。");
        auto body = reader.Until(start + size, tag);
        auto node = ParseNode(body, limits, totalKeys, fixedTail, tag);
        node.kind = kind;
        model.otherNodes.push_back(std::move(node));
        reader.Skip(size - 4, tag);
    }
}

inline void ParseEvents(Reader reader, MdxModel& model, const MdxLimits& limits, std::size_t& totalKeys) {
    while (reader.Remaining() != 0) {
        CheckNodeBudget(model, limits);
        auto node = ParseNode(reader, limits, totalKeys, 0, "EVTS");
        node.kind = MdxNodeKind::Event;
        // EVTS 没有外层长度；KEVT 可缺省，存在时用计数跳过，不能套用 sized-node 规则。
        if (reader.PeekTag() == "KEVT") {
            reader.Tag("事件轨迹");
            const auto count = reader.Count(4, limits.maxTrackKeys, "事件按键");
            reader.U32("事件全局序列");
            reader.Skip(static_cast<std::size_t>(count) * 4, "事件按键");
        }
        model.otherNodes.push_back(std::move(node));
    }
}

inline void ParseCollisions(Reader reader, MdxModel& model, const MdxLimits& limits, std::size_t& totalKeys) {
    while (reader.Remaining() != 0) {
        CheckNodeBudget(model, limits);
        auto node = ParseNode(reader, limits, totalKeys, 4, "CLID");
        node.kind = MdxNodeKind::Collision;
        const auto type = reader.U32("碰撞形状类型");
        if (type > 3) Fail("MDX_COLLISION_TYPE_UNSUPPORTED", "无法确定未知碰撞形状的字节边界。");
        reader.Skip(type == 2 ? 12 : 24, "碰撞形状顶点");
        if (type == 2 || type == 3) reader.Skip(4, "碰撞形状半径");
        model.otherNodes.push_back(std::move(node));
    }
}
} // namespace War3Map::Render::MdxReadInternal
