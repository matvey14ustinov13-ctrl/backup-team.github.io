#!/usr/bin/env bash
# End-to-end GUI flow test with a stub engine: Review -> confirm overlay ->
# progress -> Done.  Saves screenshots to out/gui-flow-*.png.
set -euo pipefail
PROJ="${1:-/mnt/c/MATEYKA/OPENCODE AI}"
OUT="$PROJ/out"
BIN="${2:-/tmp/zynx-gui}"
mkdir -p "$OUT"

pkill -f 'Xvfb :98' 2>/dev/null || true
Xvfb :98 -screen 0 900x760x24 -listen tcp -nolisten unix >/dev/null 2>&1 &
XPID=$!
cleanup() { kill "$XPID" 2>/dev/null || true; }
trap cleanup EXIT
sleep 1

cat > /tmp/zynx-engine-flow <<'EOF'
#!/usr/bin/env bash
echo "stub engine started: $*" >> "${ZYNX_LOG:-/tmp/zynx-gui.log}"
for pct in 5 20 45 70 90 100; do
    printf '%s\nInstalling component %s%%\n' "$pct" "$pct" > "${ZYNX_STATUS:-/tmp/zynx-gui.status}"
    echo "step $pct" >> "${ZYNX_LOG:-/tmp/zynx-gui.log}"
    sleep 0.7
done
echo "stub engine finished" >> "${ZYNX_LOG:-/tmp/zynx-gui.log}"
EOF
chmod +x /tmp/zynx-engine-flow
: > /tmp/zynx-gui.log
rm -f /tmp/zynx-gui.status

export DISPLAY=127.0.0.1:98.0
export GDK_BACKEND=x11
export ZYNX_ENGINE=/tmp/zynx-engine-flow
export ZYNX_LOG=/tmp/zynx-gui.log
export ZYNX_STATUS=/tmp/zynx-gui.status
export ZYNX_SHOW_ALL_DISKS=1

ZYNX_TEST_PAGE=summary "$BIN" >/tmp/zynx-gui-flow.out 2>&1 &
pid=$!
sleep 2
import -window root "$OUT/gui-flow-summary.png"

# Click "Install" in the footer, then confirm by typing ERASE.
xdotool mousemove 797 700 click 1
sleep 1
import -window root "$OUT/gui-flow-confirm.png"
xdotool type --delay 60 "INSTALL"
sleep 0.3
xdotool key Return
sleep 2
import -window root "$OUT/gui-flow-installing.png"
sleep 5
import -window root "$OUT/gui-flow-done.png"

kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
echo "flow screenshots written"
