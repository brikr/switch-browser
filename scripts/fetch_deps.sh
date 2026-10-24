#!/usr/bin/env bash
# Fetches build dependencies into deps/ (gitignored). Nothing is installed system-wide.
#
# libc++ (aarch64): core_3 imports some libc++ (std::__1) internals: string append/resize,
# shared_ptr control-block functions, atomic wait/notify, std::sort instantiations. They are
# taken from Ubuntu's arm64 libc++-14-dev package: the .deb is only unpacked (dpkg-deb -x), and
# the few archive members needed are combined into one object with every symbol hidden except the
# ones the loader binds, so nothing else in that library can collide with glibc/libstdc++.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS="$ROOT/deps"
CROSS="${CROSS:-aarch64-linux-gnu-}"

DEB_URL="http://ports.ubuntu.com/ubuntu-ports/pool/universe/l/llvm-toolchain-14/libc++-14-dev_14.0.0-1ubuntu1.1_arm64.deb"
DEB="$DEPS/download/$(basename "$DEB_URL")"
PKG="$DEPS/libcxx-14-arm64"
OUT="$DEPS/libcxx_subset.o"

# Archive members to take, and the symbols to keep global (bound in loader/src/shims/libcxx_prebuilt.cpp).
MEMBERS=(string.cpp.o memory.cpp.o atomic.cpp.o algorithm.cpp.o charconv.cpp.o d2fixed.cpp.o d2s.cpp.o f2s.cpp.o
         stdexcept.cpp.o stdlib_exception.cpp.o)
KEEP=(
  _ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6appendEPKcm
  _ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6resizeEmc
  _ZNKSt3__119__shared_weak_count13__get_deleterERKSt9type_info
  _ZNSt3__119__shared_weak_count14__release_weakEv
  _ZNSt3__119__shared_weak_count4lockEv
  _ZNSt3__119__shared_weak_countD2Ev
  _ZNSt3__120__libcpp_atomic_waitEPVKvi
  _ZNSt3__123__cxx_atomic_notify_allEPVKv
  _ZNSt3__123__libcpp_atomic_monitorEPVKv
  _ZNSt3__16__sortIRNS_6__lessIaaEEPaEEvT0_S5_T_
  _ZNSt3__16__sortIRNS_6__lessIhhEEPhEEvT0_S5_T_
  _ZNSt3__16__sortIRNS_6__lessIssEEPsEEvT0_S5_T_
  _ZNSt3__16__sortIRNS_6__lessIttEEPtEEvT0_S5_T_
  _ZNSt3__16__sortIRNS_6__lessIllEEPlEEvT0_S5_T_
  _ZNSt3__16__sortIRNS_6__lessImmEEPmEEvT0_S5_T_
  _ZNSt3__16__sortIRNS_6__lessIddEEPdEEvT0_S5_T_
)

mkdir -p "$DEPS/download"
[ -f "$DEB" ] || curl -fL -o "$DEB" "$DEB_URL"
rm -rf "$PKG" && dpkg-deb -x "$DEB" "$PKG"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
(cd "$WORK" && "${CROSS}ar" x "$PKG/usr/lib/llvm-14/lib/libc++.a" "${MEMBERS[@]}")
"${CROSS}ld" -r -o "$WORK/subset.o" "${MEMBERS[@]/#/$WORK/}"
printf '%s\n' "${KEEP[@]}" > "$WORK/keep.txt"
"${CROSS}objcopy" --keep-global-symbols="$WORK/keep.txt" "$WORK/subset.o" "$OUT"

missing=0
for s in "${KEEP[@]}"; do
  "${CROSS}nm" --defined-only "$OUT" | grep -q " $s$" || { echo "missing: $s"; missing=1; }
done
[ "$missing" = 0 ] || exit 1
echo "wrote $OUT"
