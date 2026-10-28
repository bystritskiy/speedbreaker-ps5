# Contributing to SpeedBreaker-ps5

Thanks for helping. This repository is the PS5 fork of
[SpeedBreaker](https://github.com/SpeedBreakerProject/speedbreaker); here is how it works.

## Reporting a bug

1. On the console, open the settings (Touchpad + Options), go to **Advanced** and choose
   **Save Bug Report**: one zip with the logs, your settings and a screenshot, in
   `/data/speedbreaker/`. [docs/TESTING.md](docs/TESTING.md) says how to get it off the console.
2. Check the [open issues](https://github.com/bystritskiy/speedbreaker-ps5/issues) first.
3. Open a new issue and attach the zip. Say what you were doing, what you expected and what
   happened.

Bugs of the PS5 build belong here, not upstream. **Never attach game files, disc images or
anything from your game folder,** including the generated `ppc/` folder, which is made from the
game's code.

## Pull requests

Pull requests for the PS5 build are welcome:

- Keep them small and focused: one fix or feature per request.
- Say how you tested it on a console: what you played or ran, and for how long.
- Include no game code, game data, game text or generated `ppc/` output. Anything derived from
  the disc is stored as hashes and offsets only, and read from the player's own disc at run time
  (`port/ps5/text/README.md`, `port/ps5/assets/README.md`).
- If you used AI tools, that's fine, as long as you've read, understood and tested the change
  yourself.

A change that isn't about the PS5 (a bug of the game's runtime on every platform, say) belongs
upstream: open it at [SpeedBreaker](https://github.com/SpeedBreakerProject/speedbreaker), as its
own CONTRIBUTING.md describes, and this fork picks it up with the next upstream release.

By contributing, you agree that your contribution is licensed under the project's license,
GPL-3.0-or-later.

## Scope

**Welcome:** bug fixes, performance, DualSense and console integration, other discs on the PS5,
and accuracy to the original Xbox 360 version.

**Out of scope:** anything that helps people get game files; online services; anything that
changes how the game plays by default.
