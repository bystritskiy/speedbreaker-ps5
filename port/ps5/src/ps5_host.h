// SpeedBreaker PS5: the host's own pieces the runtime calls (port/ps5/src).
// GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <cstddef>

namespace ps5::pad
{
    // Attaches the DualSense as an SDL virtual gamepad (after SDL's gamepad
    // subsystem is up: hid::Initialize).
    void Attach();
    // Stops the motors, frees the triggers and closes the pad.
    void Shutdown();
    // The adaptive triggers, as Settings > Input asks. Trigger Resistance
    // (`level`: 0 off, 1 light, 2 medium, 3 strong): the accelerator (R2)
    // pushes back steadily, the brake (L2) more and more along its travel,
    // like a pedal. Trigger Effects (`effects`): the accelerator also shakes
    // with the game's rumble (crashes, bumps, rough ground) and buzzes with
    // nitrous. Sent when what the triggers should do changes, and again
    // whenever the pad is reopened. Main thread, once per frame (hid::Update).
    void SetTriggers(int level, bool effects);
}

namespace ps5::diagnostics
{
    // Main-thread polling of an FTP-created bug-report request.
    void Poll();
}

namespace ps5::controls
{
    // Eligible 256x256 USA loading diagrams; original tiled guest bytes in,
    // native BGRA mip out. Null means preserve the game's own texture.
    const uint8_t* ControllerMip(const uint8_t* tiled, uint32_t level);
}

namespace ps5::glyphs
{
    // Lossless 128px artwork, complete eight-level RGBA mip chain.
    const uint8_t* RgbaMip(size_t index, uint32_t level);
}
