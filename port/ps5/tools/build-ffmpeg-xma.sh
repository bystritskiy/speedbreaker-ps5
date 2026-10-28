#!/usr/bin/env bash
# SpeedBreaker PS5: FFmpeg's XMA decoders for the PS5 (scripts/build_ffmpeg_xma.sh's
# build, cross-compiled with LLVM 18 for x86_64-sie-ps5 against the payload SDK).
#
#   port/ps5/tools/build-ffmpeg-xma.sh <sdk root> [prefix]   (default build/ps5-deps/ffmpeg-xma)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SDK="$(cd "$1" && pwd)"
PREFIX="${2:-$ROOT/build/ps5-deps/ffmpeg-xma}"
LLVM="${LLVM:-/opt/homebrew/opt/llvm@18/bin}"
# The SDK's prospero-clang finds clang and ld.lld through LLVM_CONFIG's --bindir
# (port/ps5/tools/llvm-bin.sh makes one with llvm@18 and Homebrew's lld).
: "${LLVM_CONFIG:?set LLVM_CONFIG (port/ps5/tools/llvm-bin.sh prints it)}"
export LLVM_CONFIG
TARBALL="$ROOT/tools/ffmpeg-src/ffmpeg-7.1.1.tar.xz"
[ -f "$TARBALL" ] || { echo "run scripts/setup_tools.sh first (it fetches $TARBALL)" >&2; exit 1; }
flags="-march=znver2 -D__PROSPERO__=1"
# The compiler by name, from PATH: FFmpeg records its configure line in the
# library, which must name no folder of this machine.
export PATH="$SDK/bin:$PATH"
"$ROOT/scripts/build_ffmpeg_xma.sh" "$TARBALL" "$PREFIX" \
    --enable-cross-compile --target-os=none --arch=x86_64 --cpu=znver2 \
    --cc=prospero-clang --ar="$LLVM/llvm-ar" --ranlib="$LLVM/llvm-ranlib" --nm="$LLVM/llvm-nm" \
    --extra-cflags="$flags" --disable-x86asm --disable-pthreads --disable-w32threads --disable-os2threads \
    --disable-iconv --disable-zlib --disable-bzlib --disable-lzma
