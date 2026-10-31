"""
STM32 & ESP32 Telemetry Test Harness & Simulator
Simulates the data flow from STM32 -> ESP32 -> Web Dashboard
Useful for offline testing, bench verification, and live booth demonstrations.
"""

import sys
import time
import random
import requests

TARGET_URL = "http://127.0.0.1:5000/api/telemetry"


def send_telemetry(flit_num, ack_rx, interval_ms, latency_us, status="NORMAL", raw=""):
    payload = {
        "flit_number": flit_num,
        "ack_received": ack_rx,
        "time_between_acks_ms": interval_ms,
        "latency_us": latency_us,
        "status": status,
        "raw_payload": raw or f"FLIT: {flit_num}, ACK: {1 if ack_rx else 0}, INTERVAL: {interval_ms}ms, LATENCY: {latency_us:.1f}us",
        "gateway_ip": "192.168.1.100"
    }

    try:
        r = requests.post(TARGET_URL, json=payload, timeout=1.0)
        print(f"[{time.strftime('%H:%M:%S')}] Flit #{flit_num:03d} | ACK: {ack_rx} | Interval: {interval_ms:4.0f}ms | Latency: {latency_us:5.1f}µs | Status: {status} -> HTTP {r.status_code}")
    except requests.exceptions.RequestException as e:
        print(f"[{time.strftime('%H:%M:%S')}] Failed to send to {TARGET_URL}: {e}")


def run_continuous_simulation():
    print("=" * 60)
    print("  Starting STM32 Telemetry Simulator (Target: 500ms cycle)")
    print("  Press Ctrl+C to terminate.")
    print("=" * 60)

    flit = 1
    total_acks = 0
    cycle = 0

    while True:
        try:
            cycle += 1
            # Every 25 cycles, simulate heartbeat fault
            if cycle % 28 == 0:
                print("\n>>> INJECTING HEARTBEAT TIMEOUT FAULT <<<")
                for _ in range(4):
                    send_telemetry(flit, False, 2200.0, 0.0, status="FAULT", raw="[FAULT] Heartbeat Timeout (Packet Lost)")
                    time.sleep(0.5)

                print(">>> SIMULATING LINK RECOVERY <<<")
                send_telemetry(flit, True, 502.0, 44.8, status="RECOVERED", raw=f"FLIT: {flit}, ACK: 1, INTERVAL: 502ms, LATENCY: 44.8us (Link Restored)")
                flit += 1
                time.sleep(0.5)
                continue

            # Every 15 cycles, inject high latency spike
            latency = 45.0 + random.uniform(-2.5, 3.5)
            if cycle % 15 == 0:
                latency = 220.0 + random.uniform(5, 35)
                print(">>> INJECTING LATENCY SPIKE <<<")

            interval = 500.0 + random.uniform(-4, 4)
            send_telemetry(flit, True, interval, latency, status="NORMAL")

            flit += 1
            time.sleep(0.5)  # 500ms delay matching STM32 delayMs(500)
        except KeyboardInterrupt:
            print("\nSimulator stopped by user.")
            break


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--single":
        send_telemetry(1, True, 500.0, 45.2, "NORMAL")
    else:
        run_continuous_simulation()
