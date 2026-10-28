#!/usr/bin/env bash
# SpeedBreaker PS5: link the PS5 build (build/ps5) with RADV into eboot.bin and
# stage the app folder. Runs on the Mac (or Linux) that compiled it.
#
#   port/ps5/tools/link.sh [build dir]        (default build/ps5)
#
# Environment:
#   PS5_VULKAN    mihawk-99/PS5_Vulkan checkout with its built .deps (the
#                 payload SDK, libvulkan_radeon.ps5.a, libc.prx, tooling);
#                 setup-ps5-vulkan.sh's by default
#   LLVM_CONFIG   for the SDK's prospero-* wrappers (port/ps5/tools/llvm-bin.sh)
#   COMPILER_RT   libclang_rt.builtins-x86_64.a for Linux x86-64 (LLVM 18);
#                 setup-ps5-vulkan.sh's by default
#   NATIVE_TOOL   ps5-native-tool (built from PS5_Vulkan/tooling/native if unset)
#   TITLE_ID      PPSA90505 by default (port/ps5/sce_sys/param.json)
#
# The recipe is PS5_Vulkan's (tools/radv-link.sh): the app's CRT (tooling/native/app_crt.cpp),
# the AGC import stubs, RADV whole, libc++/libc++abi/libunwind, the platform
# layer's libc, the SDK's module stubs; then ps5-native-tool converts the PIE
# to the PS5 layout and signs it as a development FSELF.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BUILD="$(cd "${1:-$ROOT/build/ps5}" && pwd)"
PS5_VULKAN="${PS5_VULKAN:-$ROOT/build/ps5-deps/PS5_Vulkan}"
COMPILER_RT="${COMPILER_RT:-$ROOT/build/ps5-deps/compiler-rt/libclang_rt.builtins-x86_64.a}"
[ -d "$PS5_VULKAN/.deps/native" ] || { echo "no PS5_Vulkan .deps in $PS5_VULKAN (port/ps5/tools/setup-ps5-vulkan.sh)" >&2; exit 1; }
[ -f "$COMPILER_RT" ] || { echo "no $COMPILER_RT (port/ps5/tools/setup-ps5-vulkan.sh)" >&2; exit 1; }
: "${LLVM_CONFIG:?set LLVM_CONFIG (port/ps5/tools/llvm-bin.sh prints it)}"
export LLVM_CONFIG
TITLE_ID="${TITLE_ID:-PPSA90505}"
SDK="$PS5_VULKAN/.deps/native/ps5-payload-sdk"
RADV="$PS5_VULKAN/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
LLVM_BIN="$("$LLVM_CONFIG" --bindir)"
WORK="$BUILD/link"
mkdir -p "$WORK/obj" "$WORK/stubs" "$WORK/root/tooling" "$WORK/clang/lib/linux"

# radv_link_recipe asks clang for its resource dir's Linux builtins: a
# stand-in clang that names one holding LLVM 18's. Emutls is supplied below.
cp "$COMPILER_RT" "$WORK/clang/lib/linux/libclang_rt.builtins-x86_64.a"
cat > "$WORK/clang/clang" <<EOF
#!/bin/sh
[ "\$1" = "--print-resource-dir" ] && { echo "$WORK/clang"; exit 0; }
exec "$LLVM_BIN/clang" "\$@"
EOF
chmod +x "$WORK/clang/clang"
export PS5_CLANG="$WORK/clang/clang"
# The recipe writes a version script under its root: a copy of PS5_Vulkan's
# tooling, so the checkout stays untouched.
rm -rf "$WORK/root/tooling"
cp -R "$PS5_VULKAN/tooling" "$WORK/root/tooling"

cxx="$SDK/bin/prospero-clang++"
cc="$SDK/bin/prospero-clang"
"$cxx" -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
    -c "$PS5_VULKAN/tooling/native/app_crt.cpp" -o "$WORK/obj/app_crt.o"
# C++ TLS destructors use a pthread key on PS5. Keep emulated TLS alive
# until their cleanup round has finished (port/ps5/compat/emutls.md).
"$cc" -std=c11 -O2 -fPIC -ffunction-sections -fdata-sections -D__PROSPERO__=1 \
    -c "$ROOT/port/ps5/compat/emutls.c" -o "$WORK/obj/emutls.o"
for pair in 'libSceAgc agc_canary_link_stub.c' 'libSceAgcDriver agc_driver_canary_link_stub.c'; do
    read -r library source <<<"$pair"
    "$cc" -std=c11 -O2 -fPIC -c "$PS5_VULKAN/vendor/ps5/sdk/stubs/$source" -o "$WORK/obj/$library.o"
    "$SDK/bin/prospero-lld" --shared -soname "$library.prx" -o "$WORK/stubs/$library.so" "$WORK/obj/$library.o"
done

source "$PS5_VULKAN/tools/radv-link.sh"
radv_link_recipe "$WORK/root" "$SDK" "$RADV"
# The recipe groups its C++ archives; they join the app's one group instead.
radv_inputs=()
for item in "${radv_link_inputs[@]}"; do
    case "$item" in --start-group|--end-group) continue;; esac
    radv_inputs+=("$item")
done

stubs=()
for file in "$SDK"/target/lib/*.so; do
    case "$file" in *libScePosixForWebKit.so|*libSceGLSlimVSH.so|*libc_stub_weak.so) continue;; esac
    stubs+=("$file")
done

# The runtime whole: its SB_HOOK definitions replace the recompiled
# functions' weak ones, and its static registrations must all run.
"$SDK/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" --wrap=mprotect --wrap=mmap \
    --version-script "$PS5_VULKAN/tooling/native/app-symbols.map" --exclude-libs=ALL --no-dynamic-linker -z nodynamic-undefined-weak --gc-sections \
    -e _start -o "$WORK/llvm-pie.elf" \
    "$WORK/obj/app_crt.o" "$WORK/obj/emutls.o" "$WORK/stubs/libSceAgc.so" "$WORK/stubs/libSceAgcDriver.so" \
    --whole-archive "$BUILD/runtime/libSpeedBreaker.a" --no-whole-archive \
    --start-group "$BUILD/recomp/libNfsmwRecompLib.a" "$BUILD/runtime/libXenonUtilsLite.a" \
    "$BUILD/runtime/libo1heap.a" "$BUILD/runtime/libimgui.a" \
    "$ROOT/build/ps5-deps/sdl3/lib/libSDL3.a" \
    "$ROOT/build/ps5-deps/glslang/lib/libglslang.a" "$ROOT/build/ps5-deps/glslang/lib/libglslang-default-resource-limits.a" \
    "$ROOT/build/ps5-deps/glslang/lib/libSPIRV.a" "$ROOT/build/ps5-deps/glslang/lib/libMachineIndependent.a" \
    "$ROOT/build/ps5-deps/glslang/lib/libGenericCodeGen.a" "$ROOT/build/ps5-deps/glslang/lib/libOSDependent.a" \
    "$ROOT/build/ps5-deps/ffmpeg-xma/lib/libavcodec.a" "$ROOT/build/ps5-deps/ffmpeg-xma/lib/libavutil.a" \
    "${radv_inputs[@]}" \
    "$SDK/target/lib/libc.a" "$SDK/target/lib/libpthread.a" --end-group \
    --as-needed "${stubs[@]}" 2>&1 | tee "$WORK/link.log"
[ "${PIPESTATUS[0]}" = 0 ] || exit 1

NATIVE_TOOL="${NATIVE_TOOL:-$WORK/ps5-native-tool}"
if [ ! -x "$NATIVE_TOOL" ]; then
    N="$PS5_VULKAN/tooling/native"
    "$LLVM_BIN/clang++" -std=c++20 -O2 "$N/native_app_builder.cpp" "$N/self_container.cpp" \
        "$N/elf_object.cpp" "$N/sce_module_writer.cpp" -lz -o "$NATIVE_TOOL"
fi
"$NATIVE_TOOL" link --in "$WORK/llvm-pie.elf" --out "$WORK/eboot.elf" \
    --stub-dir "$SDK/target/lib" --stub "$WORK/stubs/libSceAgc.so" --stub "$WORK/stubs/libSceAgcDriver.so" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf > "$WORK/convert.log"

APP="$BUILD/app/$TITLE_ID"
rm -rf "$APP"
mkdir -p "$APP/sce_sys" "$APP/sce_module"
"$NATIVE_TOOL" self --sign --in "$WORK/eboot.elf" --out "$APP/eboot.bin" --magic 0x1D3D154F > "$WORK/sign.log"
cp "$PS5_VULKAN/runtime/libc.prx" "$APP/sce_module/libc.prx"
cp "$ROOT/port/ps5/sce_sys/"* "$APP/sce_sys/"
cp "$ROOT/port/ps5/assets/dualsense.bgra" "$APP/dualsense.bgra"
cp "$ROOT/port/ps5/assets/glyphs.rgba" "$APP/glyphs.rgba"
# The notices of everything linked in (THIRD_PARTY_NOTICES.md, "In the PS5
# app"), and the version and commit the build compiled in.
mkdir -p "$APP/licenses"
cp "$ROOT/COPYING" "$ROOT/NOTICE" "$ROOT/THIRD_PARTY_NOTICES.md" "$APP/licenses/"
cp -R "$ROOT/LICENSES" "$APP/licenses/LICENSES"
info() { sed -n "s/^#define NFSMW_BUILD_$1 \"\{0,1\}\([^\"]*\)\"\{0,1\}\$/\1/p" "$BUILD/runtime/generated/build_info.inc"; }
commit="$(info COMMIT)"; [ "$(info DIRTY)" = 1 ] && commit="$commit-dirty"
echo "SpeedBreaker v$(info VERSION) ($commit, built $(info DATE)), PS5" > "$APP/VERSION"
for file in "$APP/eboot.bin" "$APP/sce_module/libc.prx"; do
    "$NATIVE_TOOL" self --inspect --file "$file" > "$WORK/$(basename "$file")-inspect.txt"
    grep -q "integrity: valid" "$WORK/$(basename "$file")-inspect.txt" || { echo "FSELF check failed: $file" >&2; exit 1; }
done
echo "app: $APP ($(wc -c < "$APP/eboot.bin" | tr -d ' ') bytes eboot.bin)"
