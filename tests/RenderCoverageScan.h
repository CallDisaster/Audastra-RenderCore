// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "RenderCoverageParticles.h"
#include "../src/Render/Tools/MdxScan/ScanDetail.h"
#include <filesystem>
#include <fstream>
#include <random>

namespace Coverage {
inline void ScanDetails() {
    using namespace War3Map::Render;
    using namespace War3Map::Render::Scan;
    auto parsed = ParseMdx(OtherParent("ATCH"));
    Require(parsed.Succeeded(), "扫描详情样本解析失败");
    auto model = *parsed.value;
    model.bones[0].flags = 0x104; model.sequences[0].end = 1; model.geosets[0].normals[0] = {};
    Record record; record.parse = record.compile = "OK";
    DescribeModel(model, record, [] {});
    for (const auto term : {"flags=0x104", "parent_kind=ATCH", "bad_sequence=0", "zero_normals geoset=0 vertices=1"})
        Require(record.detail.find(term) != std::string::npos, "扫描详情缺少定位字段");
    const auto root = std::filesystem::temp_directory_path() / ("forge-scan-detail-" + std::to_string(std::random_device{}()));
    Require(std::filesystem::create_directory(root), "临时详情目录已存在");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path,ec); } } cleanup{root};
    const auto path = root / "record.tsv";
    record.path = "合成\\含空格\t换行\n.mdx"; record.detail += "; 控制符\t\n\\";
    WriteRecord(path, record); const auto roundtrip = ReadRecord(path);
    Require(roundtrip.detail == record.detail && roundtrip.path == record.path && Header().ends_with("\tsample_rejection_codes\tprimary_rejection_code"),
        "扫描TSV转义/29列往返错误");
    record.particleOnly = 1; record.state = "particle_only_unimplemented";
    WriteRecord(path,record); Require(ReadRecord(path).particleOnly == 1, "纯粒子计数未跨子进程保留");
    record.particleOnly = 2; WriteRecord(path,record);
    bool rejected = false; try { (void)ReadRecord(path); } catch (const std::exception&) { rejected = true; }
    Require(rejected, "非法粒子计数未拒绝");
    record = {}; record.compile = "OK"; record.state = "complete"; record.planned = record.samples = record.matchedRejections = 2;
    record.rejectionCodes = {{"MDX_GEOA_UNINTERPRETED",1},{"MDX_GEOA_BOUNDARY_UNSUPPORTED",1}};
    WriteRecord(path,record); const auto counts = ReadRecord(path);
    Require(counts.rejectionCodes == record.rejectionCodes && counts.AllSamplesRejected() &&
        counts.PrimaryRejection() == "MDX_GEOA_BOUNDARY_UNSUPPORTED", "主拒绝码或全拒分账错误");
    record.state = "SCAN_SAMPLE_LIMIT";
    Require(!record.AllSamplesRejected(), "截断计划不能称全模型全拒");
    record.rejectionCodes["MDX_GEOA_UNINTERPRETED"] = 3; WriteRecord(path,record);
    rejected = false; try { (void)ReadRecord(path); } catch (const std::exception&) { rejected = true; }
    Require(rejected, "拒绝计数超过样本数未拒绝");
    record = {};
    for (int i = 0; i < 10000; ++i) AppendDetail(record, "父节点详情：parent_kind=ATCH flags=0x104");
    Require(record.detail.size() <= MaxDetailBytes && record.detail.ends_with("detail_truncated=1"), "详情长度没有上限");
    std::cout << "scan_detail=1 flags_parent_sequence_normals=1 tsv_roundtrip=1 bounded=1 malformed_rejected=1\n";
}
} // namespace Coverage
