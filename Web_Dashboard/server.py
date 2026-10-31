"""
Embedded Equipment Health & Communication Monitor - Web Dashboard Backend
Electronics & Robotics Club | BITS Pilani

Features:
- Telemetry ingest endpoint: POST /api/telemetry (from ESP32 Gateway)
- Real-time Server-Sent Events (SSE): GET /api/stream (to Web Dashboard)
- Historical telemetry buffer: GET /api/telemetry/history
- Event & Fault logging: GET /api/events
- Interactive Fault Injection: POST /api/simulate
- Autonomous Demo Simulator: POST /api/demo/toggle
- CSV / JSON telemetry export: GET /api/export
"""

import os
import sys
import time
import json
import random
import threading
from collections import deque
from datetime import datetime
from flask import Flask, request, jsonify, render_template, Response
from flask_cors import CORS

app = Flask(__name__, static_folder="static", template_folder="templates")
CORS(app)

# ==============================================================================
# In-Memory State & Ring Buffers
# ==============================================================================
MAX_HISTORY = 500
MAX_EVENTS = 200

telemetry_history = deque(maxlen=MAX_HISTORY)
event_log = deque(maxlen=MAX_EVENTS)

# Client notification queues for SSE
sse_subscribers = []
sse_lock = threading.Lock()

current_state = {
    "flit_number": 0,
    "ack_received": True,
    "time_between_acks_ms": 500.0,
    "latency_us": 45.2,
    "status": "NORMAL",  # NORMAL | FAULT | RECOVERED
    "uptime_ms": 0,
    "total_flits": 0,
    "total_acks": 0,
    "packet_loss_pct": 0.0,
    "jitter_ms": 1.2,
    "min_latency_us": 42.0,
    "max_latency_us": 58.4,
    "avg_latency_us": 45.8,
    "raw_payload": "FLIT: 0, ACK: 1, TIME_BETWEEN_ACKS: 500, LATENCY: 45.2",
    "gateway_ip": "192.168.1.100",
    "last_seen_timestamp": time.time(),
    "is_live_hardware": False,
    "demo_mode": False
}

# Add initial system boot event
event_log.append({
    "id": 1,
    "timestamp": datetime.now().strftime("%H:%M:%S.%f")[:-3],
    "type": "INFO",
    "title": "System Initialized",
    "message": "Telemetry server listening on port 5000. Waiting for ESP32 gateway...",
    "state": "NORMAL"
})


def add_event(event_type, title, message, state=None):
    event_id = len(event_log) + 1
    event = {
        "id": event_id,
        "timestamp": datetime.now().strftime("%H:%M:%S.%f")[:-3],
        "type": event_type,  # INFO | FAULT | RECOVERY | WARNING
        "title": title,
        "message": message,
        "state": state or current_state["status"]
    }
    event_log.appendleft(event)
    broadcast_sse({"event": "system_event", "data": event})


def broadcast_sse(data):
    """Push event data to all connected browser SSE streams."""
    with sse_lock:
        stale = []
        for q in sse_subscribers:
            try:
                q.append(data)
            except Exception:
                stale.append(q)
        for s in stale:
            if s in sse_subscribers:
                sse_subscribers.remove(s)


def update_telemetry(payload, is_hardware=True):
    """Processes incoming telemetry from ESP32 or simulation."""
    global current_state

    prev_status = current_state["status"]
    new_status = payload.get("status", "NORMAL").upper()

    flit_num = int(payload.get("flit_number", current_state["flit_number"] + 1))
    ack_rx = bool(payload.get("ack_received", True))
    interval_ms = float(payload.get("time_between_acks_ms", payload.get("time_between_acks", 500.0)))
    latency_us = float(payload.get("latency_us", payload.get("latency", 45.0)))

    # Compute totals and loss
    total_flits = int(payload.get("total_flits", current_state["total_flits"] + 1))
    total_acks = int(payload.get("total_acks", current_state["total_acks"] + (1 if ack_rx else 0)))
    loss_pct = float(payload.get("packet_loss_pct", 0.0))
    if total_flits > 0 and "packet_loss_pct" not in payload:
        loss_pct = round(max(0.0, (total_flits - total_acks) / total_flits * 100.0), 2)

    jitter_ms = float(payload.get("jitter_ms", current_state["jitter_ms"]))

    # Update state
    current_state["flit_number"] = flit_num
    current_state["ack_received"] = ack_rx
    current_state["time_between_acks_ms"] = interval_ms
    current_state["latency_us"] = latency_us
    current_state["status"] = new_status
    current_state["uptime_ms"] = int(payload.get("uptime_ms", int(time.time() * 1000)))
    current_state["total_flits"] = total_flits
    current_state["total_acks"] = total_acks
    current_state["packet_loss_pct"] = loss_pct
    current_state["jitter_ms"] = jitter_ms
    current_state["min_latency_us"] = float(payload.get("min_latency_us", min(current_state["min_latency_us"], latency_us)))
    current_state["max_latency_us"] = float(payload.get("max_latency_us", max(current_state["max_latency_us"], latency_us)))
    current_state["avg_latency_us"] = float(payload.get("avg_latency_us", round((current_state["avg_latency_us"] * 0.95) + (latency_us * 0.05), 2)))
    current_state["raw_payload"] = payload.get("raw_payload", str(payload))
    current_state["gateway_ip"] = payload.get("gateway_ip", current_state["gateway_ip"])
    current_state["last_seen_timestamp"] = time.time()
    current_state["is_live_hardware"] = is_hardware

    data_point = {
        "timestamp": datetime.now().strftime("%H:%M:%S.%f")[:-3],
        "flit_number": flit_num,
        "ack_received": ack_rx,
        "time_between_acks_ms": interval_ms,
        "latency_us": latency_us,
        "status": new_status,
        "total_flits": total_flits,
        "total_acks": total_acks,
        "packet_loss_pct": loss_pct,
        "jitter_ms": jitter_ms
    }
    telemetry_history.append(data_point)

    # State transition detection and logging
    if prev_status != new_status:
        if new_status == "FAULT":
            add_event("FAULT", "Heartbeat Timeout Detected",
                      f"Communication loss between STM32 and slave! No ACK or heartbeat received. Flit: {flit_num}",
                      state="FAULT")
        elif new_status == "RECOVERED":
            add_event("RECOVERY", "Communication Restored",
                      f"Valid ACK received from STM32. Bus synchronized. RTT: {latency_us:.1f} µs",
                      state="RECOVERED")
        elif new_status == "NORMAL" and prev_status == "RECOVERED":
            add_event("INFO", "Normal Operation Resumed",
                      f"Continuous healthy heartbeats confirmed at ~{interval_ms:.0f}ms interval.",
                      state="NORMAL")

    # Broadcast to web dashboard
    broadcast_sse({"event": "telemetry", "data": data_point, "state": current_state})


# ==============================================================================
# Autonomous Demo Mode Simulator
# ==============================================================================
demo_running = False
demo_thread = None


def demo_simulator_loop():
    global demo_running
    flit = 1
    step_count = 0
    in_fault = False
    fault_timer = 0

    while demo_running:
        time.sleep(0.5)  # 500ms cycle matching STM32 delayMs(500)
        step_count += 1

        # Check if in simulated fault
        if in_fault:
            fault_timer -= 1
            if fault_timer <= 0:
                # Recover
                in_fault = False
                payload = {
                    "flit_number": flit,
                    "ack_received": True,
                    "time_between_acks_ms": round(500.0 + random.uniform(-6, 6), 2),
                    "latency_us": round(44.0 + random.uniform(0, 5), 2),
                    "status": "RECOVERED",
                    "raw_payload": f"FLIT: {flit}, ACK: 1, TIME_BETWEEN_ACKS: 502, LATENCY: 44.8"
                }
                flit += 1
                update_telemetry(payload, is_hardware=False)
            else:
                # Remain in fault
                payload = {
                    "flit_number": flit,
                    "ack_received": False,
                    "time_between_acks_ms": 2000.0 + (fault_timer * 500),
                    "latency_us": 0.0,
                    "status": "FAULT",
                    "raw_payload": "[FAULT] Heartbeat Timeout (Packet dropped / link loss)"
                }
                update_telemetry(payload, is_hardware=False)
            continue

        # Normal operation: periodically inject fault after 20 steps (10 seconds)
        if step_count % 35 == 0:
            in_fault = True
            fault_timer = 6  # 3 seconds in fault
            payload = {
                "flit_number": flit,
                "ack_received": False,
                "time_between_acks_ms": 2100.0,
                "latency_us": 0.0,
                "status": "FAULT",
                "raw_payload": "[FAULT] Heartbeat loss timeout!"
            }
            update_telemetry(payload, is_hardware=False)
            continue

        # Occasional minor jitter or latency fluctuation for realism
        base_latency = 44.5
        if random.random() < 0.15:
            base_latency += random.uniform(5.0, 18.0)  # minor latency spike

        interval = 500.0 + random.uniform(-4.0, 4.0)
        status = "NORMAL"

        payload = {
            "flit_number": flit,
            "ack_received": True,
            "time_between_acks_ms": round(interval, 2),
            "latency_us": round(base_latency + random.uniform(-1.0, 2.0), 2),
            "status": status,
            "raw_payload": f"FLIT: {flit}, ACK: 1, TIME_BETWEEN_ACKS: {int(interval)}, LATENCY: {base_latency:.1f}"
        }
        flit += 1
        update_telemetry(payload, is_hardware=False)


# ==============================================================================
# Routes & Endpoints
# ==============================================================================

@app.after_request
def add_cache_headers(response):
    response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
    response.headers["Pragma"] = "no-cache"
    response.headers["Expires"] = "0"
    return response


@app.route("/")
def index():
    return render_template("index.html")


@app.route("/api/telemetry", methods=["POST"])
def receive_telemetry():
    """Receives JSON telemetry from ESP32 gateway."""
    data = request.get_json(silent=True) or {}
    # Also support form or raw string if needed
    if not data and request.data:
        try:
            data = json.loads(request.data.decode("utf-8"))
        except Exception:
            data = {"raw_payload": request.data.decode("utf-8", errors="ignore")}

    update_telemetry(data, is_hardware=True)
    return jsonify({"status": "success", "received_flit": current_state["flit_number"]}), 200


@app.route("/api/telemetry/latest", methods=["GET"])
def get_latest_telemetry():
    return jsonify(current_state)


@app.route("/api/telemetry/history", methods=["GET"])
def get_telemetry_history():
    return jsonify(list(telemetry_history))


@app.route("/api/events", methods=["GET"])
def get_events():
    return jsonify(list(event_log))


@app.route("/api/simulate", methods=["POST"])
def simulate_action():
    """Manual fault injection & testing API."""
    data = request.get_json(silent=True) or {}
    action = data.get("action", "normal").lower()

    global current_state
    flit = current_state["flit_number"] + 1

    if action == "toggle":
        if current_state["status"] == "FAULT":
            action = "recover"
        else:
            action = "fault"

    if action == "normal":
        payload = {
            "flit_number": flit,
            "ack_received": True,
            "time_between_acks_ms": 501.0 + random.uniform(-3, 3),
            "latency_us": round(44.0 + random.uniform(0, 4), 2),
            "status": "NORMAL",
            "raw_payload": f"FLIT: {flit}, ACK: 1, TIME_BETWEEN_ACKS: 501, LATENCY: 45.2"
        }
        update_telemetry(payload, is_hardware=False)
        return jsonify({"result": "Injected normal flit", "payload": payload})

    elif action == "fault":
        payload = {
            "flit_number": flit,
            "ack_received": False,
            "time_between_acks_ms": 2500.0,
            "latency_us": 0.0,
            "status": "FAULT",
            "raw_payload": "[SIMULATED FAULT] Heartbeat timeout & ACK lost!"
        }
        update_telemetry(payload, is_hardware=False)
        return jsonify({"result": "Injected heartbeat fault", "payload": payload})

    elif action == "recover":
        payload = {
            "flit_number": flit,
            "ack_received": True,
            "time_between_acks_ms": 500.0,
            "latency_us": 44.5,
            "status": "RECOVERED",
            "raw_payload": f"FLIT: {flit}, ACK: 1, TIME_BETWEEN_ACKS: 500, LATENCY: 44.5 (Restored)"
        }
        update_telemetry(payload, is_hardware=False)
        return jsonify({"result": "Injected recovery", "payload": payload})

    elif action == "high_latency":
        payload = {
            "flit_number": flit,
            "ack_received": True,
            "time_between_acks_ms": 520.0,
            "latency_us": round(215.0 + random.uniform(10, 45), 2),
            "status": "NORMAL",
            "raw_payload": f"FLIT: {flit}, ACK: 1, TIME_BETWEEN_ACKS: 520, LATENCY: 235.0 (High RTT Jitter)"
        }
        update_telemetry(payload, is_hardware=False)
        add_event("WARNING", "Latency Spike / Bus Congestion",
                  f"Round-trip latency surged to {payload['latency_us']:.1f} µs (Threshold: 100 µs)",
                  state="NORMAL")
        return jsonify({"result": "Injected high latency", "payload": payload})

    elif action == "packet_drop":
        payload = {
            "flit_number": flit,
            "ack_received": False,
            "time_between_acks_ms": 1010.0,
            "latency_us": 0.0,
            "status": "NORMAL",
            "raw_payload": f"FLIT: {flit}, ACK: 0 (Corrupted ACK / NACK)"
        }
        update_telemetry(payload, is_hardware=False)
        add_event("WARNING", "ACK Not Received",
                  f"Flit {flit} sent but slave response was corrupted or unacknowledged.",
                  state="NORMAL")
        return jsonify({"result": "Injected packet drop", "payload": payload})

    return jsonify({"error": "Unknown action"}), 400


@app.route("/api/demo/toggle", methods=["POST"])
def toggle_demo():
    global demo_running, demo_thread
    demo_running = not demo_running
    current_state["demo_mode"] = demo_running

    if demo_running:
        demo_thread = threading.Thread(target=demo_simulator_loop, daemon=True)
        demo_thread.start()
        add_event("INFO", "Auto Demo Simulator Started",
                  "Generating simulated STM32/NXP telemetry with periodic fault injection.",
                  state=current_state["status"])
    else:
        add_event("INFO", "Auto Demo Simulator Stopped",
                  "Listening exclusively for physical hardware / manual inputs.",
                  state=current_state["status"])

    return jsonify({"demo_running": demo_running})


@app.route("/api/export", methods=["GET"])
def export_data():
    """Exports historical telemetry as CSV or JSON."""
    fmt = request.args.get("format", "csv").lower()
    if fmt == "json":
        return Response(
            json.dumps(list(telemetry_history), indent=2),
            mimetype="application/json",
            headers={"Content-Disposition": "attachment;filename=telemetry_export.json"}
        )

    # CSV format
    lines = ["Timestamp,FlitNumber,AckReceived,TimeBetweenAcks_ms,Latency_us,Status,TotalFlits,TotalAcks,LossPct"]
    for d in telemetry_history:
        lines.append(f"{d['timestamp']},{d['flit_number']},{d['ack_received']},{d['time_between_acks_ms']},"
                     f"{d['latency_us']},{d['status']},{d['total_flits']},{d['total_acks']},{d['packet_loss_pct']}")
    csv_content = "\n".join(lines)
    return Response(
        csv_content,
        mimetype="text/csv",
        headers={"Content-Disposition": "attachment;filename=telemetry_export.csv"}
    )


@app.route("/api/stream")
def sse_stream():
    """Server-Sent Events endpoint for real-time frontend push."""
    def event_generator():
        q = deque(maxlen=20)
        with sse_lock:
            sse_subscribers.append(q)
        try:
            # Send initial sync
            sync_payload = {
                "event": "sync",
                "state": current_state,
                "history": list(telemetry_history),
                "events": list(event_log)[:25]
            }
            yield f"data: {json.dumps(sync_payload)}\n\n"

            while True:
                if q:
                    item = q.popleft()
                    yield f"data: {json.dumps(item)}\n\n"
                else:
                    time.sleep(0.08)
        except GeneratorExit:
            with sse_lock:
                if q in sse_subscribers:
                    sse_subscribers.remove(q)

    return Response(event_generator(), mimetype="text/event-stream")


if __name__ == "__main__":
    print("\n" + "=" * 60)
    print("  Embedded Equipment Health & Communication Monitor")
    print("             Electronics & Robotics Club           ")
    print("               BITS Pilani Dashboard               ")
    print("=" * 60)
    print(" * Dashboard URL: http://localhost:5000")
    print(" * ESP32 Gateway Endpoint: http://<YOUR_LOCAL_IP>:5000/api/telemetry")
    print(" * Demo Simulator: Use dashboard UI or POST /api/demo/toggle")
    print("=" * 60 + "\n")
    app.run(host="0.0.0.0", port=5000, debug=False, threaded=True)
