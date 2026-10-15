import sys, struct, numpy as np
data = open(sys.argv[1], 'rb').read()
pos = 0
snaps = []  # (t, calls, buttons, lt, rt, {start: array})
cur = {}
sizes = {0xcf6000: 0xa8000, 0x1a56000: 0x2d4000}
for st, sz in sizes.items():
    cur[st] = np.zeros(sz // 4, dtype=np.uint32)
while pos < len(data):
    assert data[pos:pos+4] == b'SNAP', pos
    idx, t, calls, btn, lt, rt = struct.unpack_from('<IdiHBB', data, pos + 4)
    pos += 4 + 4 + 8 + 4 + 4
    for _ in range(2):
        start, count = struct.unpack_from('<II', data, pos); pos += 8
        rec = np.frombuffer(data, dtype='<u4', count=count * 2, offset=pos).reshape(-1, 2)
        pos += count * 8
        cur[start][rec[:, 0] // 4] = rec[:, 1]
    snaps.append((t, {k: v.copy() for k, v in cur.items()}))
np.save(sys.argv[2], np.array([s[0] for s in snaps]))
for st in sizes:
    np.save(f"{sys.argv[2]}_{st:x}", np.stack([s[1][st] for s in snaps]))
print(len(snaps), "snapshots", snaps[0][0], snaps[-1][0])
