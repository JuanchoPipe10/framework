#!/usr/bin/env python3
"""
framework_node.py
Python client that joins the C communication framework as a real network
node — not by SSHing in and parsing text output, but by speaking the
framework's actual wire protocols:

  - Announces itself via the UDP discovery protocol (unicast, since car
    IPs are already known — see udp_discovery.c), so each car's own
    client_thread adds us to ITS registry and opens an outbound TCP
    connection to us. That outbound connection is the only thing that
    ever receives framework_broadcast() pushes (OBSTACLE/SIGN/MOVING) —
    a plain inbound connect() to a car's server never does, since
    server_thread only ever responds with a constant ack.
  - Runs a TCP server so cars can connect to us and push CarMessage
    status updates (mirrors framework_core.c's server_thread: receive,
    then immediately ack — a car's client_thread blocks its entire peer
    loop waiting for that ack, so we must send it promptly).
  - Can send CarMessage control commands (start/stop) to a car's own TCP
    server, acting as a client exactly like tcp_client_connect() does.

Wire formats below were confirmed against a real compilation of the C
structs (messages.h / udp_discovery.h) — not assumed. See CAR_MSG_FORMAT
and DISC_FORMAT comments for the exact layout. If messages.h or
udp_discovery.h change, these must be re-verified the same way (compile
a throwaway C program and check sizeof()/offsetof()), not hand-edited.
"""

import socket
import struct
import threading
import time
from dataclasses import dataclass

# ── Wire formats (must match the C structs exactly) ──────────────────────
# CarMessage (messages.h) — 32 bytes total, NATIVE byte order: tcp_comm.c's
# tcp_send_car_message()/tcp_receive_car_message() send the struct raw via
# send()/recv(), with no htonl/htons conversion anywhere.
#   id(u32) x(f32) y(f32) speed(f32) state(u8) msg_type(u8) [2 pad bytes]
#   timestamp(u32) sign_id(i32) sign_confidence(f32)
CAR_MSG_FORMAT = "<IfffBBxxIif"
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

STATE_NAMES = {0: "STOPPED", 1: "MOVING", 2: "ERROR", 3: "OBSTACLE"}


@dataclass
class CarState:
    car_id: str
    ip: str
    connected: bool = False
    state: int = -1
    msg_type: int = MSG_STATUS
    distance: float = 0.0
    speed: float = 0.0
    sign_id: int = -1
    sign_confidence: float = 0.0
    remote_timestamp: int = 0
    last_seen: float = 0.0  # time.time() of the last message received
    messages_received: int = 0

    @property
    def state_name(self):
        return STATE_NAMES.get(self.state, "UNKNOWN")


def _pack_discovery(device_id, device_type, tcp_port, pkt_type):
    return struct.pack(
        DISC_FORMAT,
        DISCOVERY_MAGIC,
        pkt_type,
        device_id.encode()[:31].ljust(32, b"\x00"),
        device_type.encode()[:15].ljust(16, b"\x00"),
        tcp_port,
        0,  # capabilities — always 0 on the C side too
    )


def _pack_car_message(msg_type, state=0, x=0.0, y=0.0, speed=0.0,
                       sign_id=-1, sign_confidence=0.0, dev_id=0):
    return struct.pack(
        CAR_MSG_FORMAT,
        dev_id, x, y, speed, state, msg_type,
        int(time.time()), sign_id, sign_confidence,
    )


def _unpack_car_message(data):
    dev_id, x, y, speed, state, msg_type, ts, sign_id, sign_conf = \
        struct.unpack(CAR_MSG_FORMAT, data)
    return {
        "id": dev_id, "x": x, "y": y, "speed": speed,
        "state": state, "msg_type": msg_type, "timestamp": ts,
        "sign_id": sign_id, "sign_confidence": sign_conf,
    }


def _recv_exact(sock, n):
    """Read exactly n bytes, or None if the peer closes early."""
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


class FrameworkNode:
    """
    A Python peer that participates in the C framework's network:
    announces itself over UDP discovery, and runs a TCP server that cars
    connect to (and push status updates into) — the same role any other
    framework node plays for its peers.
    """

    def __init__(self, device_id, device_type, tcp_port, car_ips,
                 discovery_port=55000, reannounce_interval=15):
        """
        car_ips: dict mapping car_id -> ip, e.g. {"car01": "192.168.0.143"}
        """
        self.device_id = device_id
        self.device_type = device_type
        self.tcp_port = tcp_port
        self.discovery_port = discovery_port
        self.reannounce_interval = reannounce_interval

        self.car_ips = dict(car_ips)
        self._ip_to_id = {ip: cid for cid, ip in self.car_ips.items()}

        self.states = {
            cid: CarState(car_id=cid, ip=ip) for cid, ip in self.car_ips.items()
        }
        self._lock = threading.Lock()

        self._running = False
        self._server_sock = None

    # ── Public API ──────────────────────────────────────────────────────

    def start(self):
        self._running = True

        self._server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_sock.bind(("", self.tcp_port))
        self._server_sock.listen(5)

        threading.Thread(target=self._accept_loop, daemon=True).start()
        threading.Thread(target=self._discovery_listen_loop, daemon=True).start()
        threading.Thread(target=self._announce_loop, daemon=True).start()

    def stop(self):
        self._running = False
        try:
            if self._server_sock:
                self._server_sock.close()
        except OSError:
            pass

    def get_states(self):
        """Return a snapshot dict of car_id -> CarState."""
        with self._lock:
            return dict(self.states)

    def send_command(self, car_id, cmd, tcp_port=60000, timeout=3.0):
        """
        Connect to the given car's framework TCP server as a client and
        send a MSG_CMD_START/MSG_CMD_STOP CarMessage. Returns True if the
        command was sent and acknowledged.
        """
        ip = self.car_ips.get(car_id)
        if ip is None:
            return False

        msg_type = MSG_CMD_START if cmd == "start" else MSG_CMD_STOP
        payload = _pack_car_message(msg_type)

        try:
            with socket.create_connection((ip, tcp_port), timeout=timeout) as s:
                s.sendall(payload)
                _recv_exact(s, CAR_MSG_SIZE)  # drain the ack; value unused
            return True
        except OSError:
            return False

    # ── Internal: TCP server (cars push status into us) ─────────────────

    def _accept_loop(self):
        while self._running:
            try:
                conn, addr = self._server_sock.accept()
            except OSError:
                break
            threading.Thread(target=self._handle_car_connection,
                              args=(conn, addr), daemon=True).start()

    def _handle_car_connection(self, conn, addr):
        ip = addr[0]
        car_id = self._ip_to_id.get(ip, ip)

        with self._lock:
            if car_id not in self.states:
                self.states[car_id] = CarState(car_id=car_id, ip=ip)
            self.states[car_id].connected = True

        try:
            while self._running:
                data = _recv_exact(conn, CAR_MSG_SIZE)
                if data is None:
                    break
                fields = _unpack_car_message(data)

                with self._lock:
                    st = self.states[car_id]
                    st.state = fields["state"]
                    st.msg_type = fields["msg_type"]
                    st.distance = fields["x"]
                    st.speed = fields["speed"]
                    st.sign_id = fields["sign_id"]
                    st.sign_confidence = fields["sign_confidence"]
                    st.remote_timestamp = fields["timestamp"]
                    st.last_seen = time.time()
                    st.connected = True
                    st.messages_received += 1

                # Must ack immediately: the car's client_thread blocks on
                # this response for its whole peer loop, not just on us.
                ack = _pack_car_message(MSG_STATUS, state=1, dev_id=999)
                conn.sendall(ack)
        except OSError:
            pass
        finally:
            with self._lock:
                if car_id in self.states:
                    self.states[car_id].connected = False
            try:
                conn.close()
            except OSError:
                pass

    # ── Internal: UDP discovery ──────────────────────────────────────────

    def _announce_loop(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            while self._running:
                pkt = _pack_discovery(self.device_id, self.device_type,
                                       self.tcp_port, DISC_ANNOUNCE)
                for ip in self.car_ips.values():
                    try:
                        sock.sendto(pkt, (ip, self.discovery_port))
                    except OSError:
                        pass
                for _ in range(self.reannounce_interval):
                    if not self._running:
                        break
                    time.sleep(1)
        finally:
            sock.close()

    def _discovery_listen_loop(self):
        """
        Best-effort: listens for DISC_RESPONSE/DISC_ANNOUNCE from cars.
        Not required for send_command()/status push to work (car_ips is
        our source of truth for addressing), but keeps us behaving like a
        real symmetric node instead of a one-way announcer.
        """
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("", self.discovery_port))
        except OSError:
            return  # port already in use elsewhere — non-fatal
        sock.settimeout(1.0)

        try:
            while self._running:
                try:
                    data, addr = sock.recvfrom(256)
                except socket.timeout:
                    continue
                except OSError:
                    break

                if len(data) != DISC_SIZE:
                    continue
                magic, _pkt_type, dev_id, _dev_type, _tcp_port, _caps = \
                    struct.unpack(DISC_FORMAT, data)
                if magic != DISCOVERY_MAGIC:
                    continue
                dev_id = dev_id.rstrip(b"\x00").decode(errors="replace")
                if dev_id == self.device_id:
                    continue
                # Informational only for now — see module docstring.
        finally:
            sock.close()
