# 设置最低 CMake 版本要求
cmake_minimum_required(VERSION 3.15)
# 项目定义
project(com2tcp C)


# === 架构检测优化 ===
if(CMAKE_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" SYSTEM_PROCESSOR_LOWER)
endif()

set(BUILD_DIR_NAME "build/")

if(DEFINED ENV{CMAKE_C_COMPILER} AND EXISTS "$ENV{CMAKE_C_COMPILER}")
    set(CMAKE_C_COMPILER "$ENV{CMAKE_C_COMPILER}" CACHE FILEPATH "C compiler" FORCE)
endif()

if(WIN32)
    set(PLATFORM_WINDOWS 1)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(DETECTED_ARCH "X64")
        set(PLATFORM_X64 1)
    else()
        set(DETECTED_ARCH "X86")
        set(PLATFORM_X86 1)
    endif()
    add_definitions(-D_WIN32_WINNT=0x0600)
    set(CMAKE_RC_COMPILER_INIT windres)
    enable_language(RC)
elseif(UNIX)
    set(PLATFORM_LINUX 1)

    if(CMAKE_C_COMPILER)
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -dumpmachine
            OUTPUT_VARIABLE COMPILER_TARGET
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        message(STATUS "Compiler target: ${COMPILER_TARGET}")
        if(COMPILER_TARGET MATCHES "aarch64" OR COMPILER_TARGET MATCHES "arm64")
            set(DETECTED_ARCH "ARM64")
            if(NOT COMPILER_TARGET STREQUAL "x86_64-linux-gnu" AND
              NOT COMPILER_TARGET MATCHES "i[0-9]86")
                set(CMAKE_CROSSCOMPILING TRUE CACHE BOOL "Cross compiling" FORCE)
            endif()
        elseif(COMPILER_TARGET MATCHES "arm" OR COMPILER_TARGET MATCHES "gnueabihf")
            set(DETECTED_ARCH "ARM32")
            if(NOT COMPILER_TARGET STREQUAL "x86_64-linux-gnu" AND
              NOT COMPILER_TARGET MATCHES "i[0-9]86")
                set(CMAKE_CROSSCOMPILING TRUE CACHE BOOL "Cross compiling" FORCE)
            endif()
        endif()
    endif()

    if(NOT DETECTED_ARCH)
        if(SYSTEM_PROCESSOR_LOWER MATCHES "aarch64" OR SYSTEM_PROCESSOR_LOWER MATCHES "arm64")
            set(DETECTED_ARCH "ARM64")
        elseif(SYSTEM_PROCESSOR_LOWER MATCHES "arm")
            set(DETECTED_ARCH "ARM32")
        elseif(SYSTEM_PROCESSOR_LOWER MATCHES "x86_64" OR SYSTEM_PROCESSOR_LOWER MATCHES "amd64")
            set(DETECTED_ARCH "X64")
        elseif(SYSTEM_PROCESSOR_LOWER MATCHES "i386" OR SYSTEM_PROCESSOR_LOWER MATCHES "i686")
            set(DETECTED_ARCH "X86")
        endif()
    endif()

    if(NOT DETECTED_ARCH)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(DETECTED_ARCH "X64")
        else()
            set(DETECTED_ARCH "X86")
        endif()
    endif()

    if(CMAKE_C_COMPILER AND NOT CMAKE_CROSSCOMPILING)
        if(CMAKE_C_COMPILER MATCHES "aarch64|arm64|arm-linux-gnueabihf")
            set(CMAKE_CROSSCOMPILING TRUE CACHE BOOL "Cross compiling" FORCE)
            message(STATUS "Forced cross-compiling based on compiler path")
        endif()
    endif()

    if(DETECTED_ARCH STREQUAL "ARM64")
        set(PLATFORM_ARM 1)
        set(PLATFORM_ARM64 1)
    elseif(DETECTED_ARCH STREQUAL "ARM32")
        set(PLATFORM_ARM 1)
        set(PLATFORM_ARM32 1)
    elseif(DETECTED_ARCH STREQUAL "X64")
        set(PLATFORM_X86 1)
        set(PLATFORM_X64 1)
    elseif(DETECTED_ARCH STREQUAL "X86")
        set(PLATFORM_X86 1)
    endif()

    add_definitions(-D_GNU_SOURCE -pthread)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
    set(DETECTED_ARCH "Generic")
endif()

if(DETECTED_ARCH STREQUAL "Generic")
    message(WARNING "Could not detect architecture, using generic build directory")
endif()

set(DEFAULT_BUILD_DIR "${BUILD_DIR_NAME}${CMAKE_SYSTEM_NAME}${DETECTED_ARCH}")
set(BUILD_DIR "${DEFAULT_BUILD_DIR}" CACHE STRING "Build directory")
message(STATUS "Build directory: ${BUILD_DIR} (${DEFAULT_BUILD_DIR})")
message(STATUS "Building for ${CMAKE_SYSTEM_NAME} Platform")


set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    message(STATUS "Using GCC compiler: ${CMAKE_C_COMPILER_VERSION}")
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(STATUS "Using Clang compiler: ${CMAKE_C_COMPILER_VERSION}")
elseif(MSVC)
    message(STATUS "Using MSVC compiler: ${CMAKE_C_COMPILER_VERSION}")
else()
    message(WARNING "Unknown compiler: ${CMAKE_C_COMPILER_ID}")
endif()

set(COMMON_C_FLAGS "-Wall -Wextra -MMD -MP")

if(PLATFORM_WINDOWS AND CMAKE_C_COMPILER_ID MATCHES "GNU")
    message(STATUS "Win+MinGW组合: 移除 -Wpedantic 并启用编译器扩展")
    set(ENABLE_C_EXTENSIONS ON)
    add_definitions(-D__USE_MINGW_ANSI_STDIO=1)
else()
    message(STATUS "其他平台组合: 启用 -Wpedantic 并禁用编译器扩展")
    set(COMMON_C_FLAGS "${COMMON_C_FLAGS} -Wpedantic")
    set(ENABLE_C_EXTENSIONS OFF)
endif()

set(CMAKE_C_FLAGS_DEBUG "${COMMON_C_FLAGS} -g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "${COMMON_C_FLAGS} -O3 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL "${COMMON_C_FLAGS} -Os -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${COMMON_C_FLAGS} -O2 -g -DNDEBUG")

if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT ENABLE_ASAN)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU")
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -dumpversion
            OUTPUT_VARIABLE GCC_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(GCC_VERSION VERSION_GREATER_EQUAL "8.5")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -no-pie")
            message(STATUS "PIE disabled for MinGW ${GCC_VERSION} to enable Addr2Line symbol resolution")
        else()
            message(STATUS "PIE not disabled for MinGW ${GCC_VERSION} (version < 8.5)")
        endif()
    endif()
endif()

option(ENABLE_MONITOR "Enable exception monitoring" ON)
option(COPY_TO_ROOT "Copy executable to root directory" ON)
option(BUILD_STATIC "Build static executable" OFF)
option(ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)

get_property(ENABLE_MONITOR_DEFINED CACHE ENABLE_MONITOR PROPERTY VALUE SET)
get_property(ENABLE_ASAN_DEFINED CACHE ENABLE_ASAN PROPERTY VALUE SET)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT ENABLE_MONITOR_DEFINED)
        set(ENABLE_MONITOR OFF CACHE BOOL "Enable exception monitoring" FORCE)
    endif()
    if(NOT ENABLE_ASAN_DEFINED)
        set(ENABLE_ASAN OFF CACHE BOOL "Enable Address Sanitizer" FORCE)
    endif()
    message(STATUS "Release build: monitoring=OFF, ASAN=OFF")
else()
    if(ENABLE_ASAN)
        if(NOT ENABLE_MONITOR_DEFINED)
            set(ENABLE_MONITOR OFF CACHE BOOL "Enable exception monitoring" FORCE)
        endif()
        message(STATUS "Debug build with ASAN: monitoring=OFF, ASAN=ON")
    else()
        if(NOT ENABLE_MONITOR_DEFINED)
            set(ENABLE_MONITOR ON CACHE BOOL "Enable exception monitoring" FORCE)
        endif()
        message(STATUS "Development build: monitoring=ON, ASAN=OFF")
    endif()
endif()

if(NOT ENABLE_MONITOR)
    add_definitions(-DCLOSE_EXCEPTION_MONITOR)
endif()

if(ENABLE_ASAN AND PLATFORM_LINUX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(ENABLE_MONITOR OFF)
    add_definitions(-DCLOSE_EXCEPTION_MONITOR)
    message(STATUS "Address Sanitizer: ENABLED - Exception monitoring disabled")
endif()

if(ENABLE_UBSAN AND PLATFORM_LINUX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
    message(STATUS "Undefined Behavior Sanitizer: ENABLED")
endif()

if(BUILD_STATIC)
    if(PLATFORM_LINUX)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        message(STATUS "Linux Static link: ENABLED")
    elseif(PLATFORM_WINDOWS)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
        if(CMAKE_C_COMPILER_ID MATCHES "GNU")
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
        endif()
        message(STATUS "Windows Static link: ENABLED")
    endif()
else()
    message(STATUS "Static link: DISABLED")
endif()


# ============================================================
# === 三方库统一配置 (third_party) ===
# ============================================================
option(FORCE_SYSTEM_THIRD_PARTY
    "Force using system libraries even if third_party exists (host build only)" OFF)
option(ALLOW_SYSTEM_FALLBACK
    "Allow fallback to system libraries when cross-compiling (debug only)" OFF)

if(PLATFORM_ARM64)
    set(TP_ARCH_DIR "aarch64")
elseif(PLATFORM_ARM32)
    set(TP_ARCH_DIR "armv7")
elseif(PLATFORM_X64)
    set(TP_ARCH_DIR "x64")
elseif(PLATFORM_X86)
    set(TP_ARCH_DIR "x86")
else()
    set(TP_ARCH_DIR "generic")
endif()

set(THIRD_PARTY_ROOT "${CMAKE_SOURCE_DIR}/source/third_party")
set(THIRD_PARTY_ARCH_ROOT "${THIRD_PARTY_ROOT}/${TP_ARCH_DIR}")

if(NOT TP_TARGET_TRIPLE)
    if(COMPILER_TARGET)
        set(TP_TARGET_TRIPLE "${COMPILER_TARGET}")
    else()
        if(PLATFORM_ARM64)
            set(TP_TARGET_TRIPLE "aarch64-linux-gnu")
        elseif(PLATFORM_ARM32)
            set(TP_TARGET_TRIPLE "arm-linux-gnueabihf")
        elseif(PLATFORM_X64)
            set(TP_TARGET_TRIPLE "x86_64-linux-gnu")
        else()
            set(TP_TARGET_TRIPLE "")
        endif()
    endif()
endif()

if(CMAKE_CROSSCOMPILING)
    set(TP_PREFER_SYSTEM FALSE)
    message(STATUS "[third_party] Cross-compiling: third_party FIRST, system fallback = ${ALLOW_SYSTEM_FALLBACK}")
else()
    if(FORCE_SYSTEM_THIRD_PARTY)
        set(TP_PREFER_SYSTEM TRUE)
        message(STATUS "[third_party] Host build: FORCE system libraries")
    else()
        set(TP_PREFER_SYSTEM FALSE)
        message(STATUS "[third_party] Host build: third_party FIRST, system fallback = ON")
    endif()
endif()

message(STATUS "[third_party] Arch root:  ${THIRD_PARTY_ARCH_ROOT}")
message(STATUS "[third_party] Target triple: ${TP_TARGET_TRIPLE}")

set(TP_ALL_INCLUDE_DIRS "")
set(TP_ALL_LIBRARIES    "")
set(TP_ALL_DEFINITIONS  "")
set(TP_SUMMARY          "")

function(_tp_find_lib_in_dir dir libname out_var)
    set(_found "")
    if(NOT EXISTS "${dir}")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    set(_suffixes ".so" ".so.1" ".so.2" ".a" ".dll.a" ".lib")
    foreach(_s ${_suffixes})
        if(EXISTS "${dir}/lib${libname}${_s}")
            set(_found "${dir}/lib${libname}${_s}")
            break()
        endif()
    endforeach()
    if(NOT _found)
        file(GLOB _candidates "${dir}/lib${libname}.so.*")
        if(_candidates)
            list(SORT _candidates)
            list(GET _candidates 0 _found)
        endif()
    endif()
    set(${out_var} "${_found}" PARENT_SCOPE)
endfunction()

function(_tp_shared_lib_dirs arch_root out_var)
    set(_dirs "")
    if(TP_TARGET_TRIPLE)
        list(APPEND _dirs "${arch_root}/lib/${TP_TARGET_TRIPLE}")
        list(APPEND _dirs "${arch_root}/usr/lib/${TP_TARGET_TRIPLE}")
    endif()
    list(APPEND _dirs "${arch_root}/lib")
    list(APPEND _dirs "${arch_root}/usr/lib")
    set(${out_var} "${_dirs}" PARENT_SCOPE)
endfunction()

function(third_party_find)
    cmake_parse_arguments(TP
        ""
        "REQUIRED;REQUIRED_ALL;LAYOUT"
        "LIB_NAMES;DEFINE"
        ${ARGN}
    )

    if(NOT TP_LIB_NAMES)
        message(FATAL_ERROR "third_party_find: LIB_NAMES is required")
    endif()
    if(NOT TP_LAYOUT)
        set(TP_LAYOUT "auto")
    endif()

    list(GET TP_LIB_NAMES 0 _tp_display_name)

    set(_found_inc "")
    set(_found_libs "")
    set(_source "none")
    set(_layout_used "")

    set(_per_name_dir "${THIRD_PARTY_ARCH_ROOT}/${_tp_display_name}")

    if(TP_LAYOUT STREQUAL "auto" OR TP_LAYOUT STREQUAL "per_name")
        if(EXISTS "${_per_name_dir}")
            if(EXISTS "${_per_name_dir}/include")
                set(_found_inc "${_per_name_dir}/include")
            endif()
            foreach(_name ${TP_LIB_NAMES})
                _tp_find_lib_in_dir("${_per_name_dir}/lib" "${_name}" _one)
                if(_one)
                    list(APPEND _found_libs "${_one}")
                endif()
            endforeach()
            if(_found_libs)
                set(_source "third_party")
                set(_layout_used "per_name")
            endif()
            if(EXISTS "${_per_name_dir}/pkg.cmake")
                include("${_per_name_dir}/pkg.cmake")
                message(STATUS "[third_party] ${_tp_display_name}: loaded pkg.cmake")
            endif()
        elseif(TP_LAYOUT STREQUAL "per_name")
            message(STATUS "[third_party] ${_tp_display_name}: per_name layout dir not found: ${_per_name_dir}")
        endif()
    endif()

    set(_need_shared FALSE)
    if(TP_LAYOUT STREQUAL "shared")
        set(_need_shared TRUE)
    elseif(TP_LAYOUT STREQUAL "auto")
        list(LENGTH TP_LIB_NAMES _want_n)
        list(LENGTH _found_libs   _got_n)
        if(_got_n LESS _want_n)
            set(_need_shared TRUE)
        endif()
    endif()

    if(_need_shared)
        set(_shared_inc "${THIRD_PARTY_ARCH_ROOT}/usr/include")
        if(EXISTS "${_shared_inc}")
            set(_found_inc "${_shared_inc}")
        endif()

        _tp_shared_lib_dirs("${THIRD_PARTY_ARCH_ROOT}" _shared_lib_dirs)
        foreach(_name ${TP_LIB_NAMES})
            set(_already "")
            foreach(_l ${_found_libs})
                get_filename_component(_fn "${_l}" NAME)
                if(_fn MATCHES "^lib${_name}\\.")
                    set(_already "${_l}")
                    break()
                endif()
            endforeach()
            if(_already)
                continue()
            endif()
            foreach(_d ${_shared_lib_dirs})
                _tp_find_lib_in_dir("${_d}" "${_name}" _one)
                if(_one)
                    list(APPEND _found_libs "${_one}")
                    break()
                endif()
            endforeach()
        endforeach()

        if(_found_libs AND _source STREQUAL "none")
            set(_source "third_party")
            set(_layout_used "shared")
        endif()

        if(EXISTS "${THIRD_PARTY_ARCH_ROOT}/pkg/${_tp_display_name}.cmake")
            include("${THIRD_PARTY_ARCH_ROOT}/pkg/${_tp_display_name}.cmake")
            message(STATUS "[third_party] ${_tp_display_name}: loaded shared pkg.cmake")
        endif()
    endif()

    set(_allow_system FALSE)
    if(CMAKE_CROSSCOMPILING)
        if(ALLOW_SYSTEM_FALLBACK)
            set(_allow_system TRUE)
        endif()
    else()
        set(_allow_system TRUE)
    endif()

    list(LENGTH TP_LIB_NAMES _want_n)
    list(LENGTH _found_libs   _got_n)

    if(_allow_system AND _got_n LESS _want_n)
        foreach(_name ${TP_LIB_NAMES})
            set(_already "")
            foreach(_l ${_found_libs})
                get_filename_component(_fn "${_l}" NAME)
                if(_fn MATCHES "^lib${_name}\\." OR _fn MATCHES "^${_name}\\.")
                    set(_already "${_l}")
                    break()
                endif()
            endforeach()
            if(_already)
                continue()
            endif()
            find_library(_sys_lib NAMES ${_name})
            if(_sys_lib)
                list(APPEND _found_libs "${_sys_lib}")
                if(_source STREQUAL "none")
                    set(_source "system")
                    set(_layout_used "system")
                endif()
            endif()
        endforeach()
    endif()

    if(NOT CMAKE_CROSSCOMPILING AND FORCE_SYSTEM_THIRD_PARTY)
        set(_found_libs "")
        set(_found_inc "")
        set(_layout_used "")
        foreach(_name ${TP_LIB_NAMES})
            find_library(_sys_lib NAMES ${_name})
            if(_sys_lib)
                list(APPEND _found_libs "${_sys_lib}")
            endif()
        endforeach()
        if(_found_libs)
            set(_source "system")
            set(_layout_used "system")
        endif()
    endif()

    list(LENGTH _found_libs _got_n)

    if(_got_n EQUAL 0)
        if(TP_REQUIRED OR TP_REQUIRED_ALL)
            message(FATAL_ERROR
                "[third_party] ${_tp_display_name}: NOT FOUND "
                "(LIB_NAMES=${TP_LIB_NAMES}, layout=${TP_LAYOUT}, "
                "per_name_dir=${_per_name_dir}, arch_root=${THIRD_PARTY_ARCH_ROOT}, "
                "cross=${CMAKE_CROSSCOMPILING}, allow_system=${_allow_system})")
        else()
            message(WARNING
                "[third_party] ${_tp_display_name}: NOT FOUND, feature disabled "
                "(LIB_NAMES=${TP_LIB_NAMES}, layout=${TP_LAYOUT}, "
                "arch_root=${THIRD_PARTY_ARCH_ROOT}, cross=${CMAKE_CROSSCOMPILING})")
            return()
        endif()
    endif()

    if(TP_REQUIRED_ALL AND _got_n LESS _want_n)
        message(FATAL_ERROR
            "[third_party] ${_tp_display_name}: REQUIRED_ALL not satisfied "
            "(${_got_n}/${_want_n} found, LIB_NAMES=${TP_LIB_NAMES})")
    endif()

    set(_src_tag "${_source}")
    if(_layout_used)
        set(_src_tag "${_source}/${_layout_used}")
    endif()
    string(REPLACE ";" ", " _libs_str "${_found_libs}")
    message(STATUS
        "[third_party] ${_tp_display_name}: [${_src_tag}] "
        "libs=${_libs_str} inc=${_found_inc}")

    set(TP_INCLUDE_DIRS "${_found_inc}" PARENT_SCOPE)
    set(TP_LIBRARY      "${_found_libs}" PARENT_SCOPE)

    if(_found_inc)
        set(TP_ALL_INCLUDE_DIRS "${TP_ALL_INCLUDE_DIRS};${_found_inc}" PARENT_SCOPE)
    endif()
    foreach(_l ${_found_libs})
        set(TP_ALL_LIBRARIES "${TP_ALL_LIBRARIES};${_l}" PARENT_SCOPE)
    endforeach()
    if(TP_DEFINE)
        set(TP_ALL_DEFINITIONS "${TP_ALL_DEFINITIONS};${TP_DEFINE}" PARENT_SCOPE)
    endif()

    set(_line "${_tp_display_name} [${_src_tag}]")
    set(TP_SUMMARY "${TP_SUMMARY};${_line}" PARENT_SCOPE)
endfunction()

if(PLATFORM_LINUX)
    third_party_find(
        LIB_NAMES udev
        REQUIRED FALSE
        LAYOUT auto
        DEFINE HAVE_LIBUDEV=1
    )
endif()
# ============================================================


# 包含目录设置
include_directories(
    source/main
    source/foundation
    source/inc
    source/NetComm
    source/utils
    source/COM
    source/protocol
    source/third_party/uthash/src
    source/third_party/minIni/dev
)

file(GLOB COMMON_SOURCES
    "source/main/*.c"
    "source/NetComm/*.c"
    "source/utils/*.c"
    "source/foundation/*.c"
    "source/src/*.c"
    "source/COM/*.c"
    "source/protocol/*.c"
)

file(GLOB THIRDPARTY_SOURCES
    "source/third_party/minIni/dev/minIni.c"
)

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

set(ALL_SOURCES
    ${COMMON_SOURCES}
    ${THIRDPARTY_SOURCES}
    ${PLATFORM_SOURCES}
)

if(PLATFORM_WINDOWS)
    set(RESOURCE_FILES "${CMAKE_SOURCE_DIR}/resources/resources.rc")
    if(EXISTS ${RESOURCE_FILES})
        set(ALL_SOURCES ${ALL_SOURCES} ${RESOURCE_FILES})
        message(STATUS "Windows resource file added: ${RESOURCE_FILES}")
    else()
        message(WARNING "Resource file not found: ${RESOURCE_FILES}")
    endif()
elseif(PLATFORM_LINUX)
    message(STATUS "Linux: Icon will be handled via .desktop file")
endif()

add_executable(${PROJECT_NAME} ${ALL_SOURCES})

if(TP_ALL_INCLUDE_DIRS)
    target_include_directories(${PROJECT_NAME} PRIVATE ${TP_ALL_INCLUDE_DIRS})
    message(STATUS "Applied third-party includes: ${TP_ALL_INCLUDE_DIRS}")
endif()

if(TP_ALL_DEFINITIONS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${TP_ALL_DEFINITIONS})
    message(STATUS "Applied third-party definitions: ${TP_ALL_DEFINITIONS}")
endif()

if(TP_ALL_LIBRARIES)
    target_link_libraries(${PROJECT_NAME} ${TP_ALL_LIBRARIES})
    message(STATUS "Applied third-party libraries: ${TP_ALL_LIBRARIES}")
endif()

if(CMAKE_CROSSCOMPILING AND TP_ALL_INCLUDE_DIRS)
    foreach(_d ${TP_ALL_INCLUDE_DIRS})
        target_compile_options(${PROJECT_NAME} PRIVATE "-I${_d}")
    endforeach()
endif()

if(PLATFORM_LINUX AND TP_ALL_LIBRARIES)
    set(_rp "${THIRD_PARTY_ARCH_ROOT}")
    if(TP_TARGET_TRIPLE)
        list(APPEND _rp "${THIRD_PARTY_ARCH_ROOT}/lib/${TP_TARGET_TRIPLE}")
    endif()
    list(APPEND _rp "${THIRD_PARTY_ARCH_ROOT}/lib")
    string(REPLACE ";" ":" _rp_str "${_rp}")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "${_rp_str}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

if(PLATFORM_WINDOWS)
    target_link_libraries(${PROJECT_NAME}
        ws2_32
        setupapi
        advapi32
        iphlpapi
    )
    if(ENABLE_MONITOR)
        target_link_libraries(${PROJECT_NAME} dbghelp psapi)
    endif()
elseif(PLATFORM_LINUX)
    target_link_libraries(${PROJECT_NAME}
        pthread
        m
        rt
        dl
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ${ENABLE_C_EXTENSIONS}
)

if(PLATFORM_LINUX)
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        COMPONENT runtime
    )
    install(FILES README.md LICENSE
        DESTINATION share/doc/${PROJECT_NAME}
        COMPONENT documentation
    )
    if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/${PROJECT_NAME}.service")
        install(FILES scripts/${PROJECT_NAME}.service
            DESTINATION /source/lib/systemd/system
            COMPONENT systemd
        )
    endif()
elseif(PLATFORM_WINDOWS)
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION .
        COMPONENT runtime
    )
endif()

if(COPY_TO_ROOT)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_SOURCE_DIR}
        COMMENT "Copying executable to root directory"
    )
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    enable_testing()
    add_test(NAME CompilationTest
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${PROJECT_NAME}
    )
endif()

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

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMENT "Cleaning all build files"
)

find_program(CLANG_FORMAT clang-format)
if(CLANG_FORMAT)
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_SOURCES}
        COMMENT "Formatting source code"
    )
endif()

include(CheckCCompilerFlag)
if(PLATFORM_LINUX)
    check_c_compiler_flag("-fstack-protector-strong" HAVE_STACK_PROTECTOR_STRONG)
    if(HAVE_STACK_PROTECTOR_STRONG)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")
    endif()
endif()

message(STATUS "")
message(STATUS "=== Build Configuration Summary ===")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Target Platform: ${CMAKE_SYSTEM_NAME} ${DETECTED_ARCH}")
message(STATUS "Host Architecture: ${CMAKE_HOST_SYSTEM_PROCESSOR}")
message(STATUS "Target Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Compiler path: ${CMAKE_C_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Exception monitoring: ${ENABLE_MONITOR}")
message(STATUS "Static linking: ${BUILD_STATIC}")
message(STATUS "C Extensions: ${ENABLE_C_EXTENSIONS}")
message(STATUS "Cross compiling: ${CMAKE_CROSSCOMPILING}")
message(STATUS "Third-party arch dir: ${TP_ARCH_DIR}")
message(STATUS "Third-party triple:   ${TP_TARGET_TRIPLE}")
if(TP_SUMMARY)
    string(REPLACE ";" "\n    " _tp_pretty "${TP_SUMMARY}")
    message(STATUS "Third-party libs:\n    ${_tp_pretty}")
endif()
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross compiler: ${CMAKE_C_COMPILER}")
endif()
message(STATUS "===================================")