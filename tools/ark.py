"""Split/Second .ark reader: header(6 u32), entries of (flag, offset, csize, usize, hash), LZSS data."""
import struct, sys

def lzss(src, usize, fill=0x00):
    ring = bytearray([fill]) * 4096
    r = 4096 - 18
    out = bytearray()
    i = 0
    while len(out) < usize and i < len(src):
        flags = src[i]; i += 1
        for bit in range(8):
            if len(out) >= usize or i >= len(src): break
            if flags & (1 << bit):
                c = src[i]; i += 1
                out.append(c); ring[r] = c; r = (r + 1) & 0xfff
            else:
                b0, b1 = src[i], src[i + 1]; i += 2
                off = b0 | ((b1 & 0xf0) << 4)
                ln = (b1 & 0x0f) + 3
                for k in range(ln):
                    c = ring[(off + k) & 0xfff]
                    out.append(c); ring[r] = c; r = (r + 1) & 0xfff
    return bytes(out)

def entries(d):
    n = struct.unpack_from('<I', d, 8)[0]
    return [struct.unpack_from('<5I', d, 0x18 + 20 * k) for k in range(n)]

def read(d, e):
    flag, off, cs, us, h = e
    raw = d[off:off + cs]
    if flag == 1:
        assert struct.unpack_from("<I", raw)[0] == us, "size prefix"
        raw = raw[4:]
    return lzss(raw, us) if flag == 1 else raw

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    for k, e in enumerate(entries(d)):
        data = read(d, e)
        print(k, hex(e[4]), len(data), e[3], data[:60])
