#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 设置交叉编译工具链默认构建 32位还是64位
DEFAULT_ARM_TARGET_ARCH="64"  # 可选: "32" 或 "64"

# === 交叉编译器默认路径配置 ===
# 用户可以在这里设置首选路径，如果留空则使用系统默认路径
# 注意：这是全局设置，会被命令行参数覆盖
ARM32_C_COMPILER="/usr/local/arm/arm-linux-gnueabihf_4.9.4/bin/arm-linux-gnueabihf-gcc"
ARM64_C_COMPILER=""

# 系统默认交叉编译器路径（用于备用）
SYS_ARM32_COMPILER=$(which arm-linux-gnueabihf-gcc)
SYS_ARM64_COMPILER=$(which aarch64-linux-gnu-gcc)

# 构建后执行额外的脚本，仅提供ARM架构
EXTRA_SH=fileCopy.sh

# 构建文件夹保存路径
BUILD_DIR_NAME=build/

# 默认平台检测
getCpuArchName() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        local arch=$(uname -m)
        case "$arch" in
            "aarch64"|"arm64")
                echo "ARM64"
                ;;
            "armv7l"|"armv6l")
                echo "ARM32"
                ;;
            "x86_64")
                echo "X64"
                ;;
            "i386"|"i686")
                echo "X86"
                ;;
            *)
                echo "General"
                ;;
        esac
    elif [[ "$OSTYPE" == "msys" ]] || [[ "$OSTYPE" == "win32" ]]; then
        echo "ForWin"
    else
        echo "Build"
    fi
}

# 平台配置
CURRENT_PLATFORM="Auto"
CPU_ARCH_NAME=$(getCpuArchName)
BUILD_DIR=${BUILD_DIR_NAME}Linux$(getCpuArchName)

# 转换为小写函数
to_lower() {
    echo "$1" | tr '[:upper:]' '[:lower:]'
}

# 验证函数
validate_arm_arch() {
    local arch="$1"
    
    if [[ -z "$arch" ]]; then
        echo "64"  # 默认值
        return 0
    fi
    
    # 标准化输入
    arch=$(echo "$arch" | tr '[:lower:]' '[:upper:]')
    
    case "$arch" in
        "32"|"ARM32"|"ARM")
            echo "32"
            return 0
            ;;
        "64"|"ARM64"|"AARCH64")
            echo "64"
            return 0
            ;;
        *)
            echo "ERROR: Invalid ARM architecture: $arch" >&2
            echo "64"  # 出错时返回默认值
            return 1
            ;;
    esac
}

# 在脚本初始化时验证
DEFAULT_ARM_TARGET_ARCH=$(validate_arm_arch "$DEFAULT_ARM_TARGET_ARCH")

# 查找可用的ARM编译器
find_arm_compiler() {
    local target="$1"  # "arm32" 或 "arm64"
    local compiler=""
    
    if [[ "$target" == "arm64" ]]; then
        # 1. 优先使用用户配置的64位编译器
        if [[ -n "$ARM64_C_COMPILER" && -x "$ARM64_C_COMPILER" ]]; then
            compiler="$ARM64_C_COMPILER"
        # 2. 使用系统默认64位编译器
        elif [[ -x "$SYS_ARM64_COMPILER" ]]; then
            compiler="$SYS_ARM64_COMPILER"
        fi
    elif [[ "$target" == "arm32" ]]; then
        # 1. 优先使用用户配置的32位编译器
        if [[ -n "$ARM32_C_COMPILER" && -x "$ARM32_C_COMPILER" ]]; then
            compiler="$ARM32_C_COMPILER"
        # 2. 使用系统默认32位编译器
        elif [[ -x "$SYS_ARM32_COMPILER" ]]; then
            compiler="$SYS_ARM32_COMPILER"
        fi
    fi
    
    echo "$compiler"
}

# 显示帮助信息
show_help() {
    echo "Usage: $0 [clean|rm|release|debug|asan|help|arm|arm32|arm64|x86|cleanBuild]"
    echo ""
    echo "快速构建命令:"
    echo "  $0               - 开发版本 (信号处理+符号解析)"
    echo "  $0 debug         - 调试版本 (ASAN内存检测)"
    echo "  $0 asan          - ASAN版本 (同debug)"
    echo "  $0 release       - 发布版本 (最优性能，无调试信息)"
    echo ""
    echo "ARM交叉编译:"
    echo "  当前配置的编译器:"
    echo "    ARM32: ${ARM32_C_COMPILER:-使用系统默认}"
    echo "    ARM64: ${ARM64_C_COMPILER:-使用系统默认}"
    echo ""
    echo "其他命令:"
    echo "  clean          - 清理构建目录"
    echo "  rm             - 删除构建目录"
    echo "  cleanBuild     - 清理并重新构建"
    echo "  arm|arm32|arm64|x86 - 指定目标平台"
    echo "  help           - 显示此帮助信息"
    echo ""
    echo "当前平台: ${CURRENT_PLATFORM}"
    echo "构建目录: ${BUILD_DIR}"
}

# 设置平台
set_platform() {
    local platform=$(to_lower "$1")
    local target_arch=""
    local compiler=""
    
    case "$platform" in
        "arm")
            CURRENT_PLATFORM="ARM${DEFAULT_ARM_TARGET_ARCH}"
            CPU_ARCH_NAME="ARM${DEFAULT_ARM_TARGET_ARCH}"
            target_arch="arm${DEFAULT_ARM_TARGET_ARCH}"
            compiler=$(find_arm_compiler "arm${DEFAULT_ARM_TARGET_ARCH}")
            ;;
        "arm32")
            CURRENT_PLATFORM="ARM32"
            CPU_ARCH_NAME="ARM32"
            target_arch="arm32"
            compiler=$(find_arm_compiler "arm32")
            ;;
        "arm64")
            CURRENT_PLATFORM="ARM64"
            CPU_ARCH_NAME="ARM64"
            target_arch="arm64"
            compiler=$(find_arm_compiler "arm64")
            ;;
        "x86"|"x64")
            CURRENT_PLATFORM="X86"
            if [[ $(uname -m) == "x86_64" ]]; then
                CPU_ARCH_NAME="X64"
            else
                CPU_ARCH_NAME="X86"
            fi
            echo -e "${CYAN}目标平台: ${CPU_ARCH_NAME} (本地编译)${NC}"
            ;;
        *)
            CURRENT_PLATFORM="Auto"
            echo -e "${CYAN}自动检测平台: ${CPU_ARCH_NAME} -> ${BUILD_DIR}${NC}"
            ;;
    esac
    
    # 如果是ARM交叉编译，检查编译器
    if [[ "$CURRENT_PLATFORM" =~ ^ARM ]]; then
        if [[ -z "$compiler" ]]; then
            echo -e "${RED}错误: 未找到可用的 ${CPU_ARCH_NAME} 交叉编译器${NC}"
            echo -e "${YELLOW}请确保已安装交叉编译器或设置 ARM${CPU_ARCH_NAME: -2}_C_COMPILER 变量${NC}"
            exit 1
        fi
        
        # 设置环境变量供CMake使用
        export CMAKE_C_COMPILER="$compiler"
        
        # 可选：根据目标架构设置构建目录
        BUILD_DIR="${BUILD_DIR_NAME}Linux${CPU_ARCH_NAME}"
    fi
}

# 检查参数
if [ "$1" = "help" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    show_help
    exit 0
fi

ORIGINAL_DIR="$(pwd)"

# 处理平台参数和清理参数
DEL_BUILD=false
CLEAN_BUILD=false
CLEAN_EXIT=false
declare -a OTHER_ARGS=()

# 参数处理循环
while [[ $# -gt 0 ]]; do
    param_lower=$(to_lower "$1")
    case "$param_lower" in
        "cleanbuild"|"clean")
            CLEAN_BUILD=true
            ;;
        "arm"|"arm32"|"arm64"|"x86")
            set_platform "${param_lower}"
            ;;
        "rm")
            DEL_BUILD=true
            CLEAN_EXIT=true
            ;;
        *)
            OTHER_ARGS+=("$1")
            ;;
    esac
    shift
done

# 执行清理构建（如果指定了 cleanBuild）
if [ "$CLEAN_BUILD" = true ]; then
    echo -e "${YELLOW}清理并构建目录: ${BUILD_DIR}...${NC}"
    if [ -d "${BUILD_DIR}" ]; then
        cd "${BUILD_DIR}"
        make clean
        cd "$ORIGINAL_DIR"
        echo -e "${GREEN}清理完成${NC}"
    else
        echo -e "${YELLOW}构建目录不存在${NC}"
    fi
fi

# 执行删除构建（如果指定了 cleanBuild）
if [ "$DEL_BUILD" = true ]; then
    echo -e "${YELLOW}删除构建目录: ${BUILD_DIR}...${NC}"
    if [ -d "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
        echo -e "${GREEN}删除完成${NC}"
    else
        echo -e "${YELLOW}构建目录不存在${NC}"
    fi
fi

if [ "$CLEAN_EXIT" = true ]; then
    exit 0
fi

# 恢复其他参数
set -- "${OTHER_ARGS[@]}"

BUILD_VERSIONS="${BLUE}Development 版本 (信号处理+符号解析)"  # 默认构建类型
CMAKE_BUILD_TYPE=Debug  #版本类型
ENABLE_MONITOR=ON       #异常监控
ENABLE_ASAN=OFF         #ASAN 高级内存异常检测

# 设置构建类型
if [ $# -gt 0 ]; then
    build_type_lower=$(to_lower "$1")
    case "$build_type_lower" in
        "release")
            CMAKE_BUILD_TYPE=Release
            ENABLE_MONITOR=OFF
            ENABLE_ASAN=OFF 
            BUILD_VERSIONS="${GREEN}Release 版本 (最优性能，无调试信息)"
            ;;
        "debug"|"asan")
            ENABLE_MONITOR=OFF
            ENABLE_ASAN=ON 
            BUILD_VERSIONS="${RED}Debug 版本 (ASAN内存检测，性能较慢)"
            ;;
        *)
            # 未知参数，保持默认
            ;;
    esac
fi

BUILD_TYPE="-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DENABLE_MONITOR=${ENABLE_MONITOR} -DENABLE_ASAN=${ENABLE_ASAN}"

# 记录开始时间
START_TIME=$(date +%s.%N)

# 准备CMake参数
CMAKE_ARGS="-B ${BUILD_DIR} -G \"Unix Makefiles\" ${BUILD_TYPE}"

C_COMPILER_PATH=$(which gcc)
C_COMPILER_CROSS="本地"

# 如果是ARM交叉编译，通过环境变量传递编译器
if [[ "$CURRENT_PLATFORM" =~ ^ARM ]]; then
    C_COMPILER_PATH=${CMAKE_C_COMPILER}
    C_COMPILER_CROSS="交叉"
fi

# 执行构建命令
echo -e "${CYAN}${C_COMPILER_CROSS}编译 ${CPU_ARCH_NAME} 平台${NC}"
echo -e "${PURPLE}编译器: ${C_COMPILER_PATH}${NC}"
echo -e "${CYAN}构建目录: ${BUILD_DIR}${NC}"
echo -e "${CYAN}CMake命令: cmake ${CMAKE_ARGS}${NC}"

cmake -B "${BUILD_DIR}" -G "Unix Makefiles" $BUILD_TYPE

# 检查CMake配置是否成功
if [ $? -ne 0 ]; then
    echo -e "${RED}CMake配置失败!${NC}"
    exit 1
fi

# 获取 CPU 核心数
CORES=$(nproc 2>/dev/null || echo 4)
echo -e "使用 ${CORES} 核心并行任务构建..."

# 并行构建
cmake --build "${BUILD_DIR}" --parallel $CORES

# 记录结束时间
END_TIME=$(date +%s.%N)

# 计算时间差
if command -v bc >/dev/null 2>&1; then
    DURATION=$(echo "$END_TIME - $START_TIME" | bc | awk '{printf "%.2f", $0}')
else
    DURATION=$(echo "$END_TIME $START_TIME" | awk '{printf "%.2f", $1 - $2}')
fi

EXECUTABLE="${BUILD_DIR}/com2tcp_server"

# 检查构建是否成功
if [ $? -eq 0 ] && [ -f "$EXECUTABLE" ]; then
    # 显示文件架构信息
    if command -v file >/dev/null 2>&1; then
        echo -e "${CYAN}文件信息:${NC}"
        file_info=$(file "$EXECUTABLE")
        echo "$file_info" | fold -s -w 80 | sed "s/^/  /"  # 移除颜色代码
    fi
    
    echo -e "${GREEN}构建成功! ${BUILD_VERSIONS}${NC}"
    echo -e "${GREEN}可执行文件: ${EXECUTABLE}${NC}"
    echo -e "${CYAN}目标平台: ${CPU_ARCH_NAME} (${C_COMPILER_CROSS}编译) --> ${C_COMPILER_PATH}${NC}"

    # 显示文件大小
    file_size=$(ls -lh "$EXECUTABLE" | awk '{print $5}')
    echo -e "${CYAN}文件大小: ${file_size}${NC}"
fi

# 输出构建用时
echo -e "${GREEN}构建用时: ${DURATION} 秒, 时间: $(date '+%Y-%m-%d %H:%M:%S')${NC}"

# 检查并执行额外脚本（仅ARM架构）
if [ $? -eq 0 ] && [[ "$CURRENT_PLATFORM" =~ ^ARM ]] && [ -f "./$EXTRA_SH" ]; then
    echo -e "${CYAN}构建ARM平台 且 找到执行 $EXTRA_SH${NC}"
    chmod +x ./$EXTRA_SH
    ./$EXTRA_SH
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}$EXTRA_SH 执行成功!${NC}"
    else
        echo -e "${RED}$EXTRA_SH 执行失败!${NC}"
    fi
fi