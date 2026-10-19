# RK3588 buildroot (alientek_rk3588) aarch64 交叉工具链
# 覆盖: -DTOOLCHAIN_ROOT=/path/to/host 或环境变量 TOOLCHAIN_ROOT
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED TOOLCHAIN_ROOT)
    if(DEFINED ENV{TOOLCHAIN_ROOT})
        set(TOOLCHAIN_ROOT $ENV{TOOLCHAIN_ROOT})
    else()
        set(TOOLCHAIN_ROOT /home/fuck/rk3588/rk3588_linux6.12_sdk/buildroot/output/alientek_rk3588/host)
    endif()
endif()

set(CMAKE_C_COMPILER   ${TOOLCHAIN_ROOT}/bin/aarch64-buildroot-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/bin/aarch64-buildroot-linux-gnu-g++)

set(CMAKE_SYSROOT ${TOOLCHAIN_ROOT}/aarch64-buildroot-linux-gnu/sysroot)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 板端 glibc 2.37 vs sysroot 2.42: 关闭 C23 strtol 族重定向 (见 3rdparty/glibc23_shim/)
get_filename_component(_REPO_DIR ${CMAKE_CURRENT_LIST_DIR}/.. ABSOLUTE)
set(CMAKE_C_FLAGS_INIT   "-isystem ${_REPO_DIR}/3rdparty/glibc23_shim")
set(CMAKE_CXX_FLAGS_INIT "-isystem ${_REPO_DIR}/3rdparty/glibc23_shim")
