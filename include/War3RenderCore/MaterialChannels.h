// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3RenderCore/PoseTypes.h"
#include <type_traits>
namespace War3RenderCore {
struct MaterialLayerLayout {
    std::uint32_t materialIndex = 0, layerIndex = 0;
    std::uint64_t sourceByteOffset = 0; // KMTA tag；无轨迹时为静态alpha字段。
};
struct MaterialLayerIssue {
    std::uint32_t materialIndex = NoIndex, layerIndex = NoIndex;
    std::uint64_t sourceByteOffset = 0;
    Code error = Code::Ok;
    std::uint32_t sourceFrame = 0, sourceValueBits = 0, reserved = 0;
};
// 显式启用；旧Create不分配层工作区、不改变旧预算/输出。
struct MaterialChannelOptions {
    std::uint32_t enabled = 0, maxLayerValues = 8192;
};
static_assert(sizeof(MaterialLayerLayout) == 16 && sizeof(MaterialLayerIssue) == 32);
static_assert(sizeof(MaterialChannelOptions) == 8 && std::is_trivially_copyable_v<MaterialLayerIssue>);
}
