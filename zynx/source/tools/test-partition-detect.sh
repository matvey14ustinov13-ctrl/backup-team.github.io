#!/usr/bin/env bash
# Offline check of the partition detection helpers from zynx-install.
set -euo pipefail
ENGINE="/mnt/c/MATEYKA/OPENCODE AI/profile/airootfs/usr/local/bin/zynx-install"

find_root_partition() { :; }
find_esp_partition() { :; }
eval "$(awk '/^find_root_partition\(\) \{/,/^\}/' "$ENGINE")"
eval "$(awk '/^find_esp_partition\(\) \{/,/^\}/' "$ENGINE")"

IMG=$(mktemp /tmp/zynx-disk-XXXX.img)
truncate -s 128M "$IMG"
LOOP=$(losetup -fP --show "$IMG")
trap 'umount -R /tmp/zynx-mnt 2>/dev/null || true; losetup -d "$LOOP" 2>/dev/null || true; rm -f "$IMG"' EXIT

sfdisk --wipe always "$LOOP" >/dev/null <<'EOF'
label: gpt
,48M,L
,,U
EOF
partprobe "$LOOP" 2>/dev/null || true
sleep 1
mkfs.ext4 -q -F "${LOOP}p1"
mkfs.fat -F32 "${LOOP}p2" >/dev/null
sleep 0.5
lsblk -nrbp -o PATH,FSTYPE,SIZE "$LOOP"

ROOT=$(find_root_partition "$LOOP")
ESP=$(find_esp_partition "$LOOP")
echo "root=$ROOT esp=$ESP"
[[ "$ROOT" == "${LOOP}p1" ]] || { echo "FAIL: root detection"; exit 1; }
[[ "$ESP" == "${LOOP}p2" ]] || { echo "FAIL: esp detection"; exit 1; }
echo "PASS"
