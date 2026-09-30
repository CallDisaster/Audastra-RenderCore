<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# 本轮抽取与新增来源

冻结来源为 FORGE `a5c09254bd112667f070b2897a5d7d7cae8ea96c`。
逐文件旧源码来源与 SHA 见 PROVENANCE.md、MIGRATION.tsv；表中 SHA 是迁移前的原字节。
插件输入声明闭包按任务明确允许的 ModelSkinInput 测试依赖整体准入，仅含标准库与 C ABI。
FORGE 兼容头只转发 include；完整声明/校验体只有共享上游一份。设备与后端没有搬入。

| 本轮文件 | 来源与用途 |
|---|---|
| `include/War3RenderCore/Result.h` | 从 FORGE `include/War3MapCore/Types.h` 抽取 DiagnosticSeverity、OriginSpan、Diagnostic、Result；原文件 SHA 为 `2c1dd1882ed596785b2672ff8cd9fec68ea023371117dd8a8de506558f007cec`；Status/项目类型留宿主，四类定义只有本文件一份 |
| `include/War3RenderCore/ExternalFrame.h` | 按本轮需求新写定宽输入、独立宿主上下文、来源/空间/版本回执，无第三方实现 |
| `tests/ExternalFrameTests.cpp` | 按新合同新写原创合成样本、严格拒绝/逐位复制/零分配/预算测试，复用本工程夹具 |
| `tools/FootmanGolden.cpp` | 新写调用共享解析/编译/求值接口的离线数值导出与重算工具，没有解析器副本 |
| `docs/FOOTMAN_GOLDEN.md`、`docs/DRAW_ASSET_VIEW.md` | 本轮接口支持边界、结果格式与未来只读绘制资产规格，未实现未来绘制接口 |
| `include/War3RenderCore/Legacy/*.h` | 自动生成的单 include 兼容转发，不含第二份数据结构或算法 |
| 根 CMake、README、COPYRIGHT、Git 属性与忽略文件 | 新写的独立构建、版本/授权和本机状态说明；无外部实现 |
| `PoseBatch.*`、`PoseTypes.h`、`Diagnostics.cpp` | 在原本工程实现上新增外部帧模式与诊断，不改旧 Computed/ExternalGroups 合同 |
| `tests/MdxModelTests.cpp` | 保留原断言与样本，仅在 19 个请求聚合初始化中显式补空 camera；值与原省略初始化相同，满足 GCC `-Wmissing-field-initializers`，未屏蔽告警 |

GPLv3 LICENSE 全文从 GNU 官方 `https://www.gnu.org/licenses/gpl-3.0.txt` 取得。
这次 AI 协作开发由作者授权：代理新写代码与本工程自有文件的局部修改，并非接收外部贡献。
来源审查只报告已核验的代码/历史证据，不构成绝对原创或法律文本审定。
