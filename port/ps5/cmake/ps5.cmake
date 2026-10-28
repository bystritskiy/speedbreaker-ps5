# SpeedBreaker PS5: CMake toolchain for the public PS5 payload SDK
# (mihawk-99/PS5_PayloadSDK, as pinned by PS5_Vulkan) and LLVM 18 clang.
#
#   cmake -S . -B build/ps5 -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$PWD/port/ps5/cmake/ps5.cmake \
#     -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@18/bin/clang \
#     -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@18/bin/clang++ \
#     -DPS5_PAYLOAD_SDK=<sdk root>
#
# Compiles only: the objects are linked with RADV and signed by
# port/ps5/tools/link.sh (port/ps5/README.md).
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES PS5_PAYLOAD_SDK)
if(NOT PS5_PAYLOAD_SDK)
    set(PS5_PAYLOAD_SDK "$ENV{PS5_PAYLOAD_SDK}")
endif()
if(NOT IS_DIRECTORY "${PS5_PAYLOAD_SDK}/target/include")
    message(FATAL_ERROR "Set PS5_PAYLOAD_SDK to an installed PS5 payload SDK")
endif()
set(PS5_PAYLOAD_SDK "${PS5_PAYLOAD_SDK}" CACHE PATH "PS5 payload SDK root")
set(CMAKE_SYSROOT "${PS5_PAYLOAD_SDK}")
set(CMAKE_C_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_CXX_COMPILER_TARGET x86_64-sie-ps5)
# Zen 2 (the PS5's CPU); emulated TLS (the PS5 ABI); no stack protector or
# PLT (the payload SDK's runtime has neither).
set(SB_PS5_FLAGS "-march=znver2 -femulated-tls -fno-stack-protector -fno-plt -fdenormal-fp-math=ieee -fvisibility-nodllstorageclass=default -D__PROSPERO__=1 -DSB_PS5=1")
set(CMAKE_C_FLAGS_INIT "-isystem ${PS5_PAYLOAD_SDK}/target/include ${SB_PS5_FLAGS}")
# libc++ 18 keeps std::jthread and std::stop_token behind -fexperimental-library;
# the PS5 target defaults to no RTTI and no exceptions, which the runtime uses
# (dynamic_cast on kernel objects, try around std::filesystem and threads);
# sb_ps5_compat.h has what libc++ 18 lacks (std::atomic_ref).
get_filename_component(SB_PS5_COMPAT "${CMAKE_CURRENT_LIST_DIR}/../compat/sb_ps5_compat.h" ABSOLUTE)
set(CMAKE_CXX_FLAGS_INIT "-isystem ${PS5_PAYLOAD_SDK}/target/include/c++/v1 -isystem ${PS5_PAYLOAD_SDK}/target/include ${SB_PS5_FLAGS} -fexperimental-library -frtti -fexceptions -include ${SB_PS5_COMPAT}")
set(CMAKE_FIND_ROOT_PATH "${PS5_PAYLOAD_SDK}/target")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(SB_PS5 ON CACHE BOOL "Build the PS5 host" FORCE)
