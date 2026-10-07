// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include <cstdint>

namespace War3RenderCore {
// 只在显式交互预览入口接受普通KMTA中位模式完全相同的同帧键。
// 不排序、不删除原键，不放宽全局轨迹、真正逆序或同帧不同值。
enum class MaterialAlphaKeySamplingPolicy : std::uint32_t {
    Strict = 0,
    IdenticalDuplicatesPreview = 1,
};
}
