// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once

#include "War3MapCore/Render/MdxModel.h"
#include "MdxTestModel.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace MdxClassicNodeTests {
using namespace War3Map::Render;

inline void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

inline void WriteU32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    Check(offset <= bytes.size() && bytes.size() - offset >= 4, "测试字段超出夹具范围");
    std::memcpy(bytes.data() + offset, &value, 4);
}

inline auto Parsed(const std::vector<std::byte>& bytes) {
    auto result = ParseMdx(bytes);
    if (!result.Succeeded()) throw std::runtime_error("经典节点读取失败：" + result.diagnostics.front().message);
    return result;
}

inline void Rejected(const std::vector<std::byte>& bytes, const char* code, const MdxLimits& limits = {}) {
    const auto result = ParseMdx(bytes, limits);
    Check(!result.Succeeded() && !result.value && !result.diagnostics.empty(), "坏节点不能发布部分模型");
    if (result.diagnostics.front().code != code)
        throw std::runtime_error(std::string("节点拒绝码不符：") + code + " / " + result.diagnostics.front().code);
}

inline void Node(MdxTest::Writer& w, std::uint32_t id, std::uint32_t flags) {
    const auto node = w.Begin(); w.Fixed("Fixture node", 80);
    w.U32(id); w.U32(UINT32_MAX); w.U32(flags); w.EndInclusive(node);
}

struct CollisionFixture {
    MdxTest::Writer writer;
    std::size_t chunk = 0;
    std::vector<std::size_t> typeOffsets, payloadOffsets;
};

inline CollisionFixture Collisions(std::initializer_list<std::uint32_t> types) {
    CollisionFixture fixture;
    auto& w = fixture.writer;
    w.bytes = MdxTest::BuildTestMdx({.selectionGroup = 7, .selectionFlags = 4});
    w.Tag("CLID"); fixture.chunk = w.Begin();
    std::uint32_t id = 1;
    for (const auto type : types) {
        Node(w, id++, 0x2000);
        fixture.typeOffsets.push_back(w.bytes.size()); w.U32(type);
        fixture.payloadOffsets.push_back(w.bytes.size());
        // 原创不对称值；按文件含义逐项列出，不用生产读取器生成夹具。
        switch (type) {
        case 0: for (const auto value : {1.f, 2.f, 3.f, 4.f, 5.f, 6.f}) w.F32(value); break;
        case 1: for (const auto value : {1.f, 2.f, 3.f, 4.f, 7.f}) w.F32(value); break;
        case 2: for (const auto value : {1.f, 2.f, 3.f, 7.f}) w.F32(value); break;
        case 3: w.F32(11); w.F32(13); break;
        default: throw std::runtime_error("未声明的测试碰撞类型");
        }
    }
    w.EndChunk(fixture.chunk);
    return fixture;
}

inline void CollisionMetadata() {
    auto fixture = Collisions({0, 1, 2, 3});
    auto& w = fixture.writer;
    w.Tag("GLBS"); w.U32(4); w.U32(777); // 必须从真实 CLID 末尾继续读取。
    const auto parsed = Parsed(w.bytes);
    const auto& model = *parsed.value;
    Check(model.collisionShapes.size() == 4 && model.globalSequences == std::vector<std::uint32_t>{777},
        "四类碰撞和后一顶层块均完整读取");
    Check(model.geosets[0].selectionGroup == 7 && model.geosets[1].selectionFlags == 4, "GEOS 选择原值");
    constexpr std::array<std::uint32_t, 4> payloadBytes{24, 20, 16, 8};
    for (std::uint32_t type = 0; type < 4; ++type) {
        const auto& shape = model.collisionShapes[type];
        Check(shape.sourceIndex == type && shape.sourceType == type && shape.nodeId == type + 1
            && shape.payloadBytes == payloadBytes[type], "碰撞来源顺序、节点和原版负载长度");
        Check(std::memcmp(shape.rawWords.data(), fixture.writer.bytes.data() + fixture.payloadOffsets[type],
            payloadBytes[type]) == 0, "已读取负载的每个原始位均保留");
        Check(shape.support == (type == 0 || type == 2 ? MdxCollisionSupport::Supported
            : MdxCollisionSupport::GeometryUnsupported), "读取布局不扩大精准拾取资格");
        Check(std::all_of(shape.rawWords.begin() + payloadBytes[type] / 4, shape.rawWords.end(),
            [](auto raw) { return raw == 0; }), "负载外槽位没有借用下一记录");
    }
    const auto& box = model.collisionShapes[0];
    Check(box.layout == MdxCollisionLayout::BoxVertices24 && box.vertexCount == 2 && !box.hasRadius
        && box.vertices[0] == std::array<float, 3>{1, 2, 3} && box.vertices[1] == std::array<float, 3>{4, 5, 6},
        "type0 保留两个顶点");
    const auto& type1 = model.collisionShapes[1];
    Check(type1.layout == MdxCollisionLayout::Type1CenterHeightRadius20 && type1.vertexCount == 1
        && type1.vertices[0] == std::array<float, 3>{1, 2, 3} && type1.vertices[1] == std::array<float, 3>{}
        && type1.height == 4 && type1.hasRadius && type1.radius == 7
        && type1.rawWords[3] == std::bit_cast<std::uint32_t>(4.f), "type1 保留中心、高度、半径，不伪造第二顶点");
    const auto& sphere = model.collisionShapes[2];
    Check(sphere.layout == MdxCollisionLayout::SphereCenterRadius16 && sphere.vertexCount == 1
        && sphere.hasRadius && sphere.radius == 7 && sphere.vertices[0] == std::array<float, 3>{1, 2, 3},
        "type2 保留球心与半径");
    const auto& type3 = model.collisionShapes[3];
    Check(type3.layout == MdxCollisionLayout::Type3Scalars8 && type3.vertexCount == 0 && !type3.hasRadius
        && type3.scalarParameters == std::array<float, 2>{11, 13}
        && type3.vertices == std::array<std::array<float, 3>, 2>{}
        && type3.rawWords[1] == std::bit_cast<std::uint32_t>(13.f), "type3 只有两个原标量");

    const auto reordered = Parsed(Collisions({1, 0, 3, 2}).writer.bytes);
    Check(reordered.value->collisionShapes.size() == 4 && reordered.value->collisionShapes[1].sourceType == 0
        && reordered.value->collisionShapes[3].sourceType == 2 && reordered.value->collisionShapes[3].radius == 7,
        "type1/type3 不吞掉紧随其后的其他碰撞记录");
}

inline void CollisionBoundaries() {
    constexpr std::array<std::uint32_t, 4> sizes{24, 20, 16, 8};
    for (const auto type : {0u, 1u, 2u, 3u}) {
        auto fixture = Collisions({type});
        Check(Parsed(fixture.writer.bytes).value->collisionShapes.size() == 1, "单碰撞精确结束于文件末尾");
        auto nonfinite = fixture.writer.bytes;
        const auto lastWord = sizes[type] / 4 - 1;
        WriteU32(nonfinite, fixture.payloadOffsets[0] + lastWord * 4, 0x7fc01234);
        const auto preserved = Parsed(nonfinite);
        Check(preserved.value->collisionShapes[0].support == MdxCollisionSupport::NonFinite
            && preserved.value->collisionShapes[0].rawWords[lastWord] == 0x7fc01234,
            "非有限碰撞原位保留，但不赋予命中资格");

        auto& w = fixture.writer;
        w.bytes.pop_back();
        w.EndChunk(fixture.chunk); // 顶层块完整，但内部负载缺一个字节。
        Rejected(w.bytes, "MDX_TRUNCATED");
        w.Tag("GLBS"); w.U32(4); w.U32(777);
        Rejected(w.bytes, "MDX_TRUNCATED"); // 不能借下一顶层块补齐。
    }
    auto height = Collisions({1});
    WriteU32(height.writer.bytes, height.payloadOffsets[0] + 12, 0x7f800000);
    Check(Parsed(height.writer.bytes).value->collisionShapes[0].support == MdxCollisionSupport::NonFinite,
        "type1 高度也参与有限值资格检查");
    auto sphere = Collisions({2});
    WriteU32(sphere.writer.bytes, sphere.payloadOffsets[0] + 12, std::bit_cast<std::uint32_t>(-1.f));
    Check(Parsed(sphere.writer.bytes).value->collisionShapes[0].support == MdxCollisionSupport::InvalidRadius,
        "负球半径仍不可用于碰撞");
    auto unknown = Collisions({0});
    WriteU32(unknown.writer.bytes, unknown.typeOffsets[0], 4);
    Rejected(unknown.writer.bytes, "MDX_COLLISION_TYPE_UNSUPPORTED");
    auto trailing = Collisions({3});
    trailing.writer.F32(99); trailing.writer.EndChunk(trailing.chunk);
    // 四字节残片已足够读取下一节点的 size，但该 size 超出 CLID 边界；不是缺少 size 字段。
    Rejected(trailing.writer.bytes, "MDX_SIZE_INVALID"); // 额外残片不是隐式填充。
    MdxLimits limits; limits.maxNodes = 1; // 原夹具已有一个 BONE。
    Rejected(Collisions({1}).writer.bytes, "MDX_LIMIT", limits);
}

struct LightFixture {
    MdxTest::Writer writer;
    std::size_t chunk = 0, chunkEnd = 0;
    std::vector<std::size_t> recordOffsets, typeOffsets, trackOffsets;
};

inline LightFixture Lights(std::initializer_list<std::uint32_t> types, bool tracks, bool withGeometry = false) {
    LightFixture fixture;
    auto& w = fixture.writer;
    if (withGeometry) w.bytes = MdxTest::BuildTestMdx();
    else { w.Tag("MDLX"); w.Tag("VERS"); w.U32(4); w.U32(800); }
    w.Tag("LITE"); fixture.chunk = w.Begin();
    std::uint32_t id = withGeometry ? 1u : 0u;
    for (const auto type : types) {
        const auto record = w.Begin(); fixture.recordOffsets.push_back(record);
        Node(w, id++, 0x200);
        fixture.typeOffsets.push_back(w.bytes.size()); w.U32(type);
        for (const auto value : {16.f, 96.f, .125f, .25f, .5f, 2.f, .25f, .5f, .75f, .125f}) w.F32(value);
        if (tracks) {
            fixture.trackOffsets.push_back(w.bytes.size());
            w.Tag("KLAI"); w.U32(2); w.U32(1); w.U32(UINT32_MAX);
            w.U32(0); w.F32(1); w.U32(100); w.F32(3);
            w.Tag("KLAC"); w.U32(1); w.U32(0); w.U32(UINT32_MAX);
            w.U32(100); w.F32(.75f); w.F32(.5f); w.F32(.25f);
        }
        w.EndInclusive(record);
    }
    w.EndChunk(fixture.chunk); fixture.chunkEnd = w.bytes.size();
    if (!withGeometry) {
        w.Tag("PIVT"); const auto pivots = w.Begin();
        for (std::uint32_t node = 0; node < id; ++node) { w.F32(0); w.F32(0); w.F32(0); }
        w.EndChunk(pivots);
    }
    return fixture;
}

inline void LightTypesAndTracks() {
    const auto fixture = Lights({0, 512, 1, 2, UINT32_MAX}, true, true);
    const auto parsed = Parsed(fixture.writer.bytes);
    const auto& model = *parsed.value;
    Check(model.lights.size() == 5 && model.otherNodes.size() == 5 && model.geosets.size() == 2,
        "非标准光源与原模型几何、节点一起保留");
    constexpr std::array<std::uint32_t, 5> types{0, 512, 1, 2, UINT32_MAX};
    for (std::size_t i = 0; i < types.size(); ++i) {
        const auto& light = model.lights[i];
        Check(light.type == types[i] && model.otherNodes[i].objectId == i + 1
            && model.otherNodes[i].kind == MdxNodeKind::Light, "完整类型与公共节点身份保留");
        Check(light.color == std::array<float, 3>{.5f, .25f, .125f} && light.intensity == 2
            && light.intensityKeys.size() == 2 && light.intensityKeys[1].frame == 100
            && light.intensityKeys[1].value == 3 && light.colorKeys.size() == 1
            && light.colorKeys[0].rgb == std::array<float, 3>{.25f, .5f, .75f}, "类型不改变静态值与尾部轨迹读取");
    }
    Check(parsed.diagnostics.size() == 2, "仅非标准类型逐条产生有界警告");
    for (std::size_t i = 0; i < 2; ++i) {
        const auto index = i == 0 ? 1u : 4u;
        const auto& diagnostic = parsed.diagnostics[i];
        Check(diagnostic.code == "MDX_LIGHT_TYPE_NONSTANDARD"
            && diagnostic.severity == War3Map::DiagnosticSeverity::Warning && diagnostic.origin
            && diagnostic.origin->offset == fixture.typeOffsets[index] && diagnostic.origin->length == 4
            && diagnostic.message.find("light=" + std::to_string(index)) != std::string::npos
            && diagnostic.message.find("type=" + std::to_string(types[index])) != std::string::npos,
            "警告绑定真实来源光源、类型与四字节字段偏移");
    }
    const auto standalone = Parsed(Lights({512}, false).writer.bytes);
    Check(standalone.value->lights[0].type == 512 && standalone.value->skinningTopologyComplete,
        "无尾轨迹的非标准光源也保持完整节点拓扑");
}

inline void LightBoundaries() {
    const auto fixture = Lights({512}, true);
    auto bad = fixture.writer.bytes;
    WriteU32(bad, fixture.typeOffsets[0] + 12, 0x7fc01234);
    Rejected(bad, "MDX_NUMBER_INVALID"); // 非标准类型不豁免颜色有限值。
    bad = fixture.writer.bytes; WriteU32(bad, fixture.trackOffsets[0] + 20, 0x7f800000);
    Rejected(bad, "MDX_NUMBER_INVALID"); // 也不豁免轨迹值。
    bad = fixture.writer.bytes; WriteU32(bad, fixture.trackOffsets[0] + 8, 4);
    Rejected(bad, "MDX_VALUE_INVALID");
    bad = fixture.writer.bytes; WriteU32(bad, fixture.trackOffsets[0], 0x5a5a5a5a);
    Rejected(bad, "MDX_TAG_MISMATCH");
    bad = fixture.writer.bytes; WriteU32(bad, fixture.recordOffsets[0], 143);
    Rejected(bad, "MDX_SIZE_INVALID");
    bad = fixture.writer.bytes; WriteU32(bad, fixture.recordOffsets[0], UINT32_MAX);
    Rejected(bad, "MDX_SIZE_INVALID");

    auto shortTrack = fixture.writer;
    shortTrack.bytes.resize(fixture.chunkEnd - 1);
    shortTrack.EndInclusive(fixture.recordOffsets[0]); shortTrack.EndChunk(fixture.chunk);
    Rejected(shortTrack.bytes, "MDX_TRUNCATED");
    shortTrack.Tag("GLBS"); shortTrack.U32(4); shortTrack.U32(777);
    Rejected(shortTrack.bytes, "MDX_TRUNCATED");

    MdxLimits limits; limits.maxMaterials = 1;
    Rejected(Lights({512, 512}, false).writer.bytes, "MDX_LIMIT", limits);
    limits = {}; limits.maxNodes = 1;
    Rejected(Lights({512, 512}, false).writer.bytes, "MDX_LIMIT", limits);
    limits = {}; limits.maxTrackKeys = 1;
    Rejected(fixture.writer.bytes, "MDX_LIMIT", limits);
}

inline void Run() {
    CollisionMetadata();
    CollisionBoundaries();
    LightTypesAndTracks();
    LightBoundaries();
}

} // namespace MdxClassicNodeTests
