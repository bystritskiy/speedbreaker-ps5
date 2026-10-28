// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The player's car as the game keeps it, read from guest memory (found with
// debug/mem_scan; memscan-findings.md): its speed, from a fixed address in
// the image's data, and its engine (revs, redline, gear), from a heap struct
// found by its shape and found again whenever it stops looking like one (a
// new car, a new event). For the DualSense's trigger effects. USA disc only.
#pragma once

namespace telemetry
{
    struct Car
    {
        bool speedValid = false;
        float speed = 0;   // m/s
        float decel = 0;   // m/s^2, smoothed; positive while slowing down
        bool engineValid = false;
        float rpm = 0;
        float redline = 0;
        int gear = 0;      // the game's: 0 reverse, 1 neutral, 2 first...
    };

    // Main thread, once per frame. A search for the engine runs on a thread
    // of its own when needed; until it finds one, engineValid is false.
    Car Read();
}
