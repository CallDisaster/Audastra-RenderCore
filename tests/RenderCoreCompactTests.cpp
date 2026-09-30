// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#include "War3RenderCore/PoseBatch.h"
#include "War3MapCore/Render/MdxModel.h"
#include "MdxTestModel.h"
#include "RenderCoreAllocProbe.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace War3RenderCore;
void Require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class T> bool Equal(std::span<const T> a, std::span<const T> b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size_bytes()) == 0);
}
void Run() {
    MdxTest::Options options; options.animatedHelper = true;
    auto parsed = War3Map::Render::ParseMdx(MdxTest::BuildTestMdx(options));
    Require(parsed.Succeeded(), "解析失败");
    // 世界规模的工作量：50 节点、10 geoset、共60组；仍是原创合成数据，不模拟实际地图帧耗时。
    auto& source = *parsed.value;
    for (std::uint32_t id = 2; id < 50; ++id) {
        War3Map::Render::MdxNode node; node.objectId = id; node.parentId = static_cast<std::int32_t>(id - 1);
        source.helpers.push_back(std::move(node));
    }
    source.pivots.resize(50);
    const auto geoset = source.geosets[0]; source.geosets.assign(10,geoset);
    for (std::uint32_t g = 0; g < 10; ++g) {
        auto& geometry = source.geosets[g]; geometry.matrixGroupSizes.assign(6,8); geometry.matrixIndices.clear();
        for (std::uint32_t member = 0; member < 48; ++member) geometry.matrixIndices.push_back((g + member) % 50);
        geometry.vertexGroups = {0,1,2,5};
    }
    auto compiled = CompileMdxModel(*parsed.value, {1, 2}, {"fixture.mdx", "synthetic", "fixture-v1"});
    Require(compiled.Succeeded(), "编译失败");
    const std::array assets{*compiled.value}; const auto& asset = assets[0];
    BatchLimits limits{64, 64 * asset.NodeCount(), 64 * asset.GroupCount(), 64 * asset.GeosetCount(), 2ull * 1024 * 1024};
    auto full = PoseBatchEvaluator::Create(assets, 11, limits);
    limits.outputMode = PoseOutputMode::GroupsAndAlphas;
    auto compact = PoseBatchEvaluator::Create(assets, 12, limits);
    Require(full.Succeeded() && compact.Succeeded(), "预热失败");
    Require(compact.value->StorageBytes() < full.value->StorageBytes(), "精简模式没有节省暂存");
    std::array<PoseRequest, 64> batch{};
    for (std::size_t i = 0; i < batch.size(); ++i) {
        batch[i].instance = {i + 1, 3}; batch[i].asset = asset.Identity();
        batch[i].sequenceIndex = 1; batch[i].sequenceElapsedMs = 11 * i;
    }
    Require(full.value->Evaluate(batch).failedInstances == 0 && compact.value->Evaluate(batch).failedInstances == 0,
        "批量失败");
    for (std::uint32_t i = 0; i < 64; ++i) {
        Require(compact.value->Nodes(i).empty() && compact.value->Receipts()[i].nodes.count == 0, "泄漏节点输出");
        Require(Equal(full.value->Groups(i), compact.value->Groups(i)), "组矩阵不一致");
        Require(Equal(full.value->Alphas(i), compact.value->Alphas(i)), "透明度不一致");
        Require(Equal(full.value->Diagnostics(i), compact.value->Diagnostics(i)), "诊断不一致");
    }
    // 宿主预先分配整帧包；每次复制当前批次的有效输出，再复用求值器。
    constexpr std::uint32_t total = 2000;
    std::vector<GroupMatrix> frameGroups(static_cast<std::size_t>(total) * asset.GroupCount());
    std::vector<float> frameAlphas(static_cast<std::size_t>(total) * asset.GeosetCount());
    std::vector<PoseReceipt> frameReceipts(total);
    RenderCoreAllocProbe::Begin();
    bool okay = true;
    for (std::uint32_t base = 0; base < total; base += 64) {
        const auto count = std::min(64u, total - base);
        for (std::uint32_t i = 0; i < count; ++i) batch[i].instance.value = base + i + 1;
        const auto result = compact.value->Evaluate(std::span(batch).first(count));
        okay = okay && result.error == Code::Ok && result.failedInstances == 0;
        if (!okay) break;
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto index = base + i;
            std::copy(compact.value->Groups(i).begin(), compact.value->Groups(i).end(),
                frameGroups.begin() + static_cast<std::ptrdiff_t>(index * asset.GroupCount()));
            std::copy(compact.value->Alphas(i).begin(), compact.value->Alphas(i).end(),
                frameAlphas.begin() + static_cast<std::ptrdiff_t>(index * asset.GeosetCount()));
            frameReceipts[index] = compact.value->Receipts()[i];
            // 不把批次内部偏移带进宿主帧包；诊断另归档或清除引用。
            frameReceipts[index].groups.offset = index * asset.GroupCount();
            frameReceipts[index].alphas.offset = index * asset.GeosetCount();
            frameReceipts[index].diagnostics = {};
        }
    }
    const auto allocations = RenderCoreAllocProbe::End();
    Require(okay && allocations == 0, "分批路径失败或产生分配");
    Require(frameReceipts.back().instance.value == 2000 && frameReceipts.front().poseVersion < frameReceipts.back().poseVersion,
        "帧包身份或版本不正确");
    const auto saved = frameGroups;
    Require(compact.value->Evaluate(std::span(batch).first(1)).failedInstances == 0, "复用失败");
    Require(Equal<GroupMatrix>(frameGroups, saved), "宿主帧包被内部暂存覆盖");
    std::vector<GroupMatrix> external(compact.value->Groups(0).begin(),compact.value->Groups(0).end());
    batch[1].source = PoseSource::ExternalGroups; batch[1].external.matrices = {0,asset.GroupCount()};
    const auto mixed = compact.value->Evaluate(std::span(batch).first(2),external);
    Require(mixed.error == Code::Ok && mixed.failedInstances == 0 && compact.value->Nodes(1).empty()
        && Equal<GroupMatrix>(external,compact.value->Groups(1)), "精简混合来源不一致");
    batch[1].external.matrices.count -= 1;
    Require(compact.value->Evaluate(std::span(batch).first(2),external).failedInstances == 1
        && compact.value->Groups(1).empty() && !compact.value->Groups(0).empty(), "精简外部拒绝污染其它实例");
    batch[0].sequenceIndex = UINT32_MAX;
    Require(compact.value->Evaluate(std::span(batch).first(1)).failedInstances == 1
        && compact.value->Receipts()[0].poseVersion == 0 && compact.value->Groups(0).empty(), "失败暴露旧姿势");
    std::cout << "compact_instances=2000 batches=32 nodes_per_instance=50 groups_per_instance=60 new_calls=" << allocations
              << " full_bytes=" << full.value->StorageBytes() << " compact_bytes=" << compact.value->StorageBytes()
              << " bitwise_equal=1 frame_owned=1\n";
}
}
int main() {
    try { Run(); std::cout << "PASS compact\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
