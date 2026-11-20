# Zynx Linux live-medium hint.  This file is harmless on an installed system.
if [ -e /etc/zynx-live ] && [ -t 0 ]; then
    printf '\nZynx live medium: use the "Install Zynx Linux" icon on the desktop.\n'
    printf 'The live user is "zynx" (sudo without a password).\n\n'
fi
