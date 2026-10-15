# Tools and formats

**English** | [Русский](tooling.ru.md)

The research scripts need Python 3 with `numpy`, `Pillow` and `capstone`; building the movies
needs Java and [JPEXS FFDec](https://github.com/jindrapetrik/jpexs-decompiler).

| File | Purpose |
|---|---|
| `tools/ark.py` | read `.ark`: `ark.py list <archive>`, `ark.py extract <archive> <hash> <file>` |
| `tools/arkdump.c` | fast unpacking of the whole archive into `<hash>.bin` (`cc -O2 -o arkdump tools/arkdump.c`) |
| `tools/ark_patch.py` | replace a single archive entry by hand (for experiments; the installer does the same with rollback) |
| `tools/mkdiff.py` | make an SSDF diff between an original and a modified entry (self-checking) |
| `tools/make_diffs.sh` | rebuild `patch/*.ssdf` from the original archive |
| `tools/embed.py` | pack the DLL and the diffs into `payload.h` for the installer |
| `tools/swf.py` | minimal SWF/GFX tag reader/writer, simple shape generation |
| `tools/sxet.py` | unpack `SXET` texture packs to PNG (names come from the matching name list) |
| `tools/padicons.py` | build `Prompt_Bar.gfx` with Xbox icons |
| `tools/pressstart.py` | prepare `FE_pressStart_Overlay.gfx` (A button clip, text field name) |
| `tools/memscan.py` | parse memory snapshots from the research build (`ss_mem.bin`) |
| `tools/ssdis.py` | helpers for disassembling the decrypted exe dump (`ss_dump.bin`) |

## `.ark` format

A header of 6 `u32` (version `0x10000`, table size, entry count, …), then from `0x18` 20-byte
records: `flag, offset, csize, usize, hash`. Records are sorted by hash; file names are stored
only as hashes.

- `flag = 1` — compressed: a `u32` unpacked size followed by an LZSS stream (4 KiB ring buffer,
  zero-filled, start position `0xFEE`; a flags byte, references are 12 bits of offset and 4 bits
  of length + 3).
- `flag = 0` — stored as is.

At startup the game decrypts the archives in the encrypted `dfe` file into
`%TEMP%\SplitSecond_Data_DFE\` — those are the Nemesis tracks, in the same format.

## Texture packs (`SXET`)

`u32 "SXET"`, `u32 0xC`, …, the texture count at `+0x10`, then from `0x28` 0x40-byte records
(format, width, height, hash). They are followed by a table `u32 total, u32 0, u32 count, u32 x`
and `count` triples `(record, 0, data offset)`; the data are `[u32 size][DDS file]` blocks.
Texture names are in a separate name-list file, in the same order. The menu packs (CommonUI) are
uncompressed 32-bit BGRA.

## Scaleform movies

`.gfx` is SWF (uncompressed, `GFX` signature, version 8) with Scaleform extensions: external
images (`DefineExternalImage`, tag 1001), `ExporterInfo` (tag 1000) with the movie name. FFDec
opens them directly; in FFDec scripts are addressed as `/DefineSprite_<id>/frame_<n>/DoAction`.

## Research build (`make diag`)

With `-DDIAG` the proxy also writes to `Documents`:

- `ss_mem.bin` — snapshots of the exe's `.data`/`Sdata` sections every 0.5 s (as deltas);
  `tools/memscan.py` turns them into arrays for finding state variables (this is how the menu and
  pause flags were found);
- `ss_dump.bin` — the exe image from memory at the first race start (the code is already decrypted
  by SecuROM), for disassembly with `tools/ssdis.py`;
- to the log — key presses, gamepad activity and the control bindings from the game's profiles.
