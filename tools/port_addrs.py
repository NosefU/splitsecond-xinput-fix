"""Find the addresses of one SplitSecond.exe build in another, from two decrypted image dumps.

  port_addrs.py <from_dump> <to_dump> <hex address>...

For every reference to an address in the code of the first dump, the bytes around it (with other
absolute addresses masked out) are searched in the second dump; unique matches vote for the new
address. Calls are relative, so functions are better found by their contents (strings they use).
This is how the Steam build addresses were derived from the retail ones.
"""
import collections, re, struct, sys

BASE = 0x400000

def masked_pattern(buf, pos, before=20, after=20):
    lo = max(0, pos - before)
    win = buf[lo:pos + after]
    keep = [True] * len(win)
    for i in range(len(win) - 3):
        v = struct.unpack_from('<I', win, i)[0]
        if 0x401000 <= v < 0x2000000:
            keep[i:i + 4] = [False] * 4
    rx = b''.join(re.escape(bytes([b])) if k else b'.' for b, k in zip(win, keep))
    return re.compile(rx, re.S), pos - lo

def port(old, new, addr, max_refs=40):
    votes = collections.Counter()
    refs = [m.start() for m in re.finditer(re.escape(struct.pack('<I', addr)), old)][:max_refs]
    for r in refs:
        rx, off = masked_pattern(old, r)
        hits = [m.start() for m in rx.finditer(new)]
        if len(hits) == 1:
            votes[struct.unpack_from('<I', new, hits[0] + off)[0]] += 1
    return len(refs), votes.most_common(4)

if __name__ == '__main__':
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    old, new = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
    for a in sys.argv[3:]:
        addr = int(a, 16)
        n, best = port(old, new, addr)
        print(f'{addr:#x}: {n} refs ->', ', '.join(f'{v:#x} ({c})' for v, c in best) or 'no match')
