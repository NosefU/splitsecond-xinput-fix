# Building

**English** | [Русский](building.ru.md)

## The patch

Requires `make`, `i686-w64-mingw32-gcc`, a C compiler for Linux (static build: `musl-gcc` or
`zig cc -target x86_64-linux-musl`), Python 3 and `zip`.

```
make dll                     # build/xinput1_3.dll
make diag                    # build/diag/xinput1_3.dll — research build
make all CC_LINUX=musl-gcc   # DLL + installers build/win/ssfix-install.exe, build/linux/ssfix-install
make dist CC_LINUX=musl-gcc  # dist/splitsecond-xinput-fix-<version>.zip
```

On macOS: `brew install mingw-w64 zig`, then
`make dist CC_LINUX="zig cc -target x86_64-linux-musl"`. The version comes from `git describe`.

The DLL and the diffs are built into both installers (`tools/embed.py` →
`build/payload/payload.h`), so a single file is enough to install. The release archive contains
both installers, `xinput1_3.dll` (for manual installation) and `INSTALL.txt`.

## UI diffs

The movie changes are kept in the repository as binary diffs `patch/*.ssdf` (≈5 KB) against the
original archive entries — the repository contains no game assets, and the game is not needed to
build the patch.

SSDF format (`tools/mkdiff.py`), little-endian:

```
"SSDF" u32 version=1
u32 name_hash                   .ark entry the diff applies to
u32 source_size, source_crc32
u32 target_size, target_crc32
operations until the end of the file:
  0x01 u32 offset u32 length    copy from the original
  0x02 u32 length bytes...      insert bytes
```

When `tools/padicons.py`, `tools/pressstart.py`, `tools/swf.py` or `ui/*.as` change, rebuild the
diffs from the original game archive:

```
tools/make_diffs.sh "<game folder>/data/resident.ark" path/to/ffdec.jar
```

Requires Python 3 (no extra modules) and Java for
[JPEXS FFDec](https://github.com/jindrapetrik/jpexs-decompiler), which compiles the ActionScript.
The script extracts the two movies, builds the modified versions in `build/ui/` and writes
`patch/prompt_bar.ssdf` and `patch/press_start.ssdf`. The result is deterministic: the same
archive gives the same bytes.

## CI

`.github/workflows/build.yml` builds the DLL and both installers on Ubuntu
(`gcc-mingw-w64-i686`, `musl-gcc`) on every push and PR and uploads the zip as an artifact. On a
`v*` tag the same zip is attached to a GitHub release, with `INSTALL.txt` as the release text:

```
git tag v1.0.0 && git push origin v1.0.0
```

## Repository layout

```
src/xinput_fix.c          proxy source (-DDIAG: research build)
src/xinput1_3.def         exports with the original xinput1_3 ordinals
installer/ssfix_install.c installer (Windows and Linux)
patch/*.ssdf              UI movie diffs
ui/*.as                   ActionScript 2 inserted into the movies
tools/                    tools for unpacking and building the game's resources
docs/                     documentation
INSTALL.txt               instructions for the release archive
Makefile                  build
```
