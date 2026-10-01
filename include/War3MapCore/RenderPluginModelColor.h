// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster

#pragma once
#include "War3MapCore/RenderPluginV2.h"
#include <cstddef>
#include <type_traits>

extern "C" {
// GEOA 静态 RGB 乘数，原始 float32，不做 BGR 交换、量化或伽马转换。
// 只在静态场景提交时传入，与 mesh 全局顺序平行；材质层共用所属网格的乘数。
struct War3RenderMeshColorV2 { float rgb[3]; };
constexpr std::uint32_t WAR3_RENDER_STATIC_MESH_RGB_V1 = 1;
struct War3RenderColoredSkinnedSceneV2 {
    War3RenderSkinnedModelSceneV2 base;
    std::uint32_t colorFormat, colorCount;
    const War3RenderMeshColorV2* colors;
};
// getSkinStatus 的可选输出尾部。调用前清零整块，base.structSize=sizeof(此结构)。
// 老插件只写旧前缀，colorFormat 保持0；新插件报告1。调用者不能用structSize猜支持。
struct War3RenderSkinColorCapabilitiesV2 {
    War3RenderSkinStatusV2 base;
    std::uint32_t colorFormat, reserved;
};
// KGAC 动态 RGB 独立于粒子接口；颜色与 alphaCount/firstAlpha 共用每实例网格序。
// 是该帧的绝对 GEOA RGB，不再乘静态 meshColors；实例色仍相乘。
// 初始姿势决定此场景是否使用动态颜色；后续更新不能切换布局，必须完整携带颜色。
constexpr std::uint32_t WAR3_RENDER_DYNAMIC_MESH_RGB_V1 = 1;
struct War3RenderDynamicColoredSkinUpdateV2 {
    War3RenderSkinUpdateV2 base;
    std::uint32_t colorFormat, colorCount;
    const War3RenderMeshColorV2* colors;
};
struct War3RenderSkinDynamicColorCapabilitiesV2 {
    War3RenderSkinColorCapabilitiesV2 base;
    std::uint32_t dynamicColorFormat, reserved;
};
struct War3RenderSkinSharedRangeCapabilitiesV2 {
    War3RenderSkinDynamicColorCapabilitiesV2 base;
    std::uint32_t sharedRangeMatrixFormat, reserved;
};
static_assert(offsetof(War3RenderSkinSharedRangeCapabilitiesV2, base) == 0);
static_assert(std::is_standard_layout_v<War3RenderSkinSharedRangeCapabilitiesV2>);
static_assert(offsetof(War3RenderDynamicColoredSkinUpdateV2, base) == 0);
static_assert(offsetof(War3RenderSkinDynamicColorCapabilitiesV2, base) == 0);
static_assert(std::is_standard_layout_v<War3RenderDynamicColoredSkinUpdateV2>);
// 新宿主须先查询能力，再将彩色场景.base传给原submitSkinnedModelScene。
// 无颜色能力且存在非白RGB时必须拒绝；不得把彩色尾部送给会静默忽略它的旧插件。
// 描述符尺寸、格式、数量、有限0..1分量严格验证；输入期深拷贝，失败保留旧模型。
// 颜色属于sceneVersion，动态矩阵更新不修改颜色；旧场景结构和所有函数签名/布局不变。
static_assert(sizeof(War3RenderMeshColorV2) == 12);
static_assert(offsetof(War3RenderColoredSkinnedSceneV2, base) == 0);
static_assert(offsetof(War3RenderSkinColorCapabilitiesV2, base) == 0);
static_assert(std::is_standard_layout_v<War3RenderColoredSkinnedSceneV2>);
static_assert(std::is_standard_layout_v<War3RenderSkinColorCapabilitiesV2>);
}
