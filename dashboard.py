#!/usr/bin/env python3
"""
Autonomous Car Network Dashboard
Joins the C framework as a real network node (via framework_node.py) —
no SSH, no text parsing. Receives live CarMessage telemetry and lets you
send remote start/stop commands.

Usage: python dashboard.py

Keys:
  s then a car number (e.g. 's1')  -> start that car
  x then a car number (e.g. 'x2')  -> stop that car
  q                                -> quit

Windows-only (uses msvcrt for non-blocking key reads), matching the
platform this is run on.
"""

import sys
import time
from datetime import datetime

if sys.platform != "win32":
    print("This dashboard uses msvcrt for keyboard input and only runs on Windows.")
    sys.exit(1)

import msvcrt

from framework_node import FrameworkNode

# ── Configuration ─────────────────────────────────────────────
CARS = {
    "car01": "192.168.0.143",
    "car02": "192.168.0.142",
}
DASHBOARD_DEVICE_ID   = "dashboard"
DASHBOARD_DEVICE_TYPE = "dashboard"
DASHBOARD_TCP_PORT    = 60010   # this dashboard's own TCP server port
CAR_TCP_PORT          = 60000   # matches ./bin/framework <id> ultra96 60000
REFRESH_RATE          = 1.0     # seconds between screen redraws
# ──────────────────────────────────────────────────────────────

RESET  = "\033[0m"
BOLD   = "\033[1m"
CYAN   = "\033[96m"
WHITE  = "\033[97m"
GREY   = "\033[90m"
GREEN  = "\033[92m"
RED    = "\033[91m"
YELLOW = "\033[93m"

STATE_COLORS = {
    "MOVING":   GREEN,
    "STOPPED":  GREY,
    "ERROR":    RED,
    "OBSTACLE": YELLOW,
    "UNKNOWN":  WHITE,
}


def clear_screen():
    print("\033[2J\033[H", end="")


def draw(node, status_line):
    clear_screen()
    now = datetime.now().strftime("%H:%M:%S")
    print(f"{BOLD}{CYAN}{'='*64}{RESET}")
    print(f"{BOLD}{CYAN}  AUTONOMOUS CAR NETWORK DASHBOARD{' '*16}{GREY}{now}{RESET}")
    print(f"{BOLD}{CYAN}{'='*64}{RESET}\n")

    states = node.get_states()
    for car_id in sorted(states):
        st = states[car_id]
        color = STATE_COLORS.get(st.state_name, WHITE)
        conn = f"{GREEN}* connected{RESET}" if st.connected else f"{RED}o offline{RESET}"
        last_seen = (
            datetime.fromtimestamp(st.last_seen).strftime("%H:%M:%S")
            if st.last_seen else "--"
        )

        print(f"{BOLD}{'-'*30}{RESET}")
        print(f"  {BOLD}{WHITE}{car_id}{RESET}  {st.ip}   {conn}")
        print(f"{'-'*30}")
        print(f"  State     : {color}{BOLD}{st.state_name}{RESET}")
        print(f"  Distance  : {st.distance:.1f} cm")
        if st.sign_id >= 0:
            print(f"  Sign      : {YELLOW}id={st.sign_id} conf={st.sign_confidence:.0%}{RESET}")
        print(f"  Msgs recv : {st.messages_received}")
        print(f"  Last seen : {last_seen}")
        print()

    print(f"{GREY}{'-'*64}{RESET}")
    print(f"{GREY}Keys: s<n> start car0<n>  |  x<n> stop car0<n>  |  q quit{RESET}")
    print(status_line)


def main():
    print("Starting dashboard — joining the network...")
    node = FrameworkNode(
        device_id=DASHBOARD_DEVICE_ID,
        device_type=DASHBOARD_DEVICE_TYPE,
        tcp_port=DASHBOARD_TCP_PORT,
        car_ips=CARS,
    )
    node.start()

    pending_cmd = None
    status_line = f"{GREY}Ready.{RESET}"
    last_draw = 0.0

    try:
        while True:
            if msvcrt.kbhit():
                ch = msvcrt.getch().decode(errors="ignore").lower()

                if ch == "q":
                    break
                elif ch in ("s", "x"):
                    pending_cmd = ch
                elif ch.isdigit() and pending_cmd:
                    car_id = f"car{int(ch):02d}"
                    action = "start" if pending_cmd == "s" else "stop"
                    if car_id in CARS:
                        ok = node.send_command(car_id, action, tcp_port=CAR_TCP_PORT)
                        if ok:
                            status_line = f"{GREEN}Sent '{action}' to {car_id}{RESET}"
                        else:
                            status_line = f"{RED}Failed to send '{action}' to {car_id} (unreachable?){RESET}"
                    else:
                        status_line = f"{RED}Unknown car: {car_id}{RESET}"
                    pending_cmd = None
                else:
                    pending_cmd = None

            now = time.time()
            if now - last_draw >= REFRESH_RATE:
                draw(node, status_line)
                last_draw = now

            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        node.stop()
        print("\nDashboard stopped.")


if __name__ == "__main__":
    main()
