# 设置最低 CMake 版本要求
cmake_minimum_required(VERSION 3.15)

# 项目定义
project(com2tcp_server C)

# === 架构检测优化 ===
# 优先通过工具链检测架构，其次通过系统处理器检测
if(CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SYSTEM_PROCESSOR_LOWER)
    message(STATUS "System processor: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

# 设置构建目录基础名称
set(BUILD_DIR_NAME "build/")

# 根据平台自动设置构建目录
if(WIN32)
    set(PLATFORM_WINDOWS 1)
    set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}Win")
    message(STATUS "Building for Windows platform")
    
    # Windows 特定设置
    add_definitions(-D_WIN32_WINNT=0x0600)
    set(CMAKE_RC_COMPILER_INIT windres)
    enable_language(RC)
    
elseif(UNIX)
    set(PLATFORM_LINUX 1)
    set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}Linux")
    message(STATUS "Building for Linux platform")
    
    # === 增强的架构检测逻辑 ===
    # 方法1: 通过编译器目标检测
    if(CMAKE_C_COMPILER AND NOT CMAKE_CROSSCOMPILING)
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -dumpmachine
            OUTPUT_VARIABLE COMPILER_TARGET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        message(STATUS "Compiler target: ${COMPILER_TARGET}")
        
        if(COMPILER_TARGET MATCHES "aarch64" OR COMPILER_TARGET MATCHES "arm64")
            set(DETECTED_ARCH "aarch64")
        elseif(COMPILER_TARGET MATCHES "arm" OR COMPILER_TARGET MATCHES "gnueabihf")
            set(DETECTED_ARCH "arm32")
        endif()
    endif()
    
    # 方法2: 通过系统处理器检测
    if(NOT DETECTED_ARCH)
        if(SYSTEM_PROCESSOR_LOWER MATCHES "aarch64" OR SYSTEM_PROCESSOR_LOWER MATCHES "arm64")
            set(DETECTED_ARCH "aarch64")
        elseif(SYSTEM_PROCESSOR_LOWER MATCHES "arm")
            set(DETECTED_ARCH "arm32")
        elseif(SYSTEM_PROCESSOR_LOWER MATCHES "x86_64" OR SYSTEM_PROCESSOR_LOWER MATCHES "amd64")
            set(DETECTED_ARCH "x86_64")
        elseif(SYSTEM_PROCESSOR_LOWER MATCHES "i386" OR SYSTEM_PROCESSOR_LOWER MATCHES "i686")
            set(DETECTED_ARCH "x86")
        endif()
    endif()
    
    # 方法3: 通过交叉编译标志检测
    if(CMAKE_CROSSCOMPILING)
        message(STATUS "Cross compiling: ${CMAKE_CROSSCOMPILING}")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
            set(DETECTED_ARCH "aarch64")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
            set(DETECTED_ARCH "arm32")
        endif()
    endif()
    
    # 设置架构相关变量
    if(DETECTED_ARCH STREQUAL "aarch64")
        set(PLATFORM_ARM 1)
        set(PLATFORM_ARM64 1)
        set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}LinuxARM64")
        message(STATUS "Target architecture: ARM64 (AArch64)")
    elseif(DETECTED_ARCH STREQUAL "arm32")
        set(PLATFORM_ARM 1)
        set(PLATFORM_ARM32 1)
        set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}LinuxARM32")
        message(STATUS "Target architecture: ARM32")
    elseif(DETECTED_ARCH STREQUAL "x86_64")
        set(PLATFORM_X86 1)
        set(PLATFORM_X64 1)
        set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}LinuxX64")
        message(STATUS "Target architecture: x86_64")
    elseif(DETECTED_ARCH STREQUAL "x86")
        set(PLATFORM_X86 1)
        set(PLATFORM_X86 1)
        set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}LinuxX86")
        message(STATUS "Target architecture: x86")
    else()
        # 默认情况
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}LinuxX64")
            message(STATUS "Target architecture: 64-bit (auto-detected)")
        else()
            set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}LinuxX86")
            message(STATUS "Target architecture: 32-bit (auto-detected)")
        endif()
    endif()
    
    # Linux 通用设置（只在这里设置一次）
    add_definitions(-D_GNU_SOURCE -pthread)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
    
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# 设置构建目录变量
set(BUILD_DIR "${DEFAULT_BUILD_DIR}" CACHE STRING "Build directory")
message(STATUS "Build目录: ${BUILD_DIR}")

# 设置 C 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译器检测和设置
if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    message(STATUS "Using GCC compiler: ${CMAKE_C_COMPILER_VERSION}")
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Using Clang compiler: ${CMAKE_C_COMPILER_VERSION}")
elseif(MSVC)
    message(STATUS "Using MSVC compiler: ${CMAKE_C_COMPILER_VERSION}")
else()
    message(WARNING "Unknown compiler: ${CMAKE_C_COMPILER_ID}")
endif()

# 编译选项配置 - 根据平台和编译器组合调整
# 基础编译选项
set(COMMON_C_FLAGS "-Wall -Wextra -MMD -MP")

# 针对 Win+MinGW 组合的特殊处理
if(PLATFORM_WINDOWS AND CMAKE_C_COMPILER_ID MATCHES "GNU")
    message(STATUS "Win+MinGW组合: 移除-Wpedantic并启用编译器扩展")
    set(COMMON_C_FLAGS "${COMMON_C_FLAGS}")
    set(ENABLE_C_EXTENSIONS ON)
    # 为MinGW添加ANSI stdio支持
    add_definitions(-D__USE_MINGW_ANSI_STDIO=1)
else()
    # 其他组合：保留严格检查
    message(STATUS "其他平台组合: 启用-Wpedantic并禁用编译器扩展")
    set(COMMON_C_FLAGS "${COMMON_C_FLAGS} -Wpedantic")
    set(ENABLE_C_EXTENSIONS OFF)
endif()

# 调试版本选项
set(CMAKE_C_FLAGS_DEBUG "${COMMON_C_FLAGS} -g -O0 -DDEBUG")
# 发布版本选项  
set(CMAKE_C_FLAGS_RELEASE "${COMMON_C_FLAGS} -O3 -DNDEBUG")
# 最小大小版本选项
set(CMAKE_C_FLAGS_MINSIZEREL "${COMMON_C_FLAGS} -Os -DNDEBUG")
# 带调试信息的发布版本
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${COMMON_C_FLAGS} -O2 -g -DNDEBUG")

# 禁用 PIE 以便符号解析正常工作（GCC 大于 8.5 版本）
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ENABLE_ASAN)
    # 检查是否是 MinGW 且版本支持 -no-pie
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -dumpversion
            OUTPUT_VARIABLE GCC_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        message(STATUS "Detected GCC version: ${GCC_VERSION}")
        
        # 扩展版本检测范围：从 5.0 开始的所有版本都需要 -no-pie
        if(GCC_VERSION VERSION_GREATER_EQUAL "8.5")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
            message(STATUS "PIE disabled for MinGW ${GCC_VERSION} to enable Addr2Line symbol resolution")
        else()
            message(STATUS "PIE not disabled for MinGW ${GCC_VERSION} (version < 8.5)")
        endif()
    endif()
endif()

# 选项配置
option(ENABLE_MONITOR "Enable exception monitoring" ON)
option(COPY_TO_ROOT "Copy executable to root directory" ON)
option(BUILD_STATIC "Build static executable" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

# === 构建类型智能配置 ===
# 记录选项是否被显式设置
get_property(ENABLE_MONITOR_DEFINED CACHE ENABLE_MONITOR PROPERTY VALUE SET)
get_property(ENABLE_ASAN_DEFINED CACHE ENABLE_ASAN PROPERTY VALUE SET)

# 根据构建类型设置默认选项
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # 发布版本：无监控，无ASAN
    if(NOT ENABLE_MONITOR_DEFINED)
        set(ENABLE_MONITOR OFF CACHE BOOL "Enable exception monitoring" FORCE)
    endif()
    if(NOT ENABLE_ASAN_DEFINED)  
        set(ENABLE_ASAN OFF CACHE BOOL "Enable Address Sanitizer" FORCE)
    endif()
    message(STATUS "Release build: monitoring=OFF, ASAN=OFF")
else()
    # 调试/开发版本：根据选项设置
    if(ENABLE_ASAN)
        # ASAN版本：禁用监控
        if(NOT ENABLE_MONITOR_DEFINED)
            set(ENABLE_MONITOR OFF CACHE BOOL "Enable exception monitoring" FORCE)
        endif()
        message(STATUS "Debug build with ASAN: monitoring=OFF, ASAN=ON")
    else()
        # 默认开发版本：启用监控
        if(NOT ENABLE_MONITOR_DEFINED)
            set(ENABLE_MONITOR ON CACHE BOOL "Enable exception monitoring" FORCE)
        endif()
        message(STATUS "Development build: monitoring=ON, ASAN=OFF")
    endif()
endif()

# 异常监控配置
if(ENABLE_MONITOR)
    message(STATUS "Exception monitoring: ENABLED")
else()
    message(STATUS "Exception monitoring: DISABLED")
    add_definitions(-DCLOSE_EXCEPTION_MONITOR)
endif()

# Sanitizer 配置
if(ENABLE_ASAN AND PLATFORM_LINUX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    
    # ASAN 版本自动禁用异常监控
    set(ENABLE_MONITOR OFF)
    add_definitions(-DCLOSE_EXCEPTION_MONITOR)
    
    message(STATUS "Address Sanitizer: ENABLED - Exception monitoring disabled")
endif()

if(ENABLE_UBSAN AND PLATFORM_LINUX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    message(STATUS "Undefined Behavior Sanitizer: ENABLED")
endif()

# 静态链接配置
if(BUILD_STATIC)
    if(PLATFORM_LINUX)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        message(STATUS "Linux静态链接: ENABLED")
    elseif(PLATFORM_WINDOWS)
        # Windows 下使用静态链接
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        
        # 对于 MinGW，可能需要额外的静态库标志
        if(CMAKE_C_COMPILER_ID MATCHES "GNU")
            # MinGW 静态链接可能需要这些标志
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
        endif()
        
        message(STATUS "Windows静态链接: ENABLED")
    endif()
else()
    message(STATUS "静态链接: DISABLED")
endif()

# 包含目录设置
include_directories(
    source/main
    source/inc
    source/middleware
    source/openSrc/uthash/src
    source/openSrc/minIni/dev
)

# 源文件分组
# 平台通用源文件
file(GLOB COMMON_SOURCES
    "source/main/*.c"
    "source/middleware/*.c"
    "source/src/*.c"
)

# 第三方库源文件
file(GLOB THIRDPARTY_SOURCES
    "source/openSrc/minIni/dev/minIni.c"
)

# 平台特定源文件
if(PLATFORM_WINDOWS)
    file(GLOB PLATFORM_SOURCES
        "source/src/platform/windows/*.c"
    )
    set(PLATFORM_DEFINITIONS -D_WIN32_WINNT=0x0600)
elseif(PLATFORM_LINUX)
    file(GLOB PLATFORM_SOURCES
        "source/src/platform/linux/*.c"
    )
endif()

# 所有源文件
set(ALL_SOURCES
    ${COMMON_SOURCES}
    ${THIRDPARTY_SOURCES}
    ${PLATFORM_SOURCES}
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${ALL_SOURCES})

# 平台特定链接库
if(PLATFORM_WINDOWS)
    # Windows 链接库
    target_link_libraries(${PROJECT_NAME} 
        ws2_32
        setupapi 
        advapi32
    )
    
    # Windows 异常监控库
    if(ENABLE_MONITOR)
        target_link_libraries(${PROJECT_NAME} dbghelp psapi)
    endif()
    
elseif(PLATFORM_LINUX)
    # Linux 基础链接库
    target_link_libraries(${PROJECT_NAME}
        pthread
        m
        rt
        dl
    )

    # === UDEV 库配置 === 
    
    # 对于ARM交叉编译，设置库搜索路径
    if(PLATFORM_ARM AND CMAKE_CROSSCOMPILING)
        if(PLATFORM_ARM64)
            set(ARM_LIB_PATH "${CMAKE_SOURCE_DIR}/source/lib/aarch64")
            message(STATUS "ARM64 cross compiling, library path: ${ARM_LIB_PATH}")
        elseif(PLATFORM_ARM32)
            set(ARM_LIB_PATH "${CMAKE_SOURCE_DIR}/source/lib/arm32")
            message(STATUS "ARM32 cross compiling, library path: ${ARM_LIB_PATH}")
        endif()
        
        if(EXISTS "${ARM_LIB_PATH}")
            # 添加库搜索路径
            set(CMAKE_LIBRARY_PATH "${ARM_LIB_PATH};${CMAKE_LIBRARY_PATH}")
            message(STATUS "Library search path: ${CMAKE_LIBRARY_PATH}")

            file(GLOB UDEV_LIB_CANDIDATES "${ARM_LIB_PATH}/libudev.so*")
            if(UDEV_LIB_CANDIDATES)
                list(GET UDEV_LIB_CANDIDATES 0 UDEV_LIB)  # 使用第一个找到的文件
                message(STATUS "Found udev library: ${UDEV_LIB}")
            else()
                message(WARNING "No udev library found in ${ARM_LIB_PATH}")
            endif()
        else()
            message(WARNING "ARM library path not found: ${ARM_LIB_PATH}")
        endif()
    endif()

# 统一搜索
find_library(UDEV_LIB udev)
if(UDEV_LIB)
    message(STATUS "Linked udev: ${UDEV_LIB}")
    target_link_libraries(${PROJECT_NAME} ${UDEV_LIB})
    add_definitions(-DHAVE_LIBUDEV=1)
else()
    message(WARNING "udev not found, using sysfs fallback")
endif()

endif()

# 目标属性设置
set_target_properties(${PROJECT_NAME} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ${ENABLE_C_EXTENSIONS}
)

# 安装配置
if(PLATFORM_LINUX)
    # Linux 安装配置
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        COMPONENT runtime
    )
    
    install(FILES README.md LICENSE
        DESTINATION share/doc/${PROJECT_NAME}
        COMPONENT documentation
    )
    
    # 创建 systemd 服务文件（可选）
    if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/com2tcp-server.service")
        install(FILES scripts/com2tcp-server.service
            DESTINATION /source/lib/systemd/system
            COMPONENT systemd
        )
    endif()
    
elseif(PLATFORM_WINDOWS)
    # Windows 安装配置
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION .
        COMPONENT runtime
    )
endif()

# 复制到根目录（开发时使用）
if(COPY_TO_ROOT)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_SOURCE_DIR}
        COMMENT "Copying executable to root directory"
    )
endif()

# 测试配置（可选）
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    enable_testing()
    
    # 添加简单的编译测试
    add_test(NAME CompilationTest
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${PROJECT_NAME}
    )
endif()

# 包信息
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "YourCompany")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Serial to TCP Bridge Server")
set(CPACK_PACKAGE_VERSION_MAJOR "1")
set(CPACK_PACKAGE_VERSION_MINOR "0")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")

if(PLATFORM_WINDOWS)
    set(CPACK_GENERATOR "ZIP;NSIS")
elseif(PLATFORM_LINUX)
    set(CPACK_GENERATOR "TGZ;DEB")
endif()

include(CPack)

# 自定义目标
# 清理构建目标
add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning all build files"
)

# 格式化代码目标（如果安装了 clang-format）
find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCES}
        COMMENT "Formatting source code"
    )
endif()

# 依赖检查
# 检查必要的头文件
#include(CheckIncludeFile)
#if(PLATFORM_LINUX__)
#    # 对于交叉编译，设置包含路径
#    if(CMAKE_CROSSCOMPILING)
#        # 添加工具链的包含路径
#        if(EXISTS "${CMAKE_SYSROOT}/usr/include")
#            include_directories(SYSTEM "${CMAKE_SYSROOT}/usr/include")
#        endif()
#        if(EXISTS "${CMAKE_FIND_ROOT_PATH}/usr/include")
#            include_directories(SYSTEM "${CMAKE_FIND_ROOT_PATH}/usr/include")
#        endif()
#    endif()
#    
#    check_include_file("pthread.h" HAVE_PTHREAD_H)
#    check_include_file("sys/socket.h" HAVE_SYS_SOCKET_H)
#    check_include_file("arpa/inet.h" HAVE_ARPA_INET_H)
#    check_include_file("termios.h" HAVE_TERMIOS_H)
#    
#    if(NOT HAVE_PTHREAD_H)
#        message(FATAL_ERROR "pthread.h not found - required for Linux build. Please check your cross-compilation toolchain installation.")
#    endif()
#    if(NOT HAVE_SYS_SOCKET_H)
#        message(FATAL_ERROR "sys/socket.h not found - required for networking. Please check your cross-compilation toolchain installation.")
#    endif()
#    if(NOT HAVE_TERMIOS_H)
#        message(FATAL_ERROR "termios.h not found - required for serial port support. Please check your cross-compilation toolchain installation.")
#    endif()
#endif()

# 编译器特性检查
include(CheckCCompilerFlag)
if(PLATFORM_LINUX)
    check_c_compiler_flag("-fstack-protector-strong" HAVE_STACK_PROTECTOR_STRONG)
    if(HAVE_STACK_PROTECTOR_STRONG)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
    endif()
endif()

# 输出构建摘要
message(STATUS "")
message(STATUS "=== Build Configuration Summary ===")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
if(PLATFORM_ARM64)
    message(STATUS "Target: ARM64 (AArch64)")
elseif(PLATFORM_ARM32)
    message(STATUS "Target: ARM32")
elseif(PLATFORM_X64)
    message(STATUS "Target: x86_64")
elseif(PLATFORM_X86)
    message(STATUS "Target: x86")
endif()
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Exception monitoring: ${ENABLE_MONITOR}")
message(STATUS "Static linking: ${BUILD_STATIC}")
message(STATUS "C Extensions: ${ENABLE_C_EXTENSIONS}")
message(STATUS "Cross compiling: ${CMAKE_CROSSCOMPILING}")
message(STATUS "===================================")