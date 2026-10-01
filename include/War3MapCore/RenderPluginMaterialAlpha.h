// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginModelColor.h"
#include <cstddef>
#include <type_traits>
extern "C" {
constexpr std::uint32_t WAR3_RENDER_DYNAMIC_LAYER_ALPHA_V1 = 1;
struct War3RenderMaterialLayerSliceV2 { std::uint32_t first, count; };
// 绝对层alpha；与base矩阵/GEOA/RGB属于同一contentVersion。输入期深拷贝。
// instanceSlices与base.base.instances同序；count是模型实际draw层数，不是mesh数。
// 此新结构colorFormat=0仅表示无动态RGB；旧彩色结构规则不变。
struct War3RenderMaterialSkinUpdateV2 {
    War3RenderDynamicColoredSkinUpdateV2 base;
    std::uint32_t layerAlphaFormat, layerAlphaCount, instanceSliceCount, reserved;
    const float* layerAlphas;
    const War3RenderMaterialLayerSliceV2* instanceSlices;
};
// getSkinStatus可选输出尾部。先清零；旧插件只写已知前缀，不能按size猜支持。
struct War3RenderSkinMaterialCapabilitiesV2 {
    War3RenderSkinSharedRangeCapabilitiesV2 base;
    std::uint32_t layerAlphaFormat, reserved;
};
static_assert(sizeof(War3RenderMaterialLayerSliceV2) == 8);
static_assert(offsetof(War3RenderMaterialSkinUpdateV2, base) == 0);
static_assert(offsetof(War3RenderSkinMaterialCapabilitiesV2, base) == 0);
static_assert(std::is_standard_layout_v<War3RenderMaterialSkinUpdateV2>);
static_assert(std::is_trivially_copyable_v<War3RenderMaterialSkinUpdateV2>);
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(War3RenderMaterialSkinUpdateV2) == 120);
#else
static_assert(sizeof(War3RenderMaterialSkinUpdateV2) == 104);
#endif
}
