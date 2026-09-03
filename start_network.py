#!/usr/bin/env python3
"""
Autonomous Car Network — Master Launcher
Launches everything with a single command:
  python start_network.py

What it does:
  1. Starts laptop_node.py in background
  2. Starts Streamlit dashboard in background
  3. SSHes into each car and runs start_car.sh
  4. Opens the browser automatically
  5. Shows a live status console
  6. Ctrl+C stops everything cleanly
"""

import subprocess
import threading
import time
import sys
import os
import signal
import webbrowser
import paramiko
from datetime import datetime

# ── Configuration — edit these ─────────────────────────────────
CARS = [
    {"id": "car01", "ip": "192.168.0.143", "password": "root"},
    {"id": "car02", "ip": "192.168.0.142", "password": "root"},
]
DASHBOARD_PORT = 8501
FRAMEWORK_DIR  = "/home/root/framework"
# ──────────────────────────────────────────────────────────────

RESET  = "\033[0m"
BOLD   = "\033[1m"
GREEN  = "\033[92m"
YELLOW = "\033[93m"
RED    = "\033[91m"
CYAN   = "\033[96m"
GREY   = "\033[90m"

processes = []
ssh_clients = []
ssh_threads = []
car_channels = {}
running = True

def ts():
    return datetime.now().strftime("%H:%M:%S")

def log(tag, msg, color=RESET):
    print(f"{GREY}{ts()}{RESET} {color}{BOLD}[{tag}]{RESET} {msg}")

def banner():
    print(f"""
{CYAN}{BOLD}╔══════════════════════════════════════════════════════╗
║        AUTONOMOUS CAR NETWORK — LAUNCHER             ║
╠══════════════════════════════════════════════════════╣
║  Cars   : {', '.join(c['id'] for c in CARS):<43}║
║  Dashboard: http://localhost:{DASHBOARD_PORT:<26}║
║  Press Ctrl+C to stop everything                     ║
╚══════════════════════════════════════════════════════╝{RESET}
""")

def start_local_process(name, cmd):
    """Start a local process in background."""
    log(name, f"Starting: {' '.join(cmd)}", YELLOW)
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        )
        processes.append(proc)
        log(name, f"Started (PID {proc.pid})", GREEN)
        return proc
    except Exception as e:
        log(name, f"Failed to start: {e}", RED)
        return None

def ssh_car_thread(car):
    """SSH into a car and run start_car.sh interactively."""
    car_id = car["id"]
    ip     = car["ip"]
    pwd    = car["password"]

    log(car_id, f"Connecting to {ip}...", YELLOW)

    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        client.connect(ip, username="root", password=pwd, timeout=15)
        ssh_clients.append(client)
        log(car_id, f"Connected to {ip}", GREEN)

        # Run start_car.sh with get_pty=True so it handles signals
        cmd = f"cd {FRAMEWORK_DIR} && ./start_car.sh {car_id}"
        channel = client.get_transport().open_session()
        channel.get_pty()
        channel.exec_command(cmd)
        car_channels[car_id] = channel

        # Stream output
        while running:
            if channel.recv_ready():
                data = channel.recv(1024).decode(errors="replace")
                for line in data.splitlines():
                    line = line.strip()
                    if not line:
                        continue
                    # Color key lines
                    if "OBSTACLE" in line or "BRAKE" in line:
                        log(car_id, line, YELLOW)
                    elif "SIGN" in line:
                        log(car_id, line, CYAN)
                    elif "ERROR" in line or "Failed" in line:
                        log(car_id, line, RED)
                    elif "Connected" in line or "established" in line:
                        log(car_id, line, GREEN)
                    else:
                        log(car_id, line, GREY)
            if channel.exit_status_ready():
                break
            time.sleep(0.1)

        channel.close()

    except Exception as e:
        log(car_id, f"SSH error: {e}", RED)
    finally:
        car_channels.pop(car_id, None)
        try:
            client.close()
        except Exception:
            pass
        log(car_id, "Disconnected", GREY)

def stop_all():
    global running
    running = False
    print(f"\n{YELLOW}{BOLD}Stopping everything...{RESET}")

    # Stop local processes
    for proc in processes:
        try:
            proc.terminate()
            log("launcher", f"Stopped PID {proc.pid}", GREY)
        except Exception:
            pass

    # Close SSH connections
    for client in ssh_clients:
        try:
            client.close()
        except Exception:
            pass

    log("launcher", "All stopped. Goodbye.", GREEN)

def send_command(car_id, command):
    """Send a command to a car's running Robot_latest process via its open PTY channel."""
    channel = car_channels.get(car_id)
    if not channel:
        log(car_id, "No active channel — is the car connected?", RED)
        return
    try:
        channel.send(f"{command}\n")
    except Exception as e:
        log(car_id, f"Send command error: {e}", RED)

def interactive_console():
    """Simple console for sending commands to cars."""
    time.sleep(3)  # Wait for everything to start
    print(f"\n{CYAN}Commands: 'start <car_id>', 'stop <car_id>', 'quit'{RESET}\n")

    while running:
        try:
            cmd = input("> ").strip()
            if not cmd:
                continue
            parts = cmd.split()
            if parts[0] == "quit":
                stop_all()
                break
            elif parts[0] == "start" and len(parts) > 1:
                send_command(parts[1], "start")
            elif parts[0] == "stop" and len(parts) > 1:
                send_command(parts[1], "stop")
            else:
                print(f"Unknown command: {cmd}")
        except (EOFError, KeyboardInterrupt):
            break

def main():
    banner()

    # Handle Ctrl+C
    signal.signal(signal.SIGINT, lambda s, f: stop_all())

    script_dir = os.path.dirname(os.path.abspath(__file__))

    # 1. Start laptop_node.py
    log("launcher", "Starting laptop node...", YELLOW)
    start_local_process("laptop", [sys.executable, os.path.join(script_dir, "laptop_node.py")])
    time.sleep(2)

    # 2. Start Streamlit dashboard
    log("launcher", "Starting dashboard...", YELLOW)
    start_local_process("dashboard", [
        sys.executable, "-m", "streamlit", "run",
        os.path.join(script_dir, "dashboard_app.py"),
        "--server.headless", "true",
        f"--server.port={DASHBOARD_PORT}"
    ])
    time.sleep(3)

    # 3. Open browser
    log("launcher", f"Opening browser at http://localhost:{DASHBOARD_PORT}", GREEN)
    webbrowser.open(f"http://localhost:{DASHBOARD_PORT}")

    # 4. SSH into each car
    for car in CARS:
        t = threading.Thread(target=ssh_car_thread, args=(car,), daemon=True)
        t.start()
        ssh_threads.append(t)
        time.sleep(1)

    log("launcher", f"{GREEN}All systems started. Type 'start car01' to start a car.{RESET}", GREEN)

    # 5. Interactive console
    try:
        interactive_console()
    except Exception:
        pass

    stop_all()

if __name__ == "__main__":
    main()
