"""Extract the Switch browser engine from a firmware package into a loader-ready directory.

Usage:
  python3 -I scripts/prepare_firmware.py --firmware Firmware.22.5.0.zip --keys prod.keys --out engine/22.5.0

--firmware is a firmware zip (as published on THZoria/NX_Firmware) or a directory of NCAs.
Requires `nstool` on PATH. Produces:
  <out>/romfs/           BrowserDll (0100000000000803) RomFS, with nro/*.nro decompressed
  <out>/engine  -> romfs/nro/netfront/core_0/Default/cfi_nocfi  (--engine for wkc-loader-core0)
  <out>/engine3 -> romfs/nro/netfront/core_3/Default/cfi_nncfi  (--engine for wkc-loader-core3)
"""
import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from unlz4nro import decompress_file  # noqa: E402

BROWSER_DLL = "0x0100000000000803"
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


def find_title(nstool, keys, nca_dir, prog_id, content_type="Data"):
    for nca in sorted(pathlib.Path(nca_dir).glob("*.nca")):
        if nca.name.endswith(".cnmt.nca"):
            continue
        info = nca_info(nstool, keys, nca)
        if info.get("prog_id") == prog_id.lower() and info.get("type") == content_type:
            return nca
    return None


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

        nca = find_title(nstool, args.keys, nca_dir, BROWSER_DLL)
        if not nca:
            sys.exit(f"BrowserDll ({BROWSER_DLL}) not found; are the keys right for this firmware?")
        print(f"BrowserDll: {nca.name}")

        extract = pathlib.Path(tmp) / "browserdll"
        subprocess.run([nstool, "-k", args.keys, "-x", str(extract), str(nca)], check=True,
                       stdout=subprocess.DEVNULL)
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(extract / "0", out / "romfs")

    for lz4 in sorted((out / "romfs").rglob("*.nro.lz4")):
        decompress_file(lz4)
    for link, sub in ENGINE_SUBDIRS.items():
        (out / link).symlink_to(pathlib.Path("romfs") / sub)
    print(f"done: {out}")
    print(f"run:  qemu-aarch64-static loader/build/wkc-loader-core3 --engine {out}/engine3 --romfs {out}/romfs")


if __name__ == "__main__":
    main()
