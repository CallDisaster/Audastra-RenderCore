<!-- SPDX-License-Identifier: GPL-3.0-only -->
<!-- Copyright (C) 2026 CallDisaster -->

> 历史回执：以下命令和测试数属于拆分前 FORGE 环境，不代表共享仓库当前验收。

# Step4 收口：模型覆盖修复（本机验收待执行）

本轮从 `cloud@308cbe4f1661c8e3643ec2e0c13154c82a2f8549` 恢复，只补剩余三项。
此前坏序列、七类公共节点和继承位的 WIP 保留，没有重新实现。
`3882a0c5` 保存零法线，`ce020c56` 保存纯粒子分类，`c1be656e` 保存扫描 detail。
本机的 MSVC/Win32/FXC/WARP/真实模型扫描及四张 Stand 图，仍由 Sol 验证。
本文的 Linux 结果不替代这些验收，所有姿势与着色仍是 `PreviewApproximate`。

## 设计与改动边界

精确的原始零法线（含带符号零）在 CPU 参考与核心都保留为规范零；加载成功附
`MDX_SKIN_ZERO_NORMAL_PREVIEW`，明确 geoset 和受影响顶点数。不把很小的非零法线
提前改值，非有限输入、非零法线被姿势压成零的旧拒绝仍保留。
两条 HLSL 归一化路径为 rsqrt 加安全下限，保留既有 +Z 预览回退；不宣称物理正确或
CPU/GPU 逐位一致。特别需要本机复核三个零法线模型的光照，不用空白图顶替成功。

纯粒子结构分类见 `include/War3RenderCore/ModelContent.h`：无 GEOS 且有 PREM/PRE2；
分类不绕过编译校验。无动作仍可编译；有动作继续核对公共节点。编译成功的纯粒子是
`compiled` 的子集，单独 `particle_only` 计数，状态 `particle_only_unimplemented`；
无动作明确 pose_samples=0。空模型、只有光源/挂点和带网格的混合模型不会误计为纯粒子。
没有实现发射、粒子/条带绘制，没有新造网格，也没有修改静态兜底。

TSV 保留原 22 列顺序，尾部加 particle_only/detail；子进程回执严格使用 24 列。
detail 包含 flags、父节点种类、坏动作原序号和零法线计数，最多 8192 字节；超限停止
追加完整条目并标 detail_truncated，不能把它当全量节点转储。详细口径见
`../Render/Tools/MdxScan/Coverage.md`。坏动作仍参与拒绝码对照，不从样本里删掉。

原零法线 fixture 已成为支持项，所以另用“不被旧 geoset 请求访问的未知位 HELP”保留
真正的局部回退阳性。真实数据要求零回退；原创阳性 fixture 故意要求检出 1 个，不能混淆。

没有改 main、Sol 的 World/Animation/Scene/MCP、插件 ABI 或物编保存服务；本轮没有 IDA 调用。
CMake 只在归属内给扫描器登记 ScanDetail.cpp，给既有 coverage 测试链接 ScanRecord/ScanDetail。
没有共享根 CMake 或 tests/CMakeLists 登记变化。原 RenderCoreTests 和 MdxModelTests 未修改。
继承位的既有只读依据保留在 `Inheritance.md`：0x6F775D10 保持低三位，
0x6F77BB80/B560/B620 的顺序及 0x6F87DCE0 的 2/4/6 分支；本轮不扩大原版语义结论。

## 容器实际执行

恢复的是相关源码缓存，不是完整远端 clone。Assets、Shaders、MdxScan 子树及 RenderCore
产品/构建文件逐项核对远端 Git 身份；原核心 oracle 恢复后 blob 为 72de19998e4e4f11bd07f8c4c9a208007e5bb0bc。
远端写入均在远端完整 base tree 上只替换归属文件，没有用稀疏目录覆盖整仓库。

环境：GCC 14.2、Clang 17、CMake 3.31.6、Linux x86_64。以下命令均实际运行：

```sh
cmake -S src/RenderCore -B ../build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake -S src/RenderCore -B ../build-clang -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake -S src/RenderCore -B ../build-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
for b in build-gcc build-clang build-sanitize; do
  cmake --build ../$b -j2
  ASAN_OPTIONS=detect_leaks=1 ctest --test-dir ../$b -V
done
```

每个配置同 8 项全过，新增目标开启 -Wall -Wextra -Werror -ffp-contract=off，无编译警告；
ASan/UBSan 没有报告。是同一套测试的三种配置，不计为 24 个独立功能。
输出摘要原文：

```text
oracle requests=198 nodes=360 vertices=1440 bitwise_mismatches=0 diagnostic_mismatches=0
node_kinds=7 parent_samples=28 bitwise_mismatches=0 bounds_and_duplicates=1
zero_normal_samples=8 affected_geosets=2 vertices=3 bitwise_mismatches=0 nonfinite_rejected=1
particle_models=4 empty_meshes=1 node_bitwise_mismatches=0 invalid_still_rejected=1
scan_detail=1 flags_parent_sequence_normals=1 tsv_roundtrip=1 bounded=1 malformed_rejected=1
inheritance_samples=48 bitwise_mismatches=0 analytic_pivots=1 singular_rejected=1
100% tests passed, 0 tests failed out of 8
```

原 MdxModelTests.cpp 另以 g++ -std=c++20 -O2 -Wall -Wextra -ffp-contract=off 编译，
链接 MdxModel.cpp/MdxPose.cpp 后输出 `MdxModelTests: all tests passed`。
这个旧测试文件有 19 条缺省 camera 初始化警告，完整保留；不能说所有测试目标零警告。
-m32 探针在包含 cstdint 时缺 `bits/c++config.h`，EXIT=1；没有安装工具链，Win32 未在容器验证。

扫描器独立入口分别用 GCC/Clang Release 配置构建，目标 war3-mdx-scan 与 war3-mdx-scan-fixtures。
两种编译器对相同原创模型字节的结果一致：

```text
baseline: models=5 parsed=4 compiled=3 particle_only=0 compile_rejected=1 legacy_positive_whole_rejected=1 samples=37 mismatch_samples=0 EXIT=0
coverage: models=9 parsed=9 compiled=8 particle_only=2 compile_rejected=1 legacy_positive_whole_rejected=1 samples=81 mismatch_samples=0 EXIT=0
scanner_fixtures=5 coverage_fixtures=9 timeout=SCAN_MODEL_TIMEOUT memory=SCAN_CHILD_EXIT_7 limits_enforced=1
```

另外实际验证：已有输出拒绝且 SHA 不变；样本/模型截断 EXIT=2；缺失模型后继续扫合法模型。
只在临时 oracle 副本把组元素加 1，扫描检出 66 个差分样本并给首例 group=0、EXIT=3；
正式源码未扰动，原严格逐位断言和拒绝门不变。没有真实游戏资源或原版矩阵运行证据。

## Sol 本机验收

保留当前工作树及第 3 步修正，先核对 cloud 历史再做正常合并；不 reset/clean、不 force。
复用现有缓存的 CMake/CTest 与低资源构建包装，每条命令单独检查退出码。

```powershell
$cmake = ((Select-String -LiteralPath build/vs2026/CMakeCache.txt -Pattern '^CMAKE_COMMAND:INTERNAL=').Line -split '=',2)[1]
$ctest = ((Select-String -LiteralPath build/vs2026/CMakeCache.txt -Pattern '^CMAKE_CTEST_COMMAND:INTERNAL=').Line -split '=',2)[1]
$generator = ((Select-String -LiteralPath build/vs2026/CMakeCache.txt -Pattern '^CMAKE_GENERATOR:INTERNAL=').Line -split '=',2)[1]
tools/dev/lowbuild.ps1 -Nodes 1 -Targets @('war3-render-coverage-tests','war3-render-core-tests','war3-render-core-compact-tests','war3-render-skin-input-tests','war3-mdx-scan','war3-mdx-scan-fixtures','war3-render-d3d11','war3-map-render-d3d11-tests','war3-model-animation')
& $ctest --test-dir build/vs2026 -C Release -j 1 --output-on-failure -V -R '^(render-core-.*|render-coverage-.*|render-skin-input|render-MdxModel|render-d3d11-headless)$'
tools/dev/lowbuild.ps1 -Nodes 1
& $ctest --test-dir build/vs2026 -C Release -j 1 --output-on-failure
& $cmake -S src/RenderCore -B build/render-core-step4-win32 -G $generator -A Win32
& $cmake --build build/render-core-step4-win32 --config Release -- /m:1 /nr:false /p:CL_MPCount=1
& $ctest --test-dir build/render-core-step4-win32 -C Release -j 1 --output-on-failure -V
```

FXC 与完整旧 render-d3d11-headless 都要跑，不能只挑新功能。Win32 的独立 8 项必须真实执行。
全量数量以当前清单为准，第 3 步 Sol 可能增加测试；不抄过去的 98/98。

先按 Coverage.md 跑两个原创清单，再用同一标准物编清单和同一游戏根复扫：

```powershell
& $scan --game-root $gameRoot --list $models --tsv (Join-Path $freshReportDir 'full.tsv') --total-seconds 3600
```

$scan 用本轮实际构建的 war3-mdx-scan.exe；$models 是前轮 2452 模型的同一清单。
summary/TSV/fallback.txt/scope.txt 一起回传。要求 mismatch_samples=0、legacy_positive_whole_rejected=0，
另外列出剩余拒绝原因、纯粒子数、坏动作号、缺失路径和预算缺口。EXIT=2 不能记全覆盖。

四张 Stand 图在同一清单中按文件名查出准确虚拟路径，不猜资源路径。建立四份 UTF-8 TSV：

instance 行按工具合同为 13 列：instance,id,path,action,time,x,y,z,angle,sx,sy,sz,team。
直接使用下面数组生成，避免人工计算 TAB。

```powershell
# $animation 为本轮 war3-model-animation.exe；$freshReportDir 已存在，四个输出子目录必须不存在。
foreach ($name in @('HeroBladeMaster','HeroTaurenChieftain','Kiljaeden','HeroFarSeer')) {
    $paths = @(Get-Content -LiteralPath $models | Where-Object { [IO.Path]::GetFileNameWithoutExtension($_) -ieq $name } | Select-Object -Unique)
    if ($paths.Count -ne 1) { throw "模型路径不唯一或缺失：$name；按清单核实后再出图" }
    $inputFile = Join-Path $freshReportDir ($name + '.tsv')
    $rows = @( (@('instance','1',$paths[0],'Stand','mid','0','0','0','0','1','1','1','0') -join "`t"), "capture`tstand" )
    [IO.File]::WriteAllLines($inputFile, $rows, [Text.UTF8Encoding]::new($false))
    & $animation --game-root $gameRoot --input $inputFile --output-dir (Join-Path $freshReportDir $name) --revision 0
    if ($LASTEXITCODE -ne 0) { throw "Stand 截帧失败：$name；保留原诊断，不换默认动作冒充" }
}
```

预期每目录 stand.bmp、stand.json、report.json；打开 BMP 人工核对完整模型/贴图/部件，
检查回执场景/内容/姿势版本与静态安装次数。若 Stand 本身被标坏，保留失败，列出具体 SEQS；
不能静默跳动作或用静态兜底图算通过。此次不承诺粒子可见，四图通过也不等于原版一致。
最需关注：FXC 零法线防护、真实非 BONE/HELP 父链、原坏序列的后续好动作，以及参考/核心差分。
