# Testing on PS5: what to send when something goes wrong

Thanks for testing. The game keeps its own logs and can pack everything needed into one file, so
a report takes one button press and a few words from you. [PS5.md](PS5.md) says what has been
tried on a console so far: anything outside it is worth a report, good or bad.

## Installing

The [README](../README.md#install) has the steps: the `PPSA90505` folder from the release in
`/data/homebrew/`, and your own extracted USA disc in `/data/speedbreaker/game/`. To build it
yourself, see [`port/ps5/README.md`](../port/ps5/README.md).

## The menu

- **Touchpad + Options** together open and close SpeedBreaker's settings; the game pauses while
  they are open.
- The D-pad picks a row, left and right change it, Cross selects, L1 and R1 switch pages, Circle
  closes.

## When something goes wrong

1. Open the menu, go to the **Advanced** page and choose **Save Bug Report**.
2. A notice says the file went to `/data/speedbreaker/`, as
   `speedbreaker-bug-report-v<version>-<date>_<time>.zip`.
3. Copy it off the console over FTP, or from a computer on the same network:

   ```sh
   python3 -I port/ps5/tools/fetch-report.py --addr <console IP> --output <folder>
   ```

   With `--request-report` added, the running game saves a new report by itself and the script
   fetches it, so you don't need to open the menu.
4. Send the zip in an [issue](https://github.com/bystritskiy/speedbreaker-ps5/issues) with a few
   words: what you were doing, what you expected, what happened, and roughly how long into the
   session (the logs count seconds from the start).

If the game **crashed**, start it again from its tile: the next bug report includes the crash
report.

If the game **froze** (no picture change for 20 seconds, outside loading), it writes a hang
report by itself. Save a bug report right there if you can; if you can't, close the app from the
PS5's menu, start it again and save one then: the hang report is kept.

A bug report holds the last five sessions' logs, any crash and hang reports, your
`settings.toml`, a summary of the system (the GPU and driver, the display, the settings in
effect) and a screenshot of the last game frame without the menu over it. Nothing is sent
anywhere by the game: you send the file. **Never attach game files or disc images.**

## Where the logs live

Each start writes its own log to `/data/speedbreaker/logs/` (the newest five are kept), named
after the date and time it started, next to any `crash-*.txt` and `hang-*.txt` reports.

The first lines of every log say which build it is (`[build] SpeedBreaker v<version>
(<commit>), built ...`, also shown at the bottom of Settings > Advanced) and whether this version
is new since the last run (`[version] updated from v<a> to v<b>`, or `first run`).
