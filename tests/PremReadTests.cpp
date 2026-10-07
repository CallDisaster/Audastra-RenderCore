// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#include "War3MapCore/Render/MdxModel.h"
#include "MdxTestModel.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
using namespace War3Map;
using namespace War3Map::Render;
using MdxTest::Writer;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::uint32_t Word(const std::vector<std::byte>& bytes, std::uint64_t offset) {
    Check(offset <= bytes.size() && bytes.size() - offset >= 4, "测试来源越界");
    std::uint32_t value = 0; std::memcpy(&value, bytes.data() + static_cast<std::size_t>(offset), 4); return value;
}
void Patch(std::vector<std::byte>& bytes, std::uint64_t offset, std::uint32_t value) {
    Check(offset <= bytes.size() && bytes.size() - offset >= 4, "测试改位越界");
    std::memcpy(bytes.data() + static_cast<std::size_t>(offset), &value, 4);
}
struct Options {
    bool allTracks = false, nodeTrack = false, noTerminator = false, emptyPath = false;
    bool unknownTail = false, shortTail = false, duplicateVisibility = false;
    std::uint32_t flags = 0x9000, global = UINT32_MAX, interpolation = 0;
    std::uint32_t lastFrame = 100;
};
void Scalar(Writer& writer, const char* tag, std::uint32_t interpolation, std::uint32_t global,
    std::uint32_t lastFrame = 100) {
    writer.Tag(tag); writer.U32(3); writer.U32(interpolation); writer.U32(global);
    for (const auto frame : {0u, 33u, lastFrame}) {
        writer.U32(frame); writer.F32(frame == 33 ? 1.0f : 0.0f);
        if (interpolation >= 2) { writer.F32(0.125f); writer.F32(0.75f); }
    }
}
std::vector<std::byte> Fixture(const Options& options = {}, bool secondBlock = false) {
    Writer writer;
    const auto base = MdxTest::BuildTestMdx();
    writer.Tag("MDLX");
    // 复用原创网格夹具，仅替换 PIVT 并追加 PREM，不含任何游戏字节。
    for (std::size_t offset = 4; offset < base.size();) {
        const auto size = Word(base, offset + 4);
        if (std::memcmp(base.data() + offset, "PIVT", 4) != 0) writer.Raw(base.data() + offset, size + 8);
        offset += size + 8;
    }
    const auto record = [&](std::uint32_t id) {
        writer.Tag("PREM"); const auto chunk = writer.Begin(); const auto outer = writer.Begin();
        const auto node = writer.Begin(); writer.Fixed("OriginalSyntheticEmitter", 80);
        writer.U32(id); writer.U32(0); writer.U32(options.flags);
        if (options.nodeTrack) {
            writer.Tag("KGTR"); writer.U32(1); writer.U32(0); writer.U32(UINT32_MAX);
            writer.U32(0); writer.F32(10); writer.F32(20); writer.F32(30);
        }
        writer.EndInclusive(node);
        writer.F32(7.25f); writer.F32(-3.5f); writer.F32(0.75f); writer.F32(0.125f);
        writer.Fixed(options.noTerminator ? std::string(260, 'x') : options.emptyPath ? "" : "Particles\\Synthetic.MDL", 260);
        writer.F32(2.75f); writer.F32(-4.25f);
        if (options.allTracks)
            for (const auto* tag : {"KPEE", "KPEG", "KPLN", "KPLT", "KPEL", "KPES"})
                Scalar(writer, tag, options.interpolation, options.global);
        Scalar(writer, "KPEV", options.interpolation, options.global, options.lastFrame);
        if (options.duplicateVisibility) Scalar(writer, "KPEV", 0, UINT32_MAX);
        if (options.unknownTail) { writer.Tag("XXXX"); writer.U32(24); }
        if (options.shortTail) writer.Raw("x", 1);
        writer.EndInclusive(outer); writer.EndChunk(chunk);
    };
    record(2); if (secondBlock) record(3);
    writer.Tag("PIVT"); writer.U32(48);
    for (std::uint32_t node = 0; node < 4; ++node) { writer.F32(static_cast<float>(node)); writer.F32(15); writer.F32(-7); }
    if (options.global != UINT32_MAX) { writer.Tag("GLBS"); writer.U32(4); writer.U32(250); }
    return std::move(writer.bytes);
}
Result<MdxModel> Parsed(const std::vector<std::byte>& bytes, const MdxLimits& limits = {}) {
    auto result = ParseMdx(bytes, limits);
    if (!result.Succeeded()) {
        for (const auto& diagnostic : result.diagnostics) std::cerr << diagnostic.code << ":" << diagnostic.message << '\n';
    }
    Check(result.Succeeded(), "安全 PREM 负载不应隐藏父模型");
    Check(result.value->geosets.size() == 2 && result.value->geosets[0].indices.size() == 6,
        "父网格未保留");
    return result;
}
void Fields() {
    Options options; options.allTracks = true; options.nodeTrack = true; options.interpolation = 3; options.global = 0;
    const auto bytes = Fixture(options, true); const auto result = Parsed(bytes); const auto& model = *result.value;
    Check(model.premEmitters.size() == 2 && model.otherNodes.size() == 2, "跨 PREM 块来源数量错误");
    for (std::uint32_t index = 0; index < 2; ++index) {
        const auto& emitter = model.premEmitters[index];
        Check(emitter.support == MdxPremSupport::Supported && emitter.sourceIndex == index
            && emitter.otherNodeIndex == index && emitter.nodeId == index + 2 && emitter.flags == 0x9000,
            "PREM 公共节点/来源身份错误");
        Check(model.otherNodes[index].objectId == emitter.nodeId && model.otherNodes[index].translation
            && model.pivots[emitter.nodeId][0] == static_cast<float>(emitter.nodeId), "Node/PIVT/轨迹关联丢失");
        Check(emitter.recordByteSize == Word(bytes, emitter.recordByteOffset)
            && emitter.payloadByteOffset == emitter.recordByteOffset + 4 + Word(bytes, emitter.recordByteOffset + 4),
            "outer/Node 包含式长度或负载物理偏移错误");
        Check(emitter.path == "Particles\\Synthetic.MDL" && emitter.pathTerminated
            && emitter.pathByteOffset == emitter.payloadByteOffset + 16
            && std::memcmp(bytes.data() + static_cast<std::size_t>(emitter.pathByteOffset), emitter.rawPath.data(), 260) == 0,
            "原路径/终止/260字节/物理偏移丢失");
        Check(emitter.emissionRate == 7.25f && emitter.gravity == -3.5f && emitter.longitude == 0.75f
            && emitter.latitude == 0.125f && emitter.lifeSpan == 2.75f && emitter.speed == -4.25f,
            "静态字段顺序、符号或角度被改写");
        for (std::size_t field = 0; field < emitter.staticValueBits.size(); ++field) {
            const auto offset = emitter.payloadByteOffset + (field < 4 ? field * 4 : 276 + (field - 4) * 4);
            Check(emitter.staticValueBits[field] == Word(bytes, offset), "静态原始浮点位丢失");
        }
        for (const auto& optional : emitter.tracks) {
            Check(optional.has_value(), "七条参数映射缺失"); const auto& track = *optional;
            Check(track.interpolation == 3 && track.globalSequence == 0 && track.sourceKeyCount == 3
                && track.keys.size() == 3 && track.sourceByteSize == 64, "标量 Hermite/Bezier 布局错误");
            for (std::size_t keyIndex = 0; keyIndex < track.keys.size(); ++keyIndex) {
                const auto& key = track.keys[keyIndex];
                Check(key.sourceByteOffset == track.sourceByteOffset + 16 + keyIndex * 16
                    && std::bit_cast<std::uint32_t>(key.frame) == Word(bytes, key.sourceByteOffset), "键原物理偏移丢失");
                for (std::size_t component = 0; component < 3; ++component)
                    Check(key.rawValueBits[component] == Word(bytes, key.sourceByteOffset + 4 + component * 4), "键/切线逐位值丢失");
            }
        }
    }
    const auto none = Parsed(Fixture()); const auto& visibility = *none.value->premEmitters[0].tracks[6];
    Check(visibility.sourceByteSize == 40 && visibility.keys[1].value == 1 && visibility.keys[1].inTangent == 0,
        "None 键步长或无切线默认值错误");
    options.interpolation = 1; const auto linear = Parsed(Fixture(options));
    Check(linear.value->premEmitters[0].tracks[6]->sourceByteSize == 40, "Linear 键布局错误");
}
void LocalFailure(const Options& options, MdxPremSupport expected, const char* code) {
    const auto result = Parsed(Fixture(options)); const auto& emitter = result.value->premEmitters[0];
    Check(emitter.support == expected && emitter.issueCode == code, "PREM 局部拒绝分类错误");
    Check(result.diagnostics.size() == 1 && result.diagnostics[0].severity == DiagnosticSeverity::Warning
        && result.diagnostics[0].origin && result.diagnostics[0].origin->offset == emitter.issueByteOffset,
        "局部原因未成为准确来源 Warning");
}
void Local() {
    Options options; options.flags = 0x11000; LocalFailure(options, MdxPremSupport::UnsupportedFlags, "MDX_PREM_FLAGS_UNSUPPORTED");
    options = {}; options.flags = 0x19000; LocalFailure(options, MdxPremSupport::UnsupportedFlags, "MDX_PREM_FLAGS_UNSUPPORTED");
    options = {}; options.flags = 0x9000 | 0x40000000; LocalFailure(options, MdxPremSupport::UnsupportedFlags, "MDX_PREM_FLAGS_UNSUPPORTED");
    options = {}; options.noTerminator = true; LocalFailure(options, MdxPremSupport::InvalidPath, "MDX_PREM_PATH_INVALID");
    options = {}; options.emptyPath = true; LocalFailure(options, MdxPremSupport::InvalidPath, "MDX_PREM_PATH_INVALID");
    options = {}; options.unknownTail = true; LocalFailure(options, MdxPremSupport::UnknownTrack, "MDX_PREM_TRACK_UNSUPPORTED");
    options = {}; options.shortTail = true; LocalFailure(options, MdxPremSupport::UnknownTrack, "MDX_PREM_TAIL_UNSUPPORTED");
    options = {}; options.duplicateVisibility = true; LocalFailure(options, MdxPremSupport::DuplicateTrack, "MDX_PREM_TRACK_DUPLICATE");
    options = {}; options.global = 1; LocalFailure(options, MdxPremSupport::InvalidGlobalSequence, "MDX_PREM_GLOBAL_INVALID");
    options = {}; options.lastFrame = 33; LocalFailure(options, MdxPremSupport::InvalidTrack, "MDX_PREM_TRACK_KEYS_INVALID");
    auto bytes = Fixture(); const auto reference = Parsed(bytes); const auto& source = reference.value->premEmitters[0];
    Patch(bytes, source.payloadByteOffset, 0x7fc01234u); const auto nan = Parsed(bytes); const auto& invalid = nan.value->premEmitters[0];
    Check(invalid.support == MdxPremSupport::InvalidParameters && invalid.staticValueBits[0] == 0x7fc01234u
        && invalid.issueValueBits == 0x7fc01234u && invalid.issueByteOffset == source.payloadByteOffset
        && std::isnan(invalid.emissionRate), "NaN 静态原位/数值/来源丢失或误隐藏父模型");
    bytes = Fixture(); const auto keyOffset = source.tracks[6]->keys[1].sourceByteOffset;
    Patch(bytes, keyOffset + 4, 0x7fc04567u); const auto keyNan = Parsed(bytes);
    Check(keyNan.value->premEmitters[0].tracks[6]->keys[1].rawValueBits[0] == 0x7fc04567u
        && keyNan.value->premEmitters[0].issueByteOffset == keyOffset + 4,
        "轨迹 NaN 来源丢失");
    bytes = Fixture({}, true); const auto two = Parsed(bytes);
    Patch(bytes, two.value->premEmitters[0].tracks[6]->sourceByteOffset, 0x58585858u);
    const auto isolated = Parsed(bytes);
    Check(isolated.value->premEmitters[0].support == MdxPremSupport::UnknownTrack
        && isolated.value->premEmitters[1].support == MdxPremSupport::Supported
        && isolated.value->premEmitters[1].sourceIndex == 1 && isolated.value->otherNodes.size() == 2,
        "局部未知轨迹读取到了下一 PREM 记录或污染来源序号");
}
void Bounds() {
    auto bytes = Fixture(); const auto reference = Parsed(bytes); const auto& source = reference.value->premEmitters[0];
    Patch(bytes, source.recordByteOffset, 383); Check(!ParseMdx(bytes).Succeeded(), "过小 outer 被放行");
    bytes = Fixture(); Patch(bytes, source.recordByteOffset, UINT32_MAX); Check(!ParseMdx(bytes).Succeeded(), "跨块 outer 被放行");
    bytes = Fixture(); Patch(bytes, source.recordByteOffset + 4, 95); Check(!ParseMdx(bytes).Succeeded(), "过小 Node 被放行");
    bytes = Fixture(); Patch(bytes, source.recordByteOffset + 4, static_cast<std::uint32_t>(source.recordByteSize));
    Check(!ParseMdx(bytes).Succeeded(), "Node 侵占 tail 被放行");
    bytes = Fixture(); Patch(bytes, source.tracks[6]->sourceByteOffset + 4, UINT32_MAX);
    auto result = Parsed(bytes); Check(result.value->premEmitters[0].support == MdxPremSupport::TrackKeyLimit
        && result.value->premEmitters[0].tracks[6]->keys.empty(), "伪造超限轨迹发生分配或隐藏父模型");
    bytes = Fixture(); Patch(bytes, source.tracks[6]->sourceByteOffset + 4, 4);
    result = Parsed(bytes); Check(result.value->premEmitters[0].support == MdxPremSupport::InvalidTrack
        && result.value->premEmitters[0].uninterpretedTailBytes == 40, "安全 outer 内截断轨迹未局部隔离");
    bytes = Fixture(); Patch(bytes, source.tracks[6]->sourceByteOffset + 8, 4);
    result = Parsed(bytes); Check(result.value->premEmitters[0].support == MdxPremSupport::InvalidTrack,
        "未知插值被猜成合法布局");
    MdxLimits limits; limits.maxTrackKeys = 2; result = Parsed(Fixture(), limits);
    Check(result.value->premEmitters[0].support == MdxPremSupport::TrackKeyLimit, "单轨迹预算未生效");
    limits = {}; limits.maxTotalBoneTrackKeys = 5; result = Parsed(Fixture({}, true), limits);
    Check(result.value->premEmitters[0].support == MdxPremSupport::Supported
        && result.value->premEmitters[1].support == MdxPremSupport::TrackKeyLimit, "跨 PREM 块共享总键预算未生效");
    limits = {}; limits.maxNodes = 1; Check(!ParseMdx(Fixture(), limits).Succeeded(), "公共 Node 总预算被 PREM 绕过");
}
std::string Environment(const char* name) {
#ifdef _MSC_VER
    std::size_t size = 0; Check(getenv_s(&size, nullptr, 0, name) == 0, "ENV 长度读取失败");
    if (!size) return {}; std::string value(size, '\0'); std::size_t written = 0;
    Check(getenv_s(&written, value.data(), value.size(), name) == 0 && written == size, "ENV 来源读取变化");
    value.resize(size - 1); return value;
#else
    const auto* value = std::getenv(name); return value ? value : "";
#endif
}
std::vector<std::byte> File(const std::string& path, std::uintmax_t expectedSize) {
    const auto source = std::filesystem::u8path(path); const auto size = std::filesystem::file_size(source);
    const auto time = std::filesystem::last_write_time(source); Check(size == expectedSize, "真实夹具大小不符");
    std::ifstream input(source, std::ios::binary); Check(static_cast<bool>(input), "真实夹具不可读");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    Check(static_cast<bool>(input) && std::filesystem::file_size(source) == size
        && std::filesystem::last_write_time(source) == time, "真实夹具读取变化");
    return bytes;
}
int Real() {
    const auto parentPath = Environment("FORGE_PREM_PARENT"); const auto childPath = Environment("FORGE_PREM_CHILD");
    if (parentPath.empty() || childPath.empty()) { std::cout << "SKIP: set FORGE_PREM_PARENT and FORGE_PREM_CHILD to owned samples\n"; return 77; }
    const auto parentBytes = File(parentPath, 69822); const auto childBytes = File(childPath, 2292);
    const auto parent = ParseMdx(parentBytes); const auto child = ParseMdx(childBytes);
    Check(parent.Succeeded() && child.Succeeded(), "真实 PREM 父/子解析失败");
    Check(parent.value->premEmitters.size() == 1, "真实 PREM 数量不符"); const auto& emitter = parent.value->premEmitters[0];
    Check(emitter.sourceIndex == 0 && emitter.nodeId == 48 && emitter.flags == 0x9000
        && emitter.recordByteOffset == 66651 && emitter.recordByteSize == 456 && emitter.payloadByteOffset == 66751
        && emitter.pathByteOffset == 66767 && emitter.path == "SharedModels\\BuildingParticlebeam.MDL"
        && emitter.pathTerminated && emitter.support == MdxPremSupport::Supported, "真实 PREM typed 合同不符");
    Check(emitter.emissionRate == 21 && emitter.gravity == 1000 && emitter.speed == 800
        && parent.value->pivots.size() > 48 && parent.value->pivots[48][0] == -6.822879791259766f,
        "真实静态字段/轴心改变");
    Check(emitter.tracks[6].has_value(), "真实 KPEV 缺失");
    const auto& track = *emitter.tracks[6]; Check(track.sourceByteOffset == 67035 && track.keys.size() == 7
        && track.interpolation == 0 && track.globalSequence == UINT32_MAX
        && track.keys[3].frame == 166700 && track.keys[3].value == 1
        && track.keys[3].sourceByteOffset == 67075 && track.keys[4].frame == 166900
        && track.keys[4].value == 0, "真实 Death visibility 窗口/物理来源丢失");
    Check(child.value->geosets.size() == 1 && child.value->premEmitters.empty() && child.value->textures.size() == 1
        && child.value->textures[0].path == "SharedModels\\BirthStuff.blp", "真实子模型几何/资源不符");
    std::cout << "REAL_PREM_CPU: one typed parent, one child geometry; no rendering/native equivalence claim\n";
    return 0;
}
} // namespace
int main(int argc, char** argv) try {
    const std::string group = argc > 1 ? argv[1] : "all";
    if (group == "real") return Real();
    if (group == "fields" || group == "all") Fields();
    if (group == "local" || group == "all") Local();
    if (group == "bounds" || group == "all") Bounds();
    Check(group == "fields" || group == "local" || group == "bounds" || group == "all", "未知 PREM 测试组");
    std::cout << "PASS: PREM " << group << '\n'; return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
