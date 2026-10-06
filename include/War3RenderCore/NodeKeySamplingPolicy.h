// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include <cstdint>
namespace War3RenderCore {
// 仅宿主显式选择。源序预览不修改键、不证明原版有历史状态的缓存等价。
enum class NodeKeySamplingPolicy : std::uint32_t { Strict = 0, SourceOrderPreview = 1 };
}
