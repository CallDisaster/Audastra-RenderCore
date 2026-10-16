// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "MdxClassicNodeTests.h"
#include "War3MapCore/Render/MdxPose.h"
#include <cstring>

namespace MdxCompatibilityTests {
using namespace War3Map::Render;
using MdxTest::Writer;
using MdxClassicNodeTests::Check;
using MdxClassicNodeTests::WriteU32;
using MdxClassicNodeTests::Rejected;

inline std::uint32_t Word(const std::vector<std::byte>& bytes, std::size_t offset) {
    Check(offset <= bytes.size() && bytes.size() - offset >= 4, "兼容夹具字段越界");
    std::uint32_t value = 0; std::memcpy(&value, bytes.data() + offset, 4); return value;
}

struct Fixture {
    std::vector<std::byte> bytes;
    std::vector<std::size_t> nodeOffsets, geosetOffsets, normalOffsets;
    std::size_t pivots = 0, helper = 0, global = 0;
};

inline Fixture PremFixture() {
    Fixture result; Writer w;
    const auto base = MdxTest::BuildTestMdx();
    w.Tag("MDLX");
    for (std::size_t offset = 4; offset < base.size();) {
        const auto size = Word(base, offset + 4);
        if (std::memcmp(base.data() + offset, "PIVT", 4) != 0
            && std::memcmp(base.data() + offset, "UNKN", 4) != 0)
            w.Raw(base.data() + offset, size + 8);
        offset += size + 8;
    }
    const auto prem = [&] {
        w.Tag("PREM"); const auto chunk = w.Begin(), outer = w.Begin(), node = w.Begin();
        result.nodeOffsets.push_back(node);
        w.Fixed("Synthetic emitter", 80); w.U32(UINT32_MAX); w.U32(0); w.U32(0x9000);
        w.EndInclusive(node);
        w.F32(2); w.F32(-1); w.F32(0); w.F32(0);
        w.Fixed("Particles\\Synthetic.mdl", 260); w.F32(1); w.F32(3);
        w.EndInclusive(outer); w.EndChunk(chunk);
    };
    prem();
    // 文件次序混合不同块；不能按 bones/helpers/otherNodes 容器次序重建身份。
    w.Tag("HELP"); const auto helperChunk = w.Begin(); result.helper = w.Begin();
    w.Fixed("Child of first emitter", 80); w.U32(2); w.U32(1); w.U32(0);
    w.EndInclusive(result.helper); w.EndChunk(helperChunk);
    prem();
    w.Tag("PIVT"); w.U32(48); result.pivots = w.bytes.size();
    for (int index = 0; index < 12; ++index) w.F32(static_cast<float>(index));
    result.bytes = std::move(w.bytes); return result;
}

inline void PremIdentity() {
    const auto fixture = PremFixture(); const auto before = fixture.bytes;
    const auto parsed = MdxClassicNodeTests::Parsed(fixture.bytes); const auto& model = *parsed.value;
    Check(fixture.bytes == before, "读取不能更改原输入字节");
    Check(model.premEmitters.size() == 2 && model.skinningTopologyComplete, "哨兵节点须保持完整拓扑");
    Check(model.bones[0].objectId == 0 && model.helpers[0].objectId == 2 && model.helpers[0].parentId == 1,
        "已有节点身份与父引用不能改写");
    std::size_t warnings = 0;
    for (const auto& diagnostic : parsed.diagnostics) {
        if (diagnostic.code != "MDX_NODE_ID_BY_SOURCE_ORDER") continue;
        Check(diagnostic.origin && diagnostic.origin->length == 4
            && diagnostic.origin->offset == fixture.nodeOffsets[warnings] + 84, "编号诊断须定位原ID字段");
        ++warnings;
    }
    Check(warnings == 2, "每个兼容节点必须给出一条诊断");
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& emitter = model.premEmitters[index]; const auto& node = model.otherNodes[emitter.otherNodeIndex];
        Check(node.objectId == index * 2 + 1 && node.sourceObjectId == UINT32_MAX
            && node.sourceByteOffset == fixture.nodeOffsets[index] && emitter.nodeId == node.objectId
            && emitter.sourceIndex == index && emitter.support == MdxPremSupport::Supported,
            "PREM源字段、有效身份与来源索引必须分别保留");
        Check(Word(fixture.bytes, node.sourceByteOffset + 84) == UINT32_MAX, "不得把重建编号写回原文件");
    }
    // 参考姿态仍经过已有父链、轴心和轨迹验证，不以读取成功代替求值。
    const auto pose = SampleMdxNodePose(model, {0, 0, 0, 2});
    Check(pose.Succeeded(), "完整哨兵拓扑应可通过既有参考姿态检查");

    auto altered = fixture.bytes;
    WriteU32(altered, fixture.nodeOffsets[0] + 84, 0); Rejected(altered, "MDX_REFERENCE_INVALID");
    altered = fixture.bytes;
    WriteU32(altered, fixture.helper + 84, 9); Rejected(altered, "MDX_REFERENCE_INVALID");
    altered = fixture.bytes;
    altered.resize(fixture.pivots + 36); WriteU32(altered, fixture.pivots - 4, 36);
    Rejected(altered, "MDX_REFERENCE_INVALID");
    altered = fixture.bytes;
    altered.insert(altered.end(), {std::byte{'U'}, std::byte{'N'}, std::byte{'K'}, std::byte{'N'},
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}});
    Rejected(altered, "MDX_REFERENCE_INVALID");
    auto limits = MdxLimits{}; limits.maxNodes = 3; Rejected(fixture.bytes, "MDX_LIMIT", limits);
    altered = fixture.bytes;
    WriteU32(altered, fixture.helper + 88, 2); Rejected(altered, "MDX_REFERENCE_INVALID");

    // 无哨兵的原有模型不强制重编号；兼容规则不扩大到任意有限重复值。
    altered = fixture.bytes;
    WriteU32(altered, fixture.nodeOffsets[0] + 84, 1);
    WriteU32(altered, fixture.nodeOffsets[1] + 84, 3);
    const auto normal = MdxClassicNodeTests::Parsed(altered);
    Check(std::none_of(normal.diagnostics.begin(), normal.diagnostics.end(), [](const auto& d) {
        return d.code == "MDX_NODE_ID_BY_SOURCE_ORDER";
    }), "正常节点不能制造兼容警告");
}

inline Fixture NormalsFixture(std::uint32_t count) {
    Fixture result; Writer w;
    const auto base = MdxTest::BuildTestMdx(); w.Tag("MDLX");
    for (std::size_t offset = 4; offset < base.size();) {
        const auto size = Word(base, offset + 4);
        if (std::memcmp(base.data() + offset, "GEOS", 4) == 0) {
            w.Tag("GEOS"); const auto chunk = w.Begin();
            for (auto item = offset + 8; item < offset + 8 + size;) {
                const auto bytes = Word(base, item); const auto normals = item + 4 + 8 + 4 * 12;
                const auto target = w.bytes.size(); result.geosetOffsets.push_back(target);
                w.Raw(base.data() + item, normals - item);
                w.Tag("NRMS"); result.normalOffsets.push_back(w.bytes.size()); w.U32(count);
                for (std::uint32_t index = 0; index < count; ++index) { w.F32(0); w.F32(0); w.F32(1); }
                const auto tail = normals + 8 + 4 * 12;
                w.Raw(base.data() + tail, item + bytes - tail); w.EndInclusive(target); item += bytes;
            }
            w.EndChunk(chunk);
        } else w.Raw(base.data() + offset, size + 8);
        offset += size + 8;
    }
    result.bytes = std::move(w.bytes); return result;
}

inline void EmptyNormals() {
    const auto fixture = NormalsFixture(0); const auto parsed = MdxClassicNodeTests::Parsed(fixture.bytes);
    Check(parsed.value->geosets.size() == 2, "空法线不能吞掉后继GEOS对象");
    std::size_t warnings = 0;
    for (const auto& diagnostic : parsed.diagnostics) {
        if (diagnostic.code != "MDX_NORMALS_ABSENT") continue;
        Check(diagnostic.origin && diagnostic.origin->offset == fixture.normalOffsets[warnings]
            && diagnostic.origin->length == 4, "空法线诊断须定位原计数"); ++warnings;
    }
    Check(warnings == 2, "每个空法线GEOS必须给出一条诊断");
    for (const auto& geoset : parsed.value->geosets)
        Check(geoset.positions.size() == 4 && geoset.normals.empty(), "不能合成原文件没有的法线");
    Check(!SampleMdxRigidSkinnedGeoset(*parsed.value, {0, 0, 0, 0}).Succeeded(), "严格参考蒙皮仍拒绝空法线");
    Rejected(NormalsFixture(3).bytes, "MDX_VALUE_INVALID");
    Rejected(NormalsFixture(5).bytes, "MDX_VALUE_INVALID");
    auto shortened = fixture.bytes;
    WriteU32(shortened, fixture.geosetOffsets[0], 12);
    const auto rejected = ParseMdx(shortened);
    Check(!rejected.Succeeded() && !rejected.value, "GEOS对象不得借后继记录补足短负载");
    Check(rejected.diagnostics.front().code == "MDX_TRUNCATED", "对象边界截断必须定位到有界读取");
}

inline void InvalidGlobalLocation() {
    auto fixture = PremFixture(); Writer tail;
    // 使用一个额外HELP节点；已有PREM按文件顺序为1/3，完整PIVT加一槽。
    fixture.bytes.resize(fixture.pivots - 8);
    tail.Tag("HELP"); const auto chunk = tail.Begin(), node = tail.Begin();
    tail.Fixed("Invalid global", 80); tail.U32(4); tail.U32(UINT32_MAX); tail.U32(0);
    const auto track = tail.bytes.size(); tail.Tag("KGSC"); tail.U32(1); tail.U32(0); tail.U32(2);
    tail.U32(0); tail.F32(1); tail.F32(1); tail.F32(1); tail.EndInclusive(node); tail.EndChunk(chunk);
    const auto expectedOffset = fixture.bytes.size() + track + 12;
    tail.Tag("GLBS"); tail.U32(8); tail.U32(100); tail.U32(200);
    tail.Tag("PIVT"); tail.U32(60); for (int index = 0; index < 15; ++index) tail.F32(0);
    fixture.bytes.insert(fixture.bytes.end(), tail.bytes.begin(), tail.bytes.end());
    const auto rejected = ParseMdx(fixture.bytes);
    Check(!rejected.Succeeded() && rejected.diagnostics.front().code == "MDX_REFERENCE_INVALID", "坏全局引用仍拒绝");
    const auto& message = rejected.diagnostics.front().message;
    Check(message.find("node=4 track=KGSC global=2 GLBS count=2") != std::string::npos
        && message.find("offset=" + std::to_string(expectedOffset)) != std::string::npos,
        "全局引用拒绝须给出精确节点、轨迹、原索引、条目数和原字段偏移");
}

inline void Run() { PremIdentity(); EmptyNormals(); InvalidGlobalLocation(); }
} // namespace MdxCompatibilityTests
