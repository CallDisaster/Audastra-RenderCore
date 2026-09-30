// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "ScanTypes.h"
#include <string_view>

namespace War3Map::Render::Scan {
inline constexpr std::size_t MaxDetailBytes = 8192;
// 每条模型报告有界；不完整条目只记 detail_truncated，不伪造完整诊断清单。
void AppendDetail(Record& record, std::string_view entry);
void DescribeModel(const MdxModel& model, Record& record, const Checkpoint& checkpoint);
} // namespace War3Map::Render::Scan
