# SpeedBreaker PS5: what was run and seen

A log of the console runs while the port was developed, newest last. The
commits it names (`ca99368`, `c607b1d`, `d068db8`, ...) are of that
development history, which is not published: the public history starts from
one commit on SpeedBreaker v0.2.0 with the port as it stands at the end of
this log.

## 2026-10-28, first console contact

Console: the developer's PS5 (ShadowMountPlus, ftpsrv, klog). Build: branch
`ps5`, USA `default.xex` (aebdf3c1...), title PPSA90505.

- **Built:** all of `ppc/` (225 files) and the runtime compile for
  `x86_64-sie-ps5`; `port/ps5/tools/link.sh` links `eboot.bin` (64.7 MB,
  366 imports, all resolved by the SDK's stubs); both FSELFs inspect as
  `integrity: valid`.
- **Installed:** the app folder in `/data/homebrew/PPSA90505` (ShadowMountPlus
  registered it in AppDb) and the 49 disc files in `/data/speedbreaker/game`.
- **Not run yet.** Starting it with a `sceSystemServiceLaunchApp` payload
  sent to the ELF loader made SceShellCore take SIGILL in SceLncIpcDispatch
  and the console rebooted: the game never started. Launch from the home
  screen only.

Not checked: anything at run time (memory mapping, Vulkan on RADV, the
window, audio, the pad, the game).

## 2026-10-28, first tile launch and startup fix

Resumed Claude session `3a818f91-52e8-4fa7-81fb-639c3c55a61a` from
commit `2faa843`. The developer had launched the tile; `work/klog2.txt`
(outside the Git checkout) contains its crash, and `/data/speedbreaker/logs`
does not yet exist.

- **Observed:** the title starts, then the kernel terminates `eboot.bin`
  with `0xa002030a (SYSTEM_ILLEGAL_FUNCTION_CALL)`, reporting
  `directly issued a syscall 74 at 0x2e16d1c`.
- **Symbolized:** the title's text base is `0x400000`; relative
  `0x2a16d1c` is the instruction after `syscall` in the payload SDK's
  `mprotect`. Its caller, relative `0x10c655`, is `Memory::Memory()`.
  This is the guest null-page guard, before `main` and log initialization.
  The low guest allocation and mapping have therefore returned success;
  physical mirrors and the rest of startup have not yet been reached.
- **Artifact checked:** FTP returns a decrypted ELF for the installed
  FSELF. Its executable LOAD segment matches the original local
  `build/ps5/link/eboot.elf`: SHA-256
  `2af3aed2f3399e2cf826d15631180470b111ccb4179daeef1cba29d553bc8ffa`.
  A whole-file comparison of a signed SELF with that FTP result is invalid.
- **Changed:** wrap `mprotect` and `mmap` through `sceKernelMprotect` and
  `sceKernelMmap`, preserving POSIX error results; check the null guard's
  result. Both SDK implementations issue forbidden raw syscalls. Skip
  the optional `posix_fadvise` disc-read hint on PS5 for the same reason.
  The executable-path fallback uses `/app0/eboot.bin` instead of Linux
  `/proc/self/exe` and its raw `readlink` fallback.
- **Host validation:** the PS5 runtime rebuilds and the app links and
  signs; FSELF integrity is valid. Symbol inspection confirms that the
  SDK's `mprotect`, `mmap` and `posix_fadvise` implementations are absent,
  with the two memory wrappers importing libkernel instead.

**Console validation pending:** the fixed app still needs a launch from
the home-screen tile. Do not infer working Vulkan, audio, input, rendering
or gameplay from this build or its upload. Do not use the remote launch
payload that previously crashed SceShellCore.

## 2026-10-28, second tile launch: execute-only text

- **Tested build:** `9426493`, uploaded to the same homebrew folder. All
  executable/data LOAD segments of the decrypted FTP readback match the
  local ELF; the build embeds `9426493` with dirty flag 0. The developer
  launched the tile and reported another crash.
- **Progress observed:** startup passed the guest-memory constructor and
  reached `main`; `/data/speedbreaker/logs/2026-10-28_16-47-22.log` was
  created (empty when the process died).
- **New fault:** `work/klog-9426493.txt` records
  `0xa0020328 (SYSTEM_XO_VIOLATION)`, RIP `0x532db7` (relative
  `0x132db7`, `report::detail::BuildSymbols(bool)`). The faulting data
  read is at `0x5d8150` (relative `0x1d8150`, `sub_820E0000`):
  `FollowJump` reads an instruction from the execute-only text segment
  while building crash-report symbols, called by `PrepareCrashReports`.
- **Changed:** PS5 `FollowJump` keeps the known function entry without
  inspecting code bytes. Crash addresses can still be symbolized offline
  against the preserved unstripped ELF. Guest-body lookup in an on-console
  report is consequently less complete than on readable-text hosts.

The execute-only fix needs another tile launch. Vulkan, audio, input and
gameplay remain unverified.

## 2026-10-28, third tile launch: first visible interface

- **Tested build:** `ca99368`, with verified FTP readback. The developer
  reported a visible screen. Retrieved their 3840x2160 PS5 screenshot from
  `/user/av_contents/photo/NPXS40087/PPSA90505/1f0/20261028_165119_00878497.jpg`;
  the local capture is `work/run-ca99368/20261028_165119_00878497.jpg`.
  It shows SpeedBreaker's first-time setup screen, saying no disc images
  were found. This confirms the Vulkan/ImGui interface and VideoOut work.
  It does not establish game rendering, audio or controller input.
- **Live evidence:** the captured `work/run-ca99368/klog.txt` has no new
  fatal signal for this run. The app writes `settings.toml` and arms its
  crash marker, but the session `.log` remains empty.
- **Install blocker:** the earlier FTP uploader copied the 49 disc files
  but never wrote `.nfsmw-install`. `FindGameInstall` requires that marker
  for the default `/data/speedbreaker/game` folder, so it correctly refused
  to treat the unverified copy as an installed game.
- **Uploader fixed:** verify the local USA manifest, read back every remote
  file and compare SHA-256 (repairing corrupt copies from the verified
  source), then publish the install marker last. Six focused Python tests
  cover publication order, incomplete copies, same-size corruption, repair
  checks, source rejection and the real manifest.
- **Logging fixed:** PS5 stream replacement with `fdopen` avoids `dup2`,
  which is forbidden to native titles. This follows the pinned platform's
  `platform/src/klog.c` implementation. The
  existing pipe writer still supplies the file, cap and recent-output ring.
  Compilation passes; populated logs need checking on the next launch.

The next console check must establish detection of the verified install,
populated session logs, and progress into the game.

## 2026-10-28, fourth launch: guest code and an imported-memory GPU fault

- **Tested build:** `e6e68a3`. The 49 remote files passed SHA-256 checks
  and `.nfsmw-install` was published (`535fd83157066102`). The new session
  log, `2026-10-28_16-58-50.log`, is populated and identifies that commit.
- **Progress:** the runtime detects `/data/speedbreaker/game`, loads the
  XEX at `82000000-82CD0000`, wires imports and calls entry `8262E768`.
  AudioOut is configured for stereo S16/48 kHz; renderer initialization
  imports 512 MB of guest physical memory and creates EDRAM targets and
  resolve pipelines. Audible output and a rendered game frame are not yet
  confirmed. The log's missing loose `D:\\...` resources are not by
  themselves proof of missing disc files: all manifest archives matched.
- **Failure:** the PS5 screenshot records CE-108255-1. The first fetched
  klog no longer held the fault, so a continuous TCP capture was started
  before the developer repeated the same launch. It caught
  `0xa0d0c005 (GPU_FAULT_PAGE_FAULT_ASYNC)`, PID 115, GPU TCP write to
  `0x10a4ff2000`, permission `0x5`: unmapped-page access in the imported
  `0xA0000000` physical window. Evidence: `work/run-e6e68a3/klog-repro.txt`.
- **Cause in code:** RADV's `radv_ps5_buffer_from_ptr` uses the CPU address
  as GPU VA and grants GPU read/write through `sceKernelMprotect`. The
  write watch later passes CPU-only `PROT_*` to that same kernel API,
  replacing the GPU rights too. On PS5, write-watch protection changes now
  retain GPU read/write on the imported A view while changing CPU rights;
  the C/E aliases remain CPU-only.
- **Input blocker found:** the runtime reported `SDL not built with
  virtual-joystick support`. SDL 3.4.18 gates `SDL_VIRTUAL_JOYSTICK` on
  HIDAPI, which this port disables. The reproducible SDL preparation now
  gates it on `SDL_JOYSTICK`, allowing the libScePad-backed virtual pad.
  SDL was rebuilt/installed and `SDL_JOYSTICK_VIRTUAL` is defined in its
  generated configuration. Controller operation still needs a console test.

The GPU-rights fix needs a launch with continuous klog capture. Its effect,
game frames, sound and DualSense operation remain to be checked.

## 2026-10-28, fifth launch: 140 game frames, thread-exit TLS failure

- **Tested build:** `79c48b6`, with the developer's home-screen launch and
  continuous klog capture. The developer saw the splash for a few seconds
  before the crash. The runtime counted 140 game frames over six seconds.
  The earlier GPU page fault did not recur in this run. DualSense attaches,
  its libScePad handle opens and SDL lists it as a gamepad with rumble;
  actual button/rumble behavior and audible output are still unconfirmed.
- **Report:** the populated session log and
  `crash-2026-10-28_17-06-33.txt` are preserved under `work/run-79c48b6`.
  Original failure is SIGSEGV at host `0x47095c7520`, PC `0x2e273e9`:
  relative `0x2a273e9`, `mspace_free`. The original thread-exit stack
  reaches `run_destructors`, relative `0x2a246d3`, immediately after a TLS
  destructor call. The later klog SIGSYS comes from the crash handler's
  `_exit(128 + SIGSEGV)`; it is not the original fault.
- **Lifetime conflict:** the Linux builtins emutls implementation sets its
  cleanup-delay counter to zero. Its pthread key is created before the PS5
  platform's `__cxa_thread_atexit_impl` key; it frees TLS object storage
  before that fallback invokes the objects' C++ destructors.
- **Reproduction and fix:** `tests/ps5_emutls_test.c`, under host ASan,
  fails against the original standalone emutls implementation with
  `TLS storage was freed before the C++ cleanup callback`. The vendored
  version delays cleanup one round on PS5 (the same mechanism it uses for
  Bionic); the test passes and confirms the storage is eventually freed
  exactly once. `port/ps5/compat/emutls.md` records provenance and the
  explicit link object prevents the zero-delay builtins object being used.
  PS5 crash reports also now include original integer registers.
- **Debugger availability:** the developer enabled ps5debug-NG; a read-only
  branding request on TCP 744 returned
  `ps5debug-NG by OSR v1.3.2 [5d2c3bbc4bd42087]`, capability `1.1`.
  No process was attached or modified through it in this investigation.

The delayed TLS cleanup needs a console launch before it is considered a
confirmed runtime fix.

## 2026-10-28, sixth launch: sustained rendering after TLS fix

Installed code build `bae9c8d`; its FSELF integrity is valid and every LOAD
segment of the installed decrypted ELF matches the preserved local build.
Evidence is under `work/build-bae9c8d` and `work/run-bae9c8d`.

The session `2026-10-28_17-14-45.log` embeds `bae9c8d`. At the latest
captured point it has reached 111.3 seconds and 5,700 game frames, well
past the previous six-second thread-exit crash. Multiple movie files have
opened and rendering continues. The most recent two-second performance
windows report 60.0 fps; earlier loading/transition windows have hitches,
so this is not a claim of a constant frame rate in gameplay.

No crash report for this session exists at that capture; its empty
`.crash-pending-*` is the expected marker for a running session. The live
klog file contains older crash-buffer material too: distinguish by PID
and session, rather than treating every earlier fatal signal as a new
failure of `bae9c8d`.

Remaining console checks: the developer's confirmation of what is on
screen, audible output, button/rumble behavior, and save/load persistence.
This is a working startup/rendering checkpoint, not a completed full-game
compatibility test.

## 2026-10-28, gameplay confirmation and DualSense prompts

The developer confirmed on installed `bae9c8d`: the menu appears, sound and
DualSense input work, and a race can be started and driven for at least a minute
without a crash. A subsequent log capture reached over 320 seconds / 17,900
frames. Save/load persistence and rumble remain unverified.

PS5 now substitutes original geometric DualSense prompt artwork for 18
fingerprinted USA textures at BC3 upload, including all six mip levels. It
preserves original disc archives, guest memory, cache validity and game input
mapping. `port/ps5/assets/README.md` records the non-obvious texture-name mappings
and reproducible generator. Host UI legends use PlayStation button names and
Touchpad + Options for Settings. Native build, all 18 resource matches, rejection
of 35 unrelated textures / mutated data, and independent decoding of all 72
replacement mip images passed. Console visual validation is pending.

The original Xbox 360 EA manual assigns the right stick to shifting gears;
free camera rotation is not a default control. RB (R1) changes view, LB (L1) or
Y (Triangle) looks back. The port retains those original assignments.

### Retrieve diagnostics without a keyboard

```sh
python3 port/ps5/tools/fetch-report.py --addr <console> --output work/report
python3 port/ps5/tools/fetch-report.py --addr <console> --output work/report --request-report
```

The first command fetches ordinary session/crash logs even after a crash. The
second additionally publishes `.request-bug-report` in `/data/speedbreaker/` by
FTP rename. The updated app consumes it on the UI thread and invokes the same
Save Bug Report action as Settings > Advanced. The completed ZIP is saved in
`/data/speedbreaker/`, retrieved and CRC-checked. It includes the current frame,
settings, system summary and logs. The helper ignores partial/older ZIPs and
reports a timeout if no new archive appears. Three host protocol tests pass;
end-to-end ZIP retrieval awaits a manual tile launch of the updated app.

## 2026-10-28, prompt visual feedback and native 4K trial

`22d5359` launched on the console. The FTP report trigger completed twice;
both ZIPs passed CRC checks and contain screenshot/settings/system/session/crash
files. User screenshots confirm the first glyph replacements, but the developer
requested circular keycaps instead of bare face symbols and a DualSense for the
loading controller diagram. Revised artwork adds circular rims/dark fill; the
loading image uses an imagegen cutout with native callout composition. The USA
English Guide label is corrected in its exact read range; original archives
remain untouched. The asset README records provenance, mapping and validation.

The captured `22d5359` log establishes the previous graphics mode: output
3840x2160, internal Auto resolving to 2560x1440, anisotropy 4x, AA/mipmaps on,
60fps cap. At the developer's request, the console settings file was backed up,
updated to `video.internal_resolution = "3x"` and
`video.anisotropic_filtering = "16x"`, and read back byte-for-byte. No other
setting was changed. This selects native 3840x2160 for the next launch; actual
runtime resolution, image quality and racing performance still need checking.

The developer caught colored face symbols in the loading cutout. They are now
neutral grey, matching a standard white DualSense; blue LED strips are retained.
This corrects only the cutout artwork/BGRA payload. Native leader lines and
button mappings stay the same. PNG/BGRA conversion and installed asset readback
are checked; the new picture appears after restarting the app.

## 2026-10-28, remove remaining player-facing source-platform labels

The developer requested cleanup of all remaining visible Xbox presentation.
PS5-only host copy now uses DualSense labels in keyboard/wheel calibration,
Original Display Tone for the display curve, native resolution descriptions,
neutral installation screens/errors, DualSense disconnect notices and PS5 port
report headings. Stable configuration keys and compatibility symbols stay.

768 exact read-time replacements cover all ten supported USA text languages:
saves/storage, Xbox Live/Gamertag/Dashboard terminology, the Guide label and
physical Start-button prompts. English online errors explicitly say online play
is unavailable; no PSN services are claimed. The patches preserve file sizes,
all string hashes/offsets and printf parameters, while archives stay original.
The independent catalog check verifies every string and finds no residual
Xbox/Gamertag or targeted Start-button mentions. The C++ test passes 60,883 read
splits and original-archive windows, rejects mismatches/unrelated files, and
checks changed bytes remain inside intended slots. Native PS5 compilation passes.
Reproduction and scope are documented in `port/ps5/text/README.md`.

Visual media audit: extracted contact sheets at two-second intervals for the
five USA tutorial videos (bounty, drag, pursuit, speedtrap, tollbooth), EA HD/EA
intros, PSA and attract movie. No platform-specific controller prompts or Xbox
logos were found in those inspected frames. The options-controls texture is a
generic gears/transmission icon, not a controller. No video re-encoding was
needed. Loading-controller and face-button art remain the corrected DualSense
assets. This is a scoped visual audit, not a frame-by-frame/full-game test.

Deployment and console visual verification of the new text still follow the
usual clean build / tile restart / FTP report path. Native4K and 16x filtering
are selected in console settings but still require fresh runtime confirmation.

## 2026-10-28, console 22435bb: native 4K confirmed; boot text cache found

The developer reports menu/save/racing checks work on `22435bb`. The fetched
session `2026-10-28_18-39-38.log` and CRC-verified full report at 18:41:40 identify
that build. Renderer logs confirm **3x3 / 3840x2160 internal resolution** and
**16x anisotropy**. Recent two-second samples are mostly 60fps; samples after
45s range from 51.3 to 60.1fps, with occasional longer frames (not a claim of
locked 60fps). The report screenshot shows a race paused with the bordered
Cross/Circle prompts. The DualSense loading asset initialized. No crash report
for this session exists; old crash files in the ZIP belong to earlier builds.

However, `[ps5 text]` never appeared. Investigation found a byte-identical copy
of `LANGUAGES/ENGLISH.BIN` at `0x586A60` inside
`GLOBAL/GLOBALMEMORYFILE.BIN` (USA ZDIR archive0, sector76150). Boot uses this
copy, so standalone language ranges did not cover the active English path.
The generator now audits all four memory-file containers and adds the 84 English
slots in that copy. There are **852 ranges** total. The independent test verifies
the resulting embedded language is exactly equal to the patched standalone
resource. Original archives remain unchanged. A fresh launch is needed to
confirm text-patch application in the runtime log and a changed text on screen.

## 2026-10-28, e9ba620 boot-cache fix confirmed on console

The developer restarted through the tile and reached the main menu. The fresh
session `2026-10-28_18-47-15.log` identifies installed `e9ba620` and reports at
0.6s: `[ps5 text] applied 84 scoped text fragments in D:\NFS\ZZDATA0.BIN`.
This confirms that the actual English boot-cache read path is covered. Menu
rendering continues normally (recent windows60fps); the report screenshot shows
bordered Cross/Triangle prompts with correct placement. The runtime still
reports internal3840x2160 and16xanisotropy. No new-session crash report exists.

The requested full bug report at18:47:59 was retrieved automatically over FTP
and passed ZIP CRC verification, including screenshot/settings/system/session
files. Evidence: `work/run-e9ba620/`. These checks establish current English
boot/menu behavior; other supported languages were validated against source
resources/tests, not individually launched on the console. The earlier22435bb
race trial establishes the observed native4K performance range, not a guarantee
of a locked60fps or a full-game compatibility pass. Installed executable stays
`e9ba620`; this follow-up records evidence only.

## 2026-10-28, button texture geometry refinement

The developer's menu screenshot exposed cramped triangle vertices inside the
circular rim. Refined all12glyphs together: equilateral triangle with clear
vertex margins/optical centring, consistent strokes, symmetric distance-based
antialiasing, actual-ink centring of shoulder/PS/R3 labels, and consistent dark
keycap treatment. The BC3 alpha encoder now fits both modes to preserve
translucent fill without edge/opacity jumps. All72mip images pass independent
Pillow decode comparisons at unchanged error limits;18source mappings and
negative cases still pass. Reviewed decoded artwork at native32px and menu
sizes in white/gold. No renderer/input/disc-resource changes. Console screenshot
confirmation follows deployment/restart of this revision.

## 2026-10-28, programmatic lossless button artwork

After enlarged-triangle feedback, replaced rounded segment joins with a joined
polygon outline and clear vertex margins. All twelve button images are now
also generated directly at128x128, with eight losslessRGBA mip levels. The
renderer recognizes the same18USA fingerprints and aliases matching sampled
views to immutable native images, retaining guest cache/write-watch handling.
The1MiB `glyphs.rgba` is shipped beside eboot; missing or malformed data uses
the existing32px BC3 fallback. Disc archives/input mapping remain unchanged.

The host build compiles the new asset loader and Vulkan upload path. Independent
BC3 decoding still passes for72fallback mips; the shippedRGBA payload is checked
against the geometry for all96mip images and manifest ordering. All18mapping
and35non-button rejection cases pass. Batch visual review covers all symbols
at menu size. Native console use of the128px path requires a fresh tile launch
and `[ps5 glyphs] ... lossless 128x128 RGBA uploaded` evidence plus screenshot.

### Console confirmation, installed c607b1d

The developer confirms everything works. Fresh session2026-10-28_19-08-49
identifies clean c607b1d and logs lossless128x128RGBA uploads for Cross, Circle,
Triangle, L1/R1, Touchpad and L2/R2. Native internal3840x2160,16xAF and the
84English boot-cache text fragments remain active. The first report screenshot
shows the three face prompts in the quick-play briefing transition: the
triangle has even joined edges and clear separation from its circular rim.
A second report captured the transition into racing. Both full report ZIPs
passed CRC verification; neither contains a new-session crash report.
Evidence: `work/run-c607b1d/`. All12glyphs were reviewed offline; this is a scoped
UI verification, not every screen or a new sustained performance benchmark.
Installed binary stays c607b1d; this follow-up records evidence only.

## 2026-10-28, d068db8: text from the player's disc, own toolchain setup

The text table now stores hashes and edits instead of the disc's strings
(`port/ps5/text/README.md`), and the app was built from scratch with
`setup-ps5-vulkan.sh`'s PS5_Vulkan, payload SDK, RADV and libc.prx, then
copied with `deploy-app.py`; all five installed LOAD segments matched.

The developer started Career and says everything works. Session
2026-10-28_19-40-05 logs `[ps5 text] applied 84 scoped text fragments` at
0.7 s, internal 3840x2160 and 16x AF, and 60 fps windows at 104 s; the report
screenshot shows free roam in Career. No new-session crash report. Evidence:
`work/run-d068db8/`.
