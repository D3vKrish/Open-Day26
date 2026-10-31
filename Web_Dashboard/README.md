# Embedded Equipment Health & Communication Monitor

**Electronics & Robotics Club &bull; BITS Pilani**  
Real-time communication, round-trip latency measurement, and fault detection system utilizing **STM32F446RE**, **NXP / STM32 Slave**, and an **ESP32 Telemetry Gateway**.

---

## 1. System Architecture Overview

```
+--------------------+        CAN / SPI Bus        +--------------------+
|  STM32 #1 (Master) | <=========================> |  STM32 #2 / NXP    |
|   Equipment Node   |    REQUEST (N) / ACK (N)    |  Monitoring Node   |
+--------------------+                             +--------------------+
          |
          | UART Telemetry
          v (TX: GPIO PA2/PA9 -> RX: GPIO 16, 115200 baud)
+--------------------+
|   ESP32 Gateway    |
|  Wireless Bridge   |
+--------------------+
          |
          | Wi-Fi HTTP POST & SSE (/api/telemetry)
          v
+--------------------+
|   Web Dashboard    |
|  Mission Control   | (http://localhost:5000)
+--------------------+
```

### Hardware Breakdown:
1. **STM32 #1 Equipment Node (`STM32_SPI/`)**:
   - Master node initiating 64-byte payload transfers every `500ms`.
   - Microsecond stopwatch hardware timer (`TIM2` prescaled to 1 MHz: `TIM2->PSC = 31`).
   - Computes Round-Trip Time ($RTT = t_{ACK} - t_{REQUEST}$).
   - Sends flit number, ACK flag, interval, and RTT over UART to ESP32.
2. **STM32 #2 / NXP Monitoring Node (`NXP_SPI/`)**:
   - Slave receiver using DMA/EDMA.
   - Verifies received flits and replies with acknowledgment packets (`'A', 'C', 'K'`).
3. **ESP32 Telemetry Gateway (`ESP32_Dashboard/`)**:
   - Connects to STM32 UART2 on **GPIO 16 (RX)** and **GPIO 17 (TX)** at **115200 baud**.
   - Intelligent parser for multi-format STM32 telemetry (JSON, Key-Value, CSV, formatted strings).
   - Autonomous **Fault Detection State Machine**:
     - `NORMAL`: Heartbeats received regularly (< 2000ms).
     - `FAULT`: Communication loss detected when heartbeats/ACKs cease > 2000ms.
     - `RECOVERED`: Transition state when communication is re-established.
   - Forwards structured JSON payloads over Wi-Fi to the central Web Dashboard endpoint: `http://<SERVER_IP>:5000/api/telemetry`.
   - Houses an emergency diagnostic web portal directly at `http://<ESP32_IP>/`.
4. **Mission Control Web Dashboard (`Web_Dashboard/`)**:
   - Python Flask backend with sub-millisecond Server-Sent Events (SSE).
   - High-performance real-time Chart.js waveforms for RTT latency and inter-ACK intervals.
   - Interactive system architecture topology with animated packet tracers on the bus.
   - Integrated **Fault Injection Console** for demonstrating fault loss and recovery on demand without pulling physical wires.
   - Live Event & Fault Log with filtering, search, and CSV/JSON export.

---

## 2. Quick Start: Launching the Web Dashboard

### Windows 1-Click Launch:
Double-click `run_dashboard.bat` in `c:\Open-Day\Web_Dashboard\`.

### Command Line Launch:
```powershell
cd c:\Open-Day\Web_Dashboard
python server.py
```
Open **`http://localhost:5000`** in any web browser.

---

## 3. Configuring the ESP32 Gateway

Open `ESP32_Dashboard/ESP32_Dashboard.ino` in Arduino IDE:
1. Set your local Wi-Fi SSID and Password:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```
2. Set the IP address of the machine running `server.py`:
   ```cpp
   const char* serverName = "http://192.168.1.XXX:5000/api/telemetry";
   ```
3. Wire the hardware connections:
   - **ESP32 GPIO 16 (RX2)** $\rightarrow$ **STM32 TX pin**
   - **ESP32 GPIO 17 (TX2)** $\rightarrow$ **STM32 RX pin** (optional)
   - **ESP32 GND** $\rightarrow$ **STM32 GND** (Common ground is mandatory)

---

## 4. Telemetry Stream Formats Supported by ESP32

The ESP32 firmware parser accepts any of the following standard embedded UART output formats:

* **Key-Value String:**
  ```
  FLIT: 42, ACK: 1, TIME_BETWEEN_ACKS: 502, LATENCY: 45.2
  ```
* **CSV Format:**
  ```
  42,1,502,45.2
  ```
* **JSON Format:**
  ```json
  {"flit": 42, "ack": 1, "time_between_acks": 502, "latency": 45.2}
  ```
* **Single / Labeled RTT:**
  ```
  RTT: 45.2us
  ```

---

## 5. Live Demonstration & Fault Injection

The dashboard includes a dedicated **Fault Injection Console**:
- **Send Normal Flit:** Injects healthy telemetry (~45 µs RTT).
- **Inject Heartbeat Timeout (Fault):** Simulates node disconnection, triggering the `FAULT` state and warning alarm.
- **Restore Link (Recovered):** Re-establishes the link, transitioning to `RECOVERED` with a harmonic recovery chime.
- **Latency Spike:** Simulates high bus traffic (> 200 µs RTT).
- **Corrupt / Drop ACK:** Simulates packet loss.
- **Auto Demo Mode:** Autonomous loop cycling through normal operation, fault injection, and recovery for hands-free presentations.
