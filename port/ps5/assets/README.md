# DualSense prompts

`generate-glyphs.py` draws the twelve white button symbols from shared geometry.
The primary asset `glyphs.rgba` contains lossless 128x128 RGBA textures with all
8 mip levels (128..1). Its 16-byte little-endian header is `PSG1`, width128,
glyph count12, mip count8, followed by each glyph's mip chain in manifest order.
The complete file is1048576bytes. `link.sh` ships it beside eboot; Pillow is
only a host build tool. The generated `../src/ps5_glyphs.h` retains six32..1
BC3 mip levels as a fallback if the external asset is missing or malformed.

`glyph-fingerprints.json` identifies 18 USA GlobalTexturesXENON textures by
FNV-1a over the active tiled BC3 blocks in their original 8-in-16 byte order.
The hashes contain no original artwork. Padding is excluded because its runtime
contents are not stable. Only 32x32, tiled BC3, 8-in-16 fetches are eligible.
Unrecognized textures use the normal renderer. The original disc archives and
guest memory remain unchanged. The renderer retains original texture cache and
write tracking, but recognized glyphs sample shared128px native images. A guest
texture overwrite clears the alias on reload. Normalized UVs, swizzles and menu
color modulation remain unchanged.

Names inherited from PS2 do **not** indicate what the Xbox image depicts:
CROSS=A -> Cross; CIRCLE=Y -> Triangle; TRIANGLE=B -> Circle; SQUARE=X -> Square.
L1=LT -> L2; L2=LB -> L1; R1=RT -> R2; R2/SELECT=RB -> R1.
BACK maps to Touchpad, START to Options. Localized trigger/click variants are
included. The texture named MAIN is an effects atlas, not a controller diagram.

Regenerate (from the repository root):

```sh
python3 port/ps5/tools/generate-glyphs.py --preview build/ps5-glyphs.png
python3 tests/ps5_glyph_codec_test.py
c++ -std=c++20 -Iport/ps5/src tests/ps5_glyphs_test.cpp -o /tmp/ps5-glyphs-test
/tmp/ps5-glyphs-test /path/to/extracted/GLOBALB.BUN
```

The last check is optional and requires locally extracted USA game data. It
confirms all 18 mappings, rejects the other eligible textures, and rejects
mutated artwork. No proprietary texture fixture is stored in the repository.

## Bordered keycaps and loading diagram

Face symbols now share a circular rim and translucent dark fill; the interior
symbol is smaller, matching the original prompt's optical size. The BC3 encoder
preserves RGB independently from alpha, including the dark keycap surface.

`dualsense.png` is the approved transparent cutout produced with built-in Codex
imagegen. Final prompt: preserve a front-view white/black DualSense, symmetric
sticks, correct face symbols and framing; remove all annotation lines and keep
the background transparent. Generated source ID:
`exec-5cc39da2-049a-47c8-87e6-e0696e0ec1fe` (2026-10-28). Earlier generated line
variants were discarded because their endpoints did not match the game's text.

`convert-controller.py` performs only resizing and BGRA format conversion to
`dualsense.bgra`. It is shipped beside eboot by `link.sh`. The runtime constructs
white antialiased leader lines as native UI primitives, using exact original
outer endpoints and the DualSense's button positions. Two steering layouts
(stick/D-pad) are supported; all mip levels use alpha-weighted RGB filtering.
The two original 256x256 uncompressed loading textures are recognized by quick
sampled fingerprints followed by a full active-pixel hash. Other textures keep
their original images. No original disc image is stored in this directory.

The USA English `Xbox Guide` caption becomes `PS Button` in the original fixed
string slot while reading its exact archive range. Split reads are handled;
archive name, offset and original bytes all have to match. Disc files and string
table offsets are unchanged. `tests/ps5_controls_test.cpp` checks both texture
fingerprints, mutated data rejection, full/split label reads, other-file
rejection and callout endpoints.

The cutout's face symbols were corrected to neutral grey to match the standard
white DualSense (the colored PlayStation symbols were inaccurate). Built-in
imagegen edit `exec-1dec02e3-dfd8-44cb-8486-90717e8f5c7e`: change only Triangle,
Circle, Cross and Square to the D-pad's neutral grey; preserve controller
geometry/framing, transparent alpha and blue LED strips. Converted BGRA size
remains 262144 bytes; native leaders and texture fingerprints are unchanged.

## Geometry polish after console feedback

The face symbols now use one centre, a common1.60px stroke and a1.45px circular
rim in the logical32px canvas. The triangle is equilateral with a7.6px
circumradius and a small0.65px optical vertical correction; every vertex has
clear separation from the rim. The earlier lower vertices nearly touched it.
Distance-based rasterization gives curves/path joins symmetric coverage at
pixel centres. Shoulder/PS/R3 labels are centred on actual ink bounds instead
of font ascent; the entire twelve-symbol set was reviewed at native size,
menu size and with the pause menu's gold tint. Touchpad/Options/R3 shapes retain
their physical meanings and now share the same outline/keycap treatment.

The BC3 alpha encoder compares both alpha modes with fitted endpoints, keeping
the translucent keycap and opaque symbol distinct. Min/max-only endpoints
previously quantized235-alpha fill to218or255. Independent Pillow decoding
checks all72mip images against the source; the existing error limits still
pass. Original32px texture layout, six mips,18fingerprints and input mapping
remain unchanged.

## Lossless glyphs after enlarged-triangle review

The triangle now uses joined signed edge planes: sharp, even mitres replace
three overlapping round-capped segments. The native128px artwork avoids the
block compression and magnification of the original32px textures; it is drawn
from the same supersampled geometry, not enlarged from the BC3 fallback.
All12symbols have complete128..1RGBA mip chains; native images are uploaded
once per used symbol and shared. Missing/invalid assets retain the BC3 path.
The controller cutout is separate and is the only imagegen artwork here.
