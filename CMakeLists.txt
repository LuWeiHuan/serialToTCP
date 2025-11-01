# 设置默认并行度
cmake_minimum_required(VERSION 3.15)

# 在 project() 之前强制设置 MinGW 环境
if(WIN32)
    # 首先查找 MinGW
    find_program(MINGW_GCC NAMES gcc)
    find_program(MINGW_GXX NAMES g++)
    find_program(MINGW_MAKE NAMES mingw32-make make)
    
    if(MINGW_GCC AND MINGW_MAKE)
        message(STATUS "MinGW detected, setting up MinGW environment")
        
        # 设置编译器
        set(CMAKE_C_COMPILER "${MINGW_GCC}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${MINGW_GXX}" CACHE FILEPATH "C++ compiler" FORCE)
        set(CMAKE_MAKE_PROGRAM "${MINGW_MAKE}" CACHE FILEPATH "Make program" FORCE)
        
        # 设置生成器为 MinGW Makefiles
        set(CMAKE_GENERATOR "MinGW Makefiles" CACHE INTERNAL "CMake Generator" FORCE)
        
        # 避免 CMake 的错误检测
        set(CMAKE_C_COMPILER_WORKS TRUE CACHE INTERNAL "")
        set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE INTERNAL "")
    else()
        message(WARNING "MinGW not found, using default generator")
    endif()
endif()

project(com2tcp_server C)

# 检测核心数
include(ProcessorCount)
ProcessorCount(CORES)
if(CORES EQUAL 0)
    set(CORES 4)  # 默认4核
endif()

message(STATUS "Detected ${CORES} CPU cores")

# 基本配置
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -MMD -MP -O3")

# 选项
option(ENABLE_MONITOR "Enable exception monitoring" ON)
option(COPY_TO_ROOT "Copy executable to root directory" ON)

# 监控配置
if(ENABLE_MONITOR)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCLOSE_EXCEPTION_MONITOR")
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
include_directories(inc openSrc/uthash/src openSrc/openthread/src)

file(GLOB SRC_FILES 
    "src/*.c" 
    "openSrc/uthash/src/*.c" 
    "openSrc/openthread/src/*.cpp"
)

add_executable(${PROJECT_NAME} ${SRC_FILES})
target_link_libraries(${PROJECT_NAME} ws2_32 setupapi uuid winpthread)

if(ENABLE_MONITOR)
    target_link_libraries(${PROJECT_NAME} dbghelp psapi)
endif()

if(COPY_TO_ROOT)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_SOURCE_DIR}
        COMMENT "Copying to root directory"
    )
endif()