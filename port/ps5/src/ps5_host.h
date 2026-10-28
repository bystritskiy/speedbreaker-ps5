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
    // Stops the motors and closes the pad.
    void Shutdown();
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
