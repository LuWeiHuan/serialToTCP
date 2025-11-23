# file: toolchain-arm.cmake
# ARM Linux GNU 工具链配置 - 使用用户指定的路径

set(CMAKE_SYSTEM_NAME Linux)

# 通过环境变量或CMake变量传递编译器路径
if(DEFINED ENV{ARM_C_COMPILER})
    set(CMAKE_C_COMPILER "$ENV{ARM_C_COMPILER}")
elseif(DEFINED ARM_C_COMPILER)
    set(CMAKE_C_COMPILER "${ARM_C_COMPILER}")
else()
    # 如果没有指定，尝试自动检测（简化版）
    if(EXISTS "/usr/bin/aarch64-linux-gnu-gcc")
        set(CMAKE_C_COMPILER "/usr/bin/aarch64-linux-gnu-gcc")
    elseif(EXISTS "/usr/bin/arm-linux-gnueabihf-gcc")
        set(CMAKE_C_COMPILER "/usr/bin/arm-linux-gnueabihf-gcc")
    else()
        message(FATAL_ERROR "ARM compiler not specified and not found in default locations. Please set ARM_C_COMPILER environment variable or CMake variable.")
    endif()
endif()

# 设置C++编译器（基于C编译器路径）
get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(COMPILER_NAME "${CMAKE_C_COMPILER}" NAME)

# 根据C编译器名称推断C++编译器
if(COMPILER_NAME MATCHES "aarch64")
    set(COMPILER_PREFIX "aarch64-linux-gnu")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
    set(ARM_ARCH "aarch64")
elseif(COMPILER_NAME MATCHES "arm")
    set(COMPILER_PREFIX "arm-linux-gnueabihf")
    set(CMAKE_SYSTEM_PROCESSOR arm)
    set(ARM_ARCH "arm32")
else()
    # 默认尝试
    set(CMAKE_SYSTEM_PROCESSOR arm)
    set(ARM_ARCH "arm32")
endif()

# 设置C++编译器
set(CMAKE_CXX_COMPILER "${COMPILER_DIR}/${COMPILER_PREFIX}-g++")

# 设置其他工具
set(CMAKE_STRIP "${COMPILER_DIR}/${COMPILER_PREFIX}-strip")
set(CMAKE_AR "${COMPILER_DIR}/${COMPILER_PREFIX}-ar")
set(CMAKE_RANLIB "${COMPILER_DIR}/${COMPILER_PREFIX}-ranlib")

# 架构特定的编译器标志
if(ARM_ARCH STREQUAL "aarch64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv8-a" CACHE STRING "C Compiler Flags")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv8-a" CACHE STRING "C++ Compiler Flags")
    message(STATUS "Using AArch64 compiler: ${CMAKE_C_COMPILER}")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfloat-abi=hard -mfpu=neon" CACHE STRING "C Compiler Flags")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -mfloat-abi=hard -mfpu=neon" CACHE STRING "C++ Compiler Flags")
    message(STATUS "Using ARM32 compiler: ${CMAKE_C_COMPILER}")
endif()

# 查找路径设置
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 输出配置信息
message(STATUS "ARM toolchain configured:")
message(STATUS "  Architecture: ${ARM_ARCH}")
message(STATUS "  C compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER}")