"""Extract textures from a Split/Second SXET texture pack (uncompressed packs only).

Layout: 0x10 count; per-texture 0x40-byte records; at the table: u32 total, u32 0,
u32 count, u32 x, then count * (u32 rec, u32 0, u32 data_offset); data blocks are
[u32 size][DDS file] relative to the data base right after the table.
Usage: sxet.py <pack.bin> <names.bin> <outdir>
"""
import io, os, struct, sys
from PIL import Image

def textures(d):
    count = struct.unpack_from('<I', d, 0x10)[0]
    tbl = 0x28 + 0x40 * count
    total, _, cnt, _ = struct.unpack_from('<IIII', d, tbl)
    assert cnt == count, (hex(tbl), cnt, count)
    base = tbl + 16 + 12 * count
    for i in range(count):
        rec, _, off = struct.unpack_from('<III', d, tbl + 16 + 12 * i)
        size, = struct.unpack_from('<I', d, base + off)
        yield i, d[base + off + 4: base + off + 4 + size]

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    names = [n.decode() for n in open(sys.argv[2], 'rb').read().split(b'\0') if n]
    for i, dds in textures(d):
        nm = names[i] if i < len(names) else f"tex{i}"
        try:
            im = Image.open(io.BytesIO(dds)); im.load()
            im.save(os.path.join(sys.argv[3], nm + ".png"))
            print(i, nm, im.size, im.mode)
        except Exception as e:
            print(i, nm, "ERR", e, dds[:4])
