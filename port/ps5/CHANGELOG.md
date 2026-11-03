# SpeedBreaker for PS5: changelog

The PS5 build's own notes, by the app's own version (`contentVersion` in `sce_sys/param.json`),
which each PS5 release raises. Each entry is the "What's new" note the PS5 app shows once, on the
first start after an update to it (runtime/CMakeLists.txt compiles it in). The other platforms'
notes are SpeedBreaker's `CHANGELOG.md`.

## 01.000.002

### What's new

- **Two-player split screen** (Quick Race > Split Screen): each player gets their own half of the
  screen. The second DualSense has to be signed in as a second PS5 user; a guest will do. It
  joins at "Player 2 press START" with Options.

## 01.000.001

### What's new

- **Install from the disc image.** Copy the .iso you made from your own disc to
  /data/speedbreaker/ and press Install: every file is checked against the USA disc in under a
  minute, and nothing is copied. The game is played from the image, so keep it where it is.
- **The installer works on a clean console.** Install could be greyed out on a first start, and
  an install closed halfway blocked the next one.
- **Free space** is no longer shown as a made-up 16 GB.

## 01.000.000

### What's new

- **The first PS5 release**, on SpeedBreaker v0.2.0: Need for Speed: Most Wanted's Xbox 360
  version, recompiled to run natively on a homebrew-enabled PS5.
- **Native 4K**: the 3D scene renders at 3840x2160, with 16x anisotropic filtering, at up to
  60 fps.
- **The DualSense throughout**: its button prompts in the menus and races, its diagram on the
  Controls and loading screens, the PlayStation names in the menus, and rumble.
- **SpeedBreaker's settings** open with Touchpad + Options, and Save Bug Report puts the logs and
  a screenshot in one zip in /data/speedbreaker/.

### More about it

- The USA disc only, for now. Your saves, settings and game files are in /data/speedbreaker/.
