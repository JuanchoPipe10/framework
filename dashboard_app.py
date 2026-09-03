#!/usr/bin/env python3
"""
Autonomous Car Network — Streamlit Dashboard
Run with: streamlit run dashboard_app.py
Requires laptop_node.py running in a separate terminal (writes car_state.json).
"""

import streamlit as st
import json
import time
import os
import pandas as pd
from datetime import datetime

import laptop_node

STATE_FILE = "car_state.json"
REFRESH_RATE = 1.5  # seconds between auto-refreshes

st.set_page_config(
    page_title="Autonomous Car Network",
    page_icon="🚗",
    layout="wide",
    initial_sidebar_state="collapsed"
)

# Custom CSS
st.markdown("""
<style>
    .main { background-color: #0e1117; }
    .metric-card {
        background: #1a1d24;
        border-radius: 12px;
        padding: 20px;
        border: 1px solid #2d3139;
        margin-bottom: 10px;
    }
    .car-header {
        font-size: 1.4em;
        font-weight: bold;
        margin-bottom: 10px;
    }
    .status-moving   { color: #00ff88; }
    .status-obstacle { color: #ff8c00; }
    .status-stopped  { color: #888888; }
    .status-error    { color: #ff4444; }
    .status-unknown  { color: #888888; }
    .connected-dot   { color: #00ff88; font-size: 1.2em; }
    .disconnected-dot{ color: #ff4444; font-size: 1.2em; }
    .log-entry { font-family: monospace; font-size: 0.85em; padding: 2px 0; }
    .sign-detected {
        background: #2a1f00;
        border: 1px solid #ff8c00;
        border-radius: 8px;
        padding: 10px;
        margin: 5px 0;
    }
    .network-status {
        background: #0d1f0d;
        border: 1px solid #00ff88;
        border-radius: 8px;
        padding: 10px;
        margin: 5px 0;
    }
</style>
""", unsafe_allow_html=True)

def load_state():
    if not os.path.exists(STATE_FILE):
        return None
    try:
        with open(STATE_FILE) as f:
            return json.load(f)
    except Exception:
        return None

def get_status_class(state_label):
    mapping = {
        "MOVING": "moving",
        "OBSTACLE": "obstacle",
        "STOPPED": "stopped",
        "ERROR": "error"
    }
    return mapping.get(state_label, "unknown")

def render_car_card(car_id, car):
    connected = car.get("connected", False)
    state_label = car.get("state_label", "UNKNOWN")
    distance = car.get("distance", 999)
    sign_id = car.get("sign_id", -1)
    sign_conf = car.get("sign_confidence", 0.0)
    msgs = car.get("messages_received", 0)
    ip = car.get("ip", "?")
    last_seen = car.get("last_seen", "?")
    status_class = get_status_class(state_label)
    conn_dot = "🟢" if connected else "🔴"

    dist_display = f"{distance:.1f} cm" if distance < 500 else "—"

    st.markdown(f"""
    <div class="metric-card">
        <div class="car-header">{conn_dot} {car_id.upper()} <span style="font-size:0.6em;color:#888">{ip}</span></div>
        <div class="status-{status_class}" style="font-size:1.2em;font-weight:bold;margin-bottom:8px;">
            ⚡ {state_label}
        </div>
        <div style="display:flex;gap:30px;margin-bottom:8px;">
            <div><span style="color:#888">SRF05</span><br><b style="font-size:1.1em">{dist_display}</b></div>
            <div><span style="color:#888">Messages</span><br><b style="font-size:1.1em">{msgs}</b></div>
            <div><span style="color:#888">Last seen</span><br><b style="font-size:1.1em">{last_seen}</b></div>
        </div>
    """, unsafe_allow_html=True)

    if sign_id >= 0:
        st.markdown(f"""
        <div class="sign-detected">
            🚦 <b>SIGN DETECTED</b> — ID: {sign_id} | Confidence: {sign_conf:.0%}
        </div>
        """, unsafe_allow_html=True)

    st.markdown("</div>", unsafe_allow_html=True)

def render_car_controls(car_id):
    """Start/Stop buttons — send_command() is a short-lived, stateless
    TCP call (see laptop_node.py docstring), safe to call directly from
    this Streamlit process even though laptop_node.py's telemetry loop
    runs in a separate process."""
    col_start, col_stop = st.columns(2)
    with col_start:
        if st.button("▶ Start", key=f"start_{car_id}", use_container_width=True):
            ok = laptop_node.send_command(car_id, "start")
            if ok:
                st.toast(f"Sent START to {car_id}", icon="✅")
            else:
                st.toast(f"Failed to reach {car_id}", icon="⚠️")
    with col_stop:
        if st.button("■ Stop", key=f"stop_{car_id}", use_container_width=True):
            ok = laptop_node.send_command(car_id, "stop")
            if ok:
                st.toast(f"Sent STOP to {car_id}", icon="✅")
            else:
                st.toast(f"Failed to reach {car_id}", icon="⚠️")

def render_distance_chart(cars):
    chart_data = {}
    for car_id, car in cars.items():
        hist = car.get("dist_history", [])
        if hist:
            distances = [h["distance"] for h in hist if h["distance"] < 400]
            if distances:
                chart_data[car_id] = distances

    if chart_data:
        max_len = max(len(v) for v in chart_data.values())
        df_data = {}
        for car_id, dists in chart_data.items():
            padded = [None] * (max_len - len(dists)) + dists
            df_data[car_id] = padded
        df = pd.DataFrame(df_data)
        st.line_chart(df, height=200)

def render_log(log_entries):
    color_map = {
        "cyan": "#00bcd4",
        "orange": "#ff8c00",
        "red": "#ff4444",
        "yellow": "#ffd700",
        "gray": "#888888",
        "green": "#00ff88",
        "white": "#ffffff"
    }
    for entry in log_entries[:20]:
        color = color_map.get(entry.get("color", "white"), "#ffffff")
        car = entry.get("car", "?")
        event = entry.get("event", "")
        t = entry.get("time", "")
        st.markdown(
            f'<div class="log-entry"><span style="color:#555">{t}</span> '
            f'<span style="color:#00bcd4">[{car}]</span> '
            f'<span style="color:{color}">{event}</span></div>',
            unsafe_allow_html=True
        )

# ── Main Layout ────────────────────────────────────────────────
st.markdown("# 🚗 Autonomous Car Network")
st.markdown("---")

data = load_state()

if data is None:
    st.warning("⏳ Waiting for laptop_node.py to start... Run it in a separate terminal.")
else:
    cars = data.get("cars", {})
    log = data.get("log", [])
    laptop = data.get("laptop", {})

    # Top status bar
    n_connected = sum(1 for c in cars.values() if c.get("connected"))
    n_total = len(cars)
    col_a, col_b, col_c, col_d = st.columns(4)
    col_a.metric("🚗 Cars online", f"{n_connected}/{n_total}")
    col_b.metric("💻 Laptop node", "● ACTIVE" if laptop.get("connected") else "○ OFF")
    col_c.metric("📡 Network", "RUB-ES")
    col_d.metric("🕐 Time", datetime.now().strftime("%H:%M:%S"))

    st.markdown("---")

    # Car cards + controls
    if not cars:
        st.info("No cars configured yet.")
    else:
        car_cols = st.columns(len(cars))
        for i, (car_id, car) in enumerate(sorted(cars.items())):
            with car_cols[i]:
                render_car_card(car_id, car)
                render_car_controls(car_id)

    # Distance graph + log
    col_left, col_right = st.columns([2, 1])

    with col_left:
        st.markdown("### 📊 Distance History (SRF05)")
        if cars:
            render_distance_chart(cars)
        else:
            st.caption("No data yet")

    with col_right:
        st.markdown("### 📋 Event Log")
        if log:
            render_log(log)
        else:
            st.caption("No events yet")

    # Network topology
    st.markdown("---")
    st.markdown("### 🌐 Network Topology")
    topo_cols = st.columns(len(cars) + 1)
    with topo_cols[0]:
        st.markdown(f"""
        <div style="text-align:center;background:#1a1d24;border-radius:8px;padding:10px;border:1px solid #00ff88">
            💻<br><b>laptop</b><br><span style="color:#888;font-size:0.8em">{laptop.get('ip','?')}</span><br>
            <span style="color:#00ff88;font-size:0.75em">● node</span>
        </div>
        """, unsafe_allow_html=True)

    for i, (car_id, car) in enumerate(sorted(cars.items())):
        with topo_cols[i+1]:
            conn = car.get("connected", False)
            color = "#00ff88" if conn else "#ff4444"
            status = "● connected" if conn else "○ offline"
            st.markdown(f"""
            <div style="text-align:center;background:#1a1d24;border-radius:8px;padding:10px;border:1px solid {color}">
                🚗<br><b>{car_id}</b><br><span style="color:#888;font-size:0.8em">{car.get('ip','?')}</span><br>
                <span style="color:{color};font-size:0.75em">{status}</span>
            </div>
            """, unsafe_allow_html=True)

# Auto-refresh: rerun the whole script after a short pause. This runs
# LAST, after buttons above have already been rendered and handled —
# Streamlit processes a button click as its own immediate rerun, so it
# never gets stuck behind this sleep. (The previous version wrapped
# everything in `while True: ... time.sleep(1)`, which never returns
# control to Streamlit's runtime — button clicks would never register.)
time.sleep(REFRESH_RATE)
st.rerun()
