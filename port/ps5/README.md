# SpeedBreaker on PS5

A native PS5 host for SpeedBreaker, the static recompilation of the Xbox 360
version of *Need for Speed: Most Wanted* (2005). The recompiled game code
(`ppc/`, from your own USA `default.xex`) and SpeedBreaker's runtime are the
same as on the other platforms; this folder adds the console's toolchain,
libraries and glue, and the runtime's PS5 parts are under `__PROSPERO__`.

Title ID **PPSA90505**, tile name **SpeedBreaker**. The app contains no game
data: it plays the files of your own disc from the console's storage.

## Status

See `docs/PS5.md` for what has been tried on a console, and what hasn't yet;
`docs/TESTING.md` for what to send when something goes wrong.

## Game files

The USA (NTSC-U) disc only for now (`default.xex` SHA-256
`aebdf3c14d1e4e4ed2c130d94bc1a0e97bd6d5bf39bdfdef33c3909c9cc4ba4c`, the
supported executable of the other platforms). Either

- copy the extracted disc (`default.xex`, `NFS/`, `Movies/`, 49 files,
  7.1 GB) to `/data/speedbreaker/game/`, for example with
  `port/ps5/tools/upload-game.py --addr <console> game/files`; or
- put the disc image (`.iso`) in `/data/speedbreaker/`, `/data/` or the top
  of a USB drive, and let the first-run installer install it.

The FTP uploader verifies the source against the USA manifest, reads back and
hashes every remote file, and writes `.nfsmw-install` last: the game counts the
files as installed only with that marker. A full readback moves 7.1 GB over the
network even when no file needs uploading.

Saves, settings, logs (`logs/`) and the shader cache (`cache/`) are in
`/data/speedbreaker/`.

## Building (macOS host)

Needs Homebrew's `llvm@18`, `lld`, `cmake` and `ninja`, and Docker (Docker
Desktop or colima). `tools/setup-ps5-vulkan.sh` fetches
[mihawk-99/PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) and its two
forks at pinned revisions into `build/ps5-deps/`, and builds in a container
what only builds on Linux: the payload SDK, RADV release and the clean-room
`libc.prx`; it also takes LLVM 18's Linux x86-64
`libclang_rt.builtins-x86_64.a` from Ubuntu's `libclang-rt-18-dev` and checks
its SHA-256. The later steps find all of it there (set `PS5_VULKAN` and
`COMPILER_RT` to use other copies).

```sh
scripts/setup_tools.sh                     # XenonRecomp, extract-xiso, the FFmpeg tarball
# ppc/ from your own default.xex, as in the main README
port/ps5/tools/setup-ps5-vulkan.sh         # PS5_Vulkan, payload SDK, RADV, libc.prx, compiler-rt
export LLVM_CONFIG=$(port/ps5/tools/llvm-bin.sh build/ps5-deps/llvm18)
port/ps5/tools/build-deps.sh               # SDL3, glslang, FFmpeg-XMA, Vulkan headers and shim
cmake -S . -B build/ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/port/ps5/cmake/ps5.cmake \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@18/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm@18/bin/clang++ \
  -DPS5_PAYLOAD_SDK=$PWD/build/ps5-deps/PS5_Vulkan/.deps/native/ps5-payload-sdk -DSB_PAL_ES=OFF
ninja -C build/ps5 SpeedBreaker
port/ps5/tools/link.sh
```

The app folder is `build/ps5/app/PPSA90505/`: `eboot.bin`, a development
FSELF; `sce_module/libc.prx`, PS5_Vulkan's clean-room libc; `sce_sys/`; the
DualSense artwork (`dualsense.bgra`, `glyphs.rgba`); `licenses/` and
`VERSION`, the commit it was built from. A release also publishes
`tools/third-party-source.sh`'s archive, the source of its LGPL and GPL parts
(`THIRD_PARTY_NOTICES.md`).

## Installing

The console needs a homebrew-enabled firmware with ShadowMountPlus and an FTP
server (ftpsrv, port 2121). Copy the folder to `/data/homebrew/PPSA90505/`:

```sh
python3 -I port/ps5/tools/deploy-app.py --addr <console>
```

The tile appears on the home screen. Then copy your game files as above.

## Releasing

Raise `contentVersion` in `sce_sys/param.json` (`01.000.001`, ...) and commit
it: consoles and the homebrew stores compare it to find updates. Build and
link that commit, then

```sh
port/ps5/tools/package-release.sh
```

writes `build/ps5/PPSA90505.zip` (the app folder at the top of the archive,
as ProsperoStore and RecompStore install it) and the third-party source
archive, and refuses a build of uncommitted changes. Attach both to a GitHub
release tagged with the content version (`01.000.000`). The title ID
PPSA90505 stays the app's for good: a new one would be another title, with
other save data.

## How it is put together

- **Toolchain:** LLVM 18 for `x86_64-sie-ps5` against the public payload
  SDK (`cmake/ps5.cmake`): Zen 2, emulated TLS, RTTI and exceptions on, and
  `compat/sb_ps5_compat.h` for what the SDK's libc++ 18 lacks
  (`std::atomic_ref`).
- **Linking:** `tools/link.sh`, PS5_Vulkan's RADV recipe: the app CRT,
  RADV whole, libc++/libc++abi/libunwind, the platform layer's libc, the
  SDK's module stubs; `ps5-native-tool` converts and signs. The runtime
  library is linked whole, so its hooks replace the recompiled functions.
  `compat/emutls.c` keeps emulated thread-local storage alive until the
  PS5 platform's C++ destructor callback has run (`compat/emutls.md`).
- **Vulkan:** RADV is linked in and exports only `vk_icdGetInstanceProcAddr`;
  `tools/gen-vulkan-shim.py` writes the loader's `vk*` exports over it from
  Mesa's own headers, so the renderer and Dear ImGui are unchanged.
- **Display:** SDL 3.4.18's offscreen video driver with a VideoOut WSI over
  RADV's `VK_KHR_display` (`sdl/SDL_ps5vulkan.c`), a 4K
  window; always fullscreen, as Steam's Game Mode.
- **Audio:** an SDL audio driver for AudioOut (`sdl/SDL_ps5audio.c`,
  S16 stereo 48 kHz), under SDL's audio streams the runtime already uses.
- **Input:** the DualSense through libScePad as an SDL virtual gamepad
  (`src/pad_ps5.cpp`): Options is Start, the touchpad click Back.
- **Memory:** the guest's 4 GB is a reserved range filled with direct
  memory (`ps5platform/shm.h`): one object below 0xA0000000, and the 512 MB
  of physical memory mapped at 0xA0000000, 0xC0000000 and 0xE0000000
  (`runtime/kernel/memory.cpp`). Released pages are cleared in place.
  `src/memory_ps5.cpp` wraps `mprotect` and `mmap` through libkernel: the
  payload SDK's raw syscalls are forbidden in a native title.
  The write watch retains GPU read/write on the imported A physical view
  while changing CPU permissions; PS5 protection changes affect both.
- **Faults:** the write watch's SIGSEGV handler reads the PS5's mcontext
  (48 bytes later than FreeBSD's; the SDK's `ucontext_t` has them).
- **Presentation:** DualSense button symbols and loading-screen artwork
  replace the Xbox ones when the game loads them (`assets/README.md`), and
  the PlayStation wording is edited into the disc's text as it is read
  (`text/README.md`). The settings, installer and notices use the PS5's
  names through `runtime/user/platform_labels.h`; the What's new note is
  `CHANGELOG.md` here.
- **Logging:** PS5 replaces the `stderr` stream with an unbuffered pipe
  stream; a native title cannot redirect descriptor 2 with `dup2`. The
  session writer preserves the same timestamped file and recent-output ring.
  Direct `write(2, ...)` calls are not captured by this stream replacement.
