#!/usr/bin/env python3
"""
shelf_server.py — Bridge + REST receiver for Shelf Analytics
============================================================
Runs two services:
  1. Serial bridge  : reads Arduino Uno JSON from COM port → broadcasts via WebSocket
  2. REST receiver  : receives ESP32-CAM POST payloads → stores + serves dashboard

Install: pip install flask flask-cors pyserial
Run:     python3 shelf_server.py --port COM3   (Windows)
         python3 shelf_server.py --port /dev/ttyUSB0  (Linux/Mac)
"""

import argparse
import json
import threading
import time
from datetime import datetime
from collections import deque

from flask import Flask, request, jsonify
from flask_cors import CORS

# Try serial — graceful if not installed
try:
    import serial
    SERIAL_AVAILABLE = True
except ImportError:
    SERIAL_AVAILABLE = False
    print("[WARN] pyserial not installed — serial bridge disabled")

# ─── Storage (in-memory, replace with SQLite/InfluxDB for prod) ──
events       = deque(maxlen=500)    # Rolling event log
sessions     = []                   # Completed sessions
metrics      = {
    "footfall": 0, "interested": 0, "pickups": 0,
    "add_to_cart": 0, "total_dwell_ms": 0, "sessions": 0
}
metrics_lock = threading.Lock()

app = Flask(__name__)
CORS(app)

# ─────────────────────────────────────────────────────────────────
# REST endpoints (for ESP32-CAM)
# ─────────────────────────────────────────────────────────────────

@app.route("/api/shelf-event", methods=["POST"])
def receive_event():
    """ESP32-CAM posts session data here."""
    try:
        data = request.get_json(force=True)
        data["server_ts"] = datetime.utcnow().isoformat()
        events.appendleft(data)

        # Update cumulative metrics
        with metrics_lock:
            if data.get("interested"):
                metrics["interested"] += 1
            if data.get("picked_up"):
                metrics["pickups"] += 1
            if data.get("added_to_cart"):
                metrics["add_to_cart"] += 1
            if data.get("dwell_ms"):
                metrics["total_dwell_ms"] += data["dwell_ms"]
                metrics["sessions"] += 1
            # Trust device's footfall counter
            if "footfall" in data:
                metrics["footfall"] = max(metrics["footfall"], data["footfall"])

        sessions.append(data)
        print(f"[ESP32] {data}")
        return jsonify({"status": "ok"}), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 400


@app.route("/api/metrics", methods=["GET"])
def get_metrics():
    """Dashboard polls this for current state."""
    with metrics_lock:
        m = dict(metrics)
    m["avg_dwell_ms"] = (
        m["total_dwell_ms"] // m["sessions"] if m["sessions"] > 0 else 0
    )
    m["interest_rate_pct"] = _pct(m["interested"], m["footfall"])
    m["pickup_rate_pct"]   = _pct(m["pickups"],    m["footfall"])
    m["cart_rate_pct"]     = _pct(m["add_to_cart"], m["footfall"])
    return jsonify(m)


@app.route("/api/events/latest", methods=["GET"])
def get_latest_events():
    """Return last N events for dashboard feed."""
    n = int(request.args.get("n", 20))
    return jsonify(list(events)[:n])


@app.route("/api/sessions", methods=["GET"])
def get_sessions():
    """Return all sessions (paginated)."""
    page  = int(request.args.get("page", 1))
    limit = int(request.args.get("limit", 50))
    start = (page - 1) * limit
    return jsonify({
        "total": len(sessions),
        "page": page,
        "data": sessions[start:start + limit]
    })


@app.route("/api/reset", methods=["POST"])
def reset():
    """Reset all metrics (dev/testing)."""
    with metrics_lock:
        for k in metrics:
            metrics[k] = 0
    events.clear()
    sessions.clear()
    return jsonify({"status": "reset"})


def _pct(n, d):
    return round((n / d) * 100, 1) if d > 0 else 0.0


# ─────────────────────────────────────────────────────────────────
# Serial bridge (for Arduino Uno)
# ─────────────────────────────────────────────────────────────────

def serial_bridge(port: str, baud: int = 115200):
    """
    Reads newline-delimited JSON from Arduino Uno serial port
    and injects into the same events store.
    """
    if not SERIAL_AVAILABLE:
        print("[Serial] pyserial not available, skipping bridge")
        return

    while True:
        try:
            print(f"[Serial] Connecting to {port} @ {baud}...")
            with serial.Serial(port, baud, timeout=2) as ser:
                print(f"[Serial] Connected to {port}")
                while True:
                    raw = ser.readline().decode("utf-8", errors="ignore").strip()
                    if not raw:
                        continue
                    try:
                        data = json.loads(raw)
                        data["source"]    = "arduino-uno"
                        data["server_ts"] = datetime.utcnow().isoformat()
                        events.appendleft(data)

                        # Mirror metrics updates
                        evt = data.get("event", "")
                        with metrics_lock:
                            if evt == "customer_detected":
                                metrics["footfall"] += 1
                            elif evt == "customer_interested":
                                metrics["interested"] += 1
                            elif evt == "item_pickup_detected":
                                metrics["pickups"] += 1
                            elif evt == "item_added_to_cart":
                                metrics["add_to_cart"] += 1
                            elif evt == "session_end":
                                metrics["sessions"] += 1
                                metrics["total_dwell_ms"] += data.get("dwell_ms", 0)

                        print(f"[Arduino] {data}")
                    except json.JSONDecodeError:
                        print(f"[Serial] Non-JSON: {raw}")
        except Exception as e:
            print(f"[Serial] Error: {e}. Retrying in 5s...")
            time.sleep(5)


# ─────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--port",  default=None,   help="Serial port for Arduino Uno (e.g. COM3 or /dev/ttyUSB0)")
    parser.add_argument("--baud",  default=115200,  type=int)
    parser.add_argument("--host",  default="0.0.0.0")
    parser.add_argument("--http",  default=5000,    type=int)
    args = parser.parse_args()

    if args.port:
        t = threading.Thread(target=serial_bridge, args=(args.port, args.baud), daemon=True)
        t.start()
    else:
        print("[Info] No --port specified; Arduino serial bridge inactive.")
        print("[Info] ESP32-CAM REST endpoint active on port", args.http)

    print(f"\n{'='*50}")
    print(f" Shelf Analytics Server")
    print(f" REST API : http://{args.host}:{args.http}/api/")
    print(f" Endpoints: /api/shelf-event (POST)")
    print(f"            /api/metrics     (GET)")
    print(f"            /api/events/latest (GET)")
    print(f"{'='*50}\n")

    app.run(host=args.host, port=args.http, debug=False, use_reloader=False)
