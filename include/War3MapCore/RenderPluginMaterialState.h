// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginMaterialAlpha.h"
#include <cstddef>
#include <type_traits>

extern "C" {
constexpr std::uint32_t WAR3_RENDER_DYNAMIC_MATERIAL_STATE_V1 = 1;
// 同一完成帧的绝对 draw 层状态。textureIndex指向已驻留scene纹理表；
// 仅沿旧层规则允许WAR3_RENDER_MODEL_NO_TEXTURE表示明确未贴图，宿主保留来源诊断。
// 坏MDX TEXS引用不能改成该哨兵，缺资源/未贴图不能报为完整贴图动画成功。
// samplerFlags仅低两位：1=U wrap，2=V wrap；未置位使用clamp。
// UV行为是二维仿射：u'=m0*u+m1*v+m2，v'=m3*u+m4*v+m5。
// 宿主无UV动画时必须传单位变换；不得逐帧重写几何或通过此通道载入贴图。
struct War3RenderMaterialStateV2 {
    std::uint32_t textureIndex, samplerFlags;
    float uvTransform[6];
};
// 可选尾部，保留所有已有descriptor尺寸/前缀。模式在场景初次提交时固定。
// 切片与base.base.base.instances同序，count为模型实际draw层数，允许已验证共享切片。
// 所有池同sceneVersion/contentVersion；输入期深拷贝，失败保留旧完成帧。
struct War3RenderMaterialStateSkinUpdateV2 {
    War3RenderMaterialSkinUpdateV2 base;
    std::uint32_t materialStateFormat, materialStateCount, materialInstanceSliceCount, reserved;
    const War3RenderMaterialStateV2* materialStates;
    const War3RenderMaterialLayerSliceV2* materialInstanceSlices;
};
// getSkinStatus可选尾部；调用前清零，能力必须由字段报告，不能按structSize猜测。
struct War3RenderSkinMaterialStateCapabilitiesV2 {
    War3RenderSkinMaterialCapabilitiesV2 base;
    std::uint32_t materialStateFormat, reserved;
};
static_assert(sizeof(War3RenderMaterialStateV2) == 32);
static_assert(offsetof(War3RenderMaterialStateSkinUpdateV2, base) == 0);
static_assert(offsetof(War3RenderSkinMaterialStateCapabilitiesV2, base) == 0);
static_assert(std::is_standard_layout_v<War3RenderMaterialStateSkinUpdateV2>);
static_assert(std::is_trivially_copyable_v<War3RenderMaterialStateSkinUpdateV2>);
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(War3RenderMaterialStateSkinUpdateV2) == 152);
#else
static_assert(sizeof(War3RenderMaterialStateSkinUpdateV2) == 128);
#endif
}
