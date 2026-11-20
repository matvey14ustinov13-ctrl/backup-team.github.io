# Zynx Linux

An Arch-based live medium with a graphical, offline installer. The installer is
written in **C + GTK3** — no Python, no terminal window, no network access.

- Interface and installed system: English (default locale `en_US.UTF-8`,
  keyboard `us`).
- Editions: **XFCE Full**, **XFCE Minimal**, **Console**.
- Login shell for the new user: **bash**, **zsh** or **fish**.
- Separate `/home` partition, configurable root size and an optional
  **swap file** (0/2/4/8 GiB).
- Formatting is **off by default**; the installer shows the exact partition plan
  before writing anything.
- Optional “Make Zynx the default boot entry” switch (UEFI `BootOrder`).
- Magic SysRq (REISUB) enabled on the live medium and the installed system.
- GRUB for both BIOS and UEFI, `fstrim.timer`, `mkfs.ext4 -m 1`, retry logic for
  slow partition device nodes and an install log copied to the target.

## Download

The ISO is published as a release asset:

<https://github.com/matvey14ustinov13-ctrl/backup-team.github.io/releases/latest>

Extract the ZIP, verify the SHA256 and write the ISO with `dd` or Rufus (DD
mode). See `README.md` inside the ZIP for the full bilingual guide.

## Requirements

- x86_64, 2 GiB RAM, 12 GiB disk (16 GiB recommended).
- UEFI or BIOS; Secure Boot must be disabled.
- USB drive of 2 GB or more.

## Repository layout

```
installer/zynx-gui.c                  GTK3 installer (C)
profile/                              archiso profile
  airootfs/usr/local/bin/zynx-install  privileged engine (bash)
  airootfs/usr/local/share/zynx/       post-install target configuration
  airootfs/etc/                        live-system configuration
  packages.x86_64                      package set
  profiledef.sh                        archiso settings
build-arch-amdgpu-shell.sh            ISO builder (run as root on Arch)
tools/                                syntax checks, tests, release packaging
```

## Building the ISO

On Arch Linux x86_64 as root:

```bash
sudo bash build-arch-amdgpu-shell.sh
```

Optional environment variables:

```bash
BUILD_ROOT=/var/tmp/zynx-build OUT_DIR=/var/tmp/zynx-out \
    sudo -E bash build-arch-amdgpu-shell.sh
```

The builder installs missing host packages (`archiso`, `gcc`, `gtk3`,
`pkgconf`, `squashfs-tools`, …), compiles `installer/zynx-gui.c`, assembles the
profile, generates the wallpaper and runs `mkarchiso`. The ISO and its SHA256
checksum are written to `OUT_DIR` (default `./out`).

To validate the profile and compile the installer without building the ISO:

```bash
VALIDATE_ONLY=1 bash build-arch-amdgpu-shell.sh
```

## Tests

Run on Arch Linux as root:

```bash
bash tools/check-syntax.sh
bash tools/test-disk-list.sh
bash tools/test-partition-detect.sh
bash tools/test-noformat-mount.sh
bash tools/test-format-layout.sh
bash tools/test-gui.sh          # needs Xvfb + ImageMagick
bash tools/test-gui-flow.sh     # needs Xvfb + ImageMagick + xdotool
```

The tests use loop devices and temporary mounts; they do not touch real disks.

## Installer options (engine)

```
zynx-install --auto --disk /dev/sda --user alex --user-password secret \
    --root-password '' --hostname zynx --timezone UTC \
    --locale en_US.UTF-8 --keyboard us --edition full --shell bash \
    --root-size 40 --swap 4 [--format|--no-format] [--no-default-boot] --yes
```

`--format` creates BIOS boot + ESP + root + `/home`; without it the largest
existing Linux root filesystem is reused and `/home` is preserved.

## License

GPL v3. Arch Linux packages and the Linux kernel keep their own licenses.
