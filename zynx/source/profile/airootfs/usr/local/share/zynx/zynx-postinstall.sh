#!/usr/bin/env bash
# ============================================================================
# zynx-postinstall - runs inside the target chroot.
# The live installer copies this file to /root/zynx-postinstall.sh and runs it
# with arch-chroot.  All values come from /root/zynx-install.env.
# ============================================================================
set -Eeuo pipefail
export LC_ALL=C
umask 022

ENV_FILE=/root/zynx-install.env
[[ -f "$ENV_FILE" ]] || { echo "missing $ENV_FILE" >&2; exit 1; }
# shellcheck disable=SC1090
source "$ENV_FILE"

: "${FORMAT:=0}"
: "${ESP_PART:=}"
: "${HOME_PART:=}"
: "${EDITION:=full}"
: "${SHELL_BIN:=bash}"
: "${SWAP_GIB:=4}"
: "${DEFAULT_BOOT:=1}"

log() { printf '[zynx-target] %s\n' "$*"; }
warn() { printf '[zynx-target] WARNING: %s\n' "$*" >&2; }
die() { printf '[zynx-target] ERROR: %s\n' "$*" >&2; exit 1; }

case "$EDITION" in
    full|minimal|console) ;;
    *) die "Unknown edition in $ENV_FILE: $EDITION" ;;
esac
case "$SHELL_BIN" in
    bash) SHELL_PATH=/usr/bin/bash ;;
    zsh)  SHELL_PATH=/usr/bin/zsh ;;
    fish) SHELL_PATH=/usr/bin/fish ;;
    *) die "Unknown login shell in $ENV_FILE: $SHELL_BIN" ;;
esac
[[ -x "$SHELL_PATH" ]] || die "The login shell $SHELL_BIN is not installed ($SHELL_PATH)."
[[ "$SWAP_GIB" =~ ^[0-9]+$ ]] || die "Invalid swap size in $ENV_FILE: $SWAP_GIB"

# Remove optional packages from the image-copy install.  A missing package is
# not an error (the live image may not contain every optional application).
remove_pkgs() {
    local pkg
    local -a installed=()
    for pkg in "$@"; do
        pacman -Qq "$pkg" >/dev/null 2>&1 && installed+=("$pkg")
    done
    ((${#installed[@]})) || return 0
    pacman -R --noconfirm --nosave -- "${installed[@]}" >/dev/null 2>&1 || \
        pacman -Rdd --noconfirm -- "${installed[@]}" >/dev/null 2>&1 || \
        warn "Could not remove: ${installed[*]}"
}

log "Configuring installed system on $TARGET_DISK (edition=$EDITION shell=$SHELL_BIN)"

# ---------------------------------------------------------------------------
# Remove live-only units and the live marker.
# ---------------------------------------------------------------------------
rm -f /etc/zynx-live
rm -f /etc/systemd/system/etc-pacman.d-gnupg.mount
rm -f /etc/systemd/system/pacman-init.service
rm -f /etc/systemd/system/multi-user.target.wants/pacman-init.service
rm -f /etc/ssh/sshd_config.d/10-archiso.conf 2>/dev/null || true
rm -rf /etc/systemd/system/cloud-init.target.wants 2>/dev/null || true

# ---------------------------------------------------------------------------
# Edition: the installer copies the live image, so Minimal and Console are
# produced by removing the optional components from the target.
# ---------------------------------------------------------------------------
MINIMAL_PKGS=(
    libreoffice-fresh libreoffice-fresh-ru gimp vlc celluloid mpv evince
    galculator xfburn parole ristretto simple-scan
    hunspell hunspell-en_us hunspell-ru hyphen hyphen-en libmythes mythes-en
)
DESKTOP_PKGS=(
    lightdm lightdm-gtk-greeter accountsservice polkit-gnome gnome-keyring
    xfce4-session xfce4-panel xfce4-settings xfce4-appfinder xfce4-terminal
    xfce4-power-manager xfce4-notifyd xfce4-screensaver xfce4-screenshooter
    xfce4-taskmanager xfce4-whiskermenu-plugin xfce4-clipman-plugin
    xfce4-pulseaudio-plugin xfce4-sensors-plugin xfce4-xkb-plugin
    xfwm4 xfdesktop xfconf thunar thunar-volman thunar-archive-plugin
    thunar-media-tags-plugin tumbler exo garcon mousepad firefox
    firefox-i18n-ru file-roller geany gnome-disk-utility gparted gufw blueman
    network-manager-applet xdg-desktop-portal xdg-desktop-portal-gtk
    materia-gtk-theme papirus-icon-theme gnome-themes-extra adwaita-icon-theme
    hicolor-icon-theme
    xorg-server xorg-xinit xorg-xrandr xorg-xinput xorg-xset xorg-xrdb
    xorg-xmodmap xorg-xhost xf86-video-amdgpu xf86-video-ati
    xf86-video-intel xf86-video-nouveau xf86-video-vesa
)
case "$EDITION" in
    full)
        log "Edition: XFCE Full"
        ;;
    minimal)
        log "Edition: XFCE Minimal - removing optional applications"
        remove_pkgs "${MINIMAL_PKGS[@]}"
        ;;
    console)
        log "Edition: Console - removing the graphical desktop"
        remove_pkgs "${MINIMAL_PKGS[@]}" "${DESKTOP_PKGS[@]}"
        systemctl disable lightdm.service >/dev/null 2>&1 || true
        systemctl set-default multi-user.target >/dev/null
        ;;
esac

# ---------------------------------------------------------------------------
# Remove the archiso initramfs package; the installed system uses the normal
# Arch linux preset and the portable fallback image.
# ---------------------------------------------------------------------------
log "Removing mkinitcpio-archiso from the target"
# Do not use -Rns: its dependency cleanup can remove tools the installed
# system still needs.  The package files are small; remove just the package.
pacman -R --noconfirm mkinitcpio-archiso >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
# Replace the live user with the user chosen in the installer.
# ---------------------------------------------------------------------------
log "Creating user $USERNAME"
userdel zynx >/dev/null 2>&1 || true
groupdel zynx >/dev/null 2>&1 || true
sed -i '/^zynx:/d' /etc/passwd /etc/shadow /etc/group /etc/gshadow 2>/dev/null || true
rm -rf /home/zynx

if getent passwd "$USERNAME" >/dev/null 2>&1; then
    die "User $USERNAME already exists in the target"
fi
useradd -m -s "$SHELL_PATH" -c "Zynx User" "$USERNAME"
for group in wheel audio video render storage optical network bluetooth power rfkill lp scanner; do
    if getent group "$group" >/dev/null 2>&1; then
        usermod -aG "$group" "$USERNAME"
    fi
done
echo "$USERNAME:$USER_PASSWORD" | chpasswd
passwd -u "$USERNAME" >/dev/null 2>&1 || true

# Root password is optional.  If it was left blank, lock root and let the
# first-boot helper offer to set one later.
if [[ -n "$ROOT_PASSWORD" ]]; then
    echo "root:$ROOT_PASSWORD" | chpasswd
    rm -f /var/lib/zynx/firstboot
else
    passwd -l root >/dev/null 2>&1 || true
    install -d -m 0755 /var/lib/zynx
    : > /var/lib/zynx/firstboot
    chmod 0644 /var/lib/zynx/firstboot
fi

install -d -m 0755 /etc/sudoers.d
rm -f /etc/sudoers.d/10-zynx-live
printf '%%wheel ALL=(ALL:ALL) ALL\n' > /etc/sudoers.d/10-wheel
chmod 0440 /etc/sudoers.d/10-wheel
chown root:root /etc/sudoers.d/10-wheel
grep -q '^@includedir /etc/sudoers.d' /etc/sudoers || \
    printf '\n@includedir /etc/sudoers.d\n' >> /etc/sudoers

# ---------------------------------------------------------------------------
# Hostname, locale, timezone, keyboard.
# ---------------------------------------------------------------------------
printf '%s\n' "$HOSTNAME" > /etc/hostname
cat > /etc/hosts <<EOF
127.0.0.1 localhost
::1 localhost
127.0.1.1 $HOSTNAME.localdomain $HOSTNAME
EOF

if grep -q "^#${LOCALE} UTF-8" /etc/locale.gen; then
    sed -i "s/^#${LOCALE} UTF-8/${LOCALE} UTF-8/" /etc/locale.gen
elif ! grep -q "^${LOCALE} UTF-8" /etc/locale.gen; then
    printf '%s UTF-8\n' "$LOCALE" >> /etc/locale.gen
fi
locale-gen
printf 'LANG=%s\n' "$LOCALE" > /etc/locale.conf

ln -sf "/usr/share/zoneinfo/$TIMEZONE" /etc/localtime
hwclock --systohc --utc >/dev/null 2>&1 || true

CONSOLE_KEYMAP=${KEYBOARD%%,*}
printf 'KEYMAP=%s\n' "$CONSOLE_KEYMAP" > /etc/vconsole.conf
install -d -m 0755 /etc/X11/xorg.conf.d
cat > /etc/X11/xorg.conf.d/00-keyboard.conf <<EOF
Section "InputClass"
    Identifier "system-keyboard"
    MatchIsKeyboard "on"
    Option "XkbLayout" "$KEYBOARD"
    Option "XkbOptions" "grp:alt_shift_toggle"
EndSection
EOF

# ---------------------------------------------------------------------------
# Disable the live autologin and show the LightDM greeter on the installed
# system.  The Console edition has no display manager at all.
# ---------------------------------------------------------------------------
rm -f /etc/lightdm/lightdm.conf.d/50-zynx-autologin.conf
if [[ "$EDITION" != console ]]; then
    install -d -m 0755 /etc/lightdm/lightdm.conf.d
    cat > /etc/lightdm/lightdm.conf.d/50-zynx.conf <<'EOF'
[Seat:*]
greeter-session=lightdm-gtk-greeter
user-session=xfce
EOF
fi
rm -f /etc/systemd/system/getty@tty1.service.d/autologin.conf
rmdir /etc/systemd/system/getty@tty1.service.d 2>/dev/null || true

# ---------------------------------------------------------------------------
# Pacman keyring for the installed system.
# ---------------------------------------------------------------------------
log "Initializing pacman keyring"
rm -rf /etc/pacman.d/gnupg
pacman-key --init >/dev/null
pacman-key --populate archlinux >/dev/null

# ---------------------------------------------------------------------------
# Magic SysRq: allow the safe emergency reboot sequence (Alt+SysRq R E I S U B)
# on both the live medium and the installed system.
# ---------------------------------------------------------------------------
install -d -m 0755 /etc/sysctl.d
cat > /etc/sysctl.d/90-zynx-sysrq.conf <<'EOF'
# Magic SysRq key.  Alt+SysRq+R E I S U B safely reboots a frozen machine:
# unraw, terminate, kill, sync, unmount, reboot.
kernel.sysrq = 1
EOF
chmod 0644 /etc/sysctl.d/90-zynx-sysrq.conf
sysctl --system >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
# Swap file (not a partition), with btrfs copy-on-write handling.
# ---------------------------------------------------------------------------
setup_swap() {
    (( SWAP_GIB > 0 )) || { log "Swap file: none"; return; }
    log "Creating a ${SWAP_GIB} GiB swap file"
    rm -f /swapfile
    : > /swapfile
    chattr +C /swapfile 2>/dev/null || true
    if ! fallocate -l "${SWAP_GIB}G" /swapfile 2>/dev/null; then
        log "fallocate failed; writing the swap file with dd"
        dd if=/dev/zero of=/swapfile bs=1M count=$((SWAP_GIB * 1024)) status=none
    fi
    chmod 0600 /swapfile
    mkswap /swapfile >/dev/null
    if ! grep -Eq '^[^#]*[[:space:]]swap[[:space:]]' /etc/fstab; then
        printf '/swapfile none swap defaults 0 0\n' >> /etc/fstab
    fi
    swapon /swapfile || warn "Could not activate the new swap file in the chroot; it will be used on boot"
}
setup_swap

# ---------------------------------------------------------------------------
# Services and firewall.
# ---------------------------------------------------------------------------
SERVICES=(
    NetworkManager.service bluetooth.service ufw.service avahi-daemon.service
    cups.socket systemd-timesyncd.service fstrim.timer
)
if [[ "$EDITION" != console ]]; then
    SERVICES+=(lightdm.service)
fi
systemctl enable "${SERVICES[@]}" >/dev/null
systemctl disable sshd.service >/dev/null 2>&1 || true
if [[ "$EDITION" == console ]]; then
    systemctl set-default multi-user.target >/dev/null
else
    systemctl set-default graphical.target >/dev/null
fi

ufw --force reset >/dev/null 2>&1 || true
ufw default deny incoming >/dev/null 2>&1 || true
ufw default allow outgoing >/dev/null 2>&1 || true
ufw --force enable >/dev/null 2>&1 || true
if [[ -f /etc/ufw/ufw.conf ]]; then
    sed -i 's/^ENABLED=.*/ENABLED=yes/' /etc/ufw/ufw.conf
else
    printf 'ENABLED=yes\n' > /etc/ufw/ufw.conf
fi

# ---------------------------------------------------------------------------
# Installed-system initramfs.
# ---------------------------------------------------------------------------
log "Generating initramfs"
cp /etc/mkinitcpio.conf.install /etc/mkinitcpio.conf
rm -f /etc/mkinitcpio.conf.install
rm -rf /etc/mkinitcpio.conf.d
mkinitcpio -P
[[ -s /boot/vmlinuz-linux ]] || \
    die "Missing /boot/vmlinuz-linux; the installer must copy the kernel from the live medium."
[[ -s /boot/initramfs-linux.img ]] || die "mkinitcpio did not create /boot/initramfs-linux.img."

# ---------------------------------------------------------------------------
# Boot loader: install GRUB for both BIOS and UEFI.  A formatted target always
# gets a 1 MiB BIOS boot partition and a 512 MiB ESP, so it can boot either
# way regardless of how the live medium was started.  In --no-format mode the
# existing layout is reused and only the boot loaders that fit are written.
# The removable UEFI copy makes the target bootable as a USB stick even when
# NVRAM is unavailable.
# ---------------------------------------------------------------------------
log "Installing GRUB for BIOS"
boot_ok=0
if grub-install --target=i386-pc --recheck "$TARGET_DISK"; then
    boot_ok=1
elif (( FORMAT == 0 )); then
    log "WARNING: BIOS GRUB install failed; the disk has no BIOS boot partition."
    log "         This is expected when keeping an existing GPT layout."
else
    die "grub-install for BIOS failed."
fi

if [[ -n "$ESP_PART" && -d /boot/efi ]]; then
    log "Installing GRUB for UEFI"
    if [[ -d /sys/firmware/efi ]]; then
        grub-install --target=x86_64-efi --efi-directory=/boot/efi \
            --bootloader-id=ZYNX --recheck || \
            grub-install --target=x86_64-efi --efi-directory=/boot/efi \
                --bootloader-id=ZYNX --no-nvram --recheck
    else
        grub-install --target=x86_64-efi --efi-directory=/boot/efi \
            --bootloader-id=ZYNX --no-nvram --recheck
    fi
    grub-install --target=x86_64-efi --efi-directory=/boot/efi \
        --removable --recheck
    boot_ok=1

    if [[ -f /etc/default/grub ]]; then
        grep -q '^GRUB_BACKGROUND=' /etc/default/grub || \
            printf 'GRUB_BACKGROUND="/usr/share/backgrounds/zynx/zynx-wallpaper.png"\n' >> /etc/default/grub
    fi
else
    log "Skipping UEFI GRUB: no EFI system partition was found"
fi

(( boot_ok == 1 )) || die "No boot loader could be installed on $TARGET_DISK."

if [[ -f /etc/default/grub ]]; then
    sed -i 's/^GRUB_DISTRIBUTOR=.*/GRUB_DISTRIBUTOR="Zynx"/' /etc/default/grub
    grep -q '^GRUB_DISTRIBUTOR=' /etc/default/grub || \
        printf 'GRUB_DISTRIBUTOR="Zynx"\n' >> /etc/default/grub
    sed -i 's/^GRUB_TIMEOUT=.*/GRUB_TIMEOUT=5/' /etc/default/grub
    grep -q '^GRUB_TIMEOUT=' /etc/default/grub || \
        printf 'GRUB_TIMEOUT=5\n' >> /etc/default/grub
    sed -i 's/^GRUB_TIMEOUT_STYLE=.*/GRUB_TIMEOUT_STYLE=menu/' /etc/default/grub
    grep -q '^GRUB_TIMEOUT_STYLE=' /etc/default/grub || \
        printf 'GRUB_TIMEOUT_STYLE=menu\n' >> /etc/default/grub
    sed -i 's/^GRUB_CMDLINE_LINUX_DEFAULT=.*/GRUB_CMDLINE_LINUX_DEFAULT="quiet"/' /etc/default/grub
    grep -q '^GRUB_CMDLINE_LINUX_DEFAULT=' /etc/default/grub || \
        printf 'GRUB_CMDLINE_LINUX_DEFAULT="quiet"\n' >> /etc/default/grub
    grep -q '^GRUB_DISABLE_OS_PROBER=' /etc/default/grub || \
        printf 'GRUB_DISABLE_OS_PROBER=false\n' >> /etc/default/grub
    grep -q '^GRUB_BACKGROUND=' /etc/default/grub || \
        printf 'GRUB_BACKGROUND="/usr/share/backgrounds/zynx/zynx-wallpaper.png"\n' >> /etc/default/grub
    if (( DEFAULT_BOOT == 1 )); then
        sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=0/' /etc/default/grub
        grep -q '^GRUB_DEFAULT=' /etc/default/grub || \
            printf 'GRUB_DEFAULT=0\n' >> /etc/default/grub
    else
        # Remember the last chosen entry instead of forcing the first one.
        sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT=saved/' /etc/default/grub
        grep -q '^GRUB_DEFAULT=' /etc/default/grub || \
            printf 'GRUB_DEFAULT=saved\n' >> /etc/default/grub
        grep -q '^GRUB_SAVEDEFAULT=' /etc/default/grub || \
            printf 'GRUB_SAVEDEFAULT=true\n' >> /etc/default/grub
    fi
fi
install -d -m 0755 /boot/grub
grub-mkconfig -o /boot/grub/grub.cfg

# ---------------------------------------------------------------------------
# "Make Zynx the default boot entry": on UEFI this moves the ZYNX entry to the
# front of BootOrder.  When the user opted out, the firmware order is not
# touched at all and GRUB simply remembers the last selection.
# ---------------------------------------------------------------------------
if (( DEFAULT_BOOT == 1 )); then
    if [[ -d /sys/firmware/efi ]] && command -v efibootmgr >/dev/null 2>&1; then
        ZYNX_BOOTNUM=$(efibootmgr 2>/dev/null | awk '
            /^Boot[0-9A-Fa-f]+/ && /ZYNX/ {
                n = $1; sub(/^Boot/, "", n); sub(/\*$/, "", n); print n; exit
            }')
        if [[ -n "$ZYNX_BOOTNUM" ]]; then
            if efibootmgr --bootorder "$ZYNX_BOOTNUM" >/dev/null 2>&1; then
                log "ZYNX ($ZYNX_BOOTNUM) is now first in the UEFI boot order"
            else
                warn "efibootmgr could not change the UEFI boot order"
            fi
        else
            warn "No ZYNX entry in the UEFI boot order; leaving it unchanged"
        fi
    else
        log "BIOS boot: GRUB is in the MBR; the firmware boot order is unchanged"
    fi
else
    log "Leaving the boot order unchanged (--no-default-boot)"
fi

# ---------------------------------------------------------------------------
# Fresh machine identity and SSH host keys.
# ---------------------------------------------------------------------------
rm -f /etc/machine-id
systemd-machine-id-setup >/dev/null
rm -f /etc/ssh/ssh_host_*
ssh-keygen -A >/dev/null

fc-cache -f >/dev/null 2>&1 || true
update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true

rm -f /root/zynx-install.env /root/zynx-postinstall.sh
sync
log "Target configuration complete"
