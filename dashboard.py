#!/usr/bin/env python3
"""
Autonomous Car Network Dashboard
Monitors two Ultra96 boards via SSH in real time.
Usage: python dashboard.py
"""

import paramiko
import threading
import time
import os
import re
import sys
from datetime import datetime

# ── Configuration ─────────────────────────────────────────────
CARS = [
    {"id": "car01", "ip": "192.168.0.143", "password": "root", "has_hw": True},
    {"id": "car02", "ip": "192.168.0.142", "password": "root", "has_hw": False},
]
REFRESH_RATE  = 3  # seconds between screen redraws
# ──────────────────────────────────────────────────────────────

STATE_LABELS = {
    "1": ("RUN",     "\033[92m"),   # green
    "2": ("ERROR",   "\033[91m"),   # red
    "3": ("OBSTACLE","\033[93m"),   # yellow
}
RESET  = "\033[0m"
BOLD   = "\033[1m"
CYAN   = "\033[96m"
WHITE  = "\033[97m"
GREY   = "\033[90m"

class CarState:
    def __init__(self, car_id, ip):
        self.car_id   = car_id
        self.ip       = ip
        self.distance = "--"
        self.state    = "--"
        self.peers    = {}          # peer_id -> "Connected"/"Disconnected"
        self.motor    = "--"
        self.last_msg = "--"
        self.ssh_ok   = False
        self.log      = []          # last 5 events
        self.lock     = threading.Lock()

    def add_log(self, entry):
        ts = datetime.now().strftime("%H:%M:%S")
        with self.lock:
            self.log.append(f"{GREY}{ts}{RESET} {entry}")
            if len(self.log) > 5:
                self.log.pop(0)

states = [CarState(c["id"], c["ip"]) for c in CARS]


def parse_line(line, state):
    """Update CarState based on a framework output line."""
    # SRF05 distance
    m = re.search(r"\[SRF05\] Distance:\s*([\d.]+)", line)
    if m:
        state.distance = f"{float(m.group(1)):.1f} cm"
        return

    m = re.search(r"Distance:\s*([\d.]+)", line)
    if m:
        state.distance = f"{float(m.group(1)):.1f} cm"
        return

    m = re.search(r"\[SRF05\] Timeout", line)
    if m:
        state.distance = "timeout"
        return

    # APP message received from peer
    m = re.search(r"\[APP\].*Message from ([\d.]+): distance=([\-\d.]+).*state=(\d)", line)
    if m:
        peer_ip = m.group(1)
        dist    = m.group(2)
        st      = m.group(3)
        label, color = STATE_LABELS.get(st, ("?", WHITE))
        state.last_msg = f"from {peer_ip}  dist={dist}cm  {color}{label}{RESET}"
        state.add_log(f"MSG {peer_ip} dist={dist}cm state={color}{label}{RESET}")
        return

    # Motor action
    if "[MOTOR] All motors braking" in line:
        state.motor = f"\033[91mBRAKING{RESET}"
        state.add_log(f"\033[91mMOTOR BRAKE{RESET}")
    elif "[MOTOR] All motors stopped" in line:
        state.motor = f"\033[93mSTOPPED{RESET}"
    elif "motor" in line.lower():
        state.motor = "RUN"

    # TCP connection
    m = re.search(r"\[CLIENT\] Persistent connection established to (\S+)", line)
    if m:
        peer = m.group(1)
        with state.lock:
            state.peers[peer] = "\033[92mConnected\033[0m"
        state.add_log(f"Connected to {peer}")
        return

    m = re.search(r"\[TCP\] Connection failed", line)
    if m:
        state.add_log(f"\033[91mTCP connection failed\033[0m")
        return

    # State from APP
    if "[APP] Other car in error" in line:
        state.state = "2"
    elif "[APP] Car nearby" in line:
        state.state = "1"

    # Robot state machine transitions
    if "Z2_MOVE_FORWARD" in line:
        state.motor = "FORWARD"
    elif "Z3_BRAKE" in line:
        state.motor = "BRAKING"
        state.state = "3"
    elif "Z5_TURN_RIGHT" in line:
        state.motor = "TURNING RIGHT"
    elif "Z7_TURN_LEFT" in line:
        state.motor = "TURNING LEFT"
    elif "Z8_MOVE_BACKWARD" in line:
        state.motor = "BACKWARD"
    elif "Z1_SCAN_FRONT" in line:
        state.state = "1"


def ssh_worker(car, state):
    """SSH into Ultra96, run framework, parse output."""
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    while True:
        try:
            client.connect(car["ip"], username="root",
                           password=car["password"], timeout=10)
            state.ssh_ok = True
            state.add_log("SSH connected")
            if car.get("has_hw"):
                cmd = f"fpgautil -b /home/root/design_1_wrapper.bit 2>/dev/null; cd /home/root/framework && pkill framework 2>/dev/null; sleep 1 && ./bin/framework {car['id']} ultra96 60000"
            else:
                cmd = f"cd /home/root/framework && pkill framework 2>/dev/null; sleep 1 && ./bin/framework {car['id']} ultra96 60000"
            _, stdout, _ = client.exec_command(cmd, get_pty=True, timeout=300)
            for line in stdout:
                line = line.strip()
                parse_line(line, state)
                # infer running state from SRF05
                if "[SRF05] Distance" in line:
                    state.state = "1"
                elif "[MOTOR] All motors braking" in line:
                    state.state = "3"
        except Exception as e:
            state.ssh_ok = False
            state.add_log(f"\033[91mSSH error: {e}\033[0m")
            time.sleep(5)
            continue
        finally:
            try:
                client.close()
            except Exception:
                pass


def draw():
    os.system("cls" if os.name == "nt" else "clear")
    now = datetime.now().strftime("%H:%M:%S")
    print(f"{BOLD}{CYAN}{'═'*64}{RESET}")
    print(f"{BOLD}{CYAN}   AUTONOMOUS CAR NETWORK DASHBOARD          {GREY}{now}{RESET}")
    print(f"{BOLD}{CYAN}{'═'*64}{RESET}")
    print()

    for state in states:
        label, color = STATE_LABELS.get(state.state, ("UNKNOWN", GREY))
        conn = f"\033[92m● SSH OK{RESET}" if state.ssh_ok else f"\033[91m● SSH DOWN{RESET}"

        print(f"{BOLD}{'─'*30}{RESET}")
        print(f"  {BOLD}{WHITE}{state.car_id}{RESET}  {state.ip}   {conn}")
        print(f"{'─'*30}")
        print(f"  SRF05    : {BOLD}{state.distance}{RESET}")
        print(f"  State    : {color}{BOLD}{label}{RESET}")
        print(f"  Motors   : {state.motor}")
        print(f"  Last msg : {state.last_msg}")

        with state.lock:
            peers = dict(state.peers)
        if peers:
            print(f"  Peers    :", end="")
            for pid, pstate in peers.items():
                print(f"  {pid} {pstate}", end="")
            print()
        else:
            print(f"  Peers    : {GREY}none yet{RESET}")

        print(f"  {GREY}── Recent events ──{RESET}")
        with state.lock:
            logs = list(state.log)
        for entry in logs[-4:]:
            print(f"    {entry}")
        print()

    print(f"{GREY}Refresh every {REFRESH_RATE}s  |  Ctrl+C to exit{RESET}")


def main():
    print("Starting dashboard — connecting to Ultra96 boards...")
    threads = []
    for car, state in zip(CARS, states):
        t = threading.Thread(target=ssh_worker, args=(car, state), daemon=True)
        t.start()
        threads.append(t)

    try:
        while True:
            draw()
            time.sleep(REFRESH_RATE)
    except KeyboardInterrupt:
        print("\nDashboard stopped.")
        sys.exit(0)


if __name__ == "__main__":
    main()
