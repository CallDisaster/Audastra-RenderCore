<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# W0/W1 文件级来源审查（只读产品代码，2026-09-30）

## 0.1.12 原序节点预览增补（2026-10-07）

`NodeKeySamplingPolicy.h`、`SourceOrderNodeTrack.h`、`GameKeyRanges.h`（0.1.16 公开，依据 IDA 反汇编原创编写）与对应 API/诊断/预热接线由本项目原创。
`SourceOrderPreviewFixture.h` 为原创空几何 MDX 字节夹具，数值独立编写；真实本机模型仅
通过环境变量及固定地图/模型 SHA 门禁测试，不在源码或包中包含。
接口/算法/测试的来源和等价边界详见 `SOURCE_ORDER_NODE_PREVIEW.md`。第三方公开读取器
仅作格式和边界资料，未复制 WarVK、第三方或游戏实现，未宣称原版缓存等价。
此段是当前候选增补；下面原始 W0/W1 逐文件 SHA 表属于其注明的历史冻结点。

冻结源码：`${FORGE_SOURCE_DIR}`，HEAD `a5c09254bd112667f070b2897a5d7d7cae8ea96c`。初查从主检出同一 HEAD 开始；随后已转冻结 worktree，当前 HEAD 与所有待审产品文件 diff 均为空。审查代理没有编译、移动源码、改许可证或联系其他聊天；唯一写入是本报告。

## 来源结论与证据限度

这份清单的 A 项可按本工程独立编写的实现准入共享仓库：逐段阅读当前文件、追查引入/后续 Git 记录、核对 Astra 交付包 blob 后，没有发现 WarVK、DXVK、HiveWE、mdx-m3-viewer 或其他项目的被复制实现/着色器、原版模型字节、反编译函数体。Git 作者/提交者仅有 CallDisaster 的两种邮箱。`672b5b0` 另有 Claude Opus 5.5 Co-Authored-By；`3260670` 明示 Code authored by Astra，这是 AI 协作来源记录，不应把 Git 作者姓名等同为完整原创或法律权属证明。

“未发现第三方实现”是本次内容与本地历史审查结果；不是对任意历史未复制的绝对保证。未出现有理由判定被复制、却不清楚许可的文件。外部行为/格式/数学资料明确列在下节。现有源码没有 SPDX/版权头本身不构成原创证据；授权仍依据用户在本会话对所拥有代码的明确决定。

### 交付来源链

- `32606706b195890218b4aa16aab9685e4199ad14`：正文说明由 Astra 编写、按 f977ff4 后继应用20路径补丁。`artifacts/astra-rounds/r1/delivery/STEP_1_NOTES.md` §1 明示未复制 WarVK/YDWE/第三方实现/着色器，沿 FORGE 自有参考公式；`r1/receipt.md` 记录 20/20 blob 一致。此次再次将 R1 MANIFEST.json 全部20项 `git_blob_sha` 与 `3260670:path` 比较：20项，0差异。
- `9970088f8ed33590ca126a4d685a7db410b2995f`：R2 输入校验/扫描/紧凑输出；`r2/delivery/STEP_2_NOTES.md:42` 声明没有加入第三方实现，`RenderSkinFixture.h:2` 明确原创双实例平面。此次再次比较 R2 MANIFEST.json 的36项 `result_git_blob` 与 `9970088:path`：36项，0差异。
- `672b5b0bd0561256cd2d6dd694f93182a79d73c4`：首次 MDX 解析器/DTO/测试，正文描述有界实现并标注原创合成模型测试。`0ffba9b325606256e8d9e955ae27139ce7b034b5` 首次自有冻结姿态参考；`68f33f7` 继续实现有标记的通用预览。
- `15add21`：将本工程 Reader/Node 解析抽为 MdxReadInternal/MdxNodeRead，并增加七种公共节点；`308cbe4f1661c8e3643ec2e0c13154c82a2f8549`：根据只读原版行为研究独立实现继承位，同时保留独立参考/手算样本；`3882a0c`、`ce020c5`、`c1be656`：零法线/纯粒子结构/扫描说明；`a6667eb`/`41133694f1dcb1ebe8f34cbeca475d2a604bc3b4`：GEOA通道与全局键原点后续工作。
- `461c3fb`：Types.h 基础模板/诊断 DTO；Result 当前实现从本工程初始提交一直未变。

### 明示的外部资料：保留归因，不搬第三方实现或研究原文

1. `MdxNodeRead.h:5–6` 参照 GhostWolf 的 [MDX Specifications](https://www.hiveworkshop.com/threads/mdx-specifications.240487/) 的节点字节边界，原注释明确“不是移植实现”。本次读到的是自有有界 Reader 与固定字段解析，格式标签/宽度事实不等于移植其 parser。
2. `MdxPose.cpp:76–80` 引 [Microsoft XMQuaternionSquad](https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmquaternionsquad) 与 [gl-matrix quat.js](https://github.com/toji/gl-matrix/blob/master/src/quat.js) 的公开数学组合，原注释明确“自行实现，不复制外部代码”；当前 C++ 自行正规化/双精度中间量/诊断，与 gl-matrix JavaScript 实现结构和控制不同。本次在线打开 gl-matrix 原文件和 MDX 规格核对来源存在，没有将其源文件拷进包。
3. `MdxPose.cpp:321–323` 引 Microsoft XMMatrixLookAtLH 的 eye/target/up 构造约定；代码使用 MDX Z-up、自有轴锁/退化策略。PoseMath.cpp 是对自有冻结参考公式的热路径实现。
4. `Inheritance.md` 与 `PoseInheritance.cpp`/`MdxInheritanceReference.inl` 保留 Game.dll 1.27a 行为研究事实；实现用本工程 std::array/double/float/hypot，不带 IDA 导出的函数体、原版程序集或 WarVK 源码。`dbbdb17`/`8131c3f`/`4113369` 的全局轨迹修正同样是行为研究后的自有采样规则。共享包不要附 IDB、IDA 导出或模型/纹理成员。

## 迁移决策

- A：来源审查准入，可以迁移；旧数值/测试语义必须保持，公开头路径可用 forwarding include，不保留第二份实现。
- P：来源准入但仅抽取文件内指定部分；Types.h 只抽 DiagnosticSeverity、OriginSpan、Diagnostic、Result，Status/项目类型留 FORGE，不能整文件另留重复。
- 插件闭包：内容来源全部准入；ModelSkinInput/ModelSceneData/RenderPlugin(V2)/RenderPluginModelColor 及 skin 输入测试可按集成负责人批准迁入共享，FORGE 留纯 include wrapper，保持 ABI 字节布局和尾部行为，禁止另留第二份 body。
- R08 关联：RenderPluginV2.h 含 `739cd00` 已提交的编辑显隐尾部；这次只做位置/引用变化，已有尾部内容/布局仍属冻结对照范围。
- O：自有历史文档，可保留来源归档；过时命令/历史测试状态不当作共享库当前说明或验收。

没有需要作者裁定第三方授权后才能迁移的疑点文件。原 C/H 建议是架构范围判定，不是发现抄袭或未经许可代码。集成负责人随后确认：RenderPluginV2 的 R08 改动已在 739cd00 提交，无当前归属冲突；允许测试闭包纳入共享并让 FORGE 保留纯 include wrapper，ABI body 只留一份。因此相关实现/DTO/测试全部按来源 A 准入，具体迁移按这项集成决定执行。`GeosetChannelTestModel.h` 在冻结树不存在，实际夹具为 `tests/GeosetChannelFixture.h`。

下面每行给出当前行数、首次引入/最近修改提交、冻结原字节 SHA-256、用途和内容来源结论；所有路径相对上述冻结源码根。原有RenderCore TestTargets 的直接闭包之外补列 MdxPose.h/MdxModelTests/DTO与夹具，以及插件输入传递闭包。

| 来源路径 | 状态 | 行数 | 首次引入 | 最近修改 | 用途 | 内容来源结论 | 冻结 SHA-256 |
|---|---|---:|---|---|---|---|---|
| `include/War3RenderCore/CompiledModel.h` | A | 49 | `3260670` | `a6667eb` | 不可变编译资产公共入口 | R1，自有DTO与Result接口，无第三方类型 | `bdd850501886aba3bd3cf490f97e17b6ca46448aeb5074261eddf2e1ce3d1920` |
| `include/War3RenderCore/GeosetColor.h` | A | 40 | `d90cb95` | `c53e226` | 静态GEOA RGB校验 | 颜色检查点独立实现，标准库有限数校验 | `02b208f915552e3a78bc7365d7835e49adeda8ec984b185db6c5cadf809a479b` |
| `include/War3RenderCore/ModelContent.h` | A | 18 | `ce020c5` | `ce020c5` | 模型结构分类 | Step4自有解析DTO分类，不含粒子资源 | `d00e0314059223d7ec1cf604667a577a9b434698f0aefdbb7465e2259b12a415` |
| `include/War3RenderCore/PoseBatch.h` | A | 45 | `3260670` | `a6667eb` | 批量姿态与输出寿命接口 | R1/R2自有预热求值合同 | `79b803a5ad77b316ca72588b5050cb979b39897473463edcff119d72f5e54f4f` |
| `include/War3RenderCore/PoseTypes.h` | A | 195 | `3260670` | `4113369` | 身份/组矩阵/请求/回执DTO | R1/R2自有定宽DTO | `a11539fbebbb61ad43e0ad71a973be6526b29ca62cbae8c4bd8106d1fe9c103b` |
| `src/RenderCore/CMakeLists.txt` | A | 27 | `3260670` | `a6667eb` | CPU独立库与浮点编译选项 | R1自有构建登记 | `30600e8822e43f11538dbfb3cef8e131f7b04bd20bc928848ec764700768b691` |
| `src/RenderCore/CompiledModel.cpp` | A | 261 | `3260670` | `4113369` | 轨迹拓扑编译与容量校验 | R1与Step4自有有界编译，沿FORGE参考公式 | `eb9ed00084679704f41f8a88feaf83d6de13d57efdac9bdaf3bd1973dd62c659` |
| `src/RenderCore/Diagnostics.cpp` | A | 95 | `3260670` | `4113369` | 诊断码与中文展开 | R1自有诊断映射 | `3212c1bc39387058d1989f778b18f454f2f365251f92d9c43c88d9ff854fe2c2` |
| `src/RenderCore/GeosetChannels.cpp` | A | 99 | `d324bad` | `4113369` | GEOA alpha/RGB轨迹采样 | B阶段独立编写；通用插值多项式 | `3785b5678370a04f319dee242d4945aedbf9aa09d4ed2fd685523afc15d7f768` |
| `src/RenderCore/GeosetChannels.h` | A | 10 | `d324bad` | `a6667eb` | 颜色轨迹内部入口 | B阶段自有内部声明 | `1949832f122c205ad782f896f018e3cacf7c7404e9c23663f28d80ec5009b371` |
| `src/RenderCore/Inheritance.md` | A | 49 | `308cbe4` | `308cbe4` | 继承位行为研究摘要 | IDA行为事实与自有说明；无反编译函数文本 | `5796183a734ce007977e2e5e78f8b0e940ca580f3e9d24e886c3b59a38235178` |
| `src/RenderCore/ModelData.h` | A | 98 | `3260670` | `4113369` | 核心私有资产与轨迹存储 | R1自有std容器模型 | `2029893ec7869e230a0d318a51ffd648c2444e44b78812bbc1ee7cecff8ada49` |
| `src/RenderCore/PoseBatch.cpp` | A | 224 | `3260670` | `a6667eb` | 批次校验与无分配求值 | R1/R2自有实例和容量规则 | `96c3c59627bd4c43f19da2a437d1f0a9be217f4f89968a0e0c51643cbe7fc312` |
| `src/RenderCore/PoseInheritance.cpp` | A | 55 | `308cbe4` | `308cbe4` | 继承位矩阵计算 | 308cbe4行为研究后的独立C++实现；未照搬原版SSE | `8d2b166349b76b124754c3b144c52055dcf74e5fa829a6c87d196c0be78dffbe` |
| `src/RenderCore/PoseMath.cpp` | A | 202 | `3260670` | `3882a0c` | 四元数/矩阵/相机/法线公式 | R1声明沿FORGE参考实现，不包含WarVK或第三方实现 | `0c776112a07635b01b9ed3bf8f0b145f4d76c04b5e39aa6d1ec9111245be7793` |
| `src/RenderCore/PoseMath.h` | A | 35 | `3260670` | `308cbe4` | 数学内部声明 | R1自有声明 | `e8928bed46d747bcd080ec7f9b1fd946751d152d123d132191c7f5897305e8b7` |
| `src/RenderCore/PoseTracks.cpp` | A | 133 | `3260670` | `4113369` | TRS选段/骨架/矩阵组采样 | R1独立编写并与FORGE参考逐位差分 | `6b8cc244ede65577b69e19f9f7b4f48af17c506a7eeb8665b4a0485aaecef72d` |
| `src/RenderCore/PoseTracks.h` | A | 8 | `3260670` | `a6667eb` | 轨迹求值内部声明 | R1自有声明 | `7917e1bde3a81d9dff26403ffe954541cbd3eeee711956937a39683743a0d7f3` |
| `src/RenderCore/PreviewSkin.cpp` | A | 25 | `3260670` | `3260670` | CPU预览蒙皮对照助手 | R1标准矩阵变换，无外部实现 | `075932cae236239a0efe58accb40ca5ff2307e97a3525d3a986c49c9a12da6a3` |
| `src/RenderCore/README.md` | A | 129 | `3260670` | `9970088` | 接口与数值寿命文档 | R1/R2自有文档；搬迁需更新路径/授权陈述 | `7309c2871917d48fa552f9de5512206ea620475db980dac7834c314c446fb340` |
| `src/RenderCore/Step4Validation.md` | O | 141 | `c3562aa` | `c3562aa` | 历史Step4验证说明 | 自有回执，但为旧环境历史操作，不宜作新共享库当前验收 | `dc68f18629afcf64205857d108dce53337a53b1ff874d0e3700a524c01ef1fc9` |
| `src/RenderCore/TestTargets.cmake` | A | 94 | `3260670` | `a6667eb` | 原CPU/参考测试登记 | R1/R2/B自有登记；已包含完整插件输入传递闭包 | `1fe6de1334b30f93dd9db9483d187660d76b58ec39c505b39e50b3fc521b3cb1` |
| `include/War3MapCore/Render/MdxModel.h` | A | 250 | `672b5b0` | `aa91d94` | MDX解析DTO/轨迹与接口 | 672b5b0起本工程自有格式DTO；无外部库结构引用 | `e4bbf5ae729e70af18a80e1fe2a9ca7db41c3fb8f2a90c5623ca0ada8bb4abea` |
| `include/War3MapCore/Render/MdxPose.h` | A | 71 | `0ffba9b` | `68f33f7` | FORGE冻结姿态参考接口 | 0ffba9b起本工程参考接口 | `dcbdf7c6af0adf03861a19b4b5618733e821563ac811855ee9d30d5c430a7cf2` |
| `src/Render/Assets/MdxModel.cpp` | A | 510 | `672b5b0` | `aa91d94` | 经典MDX v800有界解析器 | 672b5b0自有解析器，提交明确原创合成测试 | `92f80387d860bd382b01c1f310f6edacef9f2c417e02aa028f7a16b61278d342` |
| `src/Render/Assets/MdxPose.cpp` | A | 599 | `0ffba9b` | `4113369` | 节点/蒙皮冻结参考实现 | 0ffba9b起自有参考；68f33f7数学引用明确自行实现 | `65a7f606441b2256bfb0e0ac9c5e8eddbcb2b00f370930f76356807596b253c5` |
| `src/Render/Assets/MdxGeosetRead.inl` | A | 54 | `d324bad` | `a6667eb` | GEOA独立对象读取 | B阶段自有有界读取，非第三方实现 | `aa816bb6ee1a97666f837913a887668bf08b6e3e5e00b39b61972c1eb5d06a5e` |
| `src/Render/Assets/MdxGeosetSample.inl` | A | 95 | `d324bad` | `4113369` | GEOA冻结参考采样 | B阶段独立扫描原始键，不调用新核心 | `407de57920bdbfd650e8add890760795f39bc9ea7f4d7e7d27b191b61711831e` |
| `src/Render/Assets/MdxInheritanceReference.inl` | A | 51 | `308cbe4` | `308cbe4` | 继承位冻结参考计算 | 308cbe4独立实现行为研究结论；无原版反编译代码 | `3d37fcb7f6e40724059687594a7e6ef2221fd6856d7d511c3384d73e748b9c90` |
| `src/Render/Assets/MdxNormalPreview.h` | A | 20 | `3882a0c` | `3882a0c` | 零法线统计与诊断 | 3882a0c自有统计与中文诊断 | `9e347de600dba01c5e3548a5bf071d3f28edf426db0caaddd2802f86ffdf487f` |
| `src/Render/Assets/MdxNodeRead.h` | A | 154 | `15add21` | `15add21` | 各类公共Node和轨迹读取 | 15add21自有Reader/Node解析；GhostWolf格式规格事实引用明确非移植 | `caa98b04e8223c7e43fd3c45ceb117f538ebc1e59cdedac809b2ba251b26d41b` |
| `src/Render/Assets/MdxReadInternal.h` | A | 96 | `15add21` | `15add21` | 有界标量/块读取器 | 15add21从本工程MDX解析器抽取同一Reader | `e47967ceebc6ed48c4cb628e8f5be32b243c7d6bd1eb38852cd6a491710b5656` |
| `include/War3MapCore/Types.h` | P | 127 | `461c3fb` | `461c3fb` | 共享Result及其诊断依赖 | 461c3fb自有基础；仅抽DiagnosticSeverity/OriginSpan/Diagnostic/Result | `2c1dd1882ed596785b2672ff8cd9fec68ea023371117dd8a8de506558f007cec` |
| `src/Render/Tools/MdxScan/ScanTypes.h` | A | 59 | `9970088` | `55a594b` | 扫描记录/有界声明 | R2/C阶段自有std数据结构 | `f31f31cc0b587a072592acc71b6e3a51f8f4312c7cb5acdd7de0c0d553db05c5` |
| `src/Render/Tools/MdxScan/ScanDetail.h` | A | 10 | `c1be656` | `c1be656` | 扫描详情接口 | c1be656自有声明 | `f47787c9c1739581640a46ca29b9f12ef57cccf1606c2f66d4d9b2f414dcd94f` |
| `src/Render/Tools/MdxScan/ScanDetail.cpp` | A | 82 | `c1be656` | `c1be656` | 解释节点/法线/坏动作 | c1be656自有遍历和诊断聚合 | `c4ca1b398e4604157cb9bc6a4f4e32023cf1efed9e337479c1379506368396d1` |
| `src/Render/Tools/MdxScan/ScanRecord.cpp` | A | 146 | `9970088` | `55a594b` | TSV记录/转义/读写校验 | R2/C阶段自有有界记录，无外部JSON或编码实现 | `f90bce66aa09ca58526e3659f4ab94cc29650cc3073d758464e4e7df195a9515` |
| `src/Render/Scene/ModelSkinInput.h` | A | 11 | `9970088` | `9970088` | 插件蒙皮输入CPU校验接口 | R2自有代码；技术闭包随已批准唯一body迁移 | `ca2ea90ee0a1df5f02b3853986c9ad52be4d62042ba64b674afeef241eac3f21` |
| `src/Render/Scene/ModelSkinInput.cpp` | A | 163 | `9970088` | `a6667eb` | 插件场景/更新深拷贝与拒绝门 | R2/A/B自有有界校验；批准与其完整DTO闭包迁移 | `c7d1ccd743a5c49866085cc98461c480ed275ff8f1090170aa6f414d5b13eed5` |
| `src/Render/Scene/ModelSceneData.h` | A | 40 | `9970088` | `a6667eb` | 插件内部几何/贴图/姿态存储 | R2/A/B自有std结构；本轮依既有测试闭包纳入 | `18599674d0ed3fd761f1a901943aa0cf86360281962a3f562341e43ec8382d94` |
| `include/War3MapCore/RenderPlugin.h` | A | 51 | `461c3fb` | `76ac0c1` | FORGE插件V1函数表 | 本工程自有C ABI；批准唯一body迁移，FORGE仅留wrapper | `072812ea04afaeb68cb6bfb1ec2667fa41d2b7a22b3d07dc4f79b4480d4c8c2d` |
| `include/War3MapCore/RenderPluginV2.h` | A | 334 | `b65a6c2` | `739cd00` | FORGE插件V2及R08可选尾部 | 本工程自有C ABI；739cd00已有尾部保持冻结，批准唯一body迁移 | `429e4ac8f6e3657c8296382a27ae7b6f440b6d64100904f0d351df8c9499bf75` |
| `include/War3MapCore/RenderPluginModelColor.h` | A | 47 | `3fb9b10` | `a6667eb` | FORGE插件静态/动态颜色尾部 | A/B自有C ABI；批准唯一body迁移，已有尾部语义不变 | `6f32b72beb1a61011a5bcc6734323102d02086d860a1d64d05a286ddb9043602` |
| `tests/RenderCoreAllocProbe.cpp` | A | 43 | `3260670` | `3260670` | 测试operator new分配探针 | R1自有测试探针；仅测试链接，不进产品库 | `1690a9576ae53543de38e027d774dc007f1a7635aa52564bfee21c1ebf615d7f` |
| `tests/RenderCoreAllocProbe.h` | A | 6 | `3260670` | `3260670` | 探针声明 | R1自有声明 | `d3e0ec06abe01b5a70e0780011a100f62447d1d6b00158d9fcc88e498a65ca65` |
| `tests/RenderCoreTests.cpp` | A | 331 | `3260670` | `4113369` | oracle/双实例/ExternalGroups回归 | R1自有合成模型和冻结参考逐位比较 | `0b9e82ce42a00b6ccfd226f6122edc9ab0f3028c358b0beabb8a2b3c74657bd1` |
| `tests/RenderCoreCompactTests.cpp` | A | 110 | `9970088` | `9970088` | 2000合成实例紧凑输出与帧所有权 | R2自有合成规模数据，不含实际地图资源 | `5ee4b4f26a056e7f76cd0f531786a3ec7abce5aed6c10035a43aa8c89a305d79` |
| `tests/MdxModelTests.cpp` | A | 511 | `672b5b0` | `64a9347` | 解析/姿态/蒙皮原测试 | 672b5b0起原创模型和手算值；没有真实模型字节 | `49c94d122d2a5b1ef31e915fbf3b015c793768575723f69aa59b3fcb78d43717` |
| `tests/MdxTestModel.h` | A | 124 | `aa1c84d` | `edc22b3` | 合成MDX二进制Writer/夹具 | aa1c84d明确原创，不来自任何游戏文件 | `b8f01aa3124735a71ddbc3f1858d866aaa250e979c39e1594387a6fceaf417e3` |
| `tests/RenderCoverageTests.cpp` | A | 86 | `227f414` | `c1be656` | 坏序列隔离与coverage入口 | 227f414/c1be656自有合成破坏测试 | `3d46bb0c636604ddcca0a76915db1496a626c4b6b4abd9c52626831864eb8b54` |
| `tests/RenderCoverageNodes.h` | A | 81 | `15add21` | `15add21` | 七种公共节点父链夹具 | 15add21复用自有Writer，固定负载零填，不来自模型文件 | `1aa3aea0ae5adb10280a464487fdb1db76390920957a4a4a6e4b8d9b30da1cdb` |
| `tests/RenderCoverageTransforms.h` | A | 73 | `308cbe4` | `308cbe4` | 继承位手算与逐位检查 | 308cbe4自有合成旋转/缩放/轴心期望值 | `3742114317a9269ebe952caae686df1093642d9e5b697b78a7c104d217c01a06` |
| `tests/RenderCoverageNormals.h` | A | 54 | `3882a0c` | `3882a0c` | 零法线/非有限对照 | 3882a0c自有合成值 | `e3c0a1e44024b73a6c2a9fa38f839c23a901d3adbb5d7ec44ae56c312a9b6c37` |
| `tests/RenderCoverageParticles.h` | A | 61 | `ce020c5` | `ce020c5` | 纯粒子结构分类测试 | ce020c5自有合成Node，无实际粒子模型字节 | `e1fddf32d51066014a89e05cb8c3a5bef6ca176b85b57aa11c6b0e061aa4c5b5` |
| `tests/RenderCoverageScan.h` | A | 48 | `c1be656` | `55a594b` | TSV详情有界往返测试 | c1be656/C自有临时目录和合成记录 | `560c4bcb2b1f5373b125c2ac68ddadc1cd39eb97cdf2bcbc3734a6f00dfda5e9` |
| `tests/RenderSkinInputTests.cpp` | A | 47 | `9970088` | `9970088` | 插件输入深拷贝/拒绝/ABI断言 | R2原创输入测试；迁移后保留既有ABI尾部断言 | `156b0614f6c22c7abfcae367623ded569844cab534b173cc8f7e638325316c8b` |
| `tests/RenderSkinFixture.h` | A | 27 | `9970088` | `9970088` | 双实例平面蒙皮夹具 | R2明确原创几何；唯一插件ABI随闭包迁移 | `6880d8f10cb5446aa03579bbd093ed01f54ba89c9359da6201988fecf4121f88` |
| `tests/GeosetColorTests.cpp` | A | 69 | `d161c77` | `eba5199` | GEOA BGR/静态RGB严格读取 | d161c77/A自有合成GEOA修改 | `7fb70a9afe95992385c1351ed8d4fd65811f6c980da05c5f7afa1496c2faa15e` |
| `tests/GeosetColorCoreTests.cpp` | A | 42 | `d90cb95` | `a6667eb` | 不可变静态RGB校验 | d90cb95/B自有浮点边界值 | `768c89e05cc5c4a38302c55b70057cf7aab74bba76043f8f5f93f1cb22bb7826` |
| `tests/GeosetChannelTests.cpp` | A | 101 | `d324bad` | `4113369` | KGAC/KGAO曲线与零全局时钟 | B/4113369自有多项式手算与冻结参考逐位检查 | `6a8a7d6b59164dd2cb1e42dda3eb3ba8b2dcda1b13f330c17ddb020be238b9f4` |
| `tests/GeosetChannelFixture.h` | A | 30 | `d324bad` | `918e95b` | GEOA/KGAC二进制夹具 | B自有Writer组装合成字节，非游戏文件 | `42075c850f4b6a13babdaec05ba80d5e6570b440a292b74777242cda6ecd4431` |

本报告只证明来源审查和冻结文件身份，不证明法律文本审定、迁移后构建、数值/图片逐字节不变、Win32/MinGW可用或原版运行时等价。
