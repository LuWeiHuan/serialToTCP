@echo off
:: cls
setlocal enabledelayedexpansion

:: 可配置变量
set TARGET_NAME=com2tcp_server
set BUILD_DIR=build

:: 显示帮助信息
if "%1"=="/?" (
    echo Usage: %0 [clean^|release^|debug]
    echo.
    echo Options:
    echo   clean    - Delete build directory
    echo   release  - Build release version
    echo   debug    - Build debug version
    echo   no args  - Normal build
    echo.
    echo Current target: %TARGET_NAME%
    goto :end
)

:: 检查并终止运行中的进程
echo Checking for running %TARGET_NAME% processes...
tasklist /FI "IMAGENAME eq %TARGET_NAME%.exe" 2>NUL | find /I "%TARGET_NAME%.exe" >NUL
if %ERRORLEVEL% equ 0 (
    echo Terminating running %TARGET_NAME% process...
    taskkill /F /IM "%TARGET_NAME%.exe" >NUL 2>&1
    timeout /t 1 /nobreak >nul
    echo Process terminated.
) else (
    echo No running %TARGET_NAME% processes found.
)

:: 检查参数
if "%1"=="rm" (
    echo Cleaning build directory...
    if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR%
    echo Clean completed.
    goto :end
)

if "%1"=="clean" (
    echo Cleaning build directory...
    if exist %BUILD_DIR% cd %BUILD_DIR%
    make clean
    cd ..
    echo Clean completed.
    goto :end
)

:: 设置构建类型
set BUILD_TYPE=
if "%1"=="release" (
    set BUILD_TYPE=-DENABLE_MONITOR=OFF
    echo Building RELEASE version...
) else if "%1"=="debug" (
    set BUILD_TYPE=-DENABLE_MONITOR=ON
    echo Building DEBUG version...
) else (
    echo Building...
)

:: 记录开始时间
for /f "delims=" %%i in ('powershell -Command "Get-Date -UFormat '%%s'"') do (
    set START_TIME=%%i
)

:: 执行构建命令 - 强制指定 MinGW 生成器
echo Configuring CMake...
if "%BUILD_TYPE%"=="" (
    cmake -B %BUILD_DIR% -G "MinGW Makefiles"
) else (
    cmake -B %BUILD_DIR% -G "MinGW Makefiles" %BUILD_TYPE%
)

:: 检查 CMake 配置是否成功
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    goto :end
)

:: 检查构建目录是否存在
if not exist "%BUILD_DIR%" (
    echo ERROR: Build directory was not created!
    goto :end
)

:: 执行构建
echo Building project...
cmake --build %BUILD_DIR% --parallel

:: 检查构建是否成功
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed!
    goto :end
)

:: 记录结束时间
for /f "delims=" %%i in ('powershell -Command "Get-Date -UFormat '%%s'"') do (
    set END_TIME=%%i
)

:: 计算时间差
for /f "delims=" %%i in ('powershell -Command "(%END_TIME% - %START_TIME%).ToString('F2')"') do (
    set DURATION=%%i
)

:: 输出构建用时
echo Build completed successfully in %DURATION% seconds

:end
endlocal

:: make.bat          # 正常构建
:: make.bat clean    # 清理
:: make.bat release  # 发布版本
:: make.bat debug    # 调试版本
:: make.bat /?       # 查看帮助