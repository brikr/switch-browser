"""Wrap a decompressed Switch NRO image in a minimal AArch64 ELF (program headers + PT_DYNAMIC).

NRO file offsets equal memory offsets, so the image is copied verbatim at a fixed file offset and
each segment gets a PT_LOAD; Ghidra's ELF loader then parses .dynamic (symbols, relocations).
"""
import struct
import sys

DATA_OFF = 0x10000  # file offset of image byte 0 (keeps p_offset % align == p_vaddr % align)
PF_X, PF_W, PF_R = 1, 2, 4


def convert(src, dst):
    img = open(src, "rb").read()
    assert img[0x10:0x14] == b"NRO0", "not a decompressed NRO"
    text_off, text_sz, ro_off, ro_sz, data_off, data_sz, bss_sz = struct.unpack_from("<7I", img, 0x20)
    mod0 = struct.unpack_from("<I", img, 4)[0]
    assert img[mod0:mod0 + 4] == b"MOD0", "missing MOD0"
    dyn_rel, bss_start_rel, bss_end_rel = struct.unpack_from("<iii", img, mod0 + 4)
    dyn_addr = mod0 + dyn_rel
    bss_end = mod0 + bss_end_rel

    # Size of .dynamic: count entries up to DT_NULL.
    n = 0
    while struct.unpack_from("<q", img, dyn_addr + 16 * n)[0] != 0:
        n += 1
    dyn_sz = 16 * (n + 1)

    data_mem = max(data_off + data_sz, bss_end) - data_off
    phdrs = [
        (1, PF_R | PF_X, text_off, text_sz, text_sz),
        (1, PF_R, ro_off, ro_sz, ro_sz),
        (1, PF_R | PF_W, data_off, data_sz, data_mem),
        (2, PF_R | PF_W, dyn_addr, dyn_sz, dyn_sz),  # PT_DYNAMIC
    ]
    ehdr = struct.pack(
        "<4sBBBBB7xHHIQQQIHHHHHH",
        b"\x7fELF", 2, 1, 1, 0, 0,  # ELFCLASS64, LSB, EV_CURRENT
        3, 183, 1,  # ET_DYN, EM_AARCH64
        text_off, 64, 0, 0, 64, 56, len(phdrs), 64, 0, 0,
    )
    ph = b"".join(
        struct.pack("<IIQQQQQQ", t, f, DATA_OFF + va, va, va, fsz, msz, 0x1000 if t == 1 else 8)
        for t, f, va, fsz, msz in phdrs
    )
    out = (ehdr + ph).ljust(DATA_OFF, b"\0") + img
    open(dst, "wb").write(out)
    print(f"{dst}: text {text_sz:#x} ro {ro_sz:#x} data {data_sz:#x} bss->{bss_end:#x} dynamic@{dyn_addr:#x} ({n} entries)")


for p in sys.argv[1:]:
    convert(p, p[: -len(".nro")] + ".elf")
