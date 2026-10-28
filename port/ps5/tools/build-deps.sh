#!/usr/bin/env bash
# SpeedBreaker PS5: the libraries the PS5 build links, built for the console
# into build/ps5-deps (ignored), at pinned revisions:
#   - SDL 3.4.18 with the VideoOut WSI and the AudioOut driver (prepare-sdl.py)
#   - glslang 16.0.0 (the renderer compiles its GLSL at run time)
#   - FFmpeg 7.1.1's XMA decoders (build-ffmpeg-xma.sh)
#   - Mesa's Vulkan headers as RADV installs them, and the loader shim
#     over them (gen-vulkan-shim.py)
#
#   LLVM_CONFIG=<llvm-bin.sh's> port/ps5/tools/build-deps.sh
#
# PS5_VULKAN is setup-ps5-vulkan.sh's checkout unless set.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PS5_VULKAN="${PS5_VULKAN:-$ROOT/build/ps5-deps/PS5_Vulkan}"
[ -d "$PS5_VULKAN/.deps/native" ] || { echo "no PS5_Vulkan .deps in $PS5_VULKAN (port/ps5/tools/setup-ps5-vulkan.sh)" >&2; exit 1; }
: "${LLVM_CONFIG:?set LLVM_CONFIG (port/ps5/tools/llvm-bin.sh prints it)}"
export LLVM_CONFIG
SDK="$PS5_VULKAN/.deps/native/ps5-payload-sdk"
DEPS="$ROOT/build/ps5-deps"
LLVM18="${LLVM18:-/opt/homebrew/opt/llvm@18}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"
SDL_REV=829a65d769d935c4852f8159e964312c0957260a      # release-3.4.18
GLSLANG_REV=a57276bf558f5cf94d3a9854ebdf5a2236849a5a  # 16.0.0
mkdir -p "$DEPS"

fetch() {  # url revision dir
    if [ ! -d "$3/.git" ]; then
        git init -q "$3"
        git -C "$3" remote add origin "$1"
    fi
    if [ "$(git -C "$3" rev-parse -q --verify HEAD 2>/dev/null)" != "$2" ]; then
        git -C "$3" fetch -q --depth 1 origin "$2"
        git -C "$3" checkout -q --force "$2"
        git -C "$3" clean -qfdx
    fi
}

cmake_ps5() {  # source build prefix options...
    local src=$1 build=$2 prefix=$3
    shift 3
    cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/port/ps5/cmake/ps5.cmake" \
        -DCMAKE_C_COMPILER="$LLVM18/bin/clang" -DCMAKE_CXX_COMPILER="$LLVM18/bin/clang++" \
        -DPS5_PAYLOAD_SDK="$SDK" -DCMAKE_INSTALL_PREFIX="$prefix" "$@" > "$build.configure.log"
    cmake --build "$build" -j "$JOBS" > "$build.build.log"
    cmake --install "$build" > /dev/null
}

# SDL: a clean checkout each time, then the PS5 patches.
fetch https://github.com/libsdl-org/SDL.git "$SDL_REV" "$DEPS/sdl3-src"
git -C "$DEPS/sdl3-src" checkout -q --force "$SDL_REV" && git -C "$DEPS/sdl3-src" clean -qfdx
python3 -I "$ROOT/port/ps5/tools/prepare-sdl.py" "$DEPS/sdl3-src"
rm -rf "$DEPS/sdl3-build" "$DEPS/sdl3"
cmake_ps5 "$DEPS/sdl3-src" "$DEPS/sdl3-build" "$DEPS/sdl3" \
    -DSDL_STATIC=ON -DSDL_SHARED=OFF -DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF -DSDL_WAYLAND=OFF \
    -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_VULKAN=ON -DSDL_OFFSCREEN=ON \
    -DSDL_HIDAPI=OFF -DSDL_JOYSTICK=ON -DSDL_VIRTUAL_JOYSTICK=ON -DSDL_AUDIO=ON -DSDL_PS5AUDIO=ON \
    -DSDL_DUMMYAUDIO=ON -DSDL_DISKAUDIO=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF -DSDL_OSS=OFF \
    -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
    -DSDL_DBUS=OFF -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_CAMERA=OFF -DSDL_GPU=OFF -DSDL_RENDER=OFF \
    -DSDL_DIALOG=OFF -DSDL_TRAY=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
    -DLIBC_HAS_WCSNLEN=OFF -DLIBC_HAS_WCSLCPY=OFF -DLIBC_HAS_WCSLCAT=OFF

# glslang, without its SPIR-V optimizer (the renderer doesn't ask for it).
fetch https://github.com/KhronosGroup/glslang.git "$GLSLANG_REV" "$DEPS/glslang-src"
rm -rf "$DEPS/glslang-build" "$DEPS/glslang"
cmake_ps5 "$DEPS/glslang-src" "$DEPS/glslang-build" "$DEPS/glslang" \
    -DENABLE_OPT=OFF -DENABLE_HLSL=OFF -DENABLE_GLSLANG_BINARIES=OFF -DENABLE_SPVREMAPPER=OFF \
    -DGLSLANG_TESTS=OFF -DBUILD_EXTERNAL=OFF -DBUILD_SHARED_LIBS=OFF -DGLSLANG_ENABLE_INSTALL=ON

"$ROOT/port/ps5/tools/build-ffmpeg-xma.sh" "$SDK" "$DEPS/ffmpeg-xma"

rm -rf "$DEPS/vulkan"
mkdir -p "$DEPS/vulkan/include"
cp -R "$PS5_VULKAN/.deps/native/radv-release/include/vulkan" "$PS5_VULKAN/.deps/native/radv-release/include/vk_video" \
    "$DEPS/vulkan/include/"
python3 -I "$ROOT/port/ps5/tools/gen-vulkan-shim.py" "$DEPS/vulkan/include/vulkan/vulkan_core.h" "$DEPS/vulkan/vulkan_shim.c"
echo "PS5 libraries in $DEPS"
