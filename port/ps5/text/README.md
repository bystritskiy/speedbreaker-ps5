# PS5 presentation text

`english.json` contains reviewed English copy for saves, profiles and the
unavailable online features. Online menu labels are neutral; error/sign-in
messages explicitly describe this port's offline state. No PSN functionality is
claimed. Physical Start/Back labels use Options/Touchpad.

`generate-text-patches.py` reads the owned USA disc's language resources and
ZDIR index. It emits `patches.json` and `../src/text_patches.h`: 852 exact string
slots across the ten languages offered in Settings. Other languages retain
their original translated text around neutral platform names. Short button
prompts use the physical Options name; translated reconnect prompts keep their
format arguments. Chinese/Korean placeholder packs and the LABELS debug pack
are not exposed in Settings and are not patched.

Neither file holds the game's text. Each slot is named by its archive range,
its size and an FNV-1a hash of its original bytes, with the edits that make
the new text: positions, lengths and inserted bytes, which are only this
port's own copy (`english.json`, platform and button names, short prompts).
The generator refuses an insert that carries a run of the original wording.

Each replacement fits its original NUL-terminated slot. Resource size, string
hashes, all offsets, format argument types/order and every unrelated string
remain intact. All padding after a shorter replacement is NUL. The original
archives, checksums and install marker are unchanged.

On the console, `text_overrides.h` binary-searches the sorted ranges only on
read-only disc file reads. The first read that touches a slot reads it whole
from the player's archive, checks its hash and applies the edits; a read is
changed only where its bytes equal the disc's original. Partial reads and
strings spanning reads are supported. Unrecognized archive names,
versions and mismatched bytes pass through. This supersedes the original
single English `Xbox Guide` patch in controls_artwork.h.

Regenerate and validate with locally extracted resources:

```sh
python3 port/ps5/tools/generate-text-patches.py --resources /path/to/ui-resources --zdir game/files/NFS/ZDIR.BIN
python3 tests/ps5_text_catalog_test.py /path/to/ui-resources
c++ -O2 -std=c++20 -Iport/ps5/src tests/ps5_text_overrides_test.cpp -o /tmp/ps5-text-test
/tmp/ps5-text-test                  # a synthetic archive
/tmp/ps5-text-test game/files/NFS   # and every slot of the player's own archives
```

Both tests need the extracted disc for the real slots: the original text is
only there. The catalog test verifies all strings in every language, including unmodified
strings and aliases, and audits residual Xbox/Gamertag/Start-button terms. The
C++ test covers all split positions plus source archive windows, mutated
input rejection, unrelated files and read boundaries.

PS5-only host UI copy lives in `runtime/user/platform_labels.h` selections:
settings/control and wheel labels, display-tone/resolution explanations,
installer screens/errors, controller disconnect notices and report headings.
Stable setting keys, XInput bits, disc magic, SDK symbols, diagnostics and
licensing/provenance are deliberately retained.

## Boot-cache coverage

The first console check exposed a second English read path: the complete
language resource is embedded at offset `0x586A60` of
`GLOBAL/GLOBALMEMORYFILE.BIN` (USA archive0, sector76150). Standalone language
checks alone cannot establish boot coverage. The generator now audits this and
the frontend/ingame/permanent memory-file containers for exact embedded copies,
adding84English ranges (852total). `patches.json` records container hashes and
copy boundaries.

The table was changed from stored original and replacement bytes to hashes
and edits after the c607b1d console run; for all 852 slots, the edits applied
to the disc's bytes give exactly the bytes that run used. The catalog test checks each patched copy equals the patched
standalone file byte-for-byte, as well as all original layout/format invariants.
