# build.ps1 — 一键重建 DeepSeekMonitor.dll（可选同时跑单元测试）
#
# 用法（在本仓库根目录执行）：
#   powershell -ExecutionPolicy Bypass -File build.ps1            # 只编译 DLL
#   powershell -ExecutionPolicy Bypass -File build.ps1 -Test      # 编译 DLL + 跑 43 项时段测试
#   powershell -ExecutionPolicy Bypass -File build.ps1 -ZigPath "C:\path\to\zig.exe"
#
# 依赖：zig 0.9.1（任选其一）
#   - pip install ziglang==0.9.1   （python -m ziglang 等价于 zig 命令）
#   - 或把 zig.exe 放到 tools\ziglang\ 下
#   - 或让 zig.exe 在 PATH 里
# 无需 Visual Studio；zig 自带 MinGW-w64 后端与 Windows 导入库。

param(
    [switch]$Test,
    [string]$ZigPath
)

$ErrorActionPreference = "Stop"
$Root = $PSScriptRoot
$Target = "x86_64-windows-gnu"   # 与 TrafficMonitor V1.86 插件 ABI 兼容

# 1. 定位 zig 编译器
if (-not $ZigPath) {
    $candidate = Join-Path $Root "tools\ziglang\zig.exe"
    if (Test-Path $candidate) { $ZigPath = $candidate }
}
if (-not $ZigPath) {
    $cmd = Get-Command zig -ErrorAction SilentlyContinue
    if ($cmd) { $ZigPath = $cmd.Source }
}
if (-not $ZigPath -or -not (Test-Path $ZigPath)) {
    Write-Error "找不到 zig 编译器。请执行 pip install ziglang==0.9.1，或把 zig.exe 放到 tools\ziglang\ 下，或用 -ZigPath 指定路径。"
    exit 1
}
Write-Host "[1/3] 编译器: $ZigPath"

# 2. 编译 DLL
$Src = Join-Path $Root "DeepSeekMonitor.cpp"
$Out = Join-Path $Root "DeepSeekMonitor.dll"
if (Test-Path $Out) { Remove-Item $Out -Force }

& $ZigPath c++ -target $Target -shared -O2 -g0 $Src -o $Out
if ($LASTEXITCODE -ne 0) { Write-Error "DLL 编译失败（exit $LASTEXITCODE）"; exit 1 }
Write-Host "[2/3] 构建完成: $Out （$((Get-Item $Out).Length) 字节）"

# 3. 可选：编译并运行时段规则单元测试
if ($Test) {
    $TestSrc = Join-Path $Root "test_period.cpp"
    $TestExe = Join-Path $Root "test_period.exe"
    if (Test-Path $TestExe) { Remove-Item $TestExe -Force }
    & $ZigPath c++ -target $Target -O2 -g0 $TestSrc -o $TestExe
    if ($LASTEXITCODE -ne 0) { Write-Error "测试程序编译失败（exit $LASTEXITCODE）"; exit 1 }
    & $TestExe
    if ($LASTEXITCODE -ne 0) { Write-Error "单元测试未全部通过（exit $LASTEXITCODE）"; exit 1 }
    Remove-Item $TestExe -Force
    Write-Host "[3/3] 单元测试全部通过"
} else {
    Write-Host "[3/3] 跳过测试（加 -Test 参数可运行 43 项时段规则测试）"
}

Write-Host ""
Write-Host "下一步："
Write-Host "  1. 完全退出 TrafficMonitor（DLL 在运行期间被占用，不能热替换）"
Write-Host "  2. 把 DeepSeekMonitor.dll 复制到 <TrafficMonitor目录>\plugins\"
Write-Host "     Release 压缩包内的 install_update.bat 可自动完成这一步"
Write-Host "  3. 重新打开 TM，右键 -> 显示项目 -> 勾选 'DeepSeek余额' 和 'DeepSeek时段'"
