#!/usr/bin/env bash
# Build the release ZIP(s) for GitHub: ISO + checksum + English/Russian guide.
#
# GitHub limits a single release asset to 2 GiB, so the archive is created as
# a split ZIP (zynx-1.0-release.z01 + zynx-1.0-release.zip).  The ISO itself is
# stored uncompressed (ZIP -0) because it already contains an xz squashfs.
#
# Run inside Arch Linux (WSL) as root:
#   bash tools/package-release.sh [ISO_PATH]
set -euo pipefail
PROJ="/mnt/c/MATEYKA/OPENCODE AI"
OUT_DIR="${OUT_DIR:-/root/zynx-release}"
ISO="${1:-$(ls -t /root/arch-amdgpu/out/zynx-*-x86_64.iso | head -1)}"
PART_SIZE="${PART_SIZE:-1300m}"
NAME="zynx-1.0-release"

[[ -f "$ISO" ]] || { echo "ISO not found: $ISO" >&2; exit 1; }
ISO_BASE=$(basename "$ISO")

echo "== ISO =="
ls -l "$ISO"

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/pkg" "$OUT_DIR/out"
PKG="$OUT_DIR/pkg"

echo "== staging files =="
cp --reflink=auto "$ISO" "$PKG/$ISO_BASE"
(cd "$PKG" && sha256sum "$ISO_BASE" > "$ISO_BASE.sha256")
cp "$PROJ/release/README.md" "$PKG/README.md"
cp "$PROJ/release/INSTALL.txt" "$PKG/INSTALL.txt"
ls -l "$PKG"

echo "== creating split ZIP ($PART_SIZE parts) =="
(cd "$PKG" && zip -0 -s "$PART_SIZE" -r "$OUT_DIR/out/$NAME.zip" \
    "$ISO_BASE" "$ISO_BASE.sha256" README.md INSTALL.txt >/dev/null)

echo "== parts =="
ls -l "$OUT_DIR/out"
(cd "$OUT_DIR/out" && sha256sum ./* > SHA256SUMS-parts.txt && cat SHA256SUMS-parts.txt)

echo "== verify the archive is intact (rejoin) =="
mkdir -p "$OUT_DIR/check"
(cd "$OUT_DIR/out" && zip -s 0 "$NAME.zip" --out "$OUT_DIR/check/$NAME-full.zip" >/dev/null)
(cd "$OUT_DIR/check" && unzip -t "$NAME-full.zip" | tail -3)
rm -rf "$OUT_DIR/check"

echo
echo "Release archive ready in $OUT_DIR/out:"
echo "  $NAME.z01 ... $NAME.zip  (split ZIP, extract with 7-Zip or 'zip -s 0')"
