# switch-browser

Runs the Nintendo Switch web browser engine on a desktop computer, so you can test how pages
render and which CSS and JavaScript features work without a console or a devkit.

The engine is NetFront Browser NX (Nintendo calls the browser "WKC"; it's built on WebKit). It
ships in the Switch firmware as a set of AArch64 modules in the BrowserDll title
(`0100000000000803`). This project loads those modules unmodified and provides everything they
expect from the console: the system services they call and the embedding application that drives
the engine. Pages render in software, so a headless desktop setup can screenshot them.

**This repository contains no Nintendo code, firmware or keys.** You supply them yourself: see
[Requirements](#requirements).

## What works

- Both engine generations shipped in BrowserDll:
  - **core_3**: NetFront 3.30.3, WebKit 613 (Safari 15 era). The current Switch browser
    reports `AppleWebKit/613`, so this is the default and the main compatibility target.
  - **core_0**: NetFront 0.30.0, WebKit 601 (Safari 9 era), an older generation that is also shipped.
- HTTP, HTTPS (TLS 1.3 on core_3) and DNS, using the Switch's own trusted root certificates.
- Text in the Switch system fonts, including Japanese, Korean and Chinese.
- The Switch's form control skin and system colours.
- The `-webkit-nintendo-switch-device-mode` media feature (docked or handheld).
- A real Switch user agent, `Intl` (ICU data), localStorage and sessionStorage (in memory only),
  session history, and console output (multiple arguments on core_3).
- PNG screenshots, and an interactive mode that shows the page in any desktop browser, with mouse,
  keyboard, scrolling, back/forward and a JavaScript console.
- Reporting when a page touches something that isn't implemented yet (see
  [Unimplemented features](#unimplemented-features)).

Known gaps:
- No media playback: `<video>` and `<audio>` fail as an unsupported format.
- No `<select>` dropdowns, clipboard or IME.
- No `alert`/`confirm`/`prompt` dialogs in the UI: alerts are logged and `confirm` returns true.
- Storage and cookies don't persist between runs.
- core_0 has no form skin, and its console messages carry only the first argument.

## How it works

```
┌────────────────────────────────────────────────────────────────────┐
│ Engine modules (from firmware, unmodified)                         │
│   webkit_wkc · oss_wkc · cairo_wkc · libfont · peer_wkc            │
├────────────────────────────────────────────────────────────────────┤
│ loader/src/host    the embedding app: WebView, clients, skin,      │
│                    storage, history, screenshots, web UI server    │
│ loader/src/shims   the console's system services: libc (musl),     │
│                    libc++, nn::os, nn::fs, nn::socket, nn::time    │
│ loader/src         NRO loader, dynamic linker, CFI gates           │
├────────────────────────────────────────────────────────────────────┤
│ Linux AArch64 (native, or qemu-aarch64 user-mode on x86-64)        │
└────────────────────────────────────────────────────────────────────┘
```

The loader is a static Linux AArch64 binary. It maps the engine's NRO modules, applies their
relocations and resolves their imports, in this order: to each other, then to the host shims, then
to trap stubs that name any missing import if it's ever called. On core_3 it also works around
Nintendo's control-flow integrity scheme (see `loader/src/cfi_gate.h`).

## Requirements

**From a Switch you own:**
- A firmware package (`Firmware.<version>.zip`, or a directory of its NCAs). 22.5.0 is the tested
  version; BrowserDll, the title containing the engine, last changed in 22.5.0.
- The matching `prod.keys`.

**From Nintendo's open source page:**
- The OSS package for the same version: `NintendoSwitch_OpenSources22.5.0.zip`, from
  <https://support.nintendo.com/jp/oss/>.
- It provides the LGPL WKC embedding headers the loader is built against.

**Host:** Linux, either AArch64 or x86-64 (WSL2 works).
- Python 3, `make`, `curl`.
- [nstool](https://github.com/jakcron/nstool) on `PATH`, to decrypt the firmware. v1.9.2 is tested;
  build it from source if the prebuilt binary needs a newer glibc than you have.
- **x86-64:** an AArch64 cross toolchain, binutils and static glibc, plus qemu user-mode emulation.
  On Debian/Ubuntu:
  ```bash
  sudo apt install g++-aarch64-linux-gnu qemu-user-static python3 make curl
  ```
- **AArch64 Linux:** the native `g++`, with static glibc and libstdc++ (`libc6-dev` on Debian/Ubuntu;
  `glibc-static` and `libstdc++-static` on Fedora). The loader runs natively, much faster than under
  emulation.
- **macOS, including Apple silicon:** the loader can't run on macOS directly. Use an arm64 Linux VM
  or container (Docker Desktop, OrbStack, Lima, UTM, ...), which runs it at native speed.
  - Run the interactive mode with `--listen-all` and forward its port, so the browser on the Mac can
    connect.
  - This setup hasn't been tested yet.
  - Kernels with 16 KiB memory pages (Asahi Linux, for example) are untested too. The engine
    modules were built for 4 KiB pages.

## Setup

1. **Put the inputs in a directory per firmware version** under `firmware/` (gitignored; never
   commit Nintendo files):
   ```
   firmware/22.5.0/Firmware.22.5.0.zip
   firmware/22.5.0/keys/prod.keys
   firmware/22.5.0/NintendoSwitch_OpenSources22.5.0.zip
   ```
2. **Prepare it.** In about 15 seconds this extracts the engine modules, RomFS, fonts and root
   certificates into `engine/`, and unpacks the OSS package into `oss/`.
   ```bash
   python3 -I scripts/prepare_firmware.py firmware/22.5.0
   ```
   - Steps whose output already exists are skipped; `--force` redoes them.
   - `--firmware`, `--keys` and `--oss` take inputs from elsewhere. `--firmware` also accepts a
     directory of NCAs.
3. **Fetch the build dependencies** into `deps/` (nothing is installed system-wide). This is a few
   internals from Ubuntu's arm64 libc++ package that the engine imports:
   ```bash
   scripts/fetch_deps.sh
   ```
4. **Build the loader**, for core_3 (the default) and optionally core_0. For another firmware
   version, add `FIRMWARE=../firmware/<version>`; the path is relative to `loader/`.
   ```bash
   make -C loader
   ```
   ```bash
   make -C loader CORE=0
   ```

## Usage

`scripts/wkc.sh` runs the loader with the engine, fonts and certificates filled in. It runs the
loader directly on AArch64 Linux and under `qemu-aarch64` elsewhere. Run it from a scratch
directory: the engine's writable data (cache, temp files) goes to `./nxb-data`.

**Interactive:** open <http://127.0.0.1:8000/> in any browser.
```bash
scripts/wkc.sh --serve 8000 --url https://example.com/
```
You get an address bar, back/forward, a docked/handheld toggle, screenshots and a console where you
can run JavaScript in the page.

**Command line:** load a page, then screenshot it and/or evaluate JavaScript in it.
```bash
scripts/wkc.sh --url https://example.com/ --screenshot out.png --eval "CSS.supports('display', 'grid')"
```
```bash
scripts/wkc.sh --html test.html --base-url https://example.com/ --eval document.title
```

| Option | |
|---|---|
| `--url URL` / `--html FILE` | page to load (`--base-url` gives an HTML file its document URL) |
| `--eval JS` | evaluate after loading and print the result (repeatable) |
| `--screenshot FILE.png` | save the rendered 1280x720 view |
| `--device-mode console\|handheld` | Switch device mode seen by CSS (default console, i.e. docked) |
| `--user-agent UA` | override the user agent (default: one observed on a real Switch) |
| `--language TAG` | `navigator.language` (default `en-US`) |
| `--timeout S` | seconds to wait for the load (default 30) |
| `--serve PORT`, `--listen-all` | interactive mode, on 127.0.0.1 or all interfaces |

| Environment | |
|---|---|
| `NXB_CORE=0\|3` | engine generation (default 3) |
| `NXB_FIRMWARE=DIR` | prepared firmware version directory (default `firmware/22.5.0`) |
| `NXB_DATA=DIR` | the engine's writable data (default `./nxb-data`) |
| `NXB_RUNNER=CMD` | run the loader with this command instead of the automatic choice; empty means run directly |
| `NXB_TIMEZONE=Area/City` | time zone seen by the page (default: the host's) |
| `NXB_DNS=IP` | DNS server (default: from `/etc/resolv.conf`) |
| `NXB_TRACE=1` | verbose logging |
| `NXB_STUBS=all` | also report the stubs that are expected to do nothing |
| `NXB_WATCHDOG=S` | print the engine stack whenever the main thread is busy for S seconds |

## Unimplemented features

Much of a browser lives in the embedding application rather than the engine: storage, history,
dialogs, popups, media playback. Where this project doesn't implement something yet, the engine
gets a stub that returns an empty answer and carries on as if the feature didn't exist.

To make that visible, the first call of each such stub is logged as `[stub] Name` (and shown in
the interactive console), and command-line runs end with a summary. Stubs where doing nothing is
correct, such as progress notifications, are counted but not shown; they're listed in
`NOOP_OK` in `scripts/gen_client_stubs.py`.

## Repository layout

| Path | |
|---|---|
| `loader/` | the loader and host (C++17), built with `make -C loader [CORE=0\|3]` |
| `frontend/` | the interactive web UI (plain HTML/CSS/JS, served by the loader) |
| `scripts/wkc.sh` | runs the loader |
| `scripts/prepare_firmware.py` | prepares a firmware version directory: engine, fonts, certificates, OSS headers |
| `scripts/fetch_deps.sh` | fetches the libc++ subset into `deps/` |
| `scripts/gen_*.py` | build-time code generators (client stubs, engine import trampolines) |
| `scripts/unlz4nro.py`, `nro2elf.py`, `nrosyms.py` | NRO tools: decompress, wrap as ELF for Ghidra, list symbols |
| `scripts/ui_smoke_test.py` | end-to-end test of the interactive mode |
| `scripts/echo_server.py` | local HTTP server that logs request headers |
| `scripts/ghidra/DecompileAt.java` | headless Ghidra script for decompiling engine code by offset |
| `AGENTS.md` | notes for contributors and coding agents: architecture, conventions, findings |

## License

The code in this repository is under the [MIT License](LICENSE).

Nintendo's firmware, keys and engine binaries are not included and must not be committed; use
files from hardware you own.

The build compiles against headers from Nintendo's open source package, which aren't in this
repository. The WKC embedding API headers are LGPL, and the JavaScriptCore API headers are BSD.
Prebuilt `wkc-loader` binaries also contain statically linked glibc (LGPL) and part of LLVM's
libc++ (Apache 2.0 with LLVM exception). Anyone distributing binaries rather than source has to
meet those licenses' terms, including the LGPL's relinking provisions.
