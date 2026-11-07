@echo off
chcp 65001 >nul
setlocal
rem ============================================================
rem  DeepSeekMonitor 安装 / 更新脚本
rem  用法：把 TrafficMonitor 安装文件夹拖到本文件上，或命令行传入：
rem        install_update.bat "D:\TrafficMonitor"
rem  注意：本脚本需与 DeepSeekMonitor.dll 放在同一目录（Release 压缩包内）
rem ============================================================

set "TM_DIR=%~1"
if "%TM_DIR%"=="" (
    echo.
    echo 请把 TrafficMonitor 安装文件夹拖到本窗口，或输入其完整路径。
    echo 示例：D:\TrafficMonitor
    echo.
    set /p "TM_DIR=安装目录: "
)
set "TM_DIR=%TM_DIR:"=%"
if "%TM_DIR:~-1%"=="\" set "TM_DIR=%TM_DIR:~0,-1%"

if not exist "%TM_DIR%\TrafficMonitor.exe" (
    echo.
    echo [失败] 在 "%TM_DIR%" 下没有找到 TrafficMonitor.exe
    echo 请确认目录是 TrafficMonitor 的安装目录（含 TrafficMonitor.exe）。
    echo.
    pause
    exit /b 1
)

if not exist "%~dp0DeepSeekMonitor.dll" (
    echo.
    echo [失败] 本脚本同目录下没有 DeepSeekMonitor.dll
    echo 请先从 GitHub Releases 下载压缩包并完整解压。
    echo.
    pause
    exit /b 1
)

tasklist /FI "IMAGENAME eq TrafficMonitor.exe" 2>nul | find /I "TrafficMonitor.exe" >nul
if not errorlevel 1 (
    echo.
    echo [失败] TrafficMonitor 正在运行，插件 DLL 被占用，无法替换。
    echo 请先在任务栏托盘右键 TrafficMonitor 图标 - 退出，然后重新运行本脚本。
    echo （若 TM 以管理员权限运行，请同样以管理员身份运行本脚本）
    echo.
    pause
    exit /b 1
)

if not exist "%TM_DIR%\plugins" mkdir "%TM_DIR%\plugins"
copy /y "%~dp0DeepSeekMonitor.dll" "%TM_DIR%\plugins\DeepSeekMonitor.dll"
if errorlevel 1 (
    echo.
    echo [失败] 无法写入 "%TM_DIR%\plugins\"，请右键本文件选择"以管理员身份运行"。
    echo.
    pause
    exit /b 1
)

echo.
echo [成功] 已安装到 "%TM_DIR%\plugins\DeepSeekMonitor.dll"
echo 现在重新打开 TrafficMonitor，在"显示项目"里勾选 DeepSeek余额 和 DeepSeek时段。
echo.
pause
