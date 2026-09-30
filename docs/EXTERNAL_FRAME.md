<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# W1 外部已求值帧：实施规格与测试清单

日期：2026-09-30。本轮实现合同与测试清单；构建结果见本机验收回执。
不修改 D3D11、WarVK 或原有动作求值行为。

## 目的与边界

新增 `ExternalFrameRequest` 和 `PoseBatchEvaluator::EvaluateExternalFrames`。请求只接收已经完成组内平均的矩阵、每 geoset 的实际 alpha/RGB，不含动作编号、动作时间、全局时钟或相机字段，也不在接收时计算这些值。

旧 `PoseRequest`、`PoseReceipt`、`ExternalGroupSlice` 的布局和大小保持不变；旧 `Computed` / `ExternalGroups` 的序列检查、计算次序、诊断、输出和回退不变。`PoseTypes.h` 只在 `Code` 末尾追加新的诊断值。

## 数据合同

- 新公共头：`include/War3RenderCore/ExternalFrame.h`，只依赖标准库和既有姿态定宽类型。
- `ExternalFrameSource` 明示 `NativeEvaluated` 或 `OfflineComputed`；零值为未知并拒绝。它是宿主声明的来源类型，核心接收成功不证明实际发生过原版采集。
- `ExternalFrameRequest` 包含 `instance`、`asset`、`assetIndex`、合同版本、来源类型、`sourceEpoch`、`frameVersion`、`ExternalGroupSlice`、alpha 范围和 RGB 范围。除合同版本默认值外，缺失的来源/帧身份保持零并拒绝。
- `ExternalFrameContext` 是宿主在每次 batch 调用时独立提供的预期合同版本、来源类型、`sourceEpoch`、`frameVersion`。输入包没有任何 `expected*` 字段；核心比较请求与该上下文，拒绝历史/错误来源的帧。
- 合同版本本轮为 1；未知版本拒绝。`sourceEpoch` / `frameVersion` 必须非零并与上下文完全一致；它们分别表示来源生命周期和其中的一帧，不与资产、求值器或设备代际合并。
- `ExternalFrameReceipt` 明示实例/资产、求值器 epoch、成功输出版本、输入合同版本/来源/epoch/frameVersion、矩阵空间与世界变换标志、错误码，以及矩阵/alpha/RGB/诊断范围。失败时成功版本为 0，所有计算结果范围为空。
- `ExternalFrameReceipts()` 返回新回执；外部帧模式下旧 `Receipts()` 返回空，避免以旧 `PoseSource::Computed` 冒充外部来源。`Groups()`、`Alphas()`、`Colors()`、`Diagnostics()` 使用当前模式的回执；`Nodes()` 返回空。

集成者已确认新增 `CreateExternalFrames(...)` 专用工厂，复用初始化和容量限制校验，准确计入新回执存储。旧 `Create(...)` 的预算和 `StorageBytes()` 保持不变。新 API 在创建阶段分配，接收阶段零分配；在旧创建模式调用新 API，以及在新创建模式调用旧 API，均明确拒绝并使旧输出失效。两个创建模式共用已有 `BatchOutcome`，不复制初始化校验分支。

## 矩阵与通道规则

- 仅 `ColumnMajor3x4`、48 字节步长；四列依次为 X/Y/Z 基列和位移列，`m[column * 3 + row]`；隐含最后一行为 `0,0,0,1`。
- 支持 `ModelLocal + worldTransformApplied=0` 和 `World + worldTransformApplied=1`。`NodeLocal`、未知空间、标志大于 1、空间/标志不一致均拒绝。
- 矩阵按原 geoset 顺序拼接，数量精确等于 `CompiledModel::GroupCount()`；顶点组号是 geoset 内局部组号。所有矩阵分量必须有限，逐位复制，不转置、不平均、不自动乘/逆乘世界矩阵。
- alpha 和 RGB 数量分别精确等于 `GeosetCount()`，不得根据动作或静态 GEOA 补齐。alpha 只要求有限，不在核心额外截断；RGB 为实际绝对值，要求有限且每分量位于 `[0,1]`，不得再次乘静态颜色。
- 使用 `offset <= size && count <= size - offset` 检查各范围，避免整数加法溢出。零 geoset 资产可有零通道，非零资产缺通道必须拒绝。
- 输入不能借用本求值器上次的任何 `Groups/Alphas/Colors` 输出；按字节区间拒绝别名，包含跨类型别名。下一次调用，包括失败调用，都会使上次全部输出失效。
- 核心接收 World 不等于当前 D3D11 绘制支持 World。W1 不接线、不改 shader，不对输入做“纠正”。

## 批次与失败

容量超限、重复实例、无效宿主上下文或输入别名是批次级错误，批次没有可读输出。资产索引、资产身份、帧身份、空间、数值、计数及范围错误是实例级错误；其回执可读，计算结果不可读。成功实例可独立保留，行为与旧批次的实例级失败边界一致。

所有结果存储在创建阶段预热。不得在热路径构造动态诊断文本、扩容容器、抛异常、做文件读取、哈希、锁等待或推进时钟。静态错误消息追加到 `Diagnostics.cpp` 时由集成者负责，实施者给出所需 Code 清单。

## 新测试清单

1. 两实例共享同一资产，分别使用不同组矩阵、alpha 和 RGB；输出与各自输入逐位一致，来源/空间/输入版本回执准确，节点输出为空。
2. 局部和世界空间各有正例；基列、位移及非均匀缩放数值均原样保留，不发生转置或空间转换。
3. 输入包含负零；用 `memcmp`/`bit_cast<uint32_t>` 验证完整逐位复制。alpha 超出 `[0,1]` 但有限仍接受；RGB 边界 0/1 接受。
4. `NodeLocal`、未知空间、space/world 标志不一致、错误 layout/stride 分别拒绝。
5. 矩阵/alpha/RGB 数量缺失、少一、多一，offset 超界及 `UINT32_MAX` 范围分别拒绝；输出范围为空，成功版本为 0。
6. 矩阵、alpha、RGB 各自的 NaN/Inf 拒绝；RGB 负值或大于 1 拒绝。
7. 资产代际过期拒绝；独立上下文预期值与请求的来源类型/epoch/frameVersion 不符、零身份、旧合同版本和未知合同版本分别拒绝。
8. 实例身份无效、重复实例、资产索引无效和批次容量超限；确认整批错误和实例错误的结果边界。
9. 从上次 `Groups/Alphas/Colors` 取得输入，包括跨类型字节别名，均在任何结果写入前拒绝；失败后使用宿主自有输入可恢复。
10. 所有正例和各类拒绝均用 `RenderCoreAllocProbe` 验证热路径零堆分配；相同数据重复输入取得单调递增的输出版本。
11. 旧 `Computed` / `ExternalGroups` 原有 oracle、compact 和所有拒绝测试不修改；追加模式切换测试确认旧回执不能泄漏为新回执，失败调用不能读到上次结果。

## Footman 数据与证据边界

旧 W1 TSV 为 Stand 500 ms、Walk 250 ms/283 ms，全局时间 0。旧报告记录模型 SHA256 `7ee255776a6e757bd89766354df763201cd0519c488585b7c30151d5e313fc13`，来源是本机游戏基础 `war3.mpq`；动作序号 0 为 `Stand - 1`，6 为 `Walk`。现有 report 只有数值指纹，必须重算导出完整矩阵/alpha/RGB 和各 geoset 的 MTGC/MATS 数值。

建议夹具以 float 的 uint32 十六进制位保存，并附合同、模型摘要、动作时间及空间说明。只存计算结果和组成员数值，不提交 MDX、贴图或其他 Blizzard 资源字节。离线结果不证明原版同帧动画或原版 palette 绑定；原版 kernel 不归一化法线而预览规则归一化的差异照实登记，不修改现有法线行为。
