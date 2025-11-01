#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 显示帮助信息
show_help() {
    echo "Usage: $0 [clean|release|debug|help]"
    echo ""
    echo "Options:"
    echo "  clean    - Delete build directory"
    echo "  release  - Build release version"
    echo "  debug    - Build debug version"
    echo "  help     - Show this help message"
    echo "  no args  - Normal build"
}

# 检查参数
if [ "$1" = "help" ] || [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
    show_help
    exit 0
fi

# 清理构建目录
if [ "$1" = "clean" ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    if [ -d "build" ]; then
        rm -rf build
        echo -e "${GREEN}Clean completed.${NC}"
    else
        echo -e "${YELLOW}Build directory does not exist.${NC}"
    fi
    exit 0
fi

# 设置构建类型
BUILD_TYPE=""
case "$1" in
    "release")
        BUILD_TYPE="-DENABLE_MONITOR=OFF"
        echo -e "${BLUE}Building RELEASE version...${NC}"
        ;;
    "debug")
        BUILD_TYPE="-DENABLE_MONITOR=ON"
        echo -e "${BLUE}Building DEBUG version...${NC}"
        ;;
    *)
        echo -e "${BLUE}Building...${NC}"
        ;;
esac

# 记录开始时间
START_TIME=$(date +%s.%N)

# 执行构建命令
if [ -z "$BUILD_TYPE" ]; then
    cmake -B build -G "Unix Makefiles"
else
    cmake -B build -G "Unix Makefiles" $BUILD_TYPE
fi

# 获取 CPU 核心数
CORES=$(nproc 2>/dev/null || echo 4)
echo -e "Building with ${CORES} parallel jobs..."

# 并行构建
cmake --build build --parallel $CORES

# 检查构建是否成功
if [ $? -eq 0 ]; then
    echo -e "${GREEN}Build successful!${NC}"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi

# 记录结束时间
END_TIME=$(date +%s.%N)

# 计算时间差
DURATION=$(echo "$END_TIME - $START_TIME" | bc | awk '{printf "%.2f", $0}')

# 输出构建用时
echo -e "${GREEN}Build duration: ${DURATION} seconds${NC}"