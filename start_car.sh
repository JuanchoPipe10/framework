#!/bin/sh
# start_car.sh — Launches the framework, and line_following if hardware is present
# Usage: ./start_car.sh <car_id>
#   car_id: e.g. car01, car02

CAR_ID=${1:-car01}

FRAMEWORK_BIN="/home/root/framework/bin/framework"
LINE_FOLLOWING_BIN="/home/root/line_following"
LOG_DIR="/home/root/framework/logs"

mkdir -p "$LOG_DIR"

# Bitstream to load, per car.
case "$CAR_ID" in
    car01)
        BITSTREAM="/home/root/design_1_wrapper.bit"
        ;;
    car02)
        BITSTREAM="/home/root/diff_movement.bit"
        ;;
    *)
        BITSTREAM="/home/root/design_1_wrapper.bit"
        ;;
esac

# Kill any previous instances
pkill -f "$FRAMEWORK_BIN" 2>/dev/null
pkill -f "$LINE_FOLLOWING_BIN" 2>/dev/null
sleep 1

echo "=================================================="
echo "  Starting $CAR_ID"
echo "=================================================="

# Detect hardware: bitstream present AND line_following binary exists
if [ -f "$BITSTREAM" ] && [ -f "$LINE_FOLLOWING_BIN" ]; then
    HAS_HW=1
else
    HAS_HW=0
fi

if [ "$HAS_HW" = "1" ]; then
    echo "[START] Hardware detected — loading bitstream ($BITSTREAM)..."
    fpgautil -b "$BITSTREAM"
fi

# Start framework in background, log to file
"$FRAMEWORK_BIN" "$CAR_ID" ultra96 60000 > "$LOG_DIR/framework.log" 2>&1 &
FW_PID=$!
echo "[START] Framework started (PID $FW_PID), log: $LOG_DIR/framework.log"

sleep 2

if [ "$HAS_HW" = "1" ]; then
    echo "[START] line_following starting — type 'start' to begin, 'stop' to halt"
    "$LINE_FOLLOWING_BIN"
    echo "[START] line_following exited. Stopping framework..."
    kill $FW_PID 2>/dev/null
else
    echo "[START] No hardware detected — framework running in foreground."
    echo "[START] Press Ctrl+C to stop."
    wait $FW_PID
fi