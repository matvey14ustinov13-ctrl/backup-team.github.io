#!/usr/bin/env bash
# ============================================================================
# Zynx Linux airootfs customization
#
# archiso runs this script inside the new airootfs chroot after all packages
# are installed and before the squashfs/initramfs are created.
# ============================================================================
set -Eeuo pipefail

log() { printf '[zynx-customize] %s\n' "$*"; }

# ---------------------------------------------------------------------------
# Root shell: bash, not zsh.  tty1 autologin is provided by the releng
# getty@tty1 drop-in and starts this shell.
# ---------------------------------------------------------------------------
usermod -s /usr/bin/bash root

# ---------------------------------------------------------------------------
# Live user
# ---------------------------------------------------------------------------
if ! id zynx >/dev/null 2>&1; then
    useradd -m -s /usr/bin/bash -c "Zynx Live User" zynx
fi
# LightDM's autologin PAM service only succeeds for members of the
# "autologin" group.  The lightdm package does not create that group, so
# create it explicitly; otherwise autologin silently falls back to the
# greeter.  The sysusers.d drop-in is the authoritative definition and is
# applied here as well as on every boot of the live medium.
log "Creating the autologin group"
systemd-sysusers /usr/lib/sysusers.d/zynx-autologin.conf >/dev/null 2>&1 || true
getent group autologin >/dev/null 2>&1 || groupadd -r autologin || true
if ! getent group autologin >/dev/null 2>&1; then
    printf '[zynx-customize] ERROR: could not create the autologin group\n' >&2
    exit 1
fi
for group in wheel autologin audio video render storage optical network bluetooth power rfkill lp scanner; do
    if getent group "$group" >/dev/null 2>&1; then
        usermod -aG "$group" zynx
    fi
done
echo 'zynx:zynx' | chpasswd

# Fail the build instead of shipping an ISO where autologin silently drops to
# the greeter.
if ! id -nG zynx | tr ' ' '\n' | grep -qx autologin; then
    printf '[zynx-customize] ERROR: zynx is not a member of the autologin group\n' >&2
    exit 1
fi
log "autologin group: $(getent group autologin)"

# The live medium has no root password.  The installer sets (or locks) the
# root account on the target system.
passwd -d root >/dev/null 2>&1 || true

# Passwordless sudo for the live user only.  The installer removes this file.
install -d -m 0755 /etc/sudoers.d
printf 'zynx ALL=(ALL:ALL) NOPASSWD: ALL\n' > /etc/sudoers.d/10-zynx-live
chmod 0440 /etc/sudoers.d/10-zynx-live
chown root:root /etc/sudoers.d/10-zynx-live
printf '%%wheel ALL=(ALL:ALL) ALL\n' > /etc/sudoers.d/10-wheel
chmod 0440 /etc/sudoers.d/10-wheel
chown root:root /etc/sudoers.d/10-wheel
grep -q '^@includedir /etc/sudoers.d' /etc/sudoers || \
    printf '\n@includedir /etc/sudoers.d\n' >> /etc/sudoers
chmod 0750 /etc/sudoers.d

# ---------------------------------------------------------------------------
# Locales: enable a useful set; English is the default.
# ---------------------------------------------------------------------------
for loc in en_US ru_RU de_DE fr_FR es_ES it_IT pt_BR pl_PL uk_UA tr_TR zh_CN ja_JP; do
    sed -i "s/^#${loc}.UTF-8 UTF-8/${loc}.UTF-8 UTF-8/" /etc/locale.gen || true
done
locale-gen
printf 'LANG=en_US.UTF-8\n' > /etc/locale.conf

# ---------------------------------------------------------------------------
# Time, keyboard and hostname
# ---------------------------------------------------------------------------
ln -sf /usr/share/zoneinfo/UTC /etc/localtime
printf 'KEYMAP=us\n' > /etc/vconsole.conf
printf 'zynx\n' > /etc/hostname
cat > /etc/hosts <<'EOF'
127.0.0.1 localhost
::1 localhost
127.0.1.1 zynx.localdomain zynx
EOF

# ---------------------------------------------------------------------------
# Magic SysRq for the live medium: allow Alt+SysRq R E I S U B when frozen.
# The same file is installed into the target system.
# ---------------------------------------------------------------------------
sysctl --system >/dev/null 2>&1 || true

# ---------------------------------------------------------------------------
# Files that are owned by packages and were overwritten during pacstrap.
# The overlay copies were created before the packages were installed, so
# restore the Zynx versions here, after the package files are in place.
# ---------------------------------------------------------------------------
if [[ -f /etc/issue.zynx ]]; then
    cp -f /etc/issue.zynx /etc/issue
fi
if [[ -f /etc/os-release ]]; then
    install -Dm644 /etc/os-release /usr/lib/os-release
fi

# LightDM greeter: branding and keyboard layout.
install -d -m 0755 /etc/lightdm
cat > /etc/lightdm/lightdm-gtk-greeter.conf <<'EOF'
[greeter]
background=/usr/share/backgrounds/zynx/zynx-wallpaper.png
theme-name=Materia-dark
icon-theme-name=Papirus-Dark
cursor-theme-name=Adwaita
font-name=Noto Sans 11
xft-antialias=true
xft-dpi=96
xft-hintstyle=hintslight
xft-rgba=rgb
indicators=~host;~spacer;~clock;~spacer;~session;~power
clock-format=%H:%M
keyboard=us,ru
EOF
chmod 0644 /etc/lightdm/lightdm-gtk-greeter.conf

# XFCE system defaults. xfce4-settings owns xsettings.xml, so copy the
# Zynx versions from /etc/skel after the package has been installed.
# xfce4-session.xml is deliberately NOT replaced: the package ships the
# session-manager defaults (general/FailsafeSessionName plus the Failsafe
# client list).  Dropping that property makes xfce4-session fall back to a
# broken failsafe and show "Unable to load a failsafe session".
install -d -m 0755 /etc/xdg/xfce4/xfconf/xfce-perchannel-xml
for f in xfce4-desktop.xml xfwm4.xml xsettings.xml; do
    if [[ -f "/etc/skel/.config/xfce4/xfconf/xfce-perchannel-xml/$f" ]]; then
        install -m 0644 "/etc/skel/.config/xfce4/xfconf/xfce-perchannel-xml/$f" \
            "/etc/xdg/xfce4/xfconf/xfce-perchannel-xml/$f"
    fi
done

# The live image must keep a usable failsafe session: it is what starts the
# first desktop (no saved session exists yet).  Fail the build if a stale
# overlay ever replaces the package file again.
if ! grep -q 'FailsafeSessionName' /etc/xdg/xfce4/xfconf/xfce-perchannel-xml/xfce4-session.xml; then
    printf '[zynx-customize] ERROR: xfce4-session.xml lost general/FailsafeSessionName\n' >&2
    exit 1
fi
if [ -e /etc/skel/.config/xfce4/xfconf/xfce-perchannel-xml/xfce4-session.xml ]; then
    printf '[zynx-customize] ERROR: skel must not ship xfce4-session.xml\n' >&2
    exit 1
fi
log "xfce4-session defaults OK"

# ---------------------------------------------------------------------------
# Enable [multilib] in the live and installed system's pacman configuration.
# ---------------------------------------------------------------------------
if ! grep -q '^\[multilib\]' /etc/pacman.conf; then
    if grep -q '^#\[multilib\]' /etc/pacman.conf; then
        sed -i 's/^#\[multilib\]/[multilib]/' /etc/pacman.conf
    else
        printf '\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n' >> /etc/pacman.conf
    fi
fi
sed -i '/^\[multilib\]/{n;s/^#Include/Include/}' /etc/pacman.conf

# ---------------------------------------------------------------------------
# Services
# ---------------------------------------------------------------------------
systemctl enable NetworkManager.service
systemctl enable bluetooth.service
systemctl enable lightdm.service
systemctl enable ufw.service
systemctl enable avahi-daemon.service
systemctl enable cups.socket
systemctl enable systemd-timesyncd.service
systemctl enable fstrim.timer
systemctl enable zynx-tty1.service
systemctl disable sshd.service >/dev/null 2>&1 || true
systemctl disable systemd-networkd.service >/dev/null 2>&1 || true
systemctl disable iwd.service >/dev/null 2>&1 || true
systemctl set-default graphical.target

# Firewall defaults: deny incoming, allow outgoing.
ufw --force reset >/dev/null 2>&1 || true
ufw default deny incoming >/dev/null 2>&1 || true
ufw default allow outgoing >/dev/null 2>&1 || true
ufw --force enable >/dev/null 2>&1 || true
if [ -f /etc/ufw/ufw.conf ]; then
    sed -i 's/^ENABLED=.*/ENABLED=yes/' /etc/ufw/ufw.conf
else
    printf 'ENABLED=yes\n' > /etc/ufw/ufw.conf
fi

# ---------------------------------------------------------------------------
# Live desktop shortcut
# ---------------------------------------------------------------------------
install -d -m 0755 /home/zynx/Desktop
cat > /home/zynx/Desktop/zynx-install.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=Install Zynx Linux
Comment=Install Zynx to a disk or USB drive
Exec=/usr/local/bin/zynx-gui
Icon=drive-harddisk
Terminal=false
StartupNotify=true
Categories=System;
EOF
chmod 0755 /home/zynx/Desktop/zynx-install.desktop
chown -R zynx:zynx /home/zynx
chmod 0700 /home/zynx

# ---------------------------------------------------------------------------
# Live initramfs: regenerate it after the overlay configuration is in place.
# ---------------------------------------------------------------------------
log "Generating the live initramfs"
mkinitcpio -P

# ---------------------------------------------------------------------------
# Small cleanups
# ---------------------------------------------------------------------------
rm -f /etc/machine-id
systemd-machine-id-setup >/dev/null 2>&1 || true
fc-cache -f >/dev/null 2>&1 || true
update-desktop-database /usr/share/applications >/dev/null 2>&1 || true
gtk-update-icon-cache -q /usr/share/icons/hicolor >/dev/null 2>&1 || true

log "autologin group at end of customization: $(getent group autologin || echo MISSING)"
log "done"
