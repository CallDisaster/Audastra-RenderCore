<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# 粒子逐绘制材质标志的可选合同

入口头为 `War3MapCore/RenderPluginParticleMaterial.h`，兼容路径为
`War3RenderCore/Legacy/RenderPluginParticleMaterial.h`。该合同为已验证的 RIBB
单层静态材质传递 Unfogged，不赋予 PRE2 节点同名原始位任何新语义。

旧 `War3RenderParticleDrawV2` 保持16字节，旧更新在 x64 保持56字节；旧 scene、
status、capture 和 `War3RenderPluginApiV2` 的大小及函数偏移全部不变。
没有新增 API 函数指针，也没有将位塞入旧 filter、texture 或 reserved。

`War3RenderParticleMaterialUpdateV2` 包含完整旧更新前缀，随后为
`materialFlagsFormat`、`materialFlagsCount` 和 `const uint32_t* materialFlags`；
x64 为72字节。调用时前缀 `structSize=sizeof(新结构)`。format只能为
`WAR3_RENDER_PARTICLE_MATERIAL_FLAGS_V1=1`，每项唯一支持
`WAR3_PARTICLE_UNFOGGED=1`，其余位拒绝。数量须精确等于drawCount，数组与draws同序；
零draw必须为零count和空指针。未知尺寸、短尾部或未知format不得被当成旧结构截断。

能力发现复用 `getSkinStatus` 的已知输出尾部：
`War3RenderParticleMaterialCapabilitiesV2` 在完整
`War3RenderSkinMaterialStateCapabilitiesV2` 后增加
`particleMaterialFlagsFormat`、`particleMaterialFlagsMask`。
现有 x64 前缀为96字节，新结构104字节。宿主清零整块并设置最内层status.structSize，
确认函数可调用且返回0、format为1、mask含Unfogged后才发送新更新。
status.result仍独立表示帧是否完成，能力发现不能伪造完成帧。
旧provider只写自己的前缀，新增字段保持0；旧provider夹具只验证此前缀约定。

能力缺失时宿主继续发送旧精确结构，并保留 Unfogged 近似诊断。
新provider收到旧输入时所有draw仍受雾；收到新输入时只按对应draw的位决定keep=1。
已知旧/新结构可逐帧切换，不要求改变静态纹理或模型资产。pass、深度、AlphaKey、
blend、排序、模拟、随机数、发射和时钟不在本合同的变更范围。

标志必须与原scene/content/modelPose同次深拷贝并原子发布，适用于初始提交、动态帧、
附加模型层、roster/append和完成帧。每draw最多增加一个uint32，实际持有的前帧、
候选和GPU侧draw数组都须计入已有工作预算；不提高65536draw或其它硬帽。
拒绝、取消或GPU fault保留上一完成帧及其标志。输入返回0只能表示CPU接收。

`render-particle-material-contract` 检查新增布局、冻结旧draw/update/API表及两种旧provider
前缀夹具。本核心库不包含实际粒子provider或GPU后端。生产输入校验/深拷贝、预算、
所有事务入口及线性/EXP/EXP2雾下的逐draw完成帧像素均由消费者独立验收；
不能将此合同测试视为 RIBB GPU、原版或游戏等价证明。
