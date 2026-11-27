// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginInstanceRoster.h"
#include "War3MapCore/RenderPluginParticleMaterial.h"
#include <cstddef>
#include <type_traits>

extern "C" {
// 地图给某种物体类型附加的表面材质：切线空间法线图与遮蔽/粗糙度/金属度图（glTF 通道：R 遮蔽、G 粗糙度、
// B 金属度），以及按世界坐标平铺的细节法线/细节 ORM/细节颜色。宿主已把带材质的类型拆成独立模型，所以按模型给，
// 与 models 同序。只作用于该模型 filterMode 为 0（不透明）或 1（透明测试）且未置 WAR3_RENDER_MODEL_UNSHADED
// 的层；混合、叠加、调制层照旧着色。
// 本类型的两张图按该层漫反射所用的纹理坐标与采样器采样，切线框架取自同一套坐标；所有法线图的 z 都由 xy 重建。
// 细节层两张图按世界坐标三平面投影平铺（z 朝上，p = 世界坐标 / detailScale；X 面 (p.z, p.y)、Y 面 (p.x, p.z)、
// Z 面 (p.x, p.y)，权重为几何法线各分量绝对值的四次方再归一化），重复寻址。细节法线是 OpenGL 约定，用 whiteout
// 叠在本类型法线之后；细节 ORM 只取相对自身均值的变化：遮蔽 *= mix(1, clamp(R/均值R, 0, 1.25), s)、
// 粗糙度 *= mix(1, G/均值G, s)；细节颜色图只取亮度 b（Rec.709 权重，按存储的 8 位值），基础色
// *= mix(1, clamp(b/均值b, 0, 2), s)；s = clamp(detailStrength, 0, 1)。
// 环境光随最终法线相对几何法线朝天的程度变化：环境光 *= clamp(1 + 0.5·(N.z − G.z), 0, 2)（世界 z 朝上），平法线不变。
// 五张图都为 WAR3_RENDER_MODEL_NO_TEXTURE 表示该模型没有材质，结果与旧路径相同。
constexpr std::uint32_t WAR3_RENDER_MODEL_SURFACE_V1 = 1;
struct War3RenderModelSurfaceV2 {
    std::uint32_t normalTexture, ormTexture;             // 场景纹理表序号或 NO_TEXTURE
    std::uint32_t detailNormalTexture, detailOrmTexture; // 同上
    std::uint32_t detailAlbedoTexture;                   // 同上
    float normalScale;      // 0..8：法线 xy 的强度（z 由未缩放的 xy 重建后再乘）
    float normalYSign;      // -1 或 +1：沿贴图 +v（向下）方向绿色的符号；OpenGL/glTF（绿向上）为 -1，DirectX 为 +1
    float roughness;        // 0..4：乘 orm.G；没有 orm 时直接作粗糙度
    float metallic;         // 0..1：乘 orm.B；没有 orm 时金属度为 0
    float occlusion;        // 0..1：orm.R 压暗环境光的程度
    float specular;         // 0..4：直射高光与环境高光的整体强度
    float detailScale;      // 8..65536：细节图每次重复的世界单位
    float detailStrength;   // 0..4：乘细节法线的 xy；ORM 混合取 clamp(·, 0, 1)
    float detailOrmMean[2]; // 细节 ORM 图 R、G 的均值，1/1024..1；宿主载入时算出，没有细节 ORM 时填 1
    float detailAlbedoMean; // 细节颜色图亮度均值，1/1024..1；宿主载入时算出，没有细节颜色图时填 1
};
// War3RenderColoredSkinnedSceneV2 的可选尾部；base.base.structSize 须为本结构大小，
// 仍经 submitSkinnedModelScene 或粒子/编辑器场景里的 models 指针提交。surfaceCount 必须等于模型数。
struct War3RenderSurfaceSkinnedSceneV2 {
    War3RenderColoredSkinnedSceneV2 base;
    std::uint32_t surfaceFormat, surfaceCount;
    const War3RenderModelSurfaceV2* surfaces;
};
// War3RenderAssetAppendV2 的可选尾部；base.structSize 须为本结构大小。surfaceCount 必须等于本次追加的模型数，
// 只描述追加的模型；纹理序号属于追加后的全局编号空间。已驻留模型的材质不变，旧场景没有材质时视为全无。
struct War3RenderSurfaceAssetAppendV2 {
    War3RenderAssetAppendV2 base;
    std::uint32_t surfaceFormat, surfaceCount;
    const War3RenderModelSurfaceV2* surfaces;
};
// getSkinStatus 的可选输出尾部，接在目前最长的粒子材质能力之后。调用前清零整块并设最内层 structSize
// 为本结构大小；新插件报告 surfaceFormat=1，旧插件只写已知前缀。宿主不能按 structSize 猜支持，
// 未报告时不得发送材质尾部（场景照旧着色，由宿主说明材质未显示）。
struct War3RenderSkinSurfaceCapabilitiesV2 {
    War3RenderParticleMaterialCapabilitiesV2 base;
    std::uint32_t surfaceFormat, reserved;
};
// 校验：格式、数量、非零数量的非空指针；纹理序号 < 纹理数或 NO_TEXTURE；数值有限且在上述范围；
// normalYSign 只能是 ±1。任一不符整份拒绝，保留旧场景。输入期深拷贝并计入原提交预算。
static_assert(sizeof(War3RenderModelSurfaceV2) == 64);
static_assert(offsetof(War3RenderSurfaceSkinnedSceneV2, base) == 0);
static_assert(offsetof(War3RenderSurfaceAssetAppendV2, base) == 0);
static_assert(offsetof(War3RenderSkinSurfaceCapabilitiesV2, base) == 0);
static_assert(offsetof(War3RenderSkinSurfaceCapabilitiesV2, surfaceFormat) == sizeof(War3RenderParticleMaterialCapabilitiesV2));
static_assert(sizeof(War3RenderSkinSurfaceCapabilitiesV2) == 112);
static_assert(std::is_standard_layout_v<War3RenderSurfaceSkinnedSceneV2>);
static_assert(std::is_trivially_copyable_v<War3RenderSurfaceSkinnedSceneV2>);
static_assert(std::is_standard_layout_v<War3RenderSurfaceAssetAppendV2>);
static_assert(std::is_trivially_copyable_v<War3RenderSurfaceAssetAppendV2>);
static_assert(std::is_standard_layout_v<War3RenderSkinSurfaceCapabilitiesV2>);
}
