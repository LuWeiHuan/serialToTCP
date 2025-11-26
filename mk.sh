#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# === 在这里设置您的交叉编译器路径 ===
# 注意：这里需要设置完整的编译器可执行文件路径，而不是目录路径
#ARM_C_COMPILER="/usr/local/arm/arm-linux-gnueabihf_4.9.4/bin/arm-linux-gnueabihf-gcc"
# 或者如果是 ARM64：
ARM_C_COMPILER="/usr/bin/aarch64-linux-gnu-gcc"

# 构建玩后执行额外的脚本，仅提供ARM架构
EXTRA_SH=fileCopy.sh

# 默认平台检测
detect_build_dir() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        local arch=$(uname -m)
        case "$arch" in
            "aarch64"|"arm64")
                echo "buildLinuxARM64"
                ;;
            "armv7l"|"armv6l")
                echo "buildLinuxARM32"
                ;;
            "x86_64")
                echo "buildLinuxX64"
                ;;
            "i386"|"i686")
                echo "buildLinuxX86"
                ;;
            *)
                echo "buildLinux"
                ;;
        esac
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
        echo "buildWin"
    else
        echo "build"
    fi
}

# 工具链文件配置
TOOLCHAIN_FILE_ARM="./toolchain-arm.cmake"

# 平台配置
CURRENT_PLATFORM="auto"
BUILD_DIR=$(detect_build_dir)

# 转换为小写函数
to_lower() {
    echo "$1" | tr '[:upper:]' '[:lower:]'
}

# 显示帮助信息
show_help() {
    echo "Usage: $0 [clean|rm|release|debug|asan|help|arm|arm64|arm32|x86|cleanBuild]"
    echo ""
    echo "快速构建命令:"
    echo "  $0               - 开发版本 (默认，信号处理+符号解析)"
    echo "  $0 debug         - 调试版本 (ASAN内存检测)"
    echo "  $0 asan          - ASAN版本 (同debug)"
    echo "  $0 release       - 发布版本 (最优性能，无调试信息)"
    echo ""
    echo "ARM交叉编译:"
    echo "  当前配置的编译器: ${ARM_C_COMPILER}"
    echo "  使用: $0 arm cleanBuild"
    echo ""
    echo "临时覆盖编译器:"
    echo "  ARM_C_COMPILER=/new/path/to/compiler-gcc $0 arm cleanBuild"
    echo ""
    echo "其他命令:"
    echo "  clean          - 清理构建目录"
    echo "  rm             - 删除构建目录"
    echo "  cleanBuild     - 清理并重新构建"
    echo "  arm|arm64|arm32|x86 - 指定目标平台"
    echo "  help           - 显示此帮助信息"
    echo ""
    echo "当前平台: ${CURRENT_PLATFORM}"
    echo "构建目录: ${BUILD_DIR}"
}

# 设置平台
set_platform() {
    local platform=$(to_lower "$1")
    case "$platform" in
        "arm"|"arm32"|"arm64"|"x86")
            CURRENT_PLATFORM="arm"
            # 使用配置的编译器路径推断架构
            if [[ -n "$ARM_C_COMPILER" ]]; then
                # 根据编译器名称推断架构
                if [[ "$ARM_C_COMPILER" == *"aarch64"* ]] || [[ "$ARM_C_COMPILER" == *"arm64"* ]]; then
                    BUILD_DIR="buildLinuxARM64"
                    echo -e "${CYAN}目标平台: ARM64 (根据编译器路径推断)${NC}"
                else
                    BUILD_DIR="buildLinuxARM32"
                    echo -e "${CYAN}目标平台: ARM32 (根据编译器路径推断)${NC}"
                fi
            else
                # 没有指定编译器时使用默认检测
                local arch=$(uname -m)
                if [[ "$arch" == "aarch64" || "$arch" == "arm64" ]]; then
                    BUILD_DIR="buildLinuxARM64"
                else
                    BUILD_DIR="buildLinuxARM32"
                fi
                echo -e "${CYAN}目标平台: ARM (自动检测)${NC}"
            fi
            ;;
        "x86")
            CURRENT_PLATFORM="x86"
            if [[ $(uname -m) == "x86_64" ]]; then
                BUILD_DIR="buildLinuxX64"
            else
                BUILD_DIR="buildLinuxX86"
            fi
            echo -e "${CYAN}目标平台设置为 x86 (本地编译)${NC}"
            ;;
        *)
            CURRENT_PLATFORM="auto"
            BUILD_DIR=$(detect_build_dir)
            # 修复：显示详细的自动检测信息
            local arch=$(uname -m)
            local os_name=""
            if [[ "$OSTYPE" == "linux-gnu"* ]]; then
                os_name="Linux"
            elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
                os_name="Windows"
            else
                os_name="$OSTYPE"
            fi
            echo -e "${CYAN}自动检测平台: ${os_name} ${arch} -> ${BUILD_DIR}${NC}"
            ;;
    esac
}

# 检查参数
if [ "$1" = "help" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    show_help
    exit 0
fi

# 处理平台参数和清理参数
CLEAN_BUILD=false
while [[ $# -gt 0 ]]; do
    param_lower=$(to_lower "$1")
    case "$param_lower" in
        "arm"|"arm32"|"arm64"|"x86")
            set_platform "$1"
            shift
            ;;
        "cleanbuild")
            CLEAN_BUILD=true
            shift
            ;;
        "clean")
            echo -e "${YELLOW}Cleaning build directory: ${BUILD_DIR}...${NC}"
            if [ -d "${BUILD_DIR}" ]; then
                cd "${BUILD_DIR}"
                make clean
                cd ..
                echo -e "${GREEN}Clean completed.${NC}"
            else
                echo -e "${YELLOW}Build directory does not exist.${NC}"
            fi
            exit 0
            ;;
        "rm")
            echo -e "${YELLOW}Removing build directory: ${BUILD_DIR}...${NC}"
            if [ -d "${BUILD_DIR}" ]; then
                rm -rf "${BUILD_DIR}"
                echo -e "${GREEN}Remove completed.${NC}"
            else
                echo -e "${YELLOW}Build directory does not exist.${NC}"
            fi
            exit 0
            ;;
        *)
            # 其他参数（构建类型）留在 $1 中处理
            break
            ;;
    esac
done

# 执行清理构建（如果指定了 cleanBuild）
if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}Clean and Build for directory: ${BUILD_DIR}...${NC}"
    if [ -d "${BUILD_DIR}" ]; then
        cd "${BUILD_DIR}"
        make clean
        cd ..
        echo -e "${GREEN}Clean completed.${NC}"
    else
        echo -e "${YELLOW}Build directory does not exist.${NC}"
    fi
fi

# 设置构建类型
BUILD_TYPE=""
build_type_lower=$(to_lower "$1")
case "$build_type_lower" in
    "release")
        BUILD_TYPE="-DCMAKE_BUILD_TYPE=Release -DENABLE_MONITOR=OFF -DENABLE_ASAN=OFF"
        echo -e "${GREEN}Building RELEASE version (最优性能，无调试信息)...${NC}"
        ;;
    "debug"|"asan")
        BUILD_TYPE="-DCMAKE_BUILD_TYPE=Debug -DENABLE_MONITOR=OFF -DENABLE_ASAN=ON"
        echo -e "${RED}Building DEBUG version (ASAN内存检测)...${NC}"
        ;;
    ""|*)
        BUILD_TYPE="-DCMAKE_BUILD_TYPE=Debug -DENABLE_MONITOR=ON -DENABLE_ASAN=OFF"
        echo -e "${BLUE}Building DEVELOPMENT version (信号处理+符号解析)...${NC}"
        ;;
esac

# 记录开始时间
START_TIME=$(date +%s.%N)

# 根据平台设置CMake参数
CMAKE_EXTRA_ARGS=""
if [ "$CURRENT_PLATFORM" = "arm" ]; then
    # 检查工具链文件是否存在
    if [ ! -f "$TOOLCHAIN_FILE_ARM" ]; then
        echo -e "${YELLOW}ARM toolchain file not found, creating toolchain file...${NC}"
        # 创建工具链文件
        cat > "$TOOLCHAIN_FILE_ARM" << 'EOF'
# file: toolchain-arm.cmake
set(CMAKE_SYSTEM_NAME Linux)

if(DEFINED ENV{ARM_C_COMPILER})
    set(CMAKE_C_COMPILER "$ENV{ARM_C_COMPILER}")
else()
    message(FATAL_ERROR "ARM compiler not specified. Please set ARM_C_COMPILER environment variable.")
endif()

# 设置系统根目录（sysroot）
get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(TOOLCHAIN_PREFIX "${COMPILER_DIR}" DIRECTORY)

# 设置系统根目录路径
if(EXISTS "${TOOLCHAIN_PREFIX}/arm-linux-gnueabihf")
    set(CMAKE_SYSROOT "${TOOLCHAIN_PREFIX}/arm-linux-gnueabihf")
    set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_PREFIX}/arm-linux-gnueabihf")
elseif(EXISTS "${TOOLCHAIN_PREFIX}/aarch64-linux-gnu")  
    set(CMAKE_SYSROOT "${TOOLCHAIN_PREFIX}/aarch64-linux-gnu")
    set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_PREFIX}/aarch64-linux-gnu")
else()
    set(CMAKE_SYSROOT "${TOOLCHAIN_PREFIX}")
    set(CMAKE_FIND_ROOT_PATH "${TOOLCHAIN_PREFIX}")
endif()

# 根据编译器名称推断架构和工具前缀
get_filename_component(COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)

if(COMPILER_NAME MATCHES "aarch64" OR COMPILER_NAME MATCHES "arm64")
    set(COMPILER_PREFIX "aarch64-linux-gnu")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a")
    message(STATUS "Using AArch64 compiler: ${CMAKE_C_COMPILER}")
else()
    set(COMPILER_PREFIX "arm-linux-gnueabihf")
    set(CMAKE_SYSTEM_PROCESSOR arm)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfloat-abi=hard -mfpu=neon")
    message(STATUS "Using ARM32 compiler: ${CMAKE_C_COMPILER}")
endif()

# 设置其他编译器
set(CMAKE_CXX_COMPILER "${COMPILER_DIR}/${COMPILER_PREFIX}-g++")

# 设置查找路径
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 输出配置信息
message(STATUS "Toolchain prefix: ${TOOLCHAIN_PREFIX}")
message(STATUS "Sysroot: ${CMAKE_SYSROOT}")
EOF
        echo -e "${GREEN}Created ARM toolchain file: ${TOOLCHAIN_FILE_ARM}${NC}"
    fi
    
    # 检查编译器是否存在
    if [[ ! -f "$ARM_C_COMPILER" ]]; then
        echo -e "${RED}错误: 配置的编译器不存在: ${ARM_C_COMPILER}${NC}"
        echo -e "${YELLOW}请修改脚本开头的 ARM_C_COMPILER 变量为正确的编译器可执行文件路径${NC}"
        echo -e "${YELLOW}例如: /usr/local/arm/arm-linux-gnueabihf_4.9.4/bin/arm-linux-gnueabihf-gcc${NC}"
        exit 1
    fi
    
    echo -e "${PURPLE}使用配置的编译器: ${ARM_C_COMPILER}${NC}"
    # 通过环境变量传递编译器路径给CMake
    export ARM_C_COMPILER="$ARM_C_COMPILER"
    
    CMAKE_EXTRA_ARGS="-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE_ARM}"
    echo -e "${PURPLE}交叉编译 ARM 平台${NC}"
    
else
    echo -e "${PURPLE}本地编译 ${CURRENT_PLATFORM} 平台${NC}"
fi

# 执行构建命令
echo -e "${CYAN}Build directory: ${BUILD_DIR}${NC}"
echo -e "${CYAN}CMake command: cmake -B ${BUILD_DIR} -G \"Unix Makefiles\" ${BUILD_TYPE} ${CMAKE_EXTRA_ARGS}${NC}"

cmake -B "${BUILD_DIR}" -G "Unix Makefiles" $BUILD_TYPE $CMAKE_EXTRA_ARGS

# 检查CMake配置是否成功
if [ $? -ne 0 ]; then
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
fi

# 获取 CPU 核心数
CORES=$(nproc 2>/dev/null || echo 4)
echo -e "Building with ${CORES} parallel jobs..."

# 并行构建
cmake --build "${BUILD_DIR}" --parallel $CORES

# 检查构建是否成功
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Build successful!${NC}"
    
    # 显示生成的可执行文件信息
    EXECUTABLE="${BUILD_DIR}/com2tcp_server"
    if [ -f "$EXECUTABLE" ]; then
        echo -e "${GREEN}Executable: ${EXECUTABLE}${NC}"
        
        # 显示文件架构信息
        if command -v file >/dev/null 2>&1; then
            echo -e "${CYAN}File info:${NC}"
            file_info=$(file "$EXECUTABLE")
            echo "$file_info" | fold -s -w 80 | sed "s/^/${CYAN}  ${NC}/"
        fi
        
        # 显示文件大小
        file_size=$(ls -lh "$EXECUTABLE" | awk '{print $5}')
        echo -e "${CYAN}File size: ${file_size}${NC}"
        
        # 显示构建类型信息
        case "$build_type_lower" in
            "release")
                echo -e "${GREEN}构建类型: Release (最优性能，无调试信息)${NC}"
                ;;
            "debug"|"asan")
                echo -e "${RED}构建类型: Debug (ASAN内存检测)${NC}"
                echo -e "${YELLOW}注    意: ASAN完整调试版本性能较慢，最好只用于调试${NC}"
                ;;
            ""|*)
                echo -e "${BLUE}构建类型: Development (信号处理+符号解析)${NC}"
                ;;
        esac
        
        # 显示平台信息
        case "$CURRENT_PLATFORM" in
            "arm")
                echo -e "${CYAN}目标平台: ARM (使用: ${ARM_C_COMPILER})${NC}"
                ;;
            "x86")
                echo -e "${CYAN}目标平台: x86 (本地编译)${NC}"
                ;;
            *)
                echo -e "${CYAN}目标平台: $(uname -s) $(uname -m) -> ${BUILD_DIR}${NC}"
                ;;
        esac
        
        # 检查并执行额外脚本（仅ARM架构）
        if [ "$CURRENT_PLATFORM" = "arm" ] && [ -f "./$EXTRA_SH" ]; then
            echo -e "${CYAN}Found $EXTRA_SH and building for ARM, executing...${NC}"
            chmod +x ./$EXTRA_SH
            ./$EXTRA_SH
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}$EXTRA_SH executed successfully!${NC}"
            else
                echo -e "${RED}$EXTRA_SH execution failed!${NC}"
            fi
        fi
    fi
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# 记录结束时间
END_TIME=$(date +%s.%N)

# 计算时间差
if command -v bc >/dev/null 2>&1; then
    DURATION=$(echo "$END_TIME - $START_TIME" | bc | awk '{printf "%.2f", $0}')
else
    DURATION=$(echo "$END_TIME $START_TIME" | awk '{printf "%.2f", $1 - $2}')
fi

# 输出构建用时
echo -e "${GREEN}Build duration: ${DURATION} seconds${NC}"