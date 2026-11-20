#!/usr/bin/env bash
# Offline check of partition_disk(): four partitions, ext4 with 1% reserved
# blocks, separate /home mounted at $MOUNT/home.
set -euo pipefail
ENGINE="/mnt/c/MATEYKA/OPENCODE AI/profile/airootfs/usr/local/bin/zynx-install"
MOUNT=/tmp/zynx-fmt
DISK=""
ROOT_GIB=1
ROOT_PART=""
ESP_PART=""
HOME_PART=""
log() { printf '[test] %s\n' "$*"; }
die() { printf '[test] ERROR: %s\n' "$*" >&2; exit 1; }

for fn in part_dev wait_partition partition_disk; do
    eval "$(awk -v n="$fn" '$0 ~ "^" n "\\(\\) \\{" {p=1} p {print} p && /^}/ {exit}' "$ENGINE")"
done

IMG=$(mktemp /tmp/zynx-fmt-XXXX.img)
truncate -s 3G "$IMG"
LOOP=$(losetup -fP --show "$IMG")
mkdir -p "$MOUNT"
cleanup() {
    umount -R "$MOUNT" 2>/dev/null || true
    losetup -d "$LOOP" 2>/dev/null || true
    rm -f "$IMG"
}
trap cleanup EXIT
DISK="$LOOP"

partition_disk

[[ "$(lsblk -nlo NAME,TYPE "$LOOP" | awk '$2 == "part"' | wc -l)" == "4" ]] || {
    echo "FAIL: expected 4 partitions"; lsblk "$LOOP"; exit 1;
}
[[ "$(lsblk -ndo PTTYPE "$LOOP")" == "gpt" ]] || { echo "FAIL: not GPT"; exit 1; }
[[ "$(blkid -s LABEL -o value "$ROOT_PART")" == "ZYNX_ROOT" ]] || { echo "FAIL: root label"; exit 1; }
[[ "$(blkid -s LABEL -o value "$HOME_PART")" == "ZYNX_HOME" ]] || { echo "FAIL: home label"; exit 1; }
mountpoint -q "$MOUNT/home" || { echo "FAIL: /home not mounted"; exit 1; }
mountpoint -q "$MOUNT/boot/efi" || { echo "FAIL: ESP not mounted"; exit 1; }

TOTAL=$(tune2fs -l "$ROOT_PART" | awk -F': *' '/^Block count/ {print $2}')
RESERVED=$(tune2fs -l "$ROOT_PART" | awk -F': *' '/^Reserved block count/ {print $2}')
PERCENT=$(awk -v r="$RESERVED" -v t="$TOTAL" 'BEGIN {printf "%.2f", (r / t) * 100}')
awk -v p="$PERCENT" 'BEGIN {exit !(p < 2.0)}' || {
    echo "FAIL: reserved blocks ${PERCENT}% (expected about 1%)"; exit 1;
}

findmnt -n -o TARGET,SOURCE,FSTYPE "$MOUNT"
findmnt -n -o TARGET,SOURCE,FSTYPE "$MOUNT/home"
findmnt -n -o TARGET,SOURCE,FSTYPE "$MOUNT/boot/efi"
echo PASS
