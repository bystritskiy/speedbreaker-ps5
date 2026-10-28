// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Debug only: a memory scanner over guest memory, Cheat Engine style, for
// finding the game's own values (the car's speed, gear, revs). Off unless
// $HOME/Documents/scan exists; then a thread reads commands from
// scan/cmd.txt (each new write runs once) and writes scan/out.txt. See
// mem_scan.cpp for the commands.
#pragma once

namespace memscan
{
    // After guest memory is up. Does nothing without the scan folder.
    void Start();
}
