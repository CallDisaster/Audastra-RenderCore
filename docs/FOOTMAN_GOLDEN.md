<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

# Footman 离线黄金夹具 v1

本夹具只保存共享核心计算出的矩阵组、geoset alpha/RGB、数值形式的组成员 objectId，以及结构计数和来源摘要。仓库不包含 Footman MDX、BLP、MPQ 或其原始块字节。使用者从自己合法安装的游戏取得模型；工具不访问游戏目录、MPQ、网络或渲染设备。

此处“黄金”表示冻结输入下可重算的共享核心参考结果，质量为 `PreviewApproximate`。它不证明原版同帧等价、原生节点映射、材质绘制或游戏运行。

## 固定来源与宿主准入

W1 已验证来源为经典 MDLX v800 `Units\Human\Footman\Footman.mdx`，游戏归档层为 `war3.mpq`。必须使用以下 SHA-256 的文件：

```text
7ee255776a6e757bd89766354df763201cd0519c488585b7c30151d5e313fc13
```

宿主须在执行工具前实际计算输入文件 SHA，核对上值；执行后再次核对同一输入，记录工具版本、架构、浮点编译选项、命令和夹具 SHA。任一步不同即拒绝发布本次结果。模型保存在忽略的本机 artifact 目录，不加入 Git。

工具要求 `--model-sha256` 与固定值完全相同，并把 `AssetSource.contentDigest` 设为 `sha256:<固定值>`。这个参数是宿主的已核验声明；工具不另实现 SHA 算法，不能把传对参数当作实际文件 SHA 证据。不同版本或其他模型不得沿用这个摘要，也不得猜动作序号后生成同名夹具。

## 导出与重算

目标由集成者登记为 `war3-render-core-footman-golden`。MSVC 接受 Unicode 路径；其他工具链的参数使用 UTF-8。输入为本机普通文件路径；输出父目录须已存在，工具拒绝覆盖已有输出。

```powershell
$goldenInput = '<忽略目录中的Footman.mdx>'
$expectedSha = '7ee255776a6e757bd89766354df763201cd0519c488585b7c30151d5e313fc13'
if ((Get-FileHash -LiteralPath $goldenInput -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedSha) { throw 'Footman source SHA mismatch' }
& '<构建目录>/Release/war3-render-core-footman-golden.exe' --input $goldenInput --model-sha256 $expectedSha --output '<新夹具路径>'
if ($LASTEXITCODE -ne 0) { throw 'Footman export rejected' }
if ((Get-FileHash -LiteralPath $goldenInput -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedSha) { throw 'Footman source changed' }
```

给定已提交的纯结果夹具，使用同一宿主准入步骤，然后调用：

```powershell
& '<构建目录>/Release/war3-render-core-footman-golden.exe' --input $goldenInput --model-sha256 $expectedSha --verify '<已有夹具路径>'
```

`--verify` 通过共享 `ParseMdx`、`CompileMdxModel` 与 `PoseBatchEvaluator` 重新计算完整正文，直接逐字节比较；不解析夹具、不容差比较、不写回任何文件。成功输出 `FOOTMAN_GOLDEN_VERIFIED: byte_equal=1 samples=3`，失败退出 1。不同编译器或架构的逐位一致须实际跑这条检查后才能登记。

## 固定样本

| 标签 | 动作序号 | 预期动作 | 动作内毫秒 | 全局毫秒 |
| --- | ---: | --- | ---: | ---: |
| `stand500` | 0 | Stand | 500 | 0 |
| `walk250` | 6 | Walk | 250 | 0 |
| `walk283` | 6 | Walk | 283 | 0 |

工具检查动作存在、可用且名称具有对应前缀。每个样本使用 `Computed`，矩阵位于 `ModelLocal`，没有实例世界变换。此次夹具不输入相机，保持核心已有缺相机的静态通用预览语义；它不等同于 W1 截图服务提供的相机朝向结果。求值失败、数组数量不完整、非有限结果或组成员范围不完整时不产生夹具，不使用上一次姿态或默认数值补缺。

## 文本编码

- UTF-8/ASCII 正文，无 BOM，LF 换行，结尾保留一个 LF；整数十进制，顺序固定。
- 前两行是以 `#` 开头的授权/版权注释；后续工具应跳过注释再读取格式标记。
- 每个 float 使用 `std::bit_cast<uint32_t>` 写成 `0x` 加八位小写十六进制，精确保留 IEEE binary32 位模式，包括负零；不输出小数近似。
- `matrix_layout ColumnMajor3x4`：12 个 float，`m[column*3+row]`，列依次为 X/Y/Z 基列和位移列，隐含第四行为 `0,0,0,1`；位置公式见 `PoseTypes.h`。
- `geoset` 按源顺序，`groups <offset> <count>` 指共享资产的扁平组数组；`vertices <count>` 仅为数量摘要，不包含顶点。
- `members <geoset> <local-group> <count> <objectId...>` 保留该组的实际成员顺序；它不是节点数组下标，也不是 GPU palette 索引。不同项目必须先证明自己的 objectId 映射。
- `matrix <global-group> <12个hex>` 使用共享资产的扁平组顺序；`alpha_rgb <geoset> <alpha> <R> <G> <B>` 使用原 geoset 顺序。
- 文件不输出原始键、原始顶点/法线/UV/索引、贴图字节、节点名称或完整解析模型；`source_bytes` 只标明来源文件字节数。

格式版本升级、样本或数值语义改变时，新建版本并保留旧夹具及对照回执。共享 Git 提交身份与实际编译选项记录在验收回执中，不把运行时间、绝对输入路径、进程地址或动态版本号混入正文。
