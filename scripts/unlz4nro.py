"""Decompress BrowserDll *.nro.lz4 files: u32 LE decompressed size + raw LZ4 block."""
import struct
import sys


def lz4_block_decompress(src, out_size):
    dst = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        dst += src[i:i + lit]
        i += lit
        if i >= n:
            break
        off = src[i] | (src[i + 1] << 8)
        i += 2
        mlen = token & 15
        if mlen == 15:
            while True:
                b = src[i]
                i += 1
                mlen += b
                if b != 255:
                    break
        mlen += 4
        start = len(dst) - off
        if off >= mlen:
            dst += dst[start:start + mlen]
        else:
            for k in range(mlen):
                dst.append(dst[start + k])
    if len(dst) != out_size:
        raise ValueError(f"size mismatch: got {len(dst)}, header says {out_size}")
    return bytes(dst)


def decompress_file(path):
    path = str(path)
    data = open(path, "rb").read()
    (size,) = struct.unpack_from("<I", data, 0)
    out = lz4_block_decompress(data[4:], size)
    if out[0x10:0x14] != b"NRO0":
        raise ValueError(f"{path}: no NRO0 magic")
    dest = path[:-4]
    open(dest, "wb").write(out)
    print(f"{dest} {size}")


if __name__ == "__main__":
    for p in sys.argv[1:]:
        decompress_file(p)
