#!/usr/bin/env python3
"""
Laptop Network Node
Joins the autonomous car network as an active observer:
  - Announces itself via the framework's UDP discovery protocol (unicast
    directly to the known car IPs — see udp_discovery.c), so each car's
    own client_thread adds us to ITS registry and opens an outbound TCP
    connection to us. That connection is the only thing that ever
    receives framework_broadcast() pushes (OBSTACLE/SIGN/MOVING) — a
    plain inbound connect() to a car never does, since its server_thread
    only ever responds with a constant ack.
  - Runs a TCP server so cars can connect to us and push CarMessage
    status updates (mirrors framework_core.c's server_thread: receive,
    then immediately ack).
  - Exposes send_command(car_id, "start"|"stop") to send a CarMessage
    control command to a car's own TCP server (we act as the client).
Writes state to a shared JSON file for the Streamlit dashboard
(dashboard_app.py) to read.

Wire formats below were confirmed against a real compilation of the C
structs (messages.h / udp_discovery.h) — not assumed. Re-verify the same
way (compile a throwaway C program, check sizeof()/offsetof()) if
messages.h or udp_discovery.h ever change.
"""

import socket
import threading
import struct
import time
import json
import os
from datetime import datetime

# ── Configuration ─────────────────────────────────────────────
LAPTOP_ID     = "laptop"
LAPTOP_TYPE   = "pc"
TCP_PORT      = 60001
UDP_DISC_PORT = 55000
DISCOVERY_INTERVAL = 15  # seconds between re-announcements (matches udp_discovery.c)
STATE_FILE    = "car_state.json"
MAX_LOG       = 50
MAX_DIST_HIST = 60

# Known cars — discovery is unicast directly to these IPs (no broadcast),
# and also used to label incoming TCP connections by car_id instead of
# raw IP, and as the target for send_command().
CAR_IPS = {
    "car01": "192.168.0.143",
    "car02": "192.168.0.142",
}
CAR_TCP_PORT = 60000  # matches ./bin/framework <id> ultra96 60000
IP_TO_CAR_ID = {ip: cid for cid, ip in CAR_IPS.items()}
# ──────────────────────────────────────────────────────────────

# ── Wire formats (must match the C structs exactly) ──────────────────────
# CarMessage (messages.h) — 32 bytes, NATIVE byte order: tcp_comm.c's
# tcp_send_car_message()/tcp_receive_car_message() send the struct raw
# via send()/recv(), no htonl/htons conversion.
#   id(u32) x(f32) y(f32) speed(f32) state(u8) msg_type(u8) [2 pad bytes]
#   timestamp(u32) sign_id(i32) sign_confidence(f32)
CAR_MSG_FORMAT = "<IfffBB2xIif"
CAR_MSG_SIZE = struct.calcsize(CAR_MSG_FORMAT)
assert CAR_MSG_SIZE == 32, f"CarMessage layout mismatch: got {CAR_MSG_SIZE}, expected 32"

# DiscoveryPacket (udp_discovery.h) — 64 bytes. magic/type/tcp_port ARE
# converted with htonl/htons on the C side (build_packet() in
# udp_discovery.c), so this must be network byte order ('>'). device_id
# and device_type are raw bytes, byte order doesn't apply to them.
# capabilities is assigned directly (no htonl) but is always 0.
DISC_FORMAT = ">II32s16sH2xI"
DISC_SIZE = struct.calcsize(DISC_FORMAT)
assert DISC_SIZE == 64, f"DiscoveryPacket layout mismatch: got {DISC_SIZE}, expected 64"

DISCOVERY_MAGIC = 0xCAFE1234
DISC_ANNOUNCE = 1
DISC_RESPONSE = 2
DISC_GOODBYE = 3

MSG_STATUS = 0
MSG_CMD_START = 1
MSG_CMD_STOP = 2

STATE_LABELS = {1: "MOVING", 2: "ERROR", 3: "OBSTACLE", 0: "STOPPED"}
STATE_COLORS = {1: "green", 2: "red", 3: "orange", 0: "gray"}

# Shared state (owned by THIS process only — see send_command()'s
# docstring for why it must never be touched from a different process)
state = {
    "cars": {},
    "log": [],
    "laptop": {
        "id": LAPTOP_ID,
        "ip": "",
        "connected": True,
        "started": datetime.now().isoformat()
    }
}
state_lock = threading.Lock()

def ts():
    return datetime.now().strftime("%H:%M:%S")

def save_state():
    with state_lock:
        try:
            with open(STATE_FILE, "w") as f:
                json.dump(state, f)
        except Exception:
            pass

def add_log(car_id, event, color="white"):
    entry = {
        "time": ts(),
        "car": car_id,
        "event": event,
        "color": color
    }
    with state_lock:
        state["log"].insert(0, entry)
        if len(state["log"]) > MAX_LOG:
            state["log"] = state["log"][:MAX_LOG]

def init_cars():
    """Pre-populate known cars so they show up (as offline) before any
    connection has ever been made, instead of only appearing once seen."""
    with state_lock:
        for car_id, ip in CAR_IPS.items():
            state["cars"][car_id] = {
                "id": car_id,
                "ip": ip,
                "state": -1,
                "state_label": "UNKNOWN",
                "distance": 0.0,
                "speed": 0.0,
                "sign_id": -1,
                "sign_confidence": 0.0,
                "msg_type": MSG_STATUS,
                "last_seen": "--",
                "connected": False,
                "dist_history": [],
                "messages_received": 0
            }

def update_car(car_id, ip, data):
    with state_lock:
        if car_id not in state["cars"]:
            state["cars"][car_id] = {
                "id": car_id,
                "ip": ip,
                "state": 1,
                "state_label": "MOVING",
                "distance": 999.0,
                "speed": 0.0,
                "sign_id": -1,
                "sign_confidence": 0.0,
                "msg_type": MSG_STATUS,
                "last_seen": ts(),
                "connected": True,
                "dist_history": [],
                "messages_received": 0
            }
        car = state["cars"][car_id]
        car["ip"] = ip
        car["state"] = data["state"]
        car["state_label"] = STATE_LABELS.get(data["state"], "UNKNOWN")
        car["distance"] = round(data["x"], 2)
        car["speed"] = round(data["speed"], 2)
        car["sign_id"] = data["sign_id"]
        car["sign_confidence"] = round(data["sign_confidence"], 2)
        car["msg_type"] = data["msg_type"]
        car["last_seen"] = ts()
        car["connected"] = True
        car["messages_received"] += 1
        # Distance history for graph
        car["dist_history"].append({
            "time": ts(),
            "distance": round(data["x"], 2)
        })
        if len(car["dist_history"]) > MAX_DIST_HIST:
            car["dist_history"] = car["dist_history"][-MAX_DIST_HIST:]

def decode_msg(data):
    if len(data) < CAR_MSG_SIZE:
        return None
    try:
        fields = struct.unpack(CAR_MSG_FORMAT, data[:CAR_MSG_SIZE])
        return {
            "id": fields[0], "x": fields[1], "y": fields[2],
            "speed": fields[3], "state": fields[4], "msg_type": fields[5],
            "timestamp": fields[6], "sign_id": fields[7],
            "sign_confidence": fields[8]
        }
    except Exception:
        return None

def encode_ack():
    return struct.pack(
        CAR_MSG_FORMAT,
        999, 0.0, 0.0, 0.0, 1, MSG_STATUS, int(time.time()), -1, 0.0
    )

def build_announce():
    return struct.pack(
        DISC_FORMAT,
        DISCOVERY_MAGIC,
        DISC_ANNOUNCE,
        LAPTOP_ID.encode()[:31].ljust(32, b'\x00'),
        LAPTOP_TYPE.encode()[:15].ljust(16, b'\x00'),
        TCP_PORT,
        0,  # capabilities — always 0 on the C side too
    )

def send_command(car_id, cmd, timeout=3.0):
    """
    Connect to the given car's framework TCP server as a client and send
    a MSG_CMD_START/MSG_CMD_STOP CarMessage. Returns True if the command
    was sent and acknowledged.

    Deliberately stateless: does NOT touch `state`/add_log()/save_state().
    This is called from the Streamlit process (dashboard_app.py), which
    is a SEPARATE process from the one running main() below — it has its
    own copy of `state`. If it called save_state(), it would overwrite
    car_state.json with its own (mostly empty) state and wipe out the
    real telemetry the actual laptop_node.py process is writing
    concurrently. Keep this function's only side effect network I/O.
    """
    ip = CAR_IPS.get(car_id)
    if ip is None:
        return False

    msg_type = MSG_CMD_START if cmd == "start" else MSG_CMD_STOP
    payload = struct.pack(
        CAR_MSG_FORMAT,
        0, 0.0, 0.0, 0.0, 0, msg_type, int(time.time()), -1, 0.0
    )

    try:
        with socket.create_connection((ip, CAR_TCP_PORT), timeout=timeout) as s:
            s.sendall(payload)
            buf = b""
            while len(buf) < CAR_MSG_SIZE:
                chunk = s.recv(CAR_MSG_SIZE - len(buf))
                if not chunk:
                    break
                buf += chunk
        return True
    except OSError:
        return False

def handle_client(conn, addr):
    car_id = IP_TO_CAR_ID.get(addr[0], addr[0])
    add_log("network", f"Car connected: {car_id} ({addr[0]})", "cyan")
    save_state()
    try:
        while True:
            data = b""
            while len(data) < CAR_MSG_SIZE:
                chunk = conn.recv(CAR_MSG_SIZE - len(data))
                if not chunk:
                    return
                data += chunk
            msg = decode_msg(data)
            if not msg:
                continue

            update_car(car_id, addr[0], msg)

            # Log important events
            if msg["sign_id"] >= 0:
                add_log(car_id, f"SIGN detected: id={msg['sign_id']} conf={msg['sign_confidence']:.0%}", "yellow")
            elif msg["state"] == 3:
                add_log(car_id, f"OBSTACLE at {msg['x']:.1f} cm", "orange")
            elif msg["state"] == 0:
                add_log(car_id, "STOPPED", "gray")

            save_state()
            # Must ack immediately — the car's client_thread blocks its
            # entire peer loop on this response, not just on us.
            conn.sendall(encode_ack())

    except Exception:
        pass
    finally:
        with state_lock:
            if car_id in state["cars"]:
                state["cars"][car_id]["connected"] = False
        add_log("network", f"Car disconnected: {car_id} ({addr[0]})", "red")
        save_state()
        conn.close()

def tcp_server_thread():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("", TCP_PORT))
    srv.listen(5)
    print(f"[{ts()}] TCP server on port {TCP_PORT}")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=handle_client, args=(conn, addr), daemon=True).start()

def discovery_thread():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", UDP_DISC_PORT))
    sock.settimeout(1.0)

    # Get own IP (cosmetic only — display in the network topology panel)
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        with state_lock:
            state["laptop"]["ip"] = s.getsockname()[0]
        s.close()
    except Exception:
        pass

    last_announce = 0
    while True:
        now = time.time()
        if now - last_announce > DISCOVERY_INTERVAL:
            pkt = build_announce()
            for car_id, ip in CAR_IPS.items():
                try:
                    sock.sendto(pkt, (ip, UDP_DISC_PORT))
                except Exception as e:
                    print(f"[{ts()}] Announce error to {car_id} ({ip}): {e}")
            print(f"[{ts()}] Announced on network (unicast to {list(CAR_IPS.values())})")
            last_announce = now

        try:
            data, addr = sock.recvfrom(256)
            if len(data) == DISC_SIZE:
                magic, _pkt_type, did, dtype, port, _caps = struct.unpack(DISC_FORMAT, data)
                if magic != DISCOVERY_MAGIC:
                    continue
                did = did.rstrip(b'\x00').decode(errors='replace')
                dtype = dtype.rstrip(b'\x00').decode(errors='replace')
                if did != LAPTOP_ID:
                    print(f"[{ts()}] Discovered: {did} ({dtype}) at {addr[0]}:{port}")
                    add_log("network", f"Discovered {did} at {addr[0]}", "cyan")
                    save_state()
        except socket.timeout:
            pass
        except Exception as e:
            print(f"[{ts()}] Discovery error: {e}")

def main():
    print("=" * 50)
    print("  AUTONOMOUS CAR NETWORK — LAPTOP NODE")
    print("=" * 50)
    print(f"  TCP Port : {TCP_PORT}")
    print(f"  Cars     : {CAR_IPS}")
    print(f"  State file: {STATE_FILE}")
    print(f"  Run dashboard: streamlit run dashboard_app.py")
    print("=" * 50)

    init_cars()
    save_state()

    threading.Thread(target=discovery_thread, daemon=True).start()
    threading.Thread(target=tcp_server_thread, daemon=True).start()

    print(f"[{ts()}] Laptop node running. Open dashboard in browser.")
    print(f"[{ts()}] Press Ctrl+C to exit.\n")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nLaptop node stopped.")

if __name__ == "__main__":
    main()
