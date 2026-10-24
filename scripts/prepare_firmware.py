"""Prepare a firmware version directory for the loader: extract the browser engine from the
firmware and unpack Nintendo's OSS package (the WKC headers the loader is built against).

Usage:
  python3 -I scripts/prepare_firmware.py firmware/<version> [--force]

Inputs, found by name in the version directory (or given with --firmware/--keys/--oss):
  Firmware.<version>.zip                   firmware package (or --firmware <directory of NCAs>)
  keys/prod.keys                           keys matching the firmware version
  NintendoSwitch_OpenSources<version>.zip  optional; needed to build the loader

Outputs, in the version directory (steps whose output exists are skipped unless --force):
  engine/romfs/    BrowserDll (0100000000000803) RomFS, with nro/*.nro and the ICU data
                   (browser/icudt*.dat) decompressed
  engine/engine  -> romfs/nro/netfront/core_0/Default/cfi_nocfi  (--engine for wkc-loader-core0)
  engine/engine3 -> romfs/nro/netfront/core_3/Default/cfi_nncfi  (--engine for wkc-loader-core3)
  engine/fonts/    Switch system fonts (titles 0810-0814), decrypted from .bfttf to .ttf
  engine/certs/    trusted root CAs: the system store (title 0800, active entries) and
                   BrowserDll's RootCaEtc.pem, as PEM
  oss/             the OSS package's contents (WKC_3.30.3/, webkit_3.30.3/, ...)

Requires `nstool` on PATH.
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


def find_one(directory, pattern, what):
    matches = sorted(directory.glob(pattern))
    if len(matches) > 1:
        sys.exit(f"several {what} in {directory} ({', '.join(m.name for m in matches)}); pick one with an option")
    return matches[0] if matches else None


def prepare_engine(nstool, firmware, keys, out):
    tmp_out = out.with_name(out.name + ".partial")
    shutil.rmtree(tmp_out, ignore_errors=True)
    with tempfile.TemporaryDirectory() as tmp:
        if firmware.is_file():
            with zipfile.ZipFile(firmware) as z:
                # Only the NCAs are needed; extract flat, rejecting odd member names.
                for m in z.infolist():
                    name = os.path.basename(m.filename)
                    if not name.endswith(".nca") or name != m.filename.split("/")[-1]:
                        continue
                    with z.open(m) as f, open(os.path.join(tmp, name), "wb") as dst:
                        shutil.copyfileobj(f, dst)
            nca_dir = tmp
        else:
            nca_dir = firmware

        titles = index_titles(nstool, keys, nca_dir, [BROWSER_DLL, SSL_TITLE] + FONT_TITLES)
        if BROWSER_DLL not in titles:
            sys.exit(f"BrowserDll ({BROWSER_DLL}) not found; are the keys right for this firmware?")
        print(f"BrowserDll: {titles[BROWSER_DLL].name}")

        romfs = extract_romfs(nstool, keys, titles[BROWSER_DLL], pathlib.Path(tmp) / "browserdll")
        shutil.copytree(romfs, tmp_out / "romfs")

        (tmp_out / "certs").mkdir()
        shutil.copy(tmp_out / "romfs" / "browser" / "RootCaEtc.pem", tmp_out / "certs" / "RootCaEtc.pem")
        if SSL_TITLE in titles:
            ssl = extract_romfs(nstool, keys, titles[SSL_TITLE], pathlib.Path(tmp) / "ssl")
            pem, n = trusted_certs_pem((ssl / "ssl_TrustedCerts.bdf").read_bytes())
            (tmp_out / "certs" / "system-trusted.pem").write_text(pem)
            print(f"certs: {n} system trusted roots")
        else:
            print(f"warning: system SSL title {SSL_TITLE} not found")

        (tmp_out / "fonts").mkdir()
        for tid in FONT_TITLES:
            if tid not in titles:
                print(f"warning: font title {tid} not found")
                continue
            fonts = extract_romfs(nstool, keys, titles[tid], pathlib.Path(tmp) / tid)
            for bfttf in sorted(fonts.glob("*.bfttf")):
                (tmp_out / "fonts" / (bfttf.stem + ".ttf")).write_bytes(decrypt_bfttf(bfttf.read_bytes()))
                print(f"font: {bfttf.stem}.ttf")

    nros = sorted((tmp_out / "romfs").rglob("*.nro.lz4"))
    for lz4 in nros:
        decompress_file(lz4, quiet=True)
    for lz4 in sorted((tmp_out / "romfs" / "browser").glob("*.dat.lz4")):
        decompress_file(lz4, expect_nro=False, quiet=True)
    print(f"engine modules: {len(nros)} NROs decompressed")
    for link, sub in ENGINE_SUBDIRS.items():
        (tmp_out / link).symlink_to(pathlib.Path("romfs") / sub)
    tmp_out.rename(out)
    print(f"engine: {out}")


def unpack_oss(oss_zip, out):
    """Extracts the OSS zip's single top-level folder (NintendoSwitch_OpenSources<version>/) as `out`."""
    tmp_out = out.with_name(out.name + ".partial")
    shutil.rmtree(tmp_out, ignore_errors=True)
    with zipfile.ZipFile(oss_zip) as z:
        members = [m for m in z.infolist() if m.filename.strip("/")]
        tops = {m.filename.split("/")[0] for m in members}
        if len(tops) != 1:
            sys.exit(f"{oss_zip}: expected one top-level folder, found {len(tops)}")
        for m in members:
            rel = pathlib.PurePosixPath(*pathlib.PurePosixPath(m.filename).parts[1:])
            if not rel.parts:
                continue
            if rel.is_absolute() or ".." in rel.parts:
                sys.exit(f"{oss_zip}: unsafe member name {m.filename!r}")
            dest = tmp_out / rel
            if m.is_dir():
                dest.mkdir(parents=True, exist_ok=True)
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            with z.open(m) as f, open(dest, "wb") as dst:
                shutil.copyfileobj(f, dst)
    if not (tmp_out / "WKC_3.30.3").is_dir():
        print(f"warning: {oss_zip} has no WKC_3.30.3/; is it the Switch OSS package for this firmware?")
    tmp_out.rename(out)
    print(f"oss: {out}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", help="firmware version directory, e.g. firmware/22.5.0")
    ap.add_argument("--firmware", help="firmware zip or directory of NCAs (default: <dir>/Firmware.*.zip)")
    ap.add_argument("--keys", help="prod.keys matching the firmware (default: <dir>/keys/prod.keys)")
    ap.add_argument("--oss", help="OSS package zip (default: <dir>/NintendoSwitch_OpenSources*.zip)")
    ap.add_argument("--force", action="store_true", help="redo steps whose output already exists")
    args = ap.parse_args()

    root = pathlib.Path(args.dir)
    if not root.is_dir():
        sys.exit(f"{root} is not a directory")
    engine_out, oss_out = root / "engine", root / "oss"

    if engine_out.exists() and not args.force:
        print(f"engine: {engine_out} exists, skipping (--force redoes it)")
    else:
        firmware = pathlib.Path(args.firmware) if args.firmware else find_one(root, "Firmware.*.zip", "firmware zips")
        keys = pathlib.Path(args.keys) if args.keys else root / "keys" / "prod.keys"
        if not firmware or not firmware.exists():
            sys.exit(f"no firmware: put Firmware.<version>.zip in {root} or pass --firmware")
        if not keys.is_file():
            sys.exit(f"no keys: put prod.keys in {root / 'keys'} or pass --keys")
        nstool = shutil.which("nstool")
        if not nstool:
            sys.exit("nstool not found on PATH")
        if engine_out.exists() or engine_out.is_symlink():
            shutil.rmtree(engine_out)
        prepare_engine(nstool, firmware, keys, engine_out)

    if oss_out.exists() and not args.force:
        print(f"oss: {oss_out} exists, skipping (--force redoes it)")
    else:
        oss_zip = pathlib.Path(args.oss) if args.oss else find_one(root, "NintendoSwitch_OpenSources*.zip", "OSS zips")
        if not oss_zip or not oss_zip.is_file():
            print(f"warning: no OSS package (NintendoSwitch_OpenSources<version>.zip) in {root}; it's needed to build "
                  "the loader")
        else:
            if oss_out.exists():
                shutil.rmtree(oss_out)
            unpack_oss(oss_zip, oss_out)


if __name__ == "__main__":
    main()
