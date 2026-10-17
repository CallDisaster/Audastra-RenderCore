// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginV2.h"
#include <cstddef>
#include <type_traits>

extern "C" {
constexpr std::uint32_t WAR3_RENDER_STATIC_LAYER_SAMPLERS_V1 = 1;
constexpr std::uint32_t WAR3_RENDER_STATIC_SAMPLER_U_WRAP = 1;
constexpr std::uint32_t WAR3_RENDER_STATIC_SAMPLER_V_WRAP = 2;
constexpr std::uint32_t WAR3_RENDER_STATIC_SAMPLER_FLAGS_MASK =
    WAR3_RENDER_STATIC_SAMPLER_U_WRAP | WAR3_RENDER_STATIC_SAMPLER_V_WRAP;
constexpr std::uint32_t WAR3_RENDER_STATIC_SAMPLER_LEGACY_FLAGS = WAR3_RENDER_STATIC_SAMPLER_FLAGS_MASK;

// 旧War3RenderModelSceneV2的完整可选输入尾部，继续通过submitModelScene或
// submitEditorModelScene中的staticModels指针提交。base.structSize须为本结构大小。
// 每项与base.layers的绝对draw层同序：0=双clamp，1=U wrap，2=V wrap，3=双wrap。
// 不能把本数组编码进旧lighting flags/filterMode/texture布局；同贴图可有不同sampler。
struct War3RenderStaticSamplerModelSceneV2 {
    War3RenderModelSceneV2 base;
    std::uint32_t samplerFormat;       // 必须为STATIC_LAYER_SAMPLERS_V1。
    std::uint32_t samplerFlagCount;    // 必须等于base.layerCount，不能只提供部分层。
    const std::uint32_t* samplerFlags;
};
// provider必须在复制前验证完整输入大小/格式、count==layerCount、非零count的非空
// 指针、零count的空指针及每项(flags & ~FLAGS_MASK)==0；不猜测未知尾部/格式。
// 同步深拷贝flags与几何，任一验证/分配/上传失败保留旧场景与完成帧。
// provider/backend的flags实际驻留容量和实际descriptor载荷均计入原模型提交预算；
// 这不表示全进程RSS上限；layer/mesh/texture等上限均不扩大。
// 旧精确大小的descriptor保持legacy双wrap，不能将新格式0或缺项当作legacy成功。

// 可选API查询的独立输出；caller先清零并设置structSize为可用容量。
// 返回0只表示能力查询成功，不代表场景已接收或GPU完成。成功必须明确报告
// samplerFormat=1、supportedSamplerFlags=3、reserved=0；flags范围为低两位0..3。
// provider只写已知前缀且将structSize设为已知大小；更大输出的未知尾字节不变。
// 容量不足/失败不写任何输出。旧provider缺函数或未报告能力时，不得发送新输入；
// host需要非legacy语义时须明确诊断并保留旧场景，不静默丢弃来源flags。
struct War3RenderStaticSamplerCapabilitiesV2 {
    std::uint32_t structSize;
    std::uint32_t samplerFormat;
    std::uint32_t supportedSamplerFlags;
    std::uint32_t reserved;
};

static_assert(WAR3_RENDER_STATIC_SAMPLER_FLAGS_MASK == 3);
static_assert(offsetof(War3RenderStaticSamplerModelSceneV2, base) == 0);
static_assert(offsetof(War3RenderStaticSamplerModelSceneV2, samplerFormat) == sizeof(War3RenderModelSceneV2));
static_assert(offsetof(War3RenderStaticSamplerModelSceneV2, samplerFlagCount) == sizeof(War3RenderModelSceneV2) + 4);
static_assert(offsetof(War3RenderStaticSamplerModelSceneV2, samplerFlags) == sizeof(War3RenderModelSceneV2) + 8);
static_assert(sizeof(War3RenderStaticSamplerCapabilitiesV2) == 16);
static_assert(std::is_standard_layout_v<War3RenderStaticSamplerModelSceneV2>);
static_assert(std::is_trivially_copyable_v<War3RenderStaticSamplerModelSceneV2>);
static_assert(std::is_standard_layout_v<War3RenderStaticSamplerCapabilitiesV2>);
static_assert(std::is_trivially_copyable_v<War3RenderStaticSamplerCapabilitiesV2>);
#if INTPTR_MAX == INT64_MAX
static_assert(sizeof(War3RenderStaticSamplerModelSceneV2) == 104);
#else
static_assert(sizeof(War3RenderStaticSamplerModelSceneV2) == 72);
#endif
}
