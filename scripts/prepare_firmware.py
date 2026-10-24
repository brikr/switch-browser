"""Extract the Switch browser engine from a firmware package into a loader-ready directory.

Usage:
  python3 -I scripts/prepare_firmware.py --firmware Firmware.22.5.0.zip --keys prod.keys --out engine/22.5.0

--firmware is a firmware zip (as published on THZoria/NX_Firmware) or a directory of NCAs.
Requires `nstool` on PATH. Produces:
  <out>/romfs/           BrowserDll (0100000000000803) RomFS, with nro/*.nro decompressed
  <out>/engine  -> romfs/nro/netfront/core_0/Default/cfi_nocfi  (--engine for wkc-loader-core0)
  <out>/engine3 -> romfs/nro/netfront/core_3/Default/cfi_nncfi  (--engine for wkc-loader-core3)
  <out>/fonts/           Switch system fonts (titles 0810-0814), decrypted from .bfttf to .ttf
  <out>/certs/           trusted root CAs: the system store (title 0800, active entries) and
                         BrowserDll's RootCaEtc.pem, as PEM
"""
import argparse
import base64
import os
import struct
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from unlz4nro import decompress_file  # noqa: E402

BROWSER_DLL = "0x0100000000000803"
FONT_TITLES = ["0x0100000000000810", "0x0100000000000811", "0x0100000000000812", "0x0100000000000813",
               "0x0100000000000814"]
BFTTF_KEY = bytes.fromhex("49621806")
SSL_TITLE = "0x0100000000000800"
ENGINE_SUBDIRS = {
    "engine": "nro/netfront/core_0/Default/cfi_nocfi",
    "engine3": "nro/netfront/core_3/Default/cfi_nncfi",
}


def nca_info(nstool, keys, nca):
    out = subprocess.run([nstool, "-k", keys, str(nca)], capture_output=True, text=True).stdout
    info = {}
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("ProgID:"):
            info["prog_id"] = line.split()[1].lower()
        elif line.startswith("Content Type:"):
            info["type"] = line.split()[2]
    return info


def index_titles(nstool, keys, nca_dir, prog_ids, content_type="Data"):
    """Maps each wanted title ID to its NCA (one nstool pass over the whole firmware)."""
    wanted = {p.lower() for p in prog_ids}
    found = {}
    for nca in sorted(pathlib.Path(nca_dir).glob("*.nca")):
        if nca.name.endswith(".cnmt.nca"):
            continue
        info = nca_info(nstool, keys, nca)
        if info.get("prog_id") in wanted and info.get("type") == content_type:
            found[info["prog_id"]] = nca
    return found


def extract_romfs(nstool, keys, nca, dest):
    subprocess.run([nstool, "-k", keys, "-x", str(dest), str(nca)], check=True, stdout=subprocess.DEVNULL)
    return pathlib.Path(dest) / "0"


def trusted_certs_pem(bdf):
    """ssl_TrustedCerts.bdf: "sslT", u32 count, then {u32 id, u32 status, u32 size, u32 offset} entries
    (offsets relative to the entry table) pointing at DER certificates. Status 1 = active."""
    magic, count = struct.unpack_from("<4sI", bdf, 0)
    if magic != b"sslT":
        raise ValueError("not an ssl_TrustedCerts file")
    out = []
    for i in range(count):
        _cid, status, size, off = struct.unpack_from("<IIII", bdf, 8 + 16 * i)
        if status != 1:
            continue
        b64 = base64.b64encode(bdf[8 + off: 8 + off + size]).decode()
        body = "\n".join(b64[j:j + 64] for j in range(0, len(b64), 64))
        out.append(f"-----BEGIN CERTIFICATE-----\n{body}\n-----END CERTIFICATE-----\n")
    return "".join(out), len(out)


def decrypt_bfttf(data):
    """Shared-font files: 8-byte header (magic, size), then TTF data XORed with a 4-byte key."""
    size = int.from_bytes(bytes(a ^ b for a, b in zip(data[4:8], BFTTF_KEY)), "big")
    body = data[8:8 + size]
    key = BFTTF_KEY * (len(body) // 4 + 1)
    ttf = bytes(a ^ b for a, b in zip(body, key))
    if ttf[:4] != b"\x00\x01\x00\x00":
        raise ValueError("decrypted font is not a TrueType file")
    return ttf


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--firmware", required=True, help="firmware zip or directory of NCAs")
    ap.add_argument("--keys", required=True, help="prod.keys matching the firmware version")
    ap.add_argument("--out", required=True, help="output directory (must not exist)")
    args = ap.parse_args()

    nstool = shutil.which("nstool")
    if not nstool:
        sys.exit("nstool not found on PATH")
    out = pathlib.Path(args.out)
    if out.exists():
        sys.exit(f"{out} already exists")

    with tempfile.TemporaryDirectory() as tmp:
        src = pathlib.Path(args.firmware)
        if src.is_file():
            with zipfile.ZipFile(src) as z:
                # Only the NCAs are needed; extract flat, rejecting odd member names.
                for m in z.infolist():
                    name = os.path.basename(m.filename)
                    if not name.endswith(".nca") or name != m.filename.split("/")[-1]:
                        continue
                    with z.open(m) as f, open(os.path.join(tmp, name), "wb") as dst:
                        shutil.copyfileobj(f, dst)
            nca_dir = tmp
        else:
            nca_dir = src

        titles = index_titles(nstool, args.keys, nca_dir, [BROWSER_DLL, SSL_TITLE] + FONT_TITLES)
        if BROWSER_DLL not in titles:
            sys.exit(f"BrowserDll ({BROWSER_DLL}) not found; are the keys right for this firmware?")
        print(f"BrowserDll: {titles[BROWSER_DLL].name}")

        romfs = extract_romfs(nstool, args.keys, titles[BROWSER_DLL], pathlib.Path(tmp) / "browserdll")
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(romfs, out / "romfs")

        (out / "certs").mkdir()
        shutil.copy(out / "romfs" / "browser" / "RootCaEtc.pem", out / "certs" / "RootCaEtc.pem")
        if SSL_TITLE in titles:
            ssl = extract_romfs(nstool, args.keys, titles[SSL_TITLE], pathlib.Path(tmp) / "ssl")
            pem, n = trusted_certs_pem((ssl / "ssl_TrustedCerts.bdf").read_bytes())
            (out / "certs" / "system-trusted.pem").write_text(pem)
            print(f"certs: {n} system trusted roots")
        else:
            print(f"warning: system SSL title {SSL_TITLE} not found")

        (out / "fonts").mkdir()
        for tid in FONT_TITLES:
            if tid not in titles:
                print(f"warning: font title {tid} not found")
                continue
            fonts = extract_romfs(nstool, args.keys, titles[tid], pathlib.Path(tmp) / tid)
            for bfttf in sorted(fonts.glob("*.bfttf")):
                (out / "fonts" / (bfttf.stem + ".ttf")).write_bytes(decrypt_bfttf(bfttf.read_bytes()))
                print(f"font: {bfttf.stem}.ttf")

    for lz4 in sorted((out / "romfs").rglob("*.nro.lz4")):
        decompress_file(lz4)
    for link, sub in ENGINE_SUBDIRS.items():
        (out / link).symlink_to(pathlib.Path("romfs") / sub)
    print(f"done: {out}")
    print(f"run:  qemu-aarch64-static loader/build/wkc-loader-core3 --engine {out}/engine3 --romfs {out}/romfs")


if __name__ == "__main__":
    main()
