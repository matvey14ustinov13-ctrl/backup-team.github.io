#!/usr/bin/env bash
# ============================================================================
# build-arch-amdgpu-shell.sh
#
# One-shot builder for the Zynx Linux ISO:
#   * XFCE desktop on top of Arch Linux;
#   * the official Arch 'linux' kernel (linux-headers + DKMS work out of the
#     box, and external modules can be built normally);
#   * native AMDGPU support for RDNA4 / Radeon RX 9070 XT plus Intel/NVIDIA
#     fallback drivers;
#   * Firefox, LibreOffice, VLC/mpv, fonts, printing, Bluetooth, SSH, rsync,
#     tmux, UFW, multilib, archinstall and a graphical Zynx installer;
#   * Zynx-branded BIOS syslinux + UEFI systemd-boot;
#   * bash as the login shell on tty1 (no custom C++ shell anywhere).
#
# RUN ON ARCH LINUX x86_64, AS ROOT:
#   sudo bash build-arch-amdgpu-shell.sh
#
# Optional environment variables:
#   BUILD_ROOT=/path       scratch directory (default: ./.build)
#   OUT_DIR=/path          ISO output directory (default: ./out)
#   KEEP_ARCHISO_WORK=1    keep the mkarchiso work directory for debugging
#   AUTO_INSTALL=0         do not auto-install missing host packages
#   ISO_SIZE_LIMIT_MB=8192 only used for the final size warning
#
# NOTES
#   * The ISO is never padded. Its real size is printed at the end.
#   * The live medium auto-logs in as the unprivileged user "zynx" on XFCE
#     and as root on tty1.  The installer creates a normal user and asks for
#     an optional root password on the target system.
#   * Firefox is a regular pacman package, so `pacman -Syu` updates it.
# ============================================================================

set -Eeuo pipefail
shopt -s nullglob

export LC_ALL=C

# ---------------------------------------------------------------------------
# Paths and defaults
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd -P)"
PROJECT_DIR="$SCRIPT_DIR"
PROFILE_SRC="$PROJECT_DIR/profile"
SPLASH_SRC="$PROFILE_SRC/zynx-splash.txt"
ARCHISO_RELENG="${ARCHISO_RELENG:-/usr/share/archiso/configs/releng}"

BUILD_ROOT="${BUILD_ROOT:-$PROJECT_DIR/.build}"
OUT_DIR="${OUT_DIR:-$PROJECT_DIR/out}"
AUTO_INSTALL="${AUTO_INSTALL:-1}"
KEEP_ARCHISO_WORK="${KEEP_ARCHISO_WORK:-0}"
ISO_SIZE_LIMIT_MB="${ISO_SIZE_LIMIT_MB:-8192}"

BUILD_PROFILE="$BUILD_ROOT/profile"
ARCHISO_WORK="$BUILD_ROOT/archiso-work"
LOG_DIR="$BUILD_ROOT/logs"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
log()  { printf '\n[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }
warn() { printf '\n[%s] WARNING: %s\n' "$(date +%H:%M:%S)" "$*" >&2; }
die()  { printf '\n[ERROR] %s\n' "$*" >&2; exit 1; }

on_error() {
    local code=$?
    printf '\n[ERROR] command failed (exit %d) at line %s: %s\n' \
        "$code" "${1:-?}" "${BASH_COMMAND:-?}" >&2
}
trap 'on_error "$LINENO"' ERR

# ---------------------------------------------------------------------------
# Root and host checks
# ---------------------------------------------------------------------------
if (( EUID != 0 )); then
    if command -v sudo >/dev/null 2>&1; then
        log "Root privileges are required; re-executing with sudo..."
        exec sudo -E -- "$BASH" "$SCRIPT_DIR/$(basename -- "$0")" "$@"
    fi
    die "Run this script as root (or install sudo)."
fi

[[ -f /etc/arch-release ]] || die "This script must run on Arch Linux (pacman/archiso are required)."
command -v pacman >/dev/null 2>&1 || die "pacman not found."

# ---------------------------------------------------------------------------
# Host dependencies
# ---------------------------------------------------------------------------
REQUIRED_PKGS=(
    archiso
    cpio
    squashfs-tools
    libisoburn
    mtools
    dosfstools
    syslinux
    arch-install-scripts
    libarchive
    zstd
    xz
    rsync
    imagemagick
    ttf-dejavu
    fontconfig
    gcc
    pkgconf
    gtk3
)

package_installed() { pacman -Qq "$1" >/dev/null 2>&1; }
missing=()
for pkg in "${REQUIRED_PKGS[@]}"; do
    package_installed "$pkg" || missing+=("$pkg")
done

if ((${#missing[@]})); then
    mapfile -t missing < <(printf '%s\n' "${missing[@]}" | awk 'NF && !seen[$0]++')
    if [[ "$AUTO_INSTALL" != 1 ]]; then
        die "Missing host packages: ${missing[*]}. Install them or set AUTO_INSTALL=1."
    fi
    log "Installing missing host packages: ${missing[*]}"
    pacman -S --needed --noconfirm -- "${missing[@]}"
fi

if command -v convert >/dev/null 2>&1; then
    CONVERT=convert
elif command -v magick >/dev/null 2>&1; then
    CONVERT=magick
else
    die "ImageMagick (convert or magick) is required to generate the wallpaper."
fi

for cmd in \
    pacman mkarchiso bsdtar mksquashfs xorriso mkfs.fat mmd mcopy cpio \
    zstd xz fc-match; do
    command -v "$cmd" >/dev/null 2>&1 || die "Required command not found after dependency check: $cmd"
done

# ---------------------------------------------------------------------------
# Project validation
# ---------------------------------------------------------------------------
for file in \
    "$PROFILE_SRC/profiledef.sh" \
    "$PROFILE_SRC/packages.x86_64" \
    "$PROFILE_SRC/pacman.conf" \
    "$PROFILE_SRC/airootfs/etc/mkinitcpio.conf.d/zynx-live.conf" \
    "$PROFILE_SRC/airootfs/etc/mkinitcpio.conf.install" \
    "$PROFILE_SRC/airootfs/etc/sysctl.d/90-zynx-sysrq.conf" \
    "$PROFILE_SRC/airootfs/root/customize_airootfs.sh" \
    "$PROFILE_SRC/airootfs/usr/local/bin/zynx-install" \
    "$PROJECT_DIR/installer/zynx-gui.c" \
    "$PROFILE_SRC/airootfs/usr/local/bin/zynx-firstboot" \
    "$PROFILE_SRC/airootfs/usr/local/bin/zynx-update" \
    "$PROFILE_SRC/airootfs/usr/local/share/zynx/zynx-postinstall.sh" \
    "$PROFILE_SRC/syslinux/splash.png" \
    "$SPLASH_SRC"; do
    [[ -f "$file" ]] || die "Missing project file: $file"
done

for script in \
    "$PROFILE_SRC/airootfs/root/customize_airootfs.sh" \
    "$PROFILE_SRC/airootfs/usr/local/bin/zynx-install" \
    "$PROFILE_SRC/airootfs/usr/local/bin/zynx-firstboot" \
    "$PROFILE_SRC/airootfs/usr/local/bin/zynx-update" \
    "$PROFILE_SRC/airootfs/usr/local/share/zynx/zynx-postinstall.sh"; do
    bash -n "$script" || die "Syntax error in $script"
done

if ! grep -Eq '^[[:space:]]*gtk3[[:space:]]*$' "$PROFILE_SRC/packages.x86_64"; then
    die "packages.x86_64 must include gtk3 for the graphical installer."
fi
if grep -Eq '^[[:space:]]*python(-gobject)?[[:space:]]*$' "$PROFILE_SRC/packages.x86_64"; then
    die "packages.x86_64 must not install Python for the graphical installer."
fi

[[ -d "$ARCHISO_RELENG" && -f "$ARCHISO_RELENG/profiledef.sh" ]] || \
    die "archiso releng profile not found at $ARCHISO_RELENG. Install 'archiso' or set ARCHISO_RELENG."

# ---------------------------------------------------------------------------
# Disk space estimate (warning only)
# ---------------------------------------------------------------------------
mkdir -p "$BUILD_ROOT" "$LOG_DIR" "$OUT_DIR"
AVAIL_KB="$(df -Pk "$BUILD_ROOT" | awk 'NR == 2 {print $4}')"
if [[ -n "$AVAIL_KB" ]] && ((AVAIL_KB < 25 * 1024 * 1024)); then
    warn "Less than 25 GiB free in $BUILD_ROOT. The airootfs plus squashfs may need more."
fi

# ---------------------------------------------------------------------------
# Package list validation
# ---------------------------------------------------------------------------
# Validate against the profile's pacman.conf, not the host's.  The host
# configuration may not have [multilib] enabled, while the ISO does.
PACMAN_CONF="$PROFILE_SRC/pacman.conf"
PACMAN=(pacman --config "$PACMAN_CONF")
if ! "${PACMAN[@]}" -Si -- lib32-mesa >/dev/null 2>&1; then
    log "Synchronizing pacman databases with the profile configuration..."
    "${PACMAN[@]}" -Sy --noconfirm || \
        die "Unable to synchronize pacman databases using $PACMAN_CONF."
fi

PKG_LIST="$PROFILE_SRC/packages.x86_64"
mapfile -t PKGS < <(grep -vE '^[[:space:]]*(#|$)' "$PKG_LIST" | sed 's/[[:space:]]*$//')
((${#PKGS[@]})) || die "Package list is empty: $PKG_LIST"

duplicates="$(printf '%s\n' "${PKGS[@]}" | sort | uniq -d)"
[[ -z "$duplicates" ]] || die "Duplicate packages in packages.x86_64: $duplicates"

for pkg in "${PKGS[@]}"; do
    if ! "${PACMAN[@]}" -Si -- "$pkg" >/dev/null 2>&1; then
        if ! "${PACMAN[@]}" -Sg -- "$pkg" >/dev/null 2>&1; then
            die "Unknown package or group in packages.x86_64: $pkg"
        fi
    fi
done

for pkg in \
    base base-devel linux linux-headers linux-firmware mkinitcpio mkinitcpio-archiso \
    xfce4-session lightdm lightdm-gtk-greeter firefox libreoffice-fresh \
    networkmanager bluez openssh rsync tmux ufw dkms zsh fish; do
    grep -Fxq "$pkg" < <(printf '%s\n' "${PKGS[@]}") || \
        die "packages.x86_64 is missing required package: $pkg"
done
if grep -Eq '^linux-(zynx|custom|amdgpu)' "$PKG_LIST"; then
    die "packages.x86_64 still contains a custom kernel package; the Arch 'linux' package is the only kernel."
fi

PKG_COUNT=${#PKGS[@]}
LINUX_VERSION="$("${PACMAN[@]}" -Si -- linux | awk -F': ' '/^Version[[:space:]]*:/ {print $2; exit}')"
log "Zynx package set: $PKG_COUNT packages; Arch kernel: $LINUX_VERSION"

# ---------------------------------------------------------------------------
# Assemble the archiso profile: releng base + project overlay
# ---------------------------------------------------------------------------
log "Assembling the archiso profile..."
rm -rf "$BUILD_PROFILE"
mkdir -p "$BUILD_PROFILE"
cp -a "$ARCHISO_RELENG/." "$BUILD_PROFILE/"
cp -a "$PROFILE_SRC/." "$BUILD_PROFILE/"

# Let the 'filesystem' package create a complete passwd/shadow database.
# The old minimal image replaced these with a root-only file; a full desktop
# needs the system users and groups.
rm -f "$BUILD_PROFILE/airootfs/etc/passwd" "$BUILD_PROFILE/airootfs/etc/shadow"

# Drop releng files that conflict with this image.
rm -f "$BUILD_PROFILE/airootfs/etc/mkinitcpio.conf.d/archiso.conf"
rmdir "$BUILD_PROFILE/airootfs/etc/mkinitcpio.conf.d" 2>/dev/null || true
rm -f "$BUILD_PROFILE/airootfs/etc/ssh/sshd_config.d/10-archiso.conf"
rmdir "$BUILD_PROFILE/airootfs/etc/ssh/sshd_config.d" 2>/dev/null || true
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/network/20-ethernet.network"
rmdir "$BUILD_PROFILE/airootfs/etc/systemd/network" 2>/dev/null || true
rm -f "$BUILD_PROFILE/airootfs/root/.automated_script.sh" \
      "$BUILD_PROFILE/airootfs/root/.zlogin"
rm -rf "$BUILD_PROFILE/airootfs/root/.gnupg"
rm -f "$BUILD_PROFILE/airootfs/usr/local/bin/Installation_guide" \
      "$BUILD_PROFILE/airootfs/usr/local/bin/choose-mirror" \
      "$BUILD_PROFILE/airootfs/usr/local/bin/livecd-sound"
rm -rf "$BUILD_PROFILE/airootfs/usr/local/share/livecd-sound"

# Do not start live-only or unrelated services.
for unit in \
    ModemManager.service choose-mirror.service hv_fcopy_daemon.service \
    hv_kvp_daemon.service hv_vss_daemon.service iwd.service \
    livecd-talk.service sshd.service systemd-networkd.service \
    vboxservice.service vmtoolsd.service vmware-vmblock-fuse.service; do
    rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/multi-user.target.wants/$unit"
done
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/sockets.target.wants/systemd-networkd.socket"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/sockets.target.wants/pcscd.socket"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/sound.target.wants/livecd-alsa-unmuter.service"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/network-online.target.wants/systemd-networkd-wait-online.service"
rm -rf "$BUILD_PROFILE/airootfs/etc/systemd/system/systemd-networkd-wait-online.service.d"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/dbus-org.freedesktop.network1.service"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/dbus-org.freedesktop.ModemManager1.service"
rm -rf "$BUILD_PROFILE/airootfs/etc/systemd/system/cloud-init.target.wants"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/choose-mirror.service"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/livecd-talk.service"
rm -f "$BUILD_PROFILE/airootfs/etc/systemd/system/livecd-alsa-unmuter.service"

# ---------------------------------------------------------------------------
# Zynx branding: boot menu titles and the orange console splash
# ---------------------------------------------------------------------------
log "Applying Zynx branding to the boot menus..."
while IFS= read -r -d '' file; do
    sed -i 's/Arch Linux/Zynx Linux/g; s/install medium/live medium/g' "$file"
done < <(find "$BUILD_PROFILE/efiboot" "$BUILD_PROFILE/syslinux" \
    "$BUILD_PROFILE/grub" "$BUILD_PROFILE/boot/grub" -type f \
    \( -name '*.conf' -o -name '*.cfg' \) -print0 2>/dev/null || true)

log "Generating the orange tty1 splash..."
# The 'filesystem' package owns /etc/issue and installs its own copy during
# pacstrap, so keep the Zynx splash in an unowned file as well and let
# customize_airootfs.sh restore it after the packages are installed.
ISSUE_FILE="$BUILD_PROFILE/airootfs/etc/issue.zynx"
install -d -m 0755 "$(dirname -- "$ISSUE_FILE")"
{
    printf '\033[2J\033[H'
    printf '\033[33m'
    cat "$SPLASH_SRC"
    printf '\033[0m\n'
    printf '\033[1;33m             Zynx Linux\033[0m\n'
    printf '\033[0;37m   Arch-based  |  XFCE  |  AMD Radeon ready\033[0m\n'
    printf '\033[1;31m   WARNING: the live tty1 root shell has no password.\033[0m\n'
    printf '\033[0;37m   Use this medium only on trusted hardware.\033[0m\n\n'
} >"$ISSUE_FILE"
cp -f "$ISSUE_FILE" "$BUILD_PROFILE/airootfs/etc/issue"
chmod 0644 "$ISSUE_FILE" "$BUILD_PROFILE/airootfs/etc/issue"

# ---------------------------------------------------------------------------
# Orange desktop wallpaper, generated from the same splash art
# ---------------------------------------------------------------------------
log "Generating the Zynx wallpaper..."
WALLPAPER_DIR="$BUILD_PROFILE/airootfs/usr/share/backgrounds/zynx"
install -d -m 0755 "$WALLPAPER_DIR"
FONT_BOLD="$(fc-match -f '%{file}\n' 'DejaVu Sans:style=Bold' 2>/dev/null | head -n1 || true)"
FONT_REG="$(fc-match -f '%{file}\n' 'DejaVu Sans' 2>/dev/null | head -n1 || true)"
[[ -n "$FONT_BOLD" && -f "$FONT_BOLD" ]] || FONT_BOLD="DejaVu-Sans-Bold"
[[ -n "$FONT_REG" && -f "$FONT_REG" ]] || FONT_REG="DejaVu-Sans"

"$CONVERT" -size 1920x1080 xc:'#0b0704' \
    \( "$PROFILE_SRC/syslinux/splash.png" -fuzz 2% -transparent black -resize 760x570 \) \
    -gravity center -geometry +0-120 -composite \
    -font "$FONT_BOLD" -pointsize 78 -fill '#ff8c00' \
    -gravity center -annotate +0+238 'ZYNX LINUX' \
    -font "$FONT_REG" -pointsize 27 -fill '#e8c9a0' \
    -gravity center -annotate +0+305 'Arch-based  •  XFCE  •  AMD Radeon ready' \
    "$WALLPAPER_DIR/zynx-wallpaper.png"
chmod 0644 "$WALLPAPER_DIR/zynx-wallpaper.png"

# ---------------------------------------------------------------------------
# Compile the graphical installer (C + GTK3, one binary, no Python).
# ---------------------------------------------------------------------------
log "Compiling the Zynx installer (C + GTK3)..."
command -v pkg-config >/dev/null 2>&1 || die "pkg-config is required to compile zynx-gui."
GUI_BIN="$BUILD_PROFILE/airootfs/usr/local/bin/zynx-gui"
install -d -m 0755 "$(dirname -- "$GUI_BIN")"
# shellcheck disable=SC2046
if ! gcc -Wall -Wextra -O2 -o "$GUI_BIN" "$PROJECT_DIR/installer/zynx-gui.c" \
    $(pkg-config --cflags --libs gtk+-3.0) 2>&1 | tee "$LOG_DIR/compile-gui.log"; then
    die "Compiling installer/zynx-gui.c failed. See $LOG_DIR/compile-gui.log"
fi
[[ -x "$GUI_BIN" ]] || die "zynx-gui was not produced by the compiler."
chmod 0755 "$GUI_BIN"

# ---------------------------------------------------------------------------
# Normalize modes for the files created from the Windows build host
# ---------------------------------------------------------------------------
chmod 0755 "$BUILD_PROFILE/airootfs/root/customize_airootfs.sh" \
    "$BUILD_PROFILE/airootfs/usr/local/bin/zynx-install" \
    "$BUILD_PROFILE/airootfs/usr/local/bin/zynx-gui" \
    "$BUILD_PROFILE/airootfs/usr/local/bin/zynx-firstboot" \
    "$BUILD_PROFILE/airootfs/usr/local/bin/zynx-update" \
    "$BUILD_PROFILE/airootfs/usr/local/share/zynx/zynx-postinstall.sh"
chmod 0440 "$BUILD_PROFILE/airootfs/etc/sudoers.d/10-zynx-live"
chmod 0644 "$BUILD_PROFILE/airootfs/etc/issue.zynx" \
    "$BUILD_PROFILE/airootfs/etc/issue" \
    "$BUILD_PROFILE/airootfs/etc/os-release" \
    "$BUILD_PROFILE/airootfs/etc/mkinitcpio.conf.d/zynx-live.conf" \
    "$BUILD_PROFILE/airootfs/etc/lightdm/lightdm.conf.d/50-zynx-autologin.conf" \
    "$BUILD_PROFILE/airootfs/etc/lightdm/lightdm-gtk-greeter.conf" \
    "$BUILD_PROFILE/airootfs/etc/xdg/autostart/zynx-firstboot.desktop" \
    "$BUILD_PROFILE/airootfs/usr/share/applications/zynx-install.desktop" \
    "$BUILD_PROFILE/airootfs/usr/share/applications/zynx-update.desktop"

# ---------------------------------------------------------------------------
# Validate the final profile
# ---------------------------------------------------------------------------
grep -Eq '^iso_label="ZYNX"' "$BUILD_PROFILE/profiledef.sh" || \
    die "profiledef.sh must set iso_label=\"ZYNX\"."
grep -Eq '^iso_name="zynx"' "$BUILD_PROFILE/profiledef.sh" || \
    die "profiledef.sh must set iso_name=\"zynx\"."
grep -Eq '^\[multilib\]' "$BUILD_PROFILE/pacman.conf" || \
    die "pacman.conf must enable [multilib]."
LIVE_INITRAMFS_CONF="$BUILD_PROFILE/airootfs/etc/mkinitcpio.conf.d/zynx-live.conf"
grep -Eq '^HOOKS=.*[[:space:](]archiso[[:space:])]' "$LIVE_INITRAMFS_CONF" || \
    die "The live mkinitcpio drop-in must include the archiso hook."
if grep -Eq '^HOOKS=.*archiso' "$BUILD_PROFILE/airootfs/etc/mkinitcpio.conf.install"; then
    die "mkinitcpio.conf.install must not contain archiso hooks."
fi
grep -Eq '^MODULES=.*amdgpu' "$LIVE_INITRAMFS_CONF" || \
    die "The live mkinitcpio drop-in must load amdgpu."

# ---------------------------------------------------------------------------
# Build the ISO
# ---------------------------------------------------------------------------
if [[ "${VALIDATE_ONLY:-0}" == 1 ]]; then
    log "VALIDATE_ONLY=1: profile validated and zynx-gui compiled; skipping mkarchiso."
    exit 0
fi

log "Running mkarchiso (this takes a while)..."
rm -rf "$ARCHISO_WORK"
MKARCHISO_OPTS=(-v -w "$ARCHISO_WORK" -o "$OUT_DIR")
if [[ "$KEEP_ARCHISO_WORK" != 1 ]]; then
    MKARCHISO_OPTS+=(-r)
fi
if ! mkarchiso "${MKARCHISO_OPTS[@]}" "$BUILD_PROFILE" 2>&1 | tee "$LOG_DIR/mkarchiso.log"; then
    die "mkarchiso failed. See $LOG_DIR/mkarchiso.log"
fi

ISO_PATH="$(find "$OUT_DIR" -maxdepth 1 -type f -name '*.iso' -printf '%T@ %p\n' \
    | sort -n | tail -n 1 | cut -d' ' -f2-)"
[[ -n "$ISO_PATH" && -f "$ISO_PATH" ]] || die "mkarchiso finished but no ISO was found in $OUT_DIR."

SIZE_BYTES="$(stat -c %s "$ISO_PATH")"
SIZE_MIB=$((SIZE_BYTES / 1024 / 1024))
SIZE_GIB="$(awk -v b="$SIZE_BYTES" 'BEGIN {printf "%.2f", b / 1024 / 1024 / 1024}')"

sha256sum "$ISO_PATH" >"$ISO_PATH.sha256"

printf '\n============================================================\n'
printf 'Zynx Linux ISO: %s\n' "$ISO_PATH"
printf 'Size:   %s MiB (%s GiB)\n' "$SIZE_MIB" "$SIZE_GIB"
printf 'SHA256: %s\n' "$(cut -d' ' -f1 "$ISO_PATH.sha256")"
printf 'Kernel: %s (official Arch linux package)\n' "$LINUX_VERSION"
printf 'Packages: %s\n' "$PKG_COUNT"
printf '============================================================\n'

if ((SIZE_BYTES > ISO_SIZE_LIMIT_MB * 1024 * 1024)); then
    warn "The ISO is larger than ${ISO_SIZE_LIMIT_MB} MiB. It was not padded; remove packages if you need it smaller."
fi

printf '\nDone. Write the ISO to a USB drive with:\n'
printf '  sudo dd if=%q of=/dev/sdX bs=8M status=progress oflag=sync\n\n' "$ISO_PATH"
