#!/usr/bin/env bash
# Render every installer page under Xvfb and save screenshots to out/.
# Run inside Arch Linux with Xvfb + ImageMagick installed.
set -euo pipefail
PROJ="${1:-/mnt/c/MATEYKA/OPENCODE AI}"
OUT="$PROJ/out"
BIN="${2:-/tmp/zynx-gui}"
mkdir -p "$OUT"

pkill -f 'Xvfb :99' 2>/dev/null || true
Xvfb :99 -screen 0 900x760x24 -listen tcp -nolisten unix >/dev/null 2>&1 &
XPID=$!
cleanup() { kill "$XPID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 1

cat > /tmp/zynx-engine <<'EOF'
#!/usr/bin/env bash
printf 'stub engine: %s\n' "$*" >> "${ZYNX_LOG:-/tmp/zynx-gui.log}"
printf '5\nPreparing the disk\n' > "${ZYNX_STATUS:-/tmp/zynx-gui.status}"
sleep 60
EOF
chmod +x /tmp/zynx-engine

export DISPLAY=127.0.0.1:99.0
export GDK_BACKEND=x11
export ZYNX_ENGINE=/tmp/zynx-engine
export ZYNX_LOG=/tmp/zynx-gui.log
export ZYNX_STATUS=/tmp/zynx-gui.status
export ZYNX_SHOW_ALL_DISKS=1
export ZYNX_DEBUG=1

for page in welcome edition disk account settings summary install; do
    ZYNX_TEST_PAGE="$page" "$BIN" >"/tmp/zynx-gui-$page.out" 2>&1 &
    pid=$!
    sleep 2
    import -window root "$OUT/gui-c-$page.png"
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    printf 'rendered %s\n' "$page"
done
