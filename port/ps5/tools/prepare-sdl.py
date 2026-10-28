#!/usr/bin/env python3
"""SpeedBreaker PS5: patch a clean SDL 3.4.18 checkout for the console.

    python3 -I port/ps5/tools/prepare-sdl.py <SDL source>

- Vulkan: the offscreen video driver's Vulkan glue becomes a VideoOut WSI
  over RADV's VK_KHR_display (port/ps5/sdl/SDL_ps5vulkan.c),
  seeded from the statically linked ICD, without dlopen; the offscreen
  desktop mode is the console's 3840x2160.
- Audio: an AudioOut driver (port/ps5/sdl/SDL_ps5audio.c), SDL_PS5AUDIO=ON.
- Joysticks: no USB HID driver; the game's pad is a virtual gamepad.

Run it once on a fresh checkout; it refuses another SDL revision and a
checkout it has already patched differently.
"""
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
SDL_REVISION = '829a65d769d935c4852f8159e964312c0957260a'  # release-3.4.18
MARK = 'SpeedBreaker PS5'


def patch(path, old, new, count=1):
    s = path.read_text()
    if new in s:
        return
    if s.count(old) != count:
        raise SystemExit(f'{path}: expected {count} of {old!r}')
    path.write_text(s.replace(old, new))


def main():
    src = pathlib.Path(sys.argv[1]).resolve()
    rev = subprocess.check_output(['git', '-C', str(src), 'rev-parse', 'HEAD'], text=True).strip()
    if rev != SDL_REVISION:
        raise SystemExit(f'SDL must be release-3.4.18 ({SDL_REVISION}), not {rev}')

    shutil.copyfile(ROOT / 'port/ps5/sdl/SDL_ps5vulkan.c', src / 'src/video/offscreen/SDL_offscreenvulkan.c')
    video = src / 'src/video/offscreen/SDL_offscreenvideo.c'
    patch(video, 'mode.w = 1024;', f'mode.w = 3840; /* {MARK}: VideoOut */')
    patch(video, 'mode.h = 768;', f'mode.h = 2160; /* {MARK}: VideoOut */')

    (src / 'src/audio/ps5').mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'port/ps5/sdl/SDL_ps5audio.c', src / 'src/audio/ps5/SDL_ps5audio.c')
    patch(src / 'CMakeLists.txt',
          'dep_option(SDL_DUMMYAUDIO          "Support the dummy audio driver" ON "SDL_AUDIO" OFF)\n',
          'dep_option(SDL_DUMMYAUDIO          "Support the dummy audio driver" ON "SDL_AUDIO" OFF)\n'
          f'option(SDL_PS5AUDIO "{MARK}: the AudioOut driver" OFF)\n')
    patch(src / 'CMakeLists.txt',
          'if(SDL_AUDIO)\n  # CheckDummyAudio/CheckDiskAudio - valid for all platforms\n',
          'if(SDL_AUDIO)\n'
          f'  # {MARK}: AudioOut\n'
          '  if(SDL_PS5AUDIO)\n'
          '    set(SDL_AUDIO_DRIVER_PS5 1)\n'
          '    sdl_glob_sources("${SDL3_SOURCE_DIR}/src/audio/ps5/*.c")\n'
          '    set(HAVE_SDL_AUDIO TRUE)\n'
          '  endif()\n'
          '  # CheckDummyAudio/CheckDiskAudio - valid for all platforms\n')
    patch(src / 'include/build_config/SDL_build_config.h.cmake',
          '#cmakedefine SDL_AUDIO_DRIVER_DUMMY 1\n',
          '#cmakedefine SDL_AUDIO_DRIVER_DUMMY 1\n#cmakedefine SDL_AUDIO_DRIVER_PS5 1\n')
    patch(src / 'src/audio/SDL_sysaudio.h',
          'extern AudioBootStrap DUMMYAUDIO_bootstrap;\n',
          'extern AudioBootStrap DUMMYAUDIO_bootstrap;\nextern AudioBootStrap PS5AUDIO_bootstrap;\n')
    patch(src / 'src/audio/SDL_audio.c',
          '#ifdef SDL_AUDIO_DRIVER_VITA\n    &VITAAUD_bootstrap,\n#endif\n',
          '#ifdef SDL_AUDIO_DRIVER_VITA\n    &VITAAUD_bootstrap,\n#endif\n'
          '#ifdef SDL_AUDIO_DRIVER_PS5\n    &PS5AUDIO_bootstrap,\n#endif\n')
    # No USB HID joysticks (FreeBSD's, which the toolchain's FreeBSD name
    # would pick): the DualSense comes through libScePad as a virtual gamepad
    # (port/ps5/src/pad_ps5.cpp).
    # SDL 3.4.18 gates the virtual driver on HIDAPI even though it does not
    # use HID devices. We disable HIDAPI on PS5 and feed it from libScePad.
    patch(src / 'CMakeLists.txt',
          'dep_option(SDL_VIRTUAL_JOYSTICK    "Enable the virtual-joystick driver" ON SDL_HIDAPI OFF)',
          'dep_option(SDL_VIRTUAL_JOYSTICK    "Enable the virtual-joystick driver" ON SDL_JOYSTICK OFF)')
    patch(src / 'CMakeLists.txt',
          '    if(FREEBSD OR NETBSD OR OPENBSD OR BSDI)\n      CheckUSBHID()\n',
          '    if((FREEBSD OR NETBSD OR OPENBSD OR BSDI) AND NOT SB_PS5)\n      CheckUSBHID()\n')
    print(f'SDL {rev[:12]} prepared for the PS5')


if __name__ == '__main__':
    main()
