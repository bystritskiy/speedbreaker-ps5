# SpeedBreaker on PS5: status

What has been tried on a console, and what hasn't. Release 01.000.004, on SpeedBreaker v0.2.4;
the build itself is described in [`port/ps5/README.md`](../port/ps5/README.md).

## Tested on a console

A homebrew-enabled PS5 with ShadowMountPlus, the app in `/data/homebrew/PPSA90505/` and the USA
disc's files in `/data/speedbreaker/game/`, started from its home-screen tile:

- **Start-up and menus:** the title screen, the main menu, the pause menu, the game's Controls
  screen and SpeedBreaker's settings menu (Touchpad + Options).
- **Playing:** Career with free roam and its cutscenes, Quick Race, races, pursuits and busts.
- **Saves and settings:** kept across a full close and restart of the app.
- **Sound and movies:** music and effects in the menus and races; the Career movies play.
- **Controls:** the DualSense in the menus and while driving, and its rumble.
- **Two-player split screen:** a second DualSense, signed in as a second PS5 user, as player 2
  in Quick Race > Split Screen.
- **Picture:** internal 3840x2160 (Internal Resolution 3x) with 16x anisotropic filtering. Most
  of a race runs at 60 fps; busy moments were measured between 51 and 60.
- **Presentation:** DualSense prompts in the menus and races, the DualSense diagram on the
  Controls and loading screens, and the PlayStation names in the English text.
- **Installing from a disc image:** the USA `.iso` in `/data/speedbreaker/`, chosen in the
  first-run installer: every file checked in under 40 seconds, nothing copied, and the game
  then played from the image to the menu and a race.
- **European discs** (01.000.004): the Europe (English) and Spain `.iso`, each installed from
  scratch and identified by the installer; their own PAL movies, Spanish speech on the Spanish
  disc, 60 fps in the menus, and a race.
- **Languages** (Settings > Language > Text): other text languages on the European English and
  Spanish discs, after a restart; the speech and movies stay the disc's.
- **Bug reports:** Save Bug Report writes its zip to `/data/speedbreaker/`, and
  `port/ps5/tools/fetch-report.py` fetches it; no crash reports in the tested sessions.

## Not tested yet

- The German disc, which the release leaves out.
- Every text language on a console; their text was checked against the disc offline.
- A disc image on a USB drive, and a whole Career played from an image.
- Steering wheels, and long sessions of several hours.
- Every event, car and part of the city: the game is long, and not all of it has been played.

## Not offered on PS5

- **Check for Updates:** the app never goes online.
- **Smooth Motion:** the PS5 draws at 60 Hz.

Found something? [Report it](../README.md#reporting-a-bug) with a bug report zip.
