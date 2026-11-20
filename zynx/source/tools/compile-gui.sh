#!/usr/bin/env bash
# Compile the GTK installer from source (run inside Arch Linux / WSL).
set -euo pipefail
SRC="${1:-/mnt/c/MATEYKA/OPENCODE AI/installer/zynx-gui.c}"
OUT="${2:-/tmp/zynx-gui}"
# shellcheck disable=SC2046
gcc -Wall -Wextra -O2 -o "$OUT" "$SRC" $(pkg-config --cflags --libs gtk+-3.0)
printf 'Built %s\n' "$OUT"
