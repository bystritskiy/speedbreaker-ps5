#!/usr/bin/env bash
# SpeedBreaker PS5: the console toolchain the build links against, into
# build/ps5-deps (ignored), at the revisions this port is tested with:
#   - mihawk-99/PS5_Vulkan, and in its .deps the payload SDK (PS5_PayloadSDK),
#     RADV release (PS5_Mesa) and the clean-room runtime/libc.prx
#   - LLVM 18's Linux x86-64 libclang_rt.builtins-x86_64.a (Ubuntu's
#     libclang-rt-18-dev), checked by SHA-256
# Mesa and the libc build on Linux, so they run in a Docker container
# (port/ps5/docker/Dockerfile); Docker Desktop or colima is enough on a Mac.
#
#   port/ps5/tools/setup-ps5-vulkan.sh
#
# Afterwards PS5_VULKAN and COMPILER_RT default to what it made
# (build-deps.sh, link.sh). It skips what is already there at these revisions.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEPS="$ROOT/build/ps5-deps"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc)}"
PS5_VULKAN_REV=f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed
PS5_MESA_REV=1ba9874f478657ccf6da72ce55f0260164cc0bd0       # Mesa 26.2.0 with the PS5 winsys
PS5_SDK_REV=b83202be73e930050e00de0f1b4d9ec46c0391bf        # payload SDK v0.42 with the platform layer
CRT_VERSION=1:18.1.3-1ubuntu1
CRT_SHA256=52f850cf79b9cda0b00b78aa2fed86bae6947e596cc7cafb52dba4fd88acb07d
IMAGE=speedbreaker-ps5-builder:local
mkdir -p "$DEPS"
docker info > /dev/null || { echo "Docker is needed (Docker Desktop or colima)" >&2; exit 2; }

fetch() {  # url revision dir: PS5_Vulkan's scripts find the forks beside it
    if [ ! -d "$3/.git" ]; then
        git init -q "$3"
        git -C "$3" remote add origin "$1"
    fi
    if [ "$(git -C "$3" rev-parse -q --verify HEAD 2>/dev/null)" != "$2" ]; then
        git -C "$3" fetch -q --depth 1 origin "$2"
        git -C "$3" checkout -q --force "$2"
    fi
}
fetch https://github.com/mihawk-99/PS5_Vulkan.git "$PS5_VULKAN_REV" "$DEPS/PS5_Vulkan"
fetch https://github.com/mihawk-99/PS5_Mesa.git "$PS5_MESA_REV" "$DEPS/PS5_Mesa"
fetch https://github.com/mihawk-99/PS5_PayloadSDK.git "$PS5_SDK_REV" "$DEPS/PS5_PayloadSDK"

docker build -q -t "$IMAGE" "$ROOT/port/ps5/docker" > /dev/null
# The payload SDK, RADV release, then libc.prx (rebuilt twice, installed only
# when byte-identical to the digest PS5_Vulkan records).
docker run --rm -v "$DEPS:/deps" -w /deps/PS5_Vulkan -e BUILD_JOBS="$JOBS" "$IMAGE" bash -lc \
    'set -e; bash tools/setup-native-dependencies.sh
     MESON=/opt/meson/bin/meson NINJA=/usr/bin/ninja bash tools/build-radv.sh release
     [ -f runtime/libc.prx ] || bash tools/rebuild-libc.sh'

# The builtins must be ELF x86-64 objects, whatever the host is.
CRT="$DEPS/compiler-rt/libclang_rt.builtins-x86_64.a"
if [ ! -f "$CRT" ]; then
    mkdir -p "$DEPS/compiler-rt"
    docker run --rm --platform linux/amd64 -v "$DEPS/compiler-rt:/out" ubuntu:24.04 bash -c \
        "set -e; apt-get update -qq; cd /tmp; apt-get download -qq libclang-rt-18-dev=$CRT_VERSION
         dpkg-deb -x libclang-rt-18-dev_*.deb x
         cp x/usr/lib/llvm-18/lib/clang/18/lib/linux/libclang_rt.builtins-x86_64.a /out/"
fi
echo "$CRT_SHA256  $CRT" | shasum -a 256 -c --status ||
    { echo "compiler-rt: $CRT is not libclang-rt-18-dev $CRT_VERSION's" >&2; exit 1; }
echo "PS5_VULKAN=$DEPS/PS5_Vulkan"
echo "COMPILER_RT=$CRT"
