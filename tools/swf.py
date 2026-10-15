"""Minimal uncompressed SWF / Scaleform GFX tag reader and writer."""
import struct

T_END, T_SHOWFRAME, T_DEFINESHAPE, T_PLACEOBJECT2, T_REMOVEOBJECT2 = 0, 1, 2, 26, 28
T_DOACTION, T_DEFINESPRITE, T_FRAMELABEL, T_PLACEOBJECT3 = 12, 39, 43, 70
T_EXPORTASSETS = 56
GFX_EXTERNALIMAGE = 1001  # Scaleform DefineExternalImage
GFX_EXTERNALIMAGE2 = 1009  # Scaleform DefineExternalImage2


class Tag:
    def __init__(self, code, data, long_hdr=False):
        self.code, self.data, self.long_hdr = code, bytes(data), long_hdr
        self.children = None  # parsed tags for DefineSprite

    def encode(self):
        data = self.data
        if self.code == T_DEFINESPRITE and self.children is not None:
            data = self.data[:4] + encode_tags(self.children)
        if len(data) < 0x3f and not self.long_hdr:
            return struct.pack('<H', (self.code << 6) | len(data)) + data
        return struct.pack('<HI', (self.code << 6) | 0x3f, len(data)) + data

    @property
    def char_id(self):
        return struct.unpack_from('<H', self.data)[0]


def parse_tags(buf, pos=0, end=None):
    end = len(buf) if end is None else end
    tags = []
    while pos < end:
        hdr, = struct.unpack_from('<H', buf, pos); pos += 2
        code, ln = hdr >> 6, hdr & 0x3f
        long_hdr = ln == 0x3f
        if long_hdr:
            ln, = struct.unpack_from('<I', buf, pos); pos += 4
        t = Tag(code, buf[pos:pos + ln], long_hdr); pos += ln
        if code == T_DEFINESPRITE:
            t.children = parse_tags(t.data, 4)
        tags.append(t)
        if code == T_END:
            break
    return tags


def encode_tags(tags):
    return b''.join(t.encode() for t in tags)


def header_len(buf):
    nbits = buf[8] >> 3
    rect_bytes = (5 + 4 * nbits + 7) // 8
    return 8 + rect_bytes + 4


def load(path):
    buf = open(path, 'rb').read()
    h = header_len(buf)
    return buf[:h], parse_tags(buf, h)


def save(path, header, tags):
    body = encode_tags(tags)
    header = bytearray(header)
    struct.pack_into('<I', header, 4, len(header) + len(body))
    open(path, 'wb').write(bytes(header) + body)


def frames(sprite):
    """Split sprite children into frames: list of lists of tags (ShowFrame excluded)."""
    out, cur = [], []
    for t in sprite.children:
        if t.code == T_SHOWFRAME:
            out.append(cur); cur = []
        elif t.code != T_END:
            cur.append(t)
    return out


def cstr(data, pos=0):
    end = data.index(b'\0', pos)
    return data[pos:end].decode('latin-1'), end + 1


def place_info(t):
    """Return (depth, char_id or None, name or None) for PlaceObject2/3."""
    d = t.data
    flags = d[0]; pos = 1
    flags2 = 0
    if t.code == T_PLACEOBJECT3:
        flags2 = d[1]; pos = 2
    depth, = struct.unpack_from('<H', d, pos); pos += 2
    if t.code == T_PLACEOBJECT3 and (flags2 & 0x08 or (flags2 & 0x10 and flags & 0x02)):
        _, pos = cstr(d, pos)  # class name
    char = None
    if flags & 0x02:
        char, = struct.unpack_from('<H', d, pos); pos += 2
    name = None
    if flags & 0x20:
        # skip matrix / color transform / ratio, which precede the name
        pos = _skip_matrix(d, pos) if flags & 0x04 else pos
        pos = _skip_cxform(d, pos) if flags & 0x08 else pos
        pos = pos + 2 if flags & 0x10 else pos
        name, _ = cstr(d, pos)
    return depth, char, name, (pos if False else None)


def char_offset(t):
    """Byte offset of the character id inside a PlaceObject2/3 tag, or None."""
    d = t.data
    flags = d[0]; pos = 1
    flags2 = 0
    if t.code == T_PLACEOBJECT3:
        flags2 = d[1]; pos = 2
    pos += 2
    if t.code == T_PLACEOBJECT3 and (flags2 & 0x08 or (flags2 & 0x10 and flags & 0x02)):
        _, pos = cstr(d, pos)
    return pos if flags & 0x02 else None


class Bits:
    def __init__(self, d, pos):
        self.d, self.bit = d, pos * 8

    def u(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.d[self.bit >> 3] >> (7 - (self.bit & 7))) & 1)
            self.bit += 1
        return v

    def end(self):
        return (self.bit + 7) >> 3


def _skip_matrix(d, pos):
    b = Bits(d, pos)
    if b.u(1):
        n = b.u(5); b.u(n); b.u(n)
    if b.u(1):
        n = b.u(5); b.u(n); b.u(n)
    n = b.u(5); b.u(n); b.u(n)
    return b.end()


def _skip_cxform(d, pos):
    b = Bits(d, pos)
    has_add, has_mult = b.u(1), b.u(1)
    n = b.u(4)
    b.u(n * 4 * (has_add + has_mult))
    return b.end()


class BitWriter:
    def __init__(self):
        self.bits = []

    def u(self, n, v):
        self.bits += [(v >> (n - 1 - i)) & 1 for i in range(n)]

    def s(self, n, v):
        self.u(n, v & ((1 << n) - 1))

    def bytes(self):
        bits = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(int(''.join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))


def sbits(*vals):
    return max(v.bit_length() + 1 if v >= 0 else (~v).bit_length() + 1 for v in vals)


def rect(xmin, xmax, ymin, ymax):
    n = max(sbits(xmin, xmax, ymin, ymax), 1)
    w = BitWriter(); w.u(5, n)
    for v in (xmin, xmax, ymin, ymax):
        w.s(n, v)
    return w.bytes()


def bitmap_rect_shape(shape_id, bitmap_id, width, height):
    """DefineShape: a width x height (pixels) rectangle filled with a clipped bitmap."""
    tw, th = width * 20, height * 20
    out = struct.pack('<H', shape_id) + rect(0, tw, 0, th)
    # fill styles: one clipped bitmap fill, matrix scale 20 (pixels -> twips)
    m = BitWriter()
    m.u(1, 1); n = sbits(20 << 16); m.u(5, n); m.s(n, 20 << 16); m.s(n, 20 << 16)
    m.u(1, 0)
    m.u(5, 0)
    out += bytes([1, 0x41]) + struct.pack('<H', bitmap_id) + m.bytes()
    out += bytes([0])  # line styles
    w = BitWriter()
    w.u(4, 1); w.u(4, 0)  # fill bits, line bits
    # style change: move to (0,0), fill0 = 1 ... use fillStyle1 for right-hand fill
    w.u(1, 0); w.u(1, 0); w.u(1, 0); w.u(1, 1); w.u(1, 0); w.u(1, 1)
    w.u(5, 1); w.s(1, 0); w.s(1, 0)
    w.u(1, 1)  # fill style 1
    for dx, dy in ((tw, 0), (0, th), (-tw, 0), (0, -th)):
        n = max(sbits(dx, dy), 2)
        w.u(1, 1); w.u(1, 1); w.u(4, n - 2)
        if dx and dy:
            w.u(1, 1); w.s(n, dx); w.s(n, dy)
        else:
            w.u(1, 0); w.u(1, 1 if dy else 0); w.s(n, dy if dy else dx)
    w.u(1, 0); w.u(5, 0)  # end of shape
    return Tag(T_DEFINESHAPE, out + w.bytes(), True)
