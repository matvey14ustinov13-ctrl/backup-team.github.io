#!/usr/bin/env bash
# Build the Zynx ISO in WSL Arch with the project sources copied to ext4.
set -euo pipefail
SRC="/mnt/c/MATEYKA/OPENCODE AI"
DST=/root/zynx-src
BUILD_ROOT=/root/arch-amdgpu/.build
OUT_DIR=/root/arch-amdgpu/out

rm -rf "$DST"
mkdir -p "$DST"
rsync -a --delete --exclude out --exclude .build "$SRC/" "$DST/"

ls -l "$DST/installer/zynx-gui.c"
grep -n '^gtk3$\|^zsh$\|^fish$' "$DST/profile/packages.x86_64"
grep -n 'python' "$DST/profile/packages.x86_64" || true

cd "$DST"
BUILD_ROOT="$BUILD_ROOT" OUT_DIR="$OUT_DIR" AUTO_INSTALL=1 \
    bash ./build-arch-amdgpu-shell.sh
