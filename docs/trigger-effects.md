# DualSense trigger effects: notes

Everything learned while building the adaptive-trigger effects: what the game keeps in memory
and where, how it was found, the values that felt right, and what is still open. Worked out on
an iPhone 17 Pro with a DualSense (upstream SpeedBreaker's iOS build, Apple's GameController
API), then ported here. **Nothing in this PR has run on a PS5 yet.**

## What the effects do

Settings → Input:

- **Trigger Resistance** (Off / **Light** / Medium / Strong): R2 pushes back steadily; L2 builds up
  along its travel like a pedal (slope feedback).
- **Trigger Effects** (on by default), from the car's state:
  - **Brake firmer at speed:** 60% of the level at a standstill, 100% from 45 m/s (160 km/h).
  - **ABS:** L2 past half and the car slowing faster than 9 m/s² → L2 knocks (full-amplitude,
    low-frequency vibration).
  - **Gear change** (the game's own gear, so automatic too) → one hard knock on R2, then R2 at
    its hardest for a moment.
  - **Rev limiter** (rpm > 97% of the redline, R2 past half) → the controller's motors buzz, on
    top of the game's own rumble. Deliberately not on the trigger: on the trigger it felt wrong.

## The car's state in memory (USA disc)

Guest addresses, big-endian. Found with `runtime/debug/mem_scan.cpp` (below) in free roam.

| Value | Address | Type | Notes |
|---|---|---|---|
| **Speed, m/s** | `82A2C250` | f32 | Image data: **the same address every run.** Checked against the speedometer. |
| Speed, mph | `82907C6C` | f32 | Image data. = m/s × 2.237. Among engine-audio parameters ("Test.gin3" nearby). |
| Game clock, s | `82A2C264` | f32 | Image data. +1 per second. |
| **Engine RPM** | engine + 0 | f32 | Hits ~6980 at the limiter, drops on a shift. |
| **Redline** | engine + 4 | f32 | 7000 for the test car. |
| Max RPM | engine + 8 | f32 | 8000 for the test car. |
| **Gear** | engine + 12 | u32 | **Gear + 2:** 0 reverse, 1 neutral, 2 first, 3 second… |
| (fill) | engine + 16 | u32 | `0x0000AAAA` at rest, **but not during a gear change.** |
| Throttle applied | `A8ED46A0` | f32 | 0–1. R2 going forward, **L2 in reverse** (braking to a stop engages reverse). |
| Throttle, smoothed | `A8ED469C` | f32 | Follows the one above with a short ramp. |
| Brake applied | `A8ED4654` | f32 | 0–1, ramps 0.8 → 0.96 over a full stop; single-frame 1.0 blips at the 1st-gear limiter. |
| Longitudinal decel? | `A8ED4648` | f32 | Down to about −17 under hard braking, ~0 otherwise. |
| Gear (input side) | `A8ED46B0` | u32 | Gear + 2, same as the engine's. |
| RPM for audio? | `BB780F6C` | f32 | Same curve, scaled, capped at exactly 10000. |

"engine" was `A8D99904` in that session, and stayed there after going back to the main menu and
into free roam again with the same car.

**Heap addresses move** (other cars, races, other runs). `game/car_telemetry.cpp` therefore:

- reads the speed from its fixed address;
- **finds the engine struct by its shape**: rpm 0…max×1.2, redline 3000–15000,
  max ≥ redline and ≤ 20000, gear ≤ 9, `0x0000AAAA` after it. It searches the physical heap
  (`A0000000`–`C0000000`) on a thread of its own, only while the car moves;
- re-checks the struct every frame **without** the fill word (it changes during a shift), and
  searches again only after it has looked wrong for a second. Meanwhile it reports the last good
  reading. Without that grace, every gear change "lost" the engine and the shift kick never
  fired.
- works out deceleration from the speed (smoothed over ~0.1 s) instead of reading `A8ED4648`.

In free roam the search finds exactly one engine. **In a race with AI cars there may be several**:
it takes the first and logs up to 8 candidates (`[telemetry] engine candidate …`). That is
untested; if the effects follow the wrong car, pick by which candidate's rpm/gear track the
player's speed.

The engine struct is referenced from a table of pointers at `A8EAABF4`–`A8EAAC30` (the car's
components?) and sits in a linked list of 0x70–0xF0-byte nodes. Nothing in the image's data
(`82000000`–`84000000`) points straight at it; a pointer path from a static root wasn't looked for.

## How it was found (to find more)

`runtime/debug/mem_scan.cpp` is a Cheat Engine-style scanner. It is off unless a `scan` folder
exists in the user folder: on the PS5, **`/data/speedbreaker/scan/`**. Write one command to
`scan/cmd.txt` over FTP; the answer appears in `scan/out.txt` (first line `> <command>`).

```
new <f32|u32|u16|u8> <min> <max> [from to]   values in [min, max] (addresses hex; default 10000–C0000000)
filter <min> <max>                           keep those in range now
changed | same | up | down                   against the last look
list [n]                                     the candidates and their values
near <addr> <bytes>                          a region as u32 and f32
watch <type>:<addr>[,...] <hz> <seconds>     a CSV of values over time (up to 120 s)
```

What worked:

- **Throttle:** `new f32 0.95 1.0` with R2 held → `filter 0 0.05` released → repeat →
  **half press** `filter 0.15 0.9`. The half press is what cuts it down: lots of values flip
  between 0 and 1. The raw trigger (116/255 = 0.4549) shows up in several input buffers too.
- **Speed:** driving steadily, `new f32 3 400`; slower → `down`; at a known speed →
  `filter 1 12` (m/s, mph and km/h all fit); faster → `filter 20 400`; slower → `down`.
  Two values were left, m/s and mph (13.60 × 2.237 = 30.42).
- **Gear:** manual gearbox. In 2nd `new u32 1 4` (room for an offset) → 3rd `up` → 4th `up`,
  `filter 3 7` → back to 3rd `down`. 14 left, most reading 4 in 3rd: hence gear + 2.
- **RPM:** `near` the gear candidates: floats of 6947 next to 7000 and 8000 were the engine.
  Confirmed with `watch` through a full acceleration with shifts.
- **Brake:** a `watch` of the throttle's struct (`A8ED4640`–`A8ED46B0`) **started before
  driving**, 90 s, while braking hard several times. Starting the recording after the manoeuvre
  misses it: allow for the delay between "go" and the command reaching the console.

## Tuning

The effects' shape can be changed without a rebuild: `/data/speedbreaker/triggers.txt`,
`key = value` lines, read again within a second of a change.

```
abs_amp = 8          # 1-8
abs_freq = 13        # Hz
kick_amp = 8         # 1-8
kick_freq = 20       # Hz
kick_thump_ms = 80   # the knock
kick_hold_ms = 60    # R2 at its hardest after it
```

The log line `[ps5] trigger tuning: …` confirms a change was read.

What was learned on the iPhone, where the same effects were tuned by feel:

- **Apple's vibration frequency is 0–1, not Hz.** Passing 30 meant "maximum" and sounded like a
  squeal. Good values there were **0.05 for ABS** and **0.08 for the shift knock**. The Hz above
  are those × 255, **a guess** at how Apple maps its scale. If ABS buzzes instead of knocking,
  lower `abs_freq`; if the shift knock isn't felt, raise `kick_freq`.
- **The knock's length sets the number of knocks:** 160 ms gave two, 80 ms one.
- A frequency too low (Apple 0.02) gave **no knock at all** in 120 ms: less than one cycle.
- **Resistance pulses** (hardest feedback on and off, ~10 Hz) felt weaker than the low-frequency
  vibration for ABS. Plain hardest feedback for the shift kick was barely noticeable.
- Rumble-driven shaking of R2 (v2 of this PR's first version) and an R2 buzz at the limiter were
  dropped: a buzz on the trigger felt like noise. The limiter is on the motors instead.

## Open

- **Not run on a PS5:** `scePadSetTriggerEffect`'s parameter layout is from public
  reverse-engineered headers (120 bytes; Feedback 1, Vibration 3, Slope 5), guarded by
  `static_assert`s on the sizes. Each change of mode is logged with its result:
  `[ps5] triggers: L2 mode M, R2 mode M (…): scePadSetTriggerEffect ok|failed (0x…)`.
- Races with AI cars (several engines, see above), other cars, the PAL Spanish disc (its own
  `default.xex`: none of these addresses apply).
- Not found yet: wheel slip, ABS as the game models it, nitrous state.
