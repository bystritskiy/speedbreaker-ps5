# DualSense artwork

The PS5 build shows DualSense buttons where the game draws Xbox 360 ones. Everything here is
this port's own artwork; no image from the disc is stored, only hashes that recognize the
game's textures when it loads them. `link.sh` ships `glyphs.rgba` and `dualsense.bgra` beside
`eboot.bin`.

## Button symbols

`generate-glyphs.py` draws the twelve white symbols (Cross, Circle, Square, Triangle, L1, L2,
R1, R2, Options, Touchpad, R3, PS) from shared geometry: one centre, a common stroke and a
circular keycap rim with a translucent dark fill; the triangle is built from joined edge planes,
so its corners are sharp. It writes:

- `glyphs.rgba`: lossless 128x128 RGBA images with all eight mip levels (128 to 1). A 16-byte
  little-endian header (`PSG1`, width 128, 12 glyphs, 8 mips) is followed by each glyph's mip
  chain in manifest order; 1,048,576 bytes in all.
- `../src/ps5_glyphs.h`: the same symbols as 32x32 BC3 with six mip levels, a fallback for a
  missing or malformed `glyphs.rgba`.

`glyph-fingerprints.json` names 18 USA `GlobalTexturesXENON` button textures by an FNV-1a hash
of their active tiled BC3 blocks (padding excluded: its contents vary at run time). Only 32x32
tiled BC3 fetches are compared. A recognized texture samples the shared 128 px image instead;
anything else, and a texture the game later overwrites, uses the normal renderer. The disc's
archives, guest memory, texture cache, write tracking, UVs, swizzles and the menus' colour
modulation stay as they are.

The texture names come from the PS2 version and don't say what the Xbox image shows:
CROSS=A is Cross, CIRCLE=Y is Triangle, TRIANGLE=B is Circle, SQUARE=X is Square; L1=LT is L2,
L2=LB is L1, R1=RT is R2, R2/SELECT=RB is R1; BACK is Touchpad and START is Options. Localized
trigger and click variants are included. The texture named MAIN is an effects atlas, not a
controller.

## Loading-screen controller

The loading screens' Xbox 360 controller diagram (two 256x256 textures in `DYNTEX.BIN`, one per
steering layout) is replaced by a front view of a white DualSense:

- `dualsense.png` is the cutout, made for this port with an image-generation model: no
  annotation lines, transparent background, neutral grey face symbols and blue light bars, as
  on the white DualSense. `convert-controller.py` only resizes it and converts it to the game's
  BGRA byte order, `dualsense.bgra` (262,144 bytes).
- The two original textures are recognized by a quick sampled hash and then a full one
  (`controller-fingerprints.json` records them; `../src/controls_artwork.h` uses them); other
  textures keep their images.
- The runtime (`../src/controls_artwork.h`) draws the white leader lines as antialiased
  primitives over the cutout, from the game's own label positions to the DualSense's buttons,
  for the stick and the D-pad steering layout, and builds all mip levels with alpha-weighted
  filtering.

## Checks

From the repository root:

```sh
python3 port/ps5/tools/generate-glyphs.py --preview build/ps5-glyphs.png
python3 tests/ps5_glyph_codec_test.py
c++ -std=c++20 -Iport/ps5/src tests/ps5_glyphs_test.cpp -o /tmp/ps5-glyphs-test
/tmp/ps5-glyphs-test /path/to/extracted/GLOBALB.BUN
c++ -std=c++20 -Iport/ps5/src tests/ps5_controls_test.cpp -o /tmp/ps5-controls-test
/tmp/ps5-controls-test [<texture 142> <texture 143>]
```

The codec test decodes every BC3 fallback mip against the drawing and checks every RGBA mip.
The glyph test needs your own extracted USA files: it confirms all 18 mappings, rejects the
other eligible textures and rejects changed artwork. The controls test checks the leader lines,
and, given the two original loading textures extracted from your own `DYNTEX.BIN`, their
fingerprints and the rejection of changed data. No texture of the
game is stored as a test fixture.
