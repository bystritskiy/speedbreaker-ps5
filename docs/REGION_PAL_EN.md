# European English (PAL) disc

The `Need for Speed - Most Wanted (Europe).iso` examined here contains English
PAL movies. It is a separate executable from the USA, Spanish and German discs:

- `default.xex` SHA-256: `e2ea2ff11bf84cb30430d824d7bfe699a3acf3e7400d21b9a7c938e3d7a10474`
- 52 files, 7,169,042,728 bytes; 36 English PAL movies.
- Region ID `pal-en`; English language default; saves in `save-pal-en/`.

The function survey matched all 22,679 USA functions (10,365 exact, 12,295
relocation-equivalent, 19 changed, none missing). The unchanged layout has
16 pieces shifted by -0x18..+0x240. The two extra leaf functions and their
jump tables are at different addresses from Spanish/German PAL. The runtime
uses this disc's own code, address map and hook forwarders.

## Build from your disc

From the repository root, after setting up the normal PS5 dependencies and
USA `ppc/` code:

```sh
tools/extract-xiso/build/extract-xiso -q -d game/files-pal-en 'source/Need for Speed - Most Wanted (Europe).iso'
mkdir -p ppc-pal-en
tools/XenonRecomp/build/XenonRecomp/XenonRecomp config/pal-en/nfsmw.toml tools/XenonRecomp/XenonUtils/ppc_context.h
python3 scripts/region_ppc.py ppc-pal-en pal_en
# Add -DSB_PAL_EN=ON to the PS5 configure command in port/ps5/README.md.
# Compile, then run port/ps5/tools/link.sh with that build directory.
```

`SB_PAL_EN=AUTO` includes the code when `ppc-pal-en/` exists; `ON` requires it;
`OFF` excludes it. The installer only offers regions whose code is included.
The PS5 linker includes each enabled region archive, alongside USA.
Spanish and German are independently available through `SB_PAL_ES` and
`SB_PAL_DE` when their own generated code is supplied. An arbitrary Europe
ISO is not interchangeable with any of these executables.

English and Spanish PAL executables both occupy 3,940,352 bytes. Size alone
cannot distinguish them: manifest size lookup returns no result when both
are enabled. Installed folders use their current manifest marker; unmarked
folders use the executable hash. Installation and runtime startup also
identify the executable by SHA-256.

## Regenerate and verify

The survey uses the player's own executables; no game data is committed.
Build `survey/xexsurvey.cpp` as described in `REGION_PAL_DE.md`, then:

```sh
mkdir -p game/survey/mw_usa game/survey/mw_en
build/xexsurvey game/files/default.xex game/survey/mw_usa > game/survey/mw_usa/xexsurvey.txt
build/xexsurvey game/files-pal-en/default.xex game/survey/mw_en > game/survey/mw_en/xexsurvey.txt
python3 -I survey/func_map.py game/survey/mw_usa game/survey/mw_en > game/survey/mw_en/funcmap.tsv
python3 -I survey/region_map.py toml game/survey/mw_usa game/survey/mw_en game/survey/mw_en/funcmap.tsv --region pal-en
python3 -I survey/region_map.py table game/survey/mw_usa game/survey/mw_en game/survey/mw_en/funcmap.tsv --region pal-en
python3 scripts/gen_install_manifest.py game/files-pal-en --config config/pal-en/nfsmw.toml -o runtime/install/manifest_pal_en.inc
python3 -I tests/region_addresses_check.py . game/survey/mw_usa game/survey/mw_en game/survey/mw_en/funcmap.tsv --region pal-en
python3 -I tests/region_parity_check.py .
```

`game_image_test` accepts any compiled-in region and checks the actual ISO
specified by `NFSMW_TEST_IMAGE`, including all file hashes during installation.
The eight `region_availability_*` tests check every combination of PAL code
availability, including the shared executable size.

Verified on PS5 (2026-11-13, v0.2.4 build `1368a91` with these changes): the
installer accepts the Europe ISO, the runtime reports "Europe (PAL, English):
its own recompiled code", the English PAL movies play, the front end runs at
60 fps, and the game is playable. Settings > Language > Text switches the
menu and race text to the disc's other languages after a restart; movies and
police speech stay English, as the disc has only `copspeech_en` (Audio shows
it).

The Spanish disc was verified the same way (2026-11-14, the same build with
`SB_PAL_ES=ON`): "Europe (PAL, Spanish)", all 49 files matched, Spanish PAL
movies, `copspeech_sp` in a race, and the Language page showing Text and
Audio (Spanish).
