#!/usr/bin/env bash
# Verify the built ISO: sha256, partitions/boot layout, squashfs contents.
set -euo pipefail
ISO="${1:-/root/arch-amdgpu/out/zynx-2026.11.19-x86_64.iso}"
MNT=/tmp/zynx-iso-mnt
SFS=/tmp/zynx-sfs-mnt

echo "== file =="
ls -l "$ISO"
stat -c '%s bytes' "$ISO"

echo "== sha256 =="
(cd "$(dirname "$ISO")" && sha256sum -c "$(basename "$ISO").sha256")

echo "== boot files =="
xorriso -indev "$ISO" -find / -maxdepth 3 -type f 2>/dev/null | \
    grep -E '/(vmlinuz|initramfs|efi|syslinux|arch)' | head -20 || true

echo "== extract squashfs =="
rm -rf "$MNT" "$SFS"
mkdir -p "$MNT" "$SFS"
LOOP=$(losetup -f --show -r "$ISO")
trap 'umount "$SFS" 2>/dev/null || true; umount "$MNT" 2>/dev/null || true; losetup -d "$LOOP" 2>/dev/null || true; rmdir "$SFS" "$MNT" 2>/dev/null || true' EXIT
mount -t iso9660 -o ro "$LOOP" "$MNT"
ls "$MNT"
ls "$MNT/arch/x86_64/"

mount -t squashfs -o loop,ro "$MNT/arch/x86_64/airootfs.sfs" "$SFS"
echo "== installer binary =="
file "$SFS/usr/local/bin/zynx-gui"
ls -l "$SFS/usr/local/bin/zynx-gui"
test -x "$SFS/usr/local/bin/zynx-gui" && echo "zynx-gui executable: yes"

echo "== sysrq live config =="
cat "$SFS/etc/sysctl.d/90-zynx-sysrq.conf"

echo "== shells =="
ls -l "$SFS/usr/bin/bash" "$SFS/usr/bin/zsh" "$SFS/usr/bin/fish"

echo "== installer engine =="
head -1 "$SFS/usr/local/bin/zynx-install"
grep -c 'python' "$SFS/usr/local/bin/zynx-install" || true

echo "== locale / keyboard =="
cat "$SFS/etc/locale.conf"
cat "$SFS/etc/vconsole.conf"

echo "OK"
