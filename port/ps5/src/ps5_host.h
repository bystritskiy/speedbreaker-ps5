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
    // Settings > Input > Trigger Resistance (0 off, 1 light, 2 medium, 3
    // strong): a steady push back on the triggers, the accelerator (R2)
    // light and the brake (L2) firmer, as driving games set them. Sent once
    // per change, and again whenever the pad is reopened. Main thread, once
    // per frame (hid::Update).
    void SetTriggerResistance(int level);
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
