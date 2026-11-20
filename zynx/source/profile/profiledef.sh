#!/usr/bin/env bash
# shellcheck disable=SC2034
#
# Zynx Linux archiso profile: Arch Linux + XFCE + official Arch kernel.
# The file is sourced by mkarchiso; all paths are relative to this profile.

iso_name="zynx"
iso_label="ZYNX"                        # max 11 chars, otherwise FAT32/EFI breaks
iso_publisher="Zynx Linux"
iso_application="Zynx Linux Live/Install Medium"
iso_version="$(date --date="@${SOURCE_DATE_EPOCH:-$(date +%s)}" +%Y.%m.%d)"

install_dir="arch"
arch="x86_64"
buildmodes=('iso')
bootmodes=('bios.syslinux' 'uefi.systemd-boot')

pacman_conf="pacman.conf"

airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'xz' '-Xbcj' 'x86' '-b' '1M' '-Xdict-size' '1M')

# Permissions are applied by mkarchiso after the airootfs overlay is copied.
# The Windows build host cannot preserve Unix modes, so list every custom file
# that needs something other than 0644/0755 here.
file_permissions=(
  ["/etc/hostname"]="0:0:644"
  ["/etc/hosts"]="0:0:644"
  ["/etc/os-release"]="0:0:644"
  ["/etc/motd"]="0:0:644"
  ["/etc/issue"]="0:0:644"
  ["/etc/issue.zynx"]="0:0:644"
  ["/etc/profile.d/zynx-prompt.sh"]="0:0:644"
  ["/etc/profile.d/zynx-live.sh"]="0:0:644"
  ["/etc/profile.d/zynx-env.sh"]="0:0:644"
  ["/etc/sudoers.d/10-zynx-live"]="0:0:440"
  ["/etc/lightdm/lightdm.conf.d/50-zynx-autologin.conf"]="0:0:644"
  ["/etc/lightdm/lightdm-gtk-greeter.conf"]="0:0:644"
  ["/etc/xdg/autostart/zynx-firstboot.desktop"]="0:0:644"
  ["/etc/xdg/autostart/zynx-wallpaper.desktop"]="0:0:644"
  ["/etc/systemd/system/zynx-tty1.service"]="0:0:644"
  ["/usr/local/bin/zynx-apply-wallpaper"]="0:0:755"
  ["/etc/skel/.dmrc"]="0:0:644"
  ["/etc/zynx-live"]="0:0:644"
  ["/etc/sysctl.d/90-zynx-sysrq.conf"]="0:0:644"
  ["/usr/local/bin/zynx-install"]="0:0:755"
  ["/usr/local/bin/zynx-gui"]="0:0:755"
  ["/usr/local/bin/zynx-firstboot"]="0:0:755"
  ["/usr/local/bin/zynx-update"]="0:0:755"
  ["/usr/local/share/zynx/zynx-postinstall.sh"]="0:0:755"
  ["/usr/share/applications/zynx-install.desktop"]="0:0:644"
  ["/usr/share/applications/zynx-update.desktop"]="0:0:644"
  ["/usr/share/backgrounds/zynx/zynx-wallpaper.png"]="0:0:644"
  ["/root"]="0:0:750"
)
