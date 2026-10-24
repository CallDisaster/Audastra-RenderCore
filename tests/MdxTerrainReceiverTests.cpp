// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3MapCore/Render/MdxTerrainReceiver.h"
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace War3Map;
using namespace War3Map::Render;
constexpr std::size_t kBudget = 128u * 1024u * 1024u;
void Check(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
template <typename T> T Take(Result<T> result) {
    if (!result.Succeeded()) throw std::runtime_error(result.diagnostics.empty() ? "missing diagnostic" : result.diagnostics.front().message);
    return std::move(*result.value);
}
bool Code(const auto& result, std::string_view code) {
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
        [&](const auto& diagnostic) { return diagnostic.code == code; });
}
std::uint32_t U32(std::span<const std::byte> bytes, std::size_t offset) {
    Check(offset <= bytes.size() && bytes.size() - offset >= 4, "test source U32 bounds");
    std::uint32_t value = 0; std::memcpy(&value, bytes.data() + offset, 4); return value;
}
void Patch(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    Check(offset <= bytes.size() && bytes.size() - offset >= 4, "test patch bounds");
    std::memcpy(bytes.data() + offset, &value, 4);
}

// 原创prefix夹具，尾部有明确外层边界但不提供完整模型语义；用于验证源视图的范围。
// 普通ParseMdx仍应拒绝它，不能把source view的成功拿来冒充模型/静态保存资格。
std::vector<std::byte> PrefixFixture(std::span<const std::uint32_t> types,
    std::span<const std::uint32_t> counts, std::span<const std::uint16_t> indices, std::size_t copies = 1) {
    MdxTest::Writer writer;
    writer.Tag("MDLX"); writer.Tag("VERS"); writer.U32(4); writer.U32(800);
    for (std::size_t copy = 0; copy < copies; ++copy) {
        writer.Tag("GEOS"); const auto chunk = writer.Begin(), geoset = writer.Begin();
        writer.Tag("VRTX"); writer.U32(4);
        for (std::uint32_t vertex = 0; vertex < 4; ++vertex) {
            writer.F32(static_cast<float>(vertex)); writer.F32(0); writer.F32(static_cast<float>(copy));
        }
        writer.Tag("NRMS"); writer.U32(0);
        writer.Tag("PTYP"); writer.U32(static_cast<std::uint32_t>(types.size()));
        for (const auto type : types) writer.U32(type);
        writer.Tag("PCNT"); writer.U32(static_cast<std::uint32_t>(counts.size()));
        for (const auto count : counts) writer.U32(count);
        writer.Tag("PVTX"); writer.U32(static_cast<std::uint32_t>(indices.size()));
        for (const auto index : indices) writer.U16(index);
        writer.Raw("opaque", 6); writer.EndInclusive(geoset); writer.EndChunk(chunk);
    }
    return std::move(writer.bytes);
}

void Ordered() {
    auto bytes = MdxTest::BuildTestMdx({.selectionGroup = 17, .selectionFlags = 0x80000000u});
    const auto original = bytes;
    const auto model = Take(ParseMdx(bytes));
    const auto source = Take(ReadMdxTerrainReceiverSource(bytes, kBudget));
    Check(source.Geosets().size() == 2 && !model.geosets[1].visibleInStand,
        "hidden Stand geoset must remain in ordered source");
    Check(source.Bytes().size() == original.size()
        && std::equal(source.Bytes().begin(), source.Bytes().end(), original.begin()), "complete original bytes must be sealed");
    for (std::size_t g = 0; g < source.Geosets().size(); ++g) {
        const auto& geoset = source.Geosets()[g];
        Check(geoset.positions == model.geosets[g].positions && U32(source.Bytes(), geoset.sourceByteOffset) == geoset.sourceByteSize,
            "geoset and positions must retain file order and inclusive origin");
        Check(std::memcmp(source.Bytes().data() + geoset.positionsByteOffset, geoset.positions.data(),
            geoset.positions.size() * sizeof(geoset.positions[0])) == 0, "decoded positions must match raw physical source");
        std::size_t first = 0;
        for (const auto& group : geoset.groups) {
            Check(group.firstIndex == first && U32(source.Bytes(), group.typeByteOffset) == group.sourceType
                && U32(source.Bytes(), group.countByteOffset) == group.indexCount, "group order and source words must remain exact");
            Check(std::memcmp(source.Bytes().data() + group.indicesByteOffset, geoset.indices.data() + first,
                static_cast<std::size_t>(group.indexCount) * 2) == 0, "group slice must retain original PVTX order");
            first += group.indexCount;
        }
        Check(first == geoset.indices.size(), "groups must cover all source indices exactly");
    }
    const auto& second = source.Geosets()[1];
    Check(second.groups.size() == 2 && second.groups[0].NativeTopology() == 3
        && second.groups[1].sourceType == 5 && second.groups[1].NativeTopology() == 4
        && second.indices.size() == 9 && model.geosets[1].indices.size() == 6,
        "raw strip must survive while ordinary preview retains triangle-only behavior");
    std::fill(bytes.begin(), bytes.end(), std::byte{0});
    Check(std::equal(source.Bytes().begin(), source.Bytes().end(), original.begin()), "caller changes cannot mutate sealed source");

    const std::uint32_t types[]{6, 4, 0xDEADBEEFu, 5};
    const std::uint32_t counts[]{3, 4, 0, 3};
    const std::uint16_t indices[]{0, 1, 2, 3, 2, 1, 0, 2, 1, 3};
    const auto prefix = PrefixFixture(types, counts, indices, 2);
    const auto raw = Take(ReadMdxTerrainReceiverSource(prefix, kBudget));
    Check(raw.Geosets().size() == 2 && raw.Geosets()[1].positions[0][2] == 1,
        "multiple GEOS chunks must preserve aggregate source order");
    const auto& groups = raw.Geosets()[0].groups;
    Check(groups.size() == 4 && groups[0].NativeTopology() == 0 && groups[0].sourceType == 6
        && groups[1].NativeTopology() == 3 && groups[1].indexCount == 4
        && groups[2].sourceType == 0xDEADBEEFu && groups[2].NativeTopology() == 0 && groups[2].indexCount == 0
        && groups[3].firstIndex == 7, "fan, unknown, empty and non-triple groups must not be dropped or repaired");
    Check(!ParseMdx(prefix).Succeeded(), "partial source view must not weaken ordinary full-model validation");
}

void Boundaries() {
    auto bytes = MdxTest::BuildTestMdx();
    const auto source = Take(ReadMdxTerrainReceiverSource(bytes, kBudget));
    const auto& geoset = source.Geosets()[0];
    Patch(bytes, geoset.positionsByteOffset, 0x80000000u);
    const auto negativeZero = Take(ReadMdxTerrainReceiverSource(bytes, kBudget));
    Check(std::bit_cast<std::uint32_t>(negativeZero.Geosets()[0].positions[0][0]) == 0x80000000u, "negative zero must retain source bits");
    Patch(bytes, geoset.positionsByteOffset, 0x7FC01234u);
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget), "MDX_NUMBER_INVALID"), "non-finite positions must fail");
    bytes = MdxTest::BuildTestMdx();
    const auto& group = geoset.groups[0];
    Patch(bytes, group.countByteOffset, group.indexCount + 1);
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget), "MDX_VALUE_INVALID"), "PCNT sum mismatch must fail before decode");
    bytes = MdxTest::BuildTestMdx();
    const std::uint16_t bad = 4;
    std::memcpy(bytes.data() + group.indicesByteOffset, &bad, 2);
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget), "MDX_VALUE_INVALID"), "out-of-range raw indices must fail");
    bytes = MdxTest::BuildTestMdx(); bytes.pop_back();
    Check(!ReadMdxTerrainReceiverSource(bytes, kBudget).Succeeded(), "unrelated outer chunk truncation must fail");
    bytes = MdxTest::BuildTestMdx();
    Patch(bytes, geoset.sourceByteOffset, 0xFFFFFFFFu);
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget), "MDX_SIZE_INVALID"), "inclusive GEOS overflow must fail");
    bytes = MdxTest::BuildTestMdx(); Patch(bytes, 12, 900);
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget), "MDX_VERSION_UNSUPPORTED"), "nonclassic version must fail");
    MdxTest::Writer missing; missing.Tag("MDLX");
    Check(Code(ReadMdxTerrainReceiverSource(missing.bytes, kBudget), "MDX_VERSION_UNSUPPORTED"), "missing VERS must fail");
    missing.Tag("VERS"); missing.U32(4); missing.U32(800);
    Check(Code(ReadMdxTerrainReceiverSource(missing.bytes, kBudget), "MDX_RECEIVER_EMPTY"), "no geosets is not a receiver model");
}

void Budgets() {
    const auto bytes = MdxTest::BuildTestMdx();
    const auto source = Take(ReadMdxTerrainReceiverSource(bytes, kBudget));
    const auto charged = source.ChargedResidentBytes();
    Check(charged > bytes.size() && ReadMdxTerrainReceiverSource(bytes, charged).Succeeded(), "exact charged resident budget must pass");
    Check(Code(ReadMdxTerrainReceiverSource(bytes, charged - 1), "MDX_RECEIVER_BUDGET"), "one-byte shortage must fail before allocating output");
    for (const auto maximum : {std::size_t{0}, kBudget + 1})
        Check(Code(ReadMdxTerrainReceiverSource(bytes, maximum), "MDX_RECEIVER_BUDGET"), "caller cannot widen hard resident limit");
    auto limits = MdxLimits{}; limits.maxTotalIndices = 14;
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget, limits), "MDX_LIMIT"), "aggregate count must include the otherwise-hidden strip");
    limits = {}; limits.maxTotalVertices = 7;
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget, limits), "MDX_LIMIT"), "aggregate vertices must remain bounded");
    limits = {}; limits.maxGeosets = 1;
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget, limits), "MDX_LIMIT"), "all GEOS chunks share geoset limit");
    limits = {}; limits.maxTotalIndices += 1;
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget, limits), "MDX_LIMITS"), "existing MDX hard limit cannot be widened");
    limits = {}; limits.maxInputBytes = bytes.size() - 1;
    Check(Code(ReadMdxTerrainReceiverSource(bytes, kBudget, limits), "MDX_LIMIT"), "stored original bytes must count toward input limit");
    const std::uint32_t types[]{4, 5, 6}; const std::uint32_t counts[]{0, 0, 0};
    const auto emptyGroups = PrefixFixture(types, counts, {}, 2);
    limits = {}; limits.maxTotalIndices = 5;
    Check(Code(ReadMdxTerrainReceiverSource(emptyGroups, kBudget, limits), "MDX_LIMIT"), "empty groups still consume cumulative group limit");
    const std::vector<std::uint32_t> manyTypes(10000, 5), noIndices(10000, 0);
    const auto large = PrefixFixture(manyTypes, noIndices, {});
    RenderCoreAllocProbe::Begin();
    const auto denied = ReadMdxTerrainReceiverSource(large, large.size() + 4096);
    static_cast<void>(RenderCoreAllocProbe::End());
    Check(Code(denied, "MDX_RECEIVER_BUDGET") && RenderCoreAllocProbe::LargestAllocationBytes() < 4096,
        "decoded group storage must be charged before source copy or large output allocation");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view group = argc > 1 ? argv[1] : "all";
        if (group == "ordered" || group == "all") Ordered();
        if (group == "boundaries" || group == "all") Boundaries();
        if (group == "budgets" || group == "all") Budgets();
        if (group != "ordered" && group != "boundaries" && group != "budgets" && group != "all") return 2;
        std::cout << "MDX terrain receiver " << group << " tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
