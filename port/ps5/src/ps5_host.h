// SpeedBreaker PS5: the host's own pieces the runtime calls (port/ps5/src).
// GPL-3.0-or-later.
#pragma once
#include <cstdint>
#include <cstddef>

namespace ps5::pad
{
    // Attaches player 1's DualSense as an SDL virtual gamepad (after SDL's
    // gamepad subsystem is up: hid::Initialize).
    void Attach();
    // Main thread, once a frame (hid::Update): once a second, which signed-in
    // users are players 1 and 2; attaches player 2's gamepad while their pad
    // is connected and detaches it when it goes.
    void Poll();
    // Stops the motors and closes the pads.
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
