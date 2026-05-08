@echo off
chcp 65001 >nul

:: 用于检测可执行文件是否在运行，并在必要时终止它
set TARGET_NAME=com2tcp_server

:: 构建目录
set BUILD_DIR=build/WinX64

:: 是否执行外部脚本
set EXTRA_BAT=fileCopy.bat

:: 记录开始时间
set START_TIME=%time%

if /i "%1"=="/?" goto show_help
if /i "%1"=="help" goto show_help
if /i "%1"=="-h" goto show_help

echo.
echo [INFO] Checking for running %TARGET_NAME% processes...
tasklist /FI "IMAGENAME eq %TARGET_NAME%.exe" 2>NUL | find /I "%TARGET_NAME%.exe" >NUL
if not errorlevel 1 (
    echo [INFO] Terminating running %TARGET_NAME% process...
    taskkill /F /IM "%TARGET_NAME%.exe" >NUL 2>&1
    timeout /t 1 /nobreak >nul
    echo [SUCCESS] Process terminated.
) else (
    echo [INFO] No running %TARGET_NAME% processes found.
)

:: 直接处理各种情况（不区分大小写）
if /i "%1"=="clean" goto do_clean
if /i "%1"=="rm" goto do_rm

if /i "%1"=="cleanBuild" (
    echo.
    echo [INFO] Cleaning build directory...
    if exist "%BUILD_DIR%" (
        cd "%BUILD_DIR%"
        make clean
        cd "%~dp0"
        echo [SUCCESS] Clean completed.
    ) else (
        echo [WARNING] Build directory does not exist.
    )
    if /i "%2"=="release" goto do_release
    if /i "%2"=="debug" goto do_debug
    goto do_development
)

if /i "%1"=="release" goto do_release
if /i "%1"=="debug" goto do_debug

:: 默认情况：无参数时构建开发版本
if "%1"=="" goto do_development

:: 如果参数不是上述任何一种，显示错误信息
echo [ERROR] 未知参数: %1
echo 使用 %0 help 查看可用参数
exit /b 1

:do_release
set BUILD_FLAGS=-DCMAKE_BUILD_TYPE=Release -DENABLE_MONITOR=OFF -DBUILD_STATIC=ON
set BUILD_TYPE=Release
set BUILD_TYPE_TIPS=(最优性能，无调试信息^)
echo.
goto do_build

:do_debug
set BUILD_FLAGS=-DCMAKE_BUILD_TYPE=Debug -DENABLE_MONITOR=ON -DBUILD_STATIC=ON
set BUILD_TYPE=Debug
set BUILD_TYPE_TIPS=(信号处理+符号解析^)
echo.
goto do_build

:do_development
set BUILD_FLAGS=-DCMAKE_BUILD_TYPE=Debug -DENABLE_MONITOR=ON -DBUILD_STATIC=ON
set BUILD_TYPE=Development
set BUILD_TYPE_TIPS=(信号处理+符号解析^)
echo.
goto do_build

:do_build
echo [INFO] Build directory: %BUILD_DIR%
echo [INFO] CMake command: cmake -B %BUILD_DIR% -G "MinGW Makefiles" %BUILD_FLAGS%
echo [%BUILD_TYPE%] Building %BUILD_TYPE% version %BUILD_TYPE_TIPS%...
echo.

echo [INFO] Configuring CMake...
cmake -B "%BUILD_DIR%" -G "MinGW Makefiles" %BUILD_FLAGS%

if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

if not exist "%BUILD_DIR%" (
    echo [ERROR] Build directory was not created!
    exit /b 1
)

echo [INFO] Building project...
cmake --build "%BUILD_DIR%" --parallel

if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo [SUCCESS] Build Successful!
echo [INFO] 目标平台: Windows
echo [INFO] 构建类型: %BUILD_TYPE% %BUILD_TYPE_TIPS%

set EXECUTABLE=%BUILD_DIR%\%TARGET_NAME%.exe 
if exist "%EXECUTABLE%" (
    :: 获取文件大小
    for %%F in ("%EXECUTABLE%") do (
        set /a FILE_SIZE_KB=%%~zF/1024
    )
    echo [INFO] 程序位置：%EXECUTABLE% 存在
)else (
    echo [WARNING] 程序位置：%EXECUTABLE% 没有 
)

echo [INFO] 程序大小：%FILE_SIZE_KB% KB

:: 计算构建时间
set END_TIME=%time%
echo [INFO] 构建时间: %START_TIME% - %END_TIME%
echo [INFO] 当前时间: %DATE% %TIME%
call :CalculateDuration "%START_TIME%" "%END_TIME%" DURATION
echo [INFO] 构建用时: %DURATION%

:: 检查并执行额外的脚本
echo.
if exist %EXTRA_BAT% (
    echo [INFO] Found %EXTRA_BAT%, executing...
    call %EXTRA_BAT%
    if errorlevel 1 (
        echo [WARNING] %EXTRA_BAT% completed with warnings or errors
    ) else (
        echo [SUCCESS] %EXTRA_BAT% executed successfully.
    )
    echo.
)

exit /b 0

:CalculateDuration
setlocal
set "start=%~1"
set "end=%~2"

:: 解析开始时间
for /f "tokens=1-3 delims=:." %%a in ("%start%") do (
    set /a "start_h=%%a, start_m=%%b, start_s=%%c"
)

:: 解析结束时间  
for /f "tokens=1-3 delims=:." %%a in ("%end%") do (
    set /a "end_h=%%a, end_m=%%b, end_s=%%c"
)

:: 计算总秒数
set /a "start_total=%start_h%*3600 + %start_m%*60 + %start_s%"
set /a "end_total=%end_h%*3600 + %end_m%*60 + %end_s%"

:: 处理跨天情况
if %end_total% lss %start_total% (
    set /a "end_total+=86400"
)

set /a "duration_sec=%end_total% - %start_total%"

:: 格式化输出
if %duration_sec% geq 3600 (
    set /a "hours=%duration_sec%/3600"
    set /a "minutes=(%duration_sec% - hours*3600)/60"
    set /a "seconds=%duration_sec% - hours*3600 - minutes*60"
    set "result=%hours%小时%minutes%分钟%seconds%秒"
) else if %duration_sec% geq 60 (
    set /a "minutes=%duration_sec%/60"
    set /a "seconds=%duration_sec% - minutes*60"
    set "result=%minutes%分钟%seconds%秒"
) else (
    set "result=%duration_sec% 秒"
)

endlocal & set "%~3=%result%"
goto :eof

:do_clean
echo.
echo [INFO] Cleaning build directory...
if exist "%BUILD_DIR%" (
    cd "%BUILD_DIR%"
    make clean
    cd "%~dp0"
    echo [SUCCESS] Clean completed.
) else (
    echo [WARNING] Build directory does not exist.
)
exit /b 0

:do_rm
echo.
echo [INFO] Removing build directory...
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%"
    if exist "%BUILD_DIR%" (
        echo [ERROR] Failed to remove build directory!
    ) else (
        echo [SUCCESS] Remove completed.
    )
) else (
    echo [WARNING] Build directory does not exist.
)
exit /b 0

:show_help
echo.
echo Usage: %0 [clean^|rm^|release^|debug^|help^|cleanBuild]
echo.
echo 快速构建命令:
echo   %0               - 开发版本 (默认，信号处理+符号解析)
echo   %0 debug         - 调试版本 (信号处理+符号解析)
echo   %0 release       - 发布版本 (最优性能，无调试信息)
echo.
echo 组合命令:
echo   %0 cleanBuild debug    - 清理并构建调试版本
echo   %0 cleanBuild release  - 清理并构建发布版本
echo.
echo 其他命令:
echo   clean          - 清理构建目录
echo   rm             - 删除构建目录
echo   cleanBuild     - 清理并重新构建
echo   help           - 显示此帮助信息
echo.
echo 当前构建目录: %BUILD_DIR%
exit /b 0