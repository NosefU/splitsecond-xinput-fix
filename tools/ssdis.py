"""Helpers over the decrypted SplitSecond.exe memory dump (image base 0x400000)."""
import re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

B = 0x400000
d = open(sys.argv[1] if len(sys.argv) > 1 else 'ss_dump.bin', 'rb').read()
cs = Cs(CS_ARCH_X86, CS_MODE_32)
TEXT_END = 0x79c000 + B

def refs(va):
    return [B + m.start() for m in re.finditer(re.escape(struct.pack('<I', va)), d)]

def cstr(va):
    o = va - B
    if not (0 <= o < len(d)): return None
    e = d.find(b'\0', o)
    s = d[o:e]
    return s.decode('latin-1') if s and all(32 <= c < 127 for c in s) else None

def func_start(va):
    """Walk back to a likely function start (after int3/nop padding or ret)."""
    o = va - B
    for k in range(o, max(o - 0x4000, 0x1000), -1):
        if d[k-1] in (0xcc, 0x90) and d[k] in (0x55, 0x83, 0x81, 0x53, 0x56, 0x57, 0x6a, 0x8b, 0x51, 0xa1, 0x64):
            return B + k
    return va - 0x100

def dis(start, n=80, mark=None):
    out = []
    for ins in cs.disasm(d[start - B:start - B + n * 8], start):
        cmt = ''
        for v in re.findall(r'0x([0-9a-f]{6,8})', ins.op_str):
            s = cstr(int(v, 16))
            if s: cmt += f'  ; "{s}"'
        out.append(f"{'>>' if mark and ins.address <= mark < ins.address + ins.size else '  '} {ins.address:08x}: {ins.mnemonic} {ins.op_str}{cmt}")
        if len(out) >= n: break
    return '\n'.join(out)
