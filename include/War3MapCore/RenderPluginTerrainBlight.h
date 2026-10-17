// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 CallDisaster
#pragma once
#include "War3MapCore/RenderPluginV2.h"
#include <cstddef>
#include <type_traits>

extern "C" {
constexpr std::uint32_t WAR3_RENDER_TERRAIN_BLIGHT_SHARED_BASE_V1 = 1;
constexpr std::uint32_t WAR3_RENDER_TERRAIN_BLIGHT_MAX_BATCHES = 32;

// caller清零并填写可用structSize。成功仅写已知16字节前缀，并把structSize设为16；
// 更大输出的未知尾字节不动，容量不足/失败不写任何输出。
// 格式1必须明确报告blightFormat=1、maxBlightBatches=32、reserved=0。
// 允许null renderer查询provider编译能力，不创建/访问GPU状态；非null时仍须是有效handle。
// API表容量或非空submit指针不能替代查询；查询不接收场景，也不证明设备可用或GPU完成。
struct War3RenderTerrainBlightCapabilitiesV2 {
    std::uint32_t structSize;
    std::uint32_t blightFormat;
    std::uint32_t maxBlightBatches;
    std::uint32_t reserved;
};

// 一批只采样真实8列×4行荒芜atlas的一个cell，不携带新顶点流或任意shader参数。
// UV必须精确等于该cell在mip0的半texel内缩矩形：
// left=(column*cellWidth+0.5)/width，right=((column+1)*cellWidth-0.5)/width；
// top/bottom同理，cellWidth=width/8=height/4。合法1像素cell允许两端相等。
// provider可枚举32个合法矩形核验；同一cell不能出现第二批，所有值须有限。
struct War3RenderTerrainBlightBatchV2 {
    std::uint32_t firstIndex, indexCount;
    float uvLeft, uvRight, uvTop, uvBottom;
};

// 只通过新optional submitTerrainBlightFrame提交，不扩展旧descriptor的structSize。
// 本结构和内嵌baseScene的structSize分别等于各自确切大小；documentRevision只取baseScene。
// baseScene仍遵守旧16纹理/原mip合同；blightTexture是单独一份8×4、方形cell原mip链。
// 新入口不是追加调用：base、过渡、现有水面与精确荒芜一次替换同一terrain revision。
struct War3RenderTerrainBlightFrameV2 {
    std::uint32_t structSize;
    // 全部可见基础quad数，含height-color fallback。前baseQuadCount*4顶点固定SW/SE/NW/NE。
    // 有hidden格时host用baseTiles映射；无hidden时空baseTiles表示原tile的identity映射。
    std::uint32_t baseQuadCount;
    War3RenderTerrainMipSceneV2 baseScene;
    std::uint32_t blightIndexCount, blightBatchCount;
    const std::uint32_t* blightIndices;
    const War3RenderTerrainBlightBatchV2* blightBatches;
    const War3RenderTextureMipChainV2* blightTexture;
};

// 非空pass：三个指针均非空，indexCount为6的倍数，batchCount为1..32。
// batches按firstIndex从0连续完整覆盖blightIndices，每批非空且indexCount为6的倍数；
// 按cell稳定分组，组内维持原tile/baseQuad顺序。每quad在全部批次中最多出现一次。
// 每组6索引只能是{q*4,q*4+1,q*4+2,q*4+1,q*4+3,q*4+2}，q<baseQuadCount；
// 不引用transition/water顶点，不换对角线。被引用quad还必须在baseScene的实际基础批次中
// 由不带ALPHA_BLEND/TRANSLUCENT的有效贴图完整绘制；height-color fallback不可引用。
// provider核有界几何/矩形/去重/实际基础贴图资格；host仍负责sealed来源、mask/variation、
// 原tile绑定和hidden排除，provider不把几何验证当作地图来源认证。
// 空pass：两count必须均为0，三个指针必须均为null；仍提交baseScene并清除旧精确荒芜。
// submitMesh/旧submitTerrainScene/旧submitTerrainMipScene成功接收也必须清除此新pass。
//
// base+blight共用原硬帽：unique vertices<=1048576，indices合计<=6291456，
// batches合计<=4096；两份资源表全部mip的RGBA合计<=64MiB，每级/链/尺寸沿旧上限。
// 合成描述符、索引与深拷贝载荷合计<=128MiB；新增有界验证暂存/owned容量须在所属
// builder/provider工作量口径内预检计费，不能每pass各领预算。它不是全进程RSS或旧新GPU峰值门。
// base vertices只计/复制一份，blight不追加四顶点/格；不能借model layer预算。
//
// 全部描述符、indices、mip像素在调用期间深拷贝，校验/分配失败不发布半份CPU场景。
// untinted base/transition和完整精确荒芜必须一起接收；失败保留旧近似场景。
// 返回0仅为CPU接收；GPU资源仍在现渲染线程异步全部准备后一次swap，失败保留前GPU场景。
// 完成/截图/拾取沿同一sceneEpoch与baseScene.documentRevision，不能用旧帧冒充新成功。
// 顺序为base+transition、blight（相同XYZ变换、LESS_EQUAL并写深度）、不透明模型、
// 现水面（深度只读）、透明模型；保留原alpha/mip/cell clamp/雾/阴影语义。
// VS用索引顶点ID%4生成SW/SE/NW/NE的UV（SW/SE取bottom），BaseVertexLocation固定0；
// 所有普通draw显式关闭UV override。地形pick仍用原CPU高度网格，不新增overlay可选实体。
// 水面高度/逐角深度色及hidden格覆盖不由本格式解决。

static_assert(sizeof(War3RenderTerrainBlightCapabilitiesV2) == 16);
static_assert(sizeof(War3RenderTerrainBlightBatchV2) == 24);
static_assert(offsetof(War3RenderTerrainBlightBatchV2, uvLeft) == 8);
static_assert(offsetof(War3RenderTerrainBlightBatchV2, uvRight) == 12);
static_assert(offsetof(War3RenderTerrainBlightBatchV2, uvTop) == 16);
static_assert(offsetof(War3RenderTerrainBlightBatchV2, uvBottom) == 20);
static_assert(offsetof(War3RenderTerrainBlightFrameV2, baseScene) == 8);
static_assert(offsetof(War3RenderTerrainBlightFrameV2, blightIndexCount) == 8 + sizeof(War3RenderTerrainMipSceneV2));
static_assert(std::is_standard_layout_v<War3RenderTerrainBlightFrameV2>);
static_assert(std::is_trivially_copyable_v<War3RenderTerrainBlightFrameV2>);
}
