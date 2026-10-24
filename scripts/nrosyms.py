"""List dynamic symbols of a decompressed Switch NRO as '<imp|exp> <type> <name>' lines.

Reads MOD0 -> .dynamic -> DT_SYMTAB/DT_STRTAB directly, so it needs no section headers.
"""
import struct
import sys

DT_STRTAB, DT_SYMTAB, DT_STRSZ = 5, 6, 10
STT = {0: "notype", 1: "object", 2: "func", 3: "section", 4: "file", 6: "tls"}


def dynsyms(img):
    assert img[0x10:0x14] == b"NRO0", "not a decompressed NRO"
    mod0 = struct.unpack_from("<I", img, 4)[0]
    dyn = mod0 + struct.unpack_from("<i", img, mod0 + 4)[0]
    tags = {}
    while True:
        tag, val = struct.unpack_from("<qQ", img, dyn)
        if tag == 0:
            break
        tags[tag] = val
        dyn += 16
    symtab, strtab, strsz = tags[DT_SYMTAB], tags[DT_STRTAB], tags[DT_STRSZ]
    assert strtab > symtab, "expected .dynstr to follow .dynsym"
    for off in range(symtab + 24, strtab, 24):  # skip the null symbol
        name_off, info, _other, shndx, _value, _size = struct.unpack_from("<IBBHQQ", img, off)
        if name_off >= strsz:
            break
        end = img.index(b"\0", strtab + name_off)
        name = img[strtab + name_off:end].decode("utf-8", "replace")
        yield ("imp" if shndx == 0 else "exp"), STT.get(info & 0xF, str(info & 0xF)), name


for path in sys.argv[1:]:
    for kind, typ, name in dynsyms(open(path, "rb").read()):
        print(kind, typ, name)
