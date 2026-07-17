#!/bin/sh
# start_car.sh — Launches the framework, and Robot_latest if hardware is present
# Usage: ./start_car.sh <car_id>
#   car_id: e.g. car01, car02

CAR_ID=${1:-car01}

FRAMEWORK_BIN="/home/root/framework/bin/framework"
ROBOT_BIN="/home/root/framework/Robot_latest"
LOG_DIR="/home/root/framework/logs"

mkdir -p "$LOG_DIR"

# Kill any previous instances
pkill -f "$FRAMEWORK_BIN" 2>/dev/null
pkill -f "$ROBOT_BIN" 2>/dev/null
sleep 1

echo "=================================================="
echo "  Starting $CAR_ID"
echo "=================================================="

# Detect hardware: bitstream present AND Robot_latest binary exists
if [ -f "/home/root/design_1_wrapper.bit" ] && [ -f "$ROBOT_BIN" ]; then
    HAS_HW=1
else
    HAS_HW=0
fi

if [ "$HAS_HW" = "1" ]; then
    echo "[START] Hardware detected — loading bitstream..."
    fpgautil -b /home/root/design_1_wrapper.bit
fi

# Start framework in background, log to file
"$FRAMEWORK_BIN" "$CAR_ID" ultra96 60000 > "$LOG_DIR/framework.log" 2>&1 &
FW_PID=$!
echo "[START] Framework started (PID $FW_PID), log: $LOG_DIR/framework.log"

sleep 2

if [ "$HAS_HW" = "1" ]; then
    echo "[START] Robot_latest starting — type 'start' to begin, 'stop' to halt"
    "$ROBOT_BIN"
    echo "[START] Robot_latest exited. Stopping framework..."
    kill $FW_PID 2>/dev/null
else
    echo "[START] No hardware detected — framework running in foreground."
    echo "[START] Press Ctrl+C to stop."
    wait $FW_PID
fi