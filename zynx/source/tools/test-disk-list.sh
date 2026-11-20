#!/usr/bin/env bash
# Offline check for the pure-bash helpers in zynx-install.
set -euo pipefail
ENGINE="/mnt/c/MATEYKA/OPENCODE AI/profile/airootfs/usr/local/bin/zynx-install"

# The installer must not depend on an interpreter for disk enumeration.
if grep -Eq 'python3?|perl|ruby' "$ENGINE"; then
    echo "FAIL: zynx-install contains an interpreter dependency"
    grep -nE 'python3?|perl|ruby' "$ENGINE"
    exit 1
fi

human_size() { :; }
list_disks_tsv() { :; }
validate_options() { :; }
eval "$(awk '/^human_size\(\) \{/,/^\}/' "$ENGINE")"
eval "$(awk '/^list_disks_tsv\(\) \{/,/^\}/' "$ENGINE")"
eval "$(awk '/^validate_options\(\) \{/,/^\}/' "$ENGINE")"

[[ "$(human_size 0)" == "0 B" ]] || { echo "FAIL human_size(0)"; exit 1; }
[[ "$(human_size 1048576)" == "1.0 MiB" ]] || { echo "FAIL human_size(1M)"; exit 1; }
[[ "$(human_size 42949672960)" == "40.0 GiB" ]] || { echo "FAIL human_size(40G)"; exit 1; }

mapfile -t rows < <(list_disks_tsv)
((${#rows[@]})) || { echo "FAIL: no disks listed"; exit 1; }
first="${rows[0]}"
[[ "$first" == /dev/* ]] || { echo "FAIL: row is not a device: $first"; exit 1; }
IFS=$'\t' read -r dev size model tran <<<"$first"
[[ -n "$dev" && -n "$size" ]] || { echo "FAIL: empty fields: $first"; exit 1; }
printf 'first disk: %s\t%s\t%s\t%s\n' "$dev" "$size" "$model" "$tran"

EDITION=full SHELL_BIN=bash ROOT_GIB=40 SWAP_GIB=4 validate_options
EDITION=bogus; if (EDITION=bogus; validate_options) 2>/dev/null; then
    echo "FAIL: bogus edition accepted"; exit 1
fi
(EDITION=console; SHELL_BIN=zsh; ROOT_GIB=20; SWAP_GIB=8; validate_options) || \
    { echo "FAIL: valid options rejected"; exit 1; }
echo PASS
