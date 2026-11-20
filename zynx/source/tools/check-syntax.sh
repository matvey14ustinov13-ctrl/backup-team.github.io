#!/usr/bin/env bash
# Syntax check for the shell scripts and the C GTK installer.
cd "/mnt/c/MATEYKA/OPENCODE AI" || exit 1
rc=0
for f in \
    profile/airootfs/usr/local/bin/zynx-install \
    profile/airootfs/usr/local/bin/zynx-firstboot \
    profile/airootfs/usr/local/bin/zynx-update \
    profile/airootfs/usr/local/bin/zynx-apply-wallpaper \
    profile/airootfs/usr/local/share/zynx/zynx-postinstall.sh \
    profile/airootfs/root/customize_airootfs.sh \
    tools/compile-gui.sh \
    tools/test-gui.sh \
    build-arch-amdgpu-shell.sh; do
    bash -n "$f" && echo "OK   $f" || { echo "FAIL $f"; rc=1; }
done

if command -v gcc >/dev/null 2>&1 && pkg-config --exists gtk+-3.0 2>/dev/null; then
    # shellcheck disable=SC2046
    gcc -Wall -Wextra -fsyntax-only installer/zynx-gui.c \
        $(pkg-config --cflags gtk+-3.0) \
        && echo "OK   installer/zynx-gui.c" \
        || { echo "FAIL installer/zynx-gui.c"; rc=1; }
else
    echo "SKIP installer/zynx-gui.c (gcc/gtk3 headers not installed)"
fi

command -v shellcheck >/dev/null 2>&1 && shellcheck -S warning \
    profile/airootfs/usr/local/bin/zynx-install \
    profile/airootfs/usr/local/bin/zynx-firstboot \
    profile/airootfs/usr/local/bin/zynx-update \
    profile/airootfs/usr/local/bin/zynx-apply-wallpaper \
    profile/airootfs/usr/local/share/zynx/zynx-postinstall.sh \
    profile/airootfs/root/customize_airootfs.sh \
    || true
exit $rc
