@echo off
chcp 65001 >nul

set TARGET_NAME=com2tcp_server
set BUILD_DIR=buildWin

if "%1"=="/?" goto show_help
if "%1"=="help" goto show_help
if "%1"=="-h" goto show_help

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

:: 直接处理各种情况
if "%1"=="clean" goto do_clean
if "%1"=="rm" goto do_rm

if "%1"=="cleanBuild" (
    echo.
    echo [INFO] Cleaning build directory...
    if exist "%BUILD_DIR%" (
        cd "%BUILD_DIR%"
        make clean
        cd ..
        echo [SUCCESS] Clean completed.
    ) else (
        echo [WARNING] Build directory does not exist.
    )
    if "%2"=="release" goto do_release
    if "%2"=="debug" goto do_debug
    goto do_development
)

if "%1"=="release" goto do_release
if "%1"=="debug" goto do_debug

:: 默认情况：无参数时构建开发版本
goto do_development

:do_release
set BUILD_FLAGS=-DCMAKE_BUILD_TYPE=Release -DENABLE_MONITOR=OFF -DBUILD_STATIC=ON
set BUILD_TYPE=Release
echo.
echo [RELEASE] Building RELEASE version (最优性能，无调试信息)...
goto do_build

:do_debug
set BUILD_FLAGS=-DCMAKE_BUILD_TYPE=Debug -DENABLE_MONITOR=ON -DBUILD_STATIC=ON
set BUILD_TYPE=Debug
echo.
echo [DEBUG] Building DEBUG version (信号处理+符号解析)...
goto do_build

:do_development
set BUILD_FLAGS=-DCMAKE_BUILD_TYPE=Debug -DENABLE_MONITOR=ON -DBUILD_STATIC=ON
set BUILD_TYPE=Development
echo.
echo [DEVELOPMENT] Building DEVELOPMENT version (信号处理+符号解析)...
goto do_build

:do_build
echo [INFO] Build directory: %BUILD_DIR%
echo [INFO] CMake command: cmake -B %BUILD_DIR% -G "MinGW Makefiles" %BUILD_FLAGS%
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

echo [SUCCESS] Build successful!

set EXECUTABLE=%BUILD_DIR%\%TARGET_NAME%.exe
if exist "%EXECUTABLE%" (
    echo [INFO] Executable: %EXECUTABLE%
    
    :: 获取文件大小并转换为KB（构建目录）
    for /f "tokens=3" %%i in ('dir /-c "%EXECUTABLE%" ^| findstr /c:"%TARGET_NAME%.exe"') do (
        set /a KB=%%i/1024
    )
    
    :: 显示构建类型信息
    if "%BUILD_TYPE%"=="Release" (
        echo [INFO] 构建类型: Release (最优性能，无调试信息)
    ) else if "%BUILD_TYPE%"=="Debug" (
        echo [INFO] 构建类型: Debug (信号处理+符号解析)
    ) else (
        echo [INFO] 构建类型: Development (信号处理+符号解析)
    )
    echo [INFO] 目标平台: Windows
) else (
    echo [WARNING] 可执行文件可能被复制到根目录，构建目录中未找到: %EXECUTABLE%
    if exist "%TARGET_NAME%.exe" (
        echo [INFO] 可执行文件已复制到: %CD%\%TARGET_NAME%.exe
        
        :: 获取文件大小并转换为KB（根目录）
        for /f "tokens=3" %%i in ('dir /-c "%TARGET_NAME%.exe" ^| findstr /c:"%TARGET_NAME%.exe"') do (
            set /a KB=%%i/1024
        )
        echo [INFO] 程序文件大小：%KB% KB
    )
)

exit /b 0

:do_clean
echo.
echo [INFO] Cleaning build directory...
if exist "%BUILD_DIR%" (
    cd "%BUILD_DIR%"
    make clean
    cd ..
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