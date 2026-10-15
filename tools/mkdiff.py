"""Make an SSDF binary diff: target file expressed as copies from a source file plus new bytes.

Format (little-endian):
  "SSDF" u32 version=1
  u32 name_hash            .ark entry the diff applies to
  u32 source_size, source_crc32
  u32 target_size, target_crc32
  ops until end of file:
    0x01 u32 offset u32 length   copy from source
    0x02 u32 length bytes...     literal bytes
Usage: mkdiff.py <entry-hash-hex> <source> <target> <out.ssdf>
"""
import struct
import sys
import zlib

KEY = 8
MIN_COPY = 12


def diff(src, dst):
    index = {}
    for i in range(len(src) - KEY + 1):
        index.setdefault(src[i:i + KEY], []).append(i)
    ops, lit, i = [], bytearray(), 0
    while i < len(dst):
        best_len, best_off = 0, 0
        for off in index.get(dst[i:i + KEY], ())[:64]:
            n = 0
            while i + n < len(dst) and off + n < len(src) and src[off + n] == dst[i + n]:
                n += 1
            if n > best_len:
                best_len, best_off = n, off
        if best_len >= MIN_COPY:
            if lit:
                ops.append(('add', bytes(lit))); lit = bytearray()
            ops.append(('copy', best_off, best_len))
            i += best_len
        else:
            lit.append(dst[i]); i += 1
    if lit:
        ops.append(('add', bytes(lit)))
    return ops


def encode(name_hash, src, dst, ops):
    out = bytearray(b'SSDF' + struct.pack('<IIIIII', 1, name_hash, len(src), zlib.crc32(src), len(dst), zlib.crc32(dst)))
    for op in ops:
        if op[0] == 'copy':
            out += struct.pack('<BII', 1, op[1], op[2])
        else:
            out += struct.pack('<BI', 2, len(op[1])) + op[1]
    return bytes(out)


def apply(src, patch):
    """Reference implementation of the applier (used to self-check)."""
    assert patch[:4] == b'SSDF'
    _, _, ssize, scrc, tsize, tcrc = struct.unpack_from('<IIIIII', patch, 4)
    assert len(src) == ssize and zlib.crc32(src) == scrc
    out, pos = bytearray(), 28
    while pos < len(patch):
        op = patch[pos]
        if op == 1:
            off, n = struct.unpack_from('<II', patch, pos + 1); out += src[off:off + n]; pos += 9
        else:
            n, = struct.unpack_from('<I', patch, pos + 1); out += patch[pos + 5:pos + 5 + n]; pos += 5 + n
    assert len(out) == tsize and zlib.crc32(out) == tcrc
    return bytes(out)


def main():
    name_hash = int(sys.argv[1], 16)
    src, dst = open(sys.argv[2], 'rb').read(), open(sys.argv[3], 'rb').read()
    patch = encode(name_hash, src, dst, diff(src, dst))
    assert apply(src, patch) == dst
    open(sys.argv[4], 'wb').write(patch)
    copied = sum(op[2] for op in diff(src, dst) if op[0] == 'copy')
    print(f"{sys.argv[4]}: {len(patch)} bytes ({copied} of {len(dst)} bytes copied from the original)")


if __name__ == '__main__':
    main()
