// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginMaterialState.h"
#include <cstddef>
#include <type_traits>

extern "C" {
constexpr std::uint32_t WAR3_RENDER_PARTICLE_MATERIAL_FLAGS_V1 = 1;
enum War3RenderParticleMaterialFlagV2 : std::uint32_t {
    WAR3_PARTICLE_UNFOGGED = 1u
};

// 可选输入尾部；不改变旧16字节draw、旧update、scene或API表的布局。
// base.structSize必须为此完整结构的sizeof；不得复用base.reserved/filter/texture。
// materialFlagsFormat必须为V1，count必须精确等于base.drawCount，且与draws同序。
// 空帧count=0且指针必须为nullptr；非空帧指针必须非空，唯一已知位为Unfogged。
// 未知尺寸、format或位必须拒绝，不能截断新尾部或按整个资产OR所有draw。
struct War3RenderParticleMaterialUpdateV2 {
    War3RenderParticleUpdateV2 base;
    std::uint32_t materialFlagsFormat, materialFlagsCount;
    const std::uint32_t* materialFlags;
};

// getSkinStatus的可选输出尾部；没有新增API函数。调用前清零整块并将最内层
// War3RenderSkinStatusV2::structSize设为sizeof此结构。仅函数返回0且format为V1、
// mask包含Unfogged才支持本合同；status.result仍是独立的实际完成帧结果。
// 旧provider只写已知前缀，新增字段保持0；不得按API版本、输入大小或mask猜format。
struct War3RenderParticleMaterialCapabilitiesV2 {
    War3RenderSkinMaterialStateCapabilitiesV2 base;
    std::uint32_t particleMaterialFlagsFormat, particleMaterialFlagsMask;
};

// 能力不足时继续旧精确大小，所有draw沿用受雾行为；宿主保留Unfogged近似诊断。
// 有能力时可逐帧在旧/新已知结构之间切换；旧输入等价于逐draw标志全0。
// 新数组与base的scene/content/modelPose属于同一次输入，初始scene、附加layer、
// roster/append及动态更新均须输入期深拷贝并原子发布，不另设异步flags写入口。
// 每draw最多增加一个uint32；每份实际拥有的前帧、候选、GPU侧draw数组均计入原预算。
// 不提高65536 draw、maxFrameBytes或其它硬帽；失败/cancel/fault保留旧完成帧及标志。
// Unfogged仅令该draw的雾keep=1；不改变pass、深度、blend、AlphaKey或贴图上传语义。
static_assert(sizeof(War3RenderParticleDrawV2) == 16);
static_assert(offsetof(War3RenderParticleMaterialUpdateV2, base) == 0);
static_assert(offsetof(War3RenderParticleMaterialUpdateV2, materialFlagsFormat)
    == sizeof(War3RenderParticleUpdateV2));
static_assert(offsetof(War3RenderParticleMaterialCapabilitiesV2, base) == 0);
static_assert(offsetof(War3RenderParticleMaterialCapabilitiesV2, particleMaterialFlagsFormat)
    == sizeof(War3RenderSkinMaterialStateCapabilitiesV2));
static_assert(std::is_standard_layout_v<War3RenderParticleMaterialUpdateV2>);
static_assert(std::is_trivially_copyable_v<War3RenderParticleMaterialUpdateV2>);
static_assert(std::is_standard_layout_v<War3RenderParticleMaterialCapabilitiesV2>);
static_assert(std::is_trivially_copyable_v<War3RenderParticleMaterialCapabilitiesV2>);
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(War3RenderParticleUpdateV2) == 56);
static_assert(sizeof(War3RenderParticleMaterialUpdateV2) == 72);
static_assert(offsetof(War3RenderParticleMaterialUpdateV2, materialFlags) == 64);
static_assert(sizeof(War3RenderSkinMaterialStateCapabilitiesV2) == 96);
static_assert(sizeof(War3RenderParticleMaterialCapabilitiesV2) == 104);
#endif
}
