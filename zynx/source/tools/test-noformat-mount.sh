#!/usr/bin/env bash
# Offline check of prepare_existing() (no-format mode) on a loop device.
set -euo pipefail
ENGINE="/mnt/c/MATEYKA/OPENCODE AI/profile/airootfs/usr/local/bin/zynx-install"
MOUNT=/tmp/zynx-mnt
DISK=""
ROOT_PART=""
ESP_PART=""
log() { printf '[test] %s\n' "$*"; }
die() { printf '[test] ERROR: %s\n' "$*" >&2; exit 1; }

for fn in find_root_partition find_esp_partition fstab_mount prepare_existing; do
    eval "$(awk -v n="$fn" '$0 ~ "^" n "\\(\\) \\{" {p=1} p {print} p && /^}/ {exit}' "$ENGINE")"
done

IMG=$(mktemp /tmp/zynx-disk-XXXX.img)
truncate -s 256M "$IMG"
LOOP=$(losetup -fP --show "$IMG")
mkdir -p "$MOUNT"
cleanup() {
    umount -R "$MOUNT" 2>/dev/null || true
    losetup -d "$LOOP" 2>/dev/null || true
    rm -f "$IMG"
}
trap cleanup EXIT

sfdisk --wipe always "$LOOP" >/dev/null <<'EOF'
label: gpt
,64M,L
,64M,L
,,U
EOF
partprobe "$LOOP" 2>/dev/null || true
sleep 1
mkfs.ext4 -q -F -L ROOT "${LOOP}p1"
mkfs.ext4 -q -F -L BOOT "${LOOP}p2"
mkfs.fat -F32 "${LOOP}p3" >/dev/null

ROOT_UUID=$(blkid -s UUID -o value "${LOOP}p1")
BOOT_UUID=$(blkid -s UUID -o value "${LOOP}p2")
ESP_UUID=$(blkid -s UUID -o value "${LOOP}p3")

mount "${LOOP}p1" "$MOUNT"
mkdir -p "$MOUNT/etc" "$MOUNT/boot"
cat >"$MOUNT/etc/fstab" <<EOF
UUID=$ROOT_UUID / ext4 rw 0 1
UUID=$BOOT_UUID /boot ext4 rw 0 2
UUID=$ESP_UUID /boot/efi vfat rw 0 2
EOF
umount "$MOUNT"

DISK="$LOOP"
prepare_existing

findmnt -n -o TARGET,SOURCE "$MOUNT"
findmnt -n -o TARGET,SOURCE "$MOUNT/boot"
findmnt -n -o TARGET,SOURCE "$MOUNT/boot/efi"
[[ "$ESP_PART" == "${LOOP}p3" ]] || { echo "FAIL esp=$ESP_PART"; exit 1; }
[[ "$ROOT_PART" == "${LOOP}p1" ]] || { echo "FAIL root=$ROOT_PART"; exit 1; }
mountpoint -q "$MOUNT/boot" || { echo "FAIL /boot not mounted"; exit 1; }
mountpoint -q "$MOUNT/boot/efi" || { echo "FAIL esp not mounted"; exit 1; }
echo PASS
