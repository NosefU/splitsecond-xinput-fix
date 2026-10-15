"""Replace one entry of a Split/Second .ark archive in place.

The new data is appended uncompressed (flag 0) at the end of the archive and
the entry's table record is pointed at it; the old data stays where it was.
Usage: ark_patch.py <archive.ark> <name-hash-hex> <new-file>
"""
import struct
import sys

ENTRY_TABLE = 0x18
ENTRY_SIZE = 20
ALIGN = 0x800


def main():
    path, name_hash, new_path = sys.argv[1], int(sys.argv[2], 16), sys.argv[3]
    new = open(new_path, 'rb').read()
    with open(path, 'r+b') as f:
        header = f.read(ENTRY_TABLE)
        version, _, count = struct.unpack_from('<3I', header)
        if version != 0x10000:
            sys.exit(f"unexpected archive version {version:#x}")
        table = f.read(count * ENTRY_SIZE)
        for k in range(count):
            flag, off, csize, usize, h = struct.unpack_from('<5I', table, k * ENTRY_SIZE)
            if h == name_hash:
                break
        else:
            sys.exit(f"entry {name_hash:08x} not found")
        print(f"entry {k}: flag={flag} offset={off:#x} csize={csize:#x} usize={usize:#x}")

        f.seek(0, 2)
        pos = f.tell()
        pos += -pos % ALIGN
        f.seek(pos)
        f.write(new)
        f.write(b'\0' * (-f.tell() % ALIGN))

        f.seek(ENTRY_TABLE + k * ENTRY_SIZE)
        f.write(struct.pack('<5I', 0, pos, len(new), len(new), name_hash))
        print(f"  -> flag=0 offset={pos:#x} size={len(new):#x}")


if __name__ == '__main__':
    main()
