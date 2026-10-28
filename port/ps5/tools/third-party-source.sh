#!/usr/bin/env bash
# SpeedBreaker PS5: the complete source of the LGPL and GPL parts in the PS5
# app, at the versions it is built with, for a release's downloads
# (THIRD_PARTY_NOTICES.md, "Source code of the LGPL and GPL parts"):
#   - FFmpeg 7.1.1 as released, SpeedBreaker's patch and both build scripts
#   - libmspack, as XenonRecomp's submodule has it
#   - PS5_Vulkan, PS5_PayloadSDK and PS5_Mesa at the revisions
#     setup-ps5-vulkan.sh pins (Mesa is MIT, here for completeness)
#
#   port/ps5/tools/third-party-source.sh      -> build/ps5/SpeedBreaker-<version>-ps5-third-party-source.tar.xz
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
DEPS="$ROOT/build/ps5-deps"
VERSION="$(sed -n 's/^project(SpeedBreaker VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "no version in CMakeLists.txt" >&2; exit 1; }
NAME="SpeedBreaker-$VERSION-ps5-third-party-source"
STAGE="$ROOT/build/ps5/$NAME"
rev() { sed -n "s/^$1=\([0-9a-f]*\).*/\1/p" "$ROOT/port/ps5/tools/setup-ps5-vulkan.sh"; }
rm -rf "$STAGE"
mkdir -p "$STAGE/ffmpeg" "$STAGE/libmspack"
cp "$ROOT/tools/ffmpeg-src/ffmpeg-7.1.1.tar.xz" "$ROOT/scripts/build_ffmpeg_xma.sh" \
    "$ROOT/port/ps5/tools/build-ffmpeg-xma.sh" "$ROOT"/patches/ffmpeg/*.patch "$STAGE/ffmpeg/"
git -C "$ROOT/tools/XenonRecomp/thirdparty/libmspack" archive HEAD libmspack | tar -x -C "$STAGE/libmspack"
for pair in PS5_Vulkan:PS5_VULKAN_REV PS5_PayloadSDK:PS5_SDK_REV PS5_Mesa:PS5_MESA_REV; do
    repo=${pair%%:*} revision=$(rev "${pair#*:}")
    [ -n "$revision" ] || { echo "no ${pair#*:} in setup-ps5-vulkan.sh" >&2; exit 1; }
    mkdir -p "$STAGE/$repo"
    git -C "$DEPS/$repo" archive "$revision" | tar -x -C "$STAGE/$repo"
    echo "$revision" > "$STAGE/$repo/REVISION"
done
cat > "$STAGE/README.txt" <<EOF
Source code of the LGPL and GPL parts of SpeedBreaker $VERSION for PS5.

ffmpeg/      FFmpeg 7.1.1 as released, the patch SpeedBreaker applies and the
             build scripts with their configure options (LGPL-2.1-or-later)
libmspack/   libmspack, as XenonRecomp's pinned submodule has it (LGPL-2.1)
PS5_Vulkan/  github.com/mihawk-99/PS5_Vulkan: the app CRT, link recipe,
             ps5-native-tool and the generated libc.prx (GPL-3.0-or-later)
PS5_PayloadSDK/  github.com/mihawk-99/PS5_PayloadSDK: the payload SDK with
             the PS5 platform layer (GPL-3.0-or-later; include/freebsd: BSD)
PS5_Mesa/    github.com/mihawk-99/PS5_Mesa: Mesa with RADV's PS5 winsys (MIT
             and others, its licenses/ folder)

Each fork's REVISION names the commit. SpeedBreaker's own source is the
repository the app's VERSION file names.
EOF
tar -C "$ROOT/build/ps5" -cJf "$ROOT/build/ps5/$NAME.tar.xz" "$NAME"
echo "$ROOT/build/ps5/$NAME.tar.xz"
