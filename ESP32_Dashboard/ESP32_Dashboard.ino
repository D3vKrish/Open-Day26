/*
 * ==============================================================================
 * Project: Embedded Equipment Health & Communication Monitor
 * Component: ESP32 Unified SPI Slave & Telemetry Gateway
 * Organization: Electronics & Robotics Club | BITS Pilani
 *
 * Description:
 *   1. Hardware SPI Slave (VSPI / SPI3_HOST with DMA):
 *      - Direct SPI connection with STM32 Master (64-byte flit transfer).
 *      - Automatically replies with ASCII "ACK" ('A', 'C', 'K') in response payload.
 *      - Uses dedicated FreeRTOS background task pinned to Core 0.
 *      - Default SPI Pins:
 *          MOSI: GPIO 23  <--- Connect to STM32 PA7 (CN5 Pin 4 / D11)
 *          MISO: GPIO 19  ---> Connect to STM32 PA6 (CN5 Pin 5 / D12)
 *          SCLK: GPIO 18  <--- Connect to STM32 PA5 (CN5 Pin 6 / D13)
 *          CS:   GPIO 5   <--- Connect to STM32 PA4 (CN8 Pin 3 / A2)
 *          GND:  GND      <--- Common Ground with STM32
 *
 *   2. UART Telemetry Receiver (UART2):
 *      - Standard ESP32 pins: GPIO 16 (RX) <--- STM32 TX
 *                             GPIO 17 (TX) ---> STM32 RX
 *      - Receives formatted string with hardware microsecond RTT latency:
 *        "FLIT: 12, ACK: 1, TIME_BETWEEN_ACKS: 504, LATENCY: 46.5"
 *
 *   3. Telemetry Gateway & Health State Machine:
 *      - Maintains states: NORMAL, FAULT (timeout >2000ms), RECOVERED.
 *      - Statistical KPIs: Min/Avg/Max RTT, Jitter, Packet Loss %.
 *      - Forwards telemetry via HTTP POST to Python Web Dashboard (/api/telemetry).
 *      - Hosts on-board emergency Web Dashboard on Port 80 (http://<ESP32_IP>/).
 * ==============================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "driver/spi_slave.h"
#include "esp_heap_caps.h"

// ==============================================================================
// 1. Wi-Fi Configuration
// ==============================================================================
const char* ssid = "OnePlus 12R";
const char* password = "hotspot@123";

// ==============================================================================
// 2. Python Dashboard Endpoint (Computer's IP address)
// ==============================================================================
// Current PC Wi-Fi IP address is 192.168.42.43
const char* serverName = "http://192.168.42.43:5000/api/telemetry";

// ==============================================================================
// 3. UART Configuration for STM32 Communication
// ==============================================================================
#define STM32_RX 16 
#define STM32_TX 17 
#define STM32_BAUD 115200

// ==============================================================================
// 4. Hardware SPI Slave Configuration (VSPI / SPI3_HOST)
// ==============================================================================
#define SPI_SLAVE_MOSI 23
#define SPI_SLAVE_MISO 19
#define SPI_SLAVE_SCLK 18
#define SPI_SLAVE_CS    5

#define SPI_BUFFER_SIZE 64

// DMA-capable transmit and receive buffers
static uint8_t* spi_tx_buf = NULL;
static uint8_t* spi_rx_buf = NULL;

// Cross-core flag and buffer for received SPI packets
static volatile bool spiPacketPending = false;
static uint8_t lastSpiRxPacket[SPI_BUFFER_SIZE];
static size_t lastSpiRxLength = 0;

// Task handle for SPI Slave
TaskHandle_t spiSlaveTaskHandle = NULL;

// ==============================================================================
// 5. Telemetry State & Health Tracking
// ==============================================================================
enum SystemState {
  STATE_NORMAL,
  STATE_FAULT,
  STATE_RECOVERED
};

struct TelemetryData {
  long flitNumber;
  bool ackReceived;
  unsigned long timeBetweenAcks; // in milliseconds
  float latencyUs;               // RTT in microseconds
  SystemState state;
  String rawPayload;
  unsigned long lastPacketTimestamp;
  unsigned long lastAckTimestamp;
};

TelemetryData currentTelemetry = {
  0,        // flitNumber
  false,    // ackReceived
  0,        // timeBetweenAcks
  0.0f,     // latencyUs
  STATE_NORMAL,
  "",       // rawPayload
  0,        // lastPacketTimestamp
  0         // lastAckTimestamp
};

// Statistical Accumulators
unsigned long totalFlitsReceived = 0;
unsigned long totalAcksReceived  = 0;
unsigned long totalFaultEvents   = 0;
float minLatencyUs = 999999.0f;
float maxLatencyUs = 0.0f;
float avgLatencyUs = 0.0f;
unsigned long latencySamples = 0;
float lastJitterMs = 0.0f;
unsigned long prevIntervalMs = 0;

// Fault Detection Thresholds
const unsigned long HEARTBEAT_TIMEOUT_MS = 2500;
unsigned long lastHeartbeatCheck = 0;
unsigned long lastStateTransitionTime = 0;

// On-board Web Server (Port 80)
WebServer server(80);

// Forward declarations
void handleRoot();
void handleStatusJson();
void parseStm32Data(const String& raw);
void forwardToDashboard();
void checkHeartbeatLoss();
void handleSerialCommands();
void processSpiPacket();
int findFirstDigit(const String& str, int fromIndex = 0);

int findFirstDigit(const String& str, int fromIndex) {
  int len = str.length();
  for (int i = max(0, fromIndex); i < len; i++) {
    char c = str.charAt(i);
    if (c >= '0' && c <= '9') {
      return i;
    }
  }
  return -1;
}

// ==============================================================================
// SPI Slave Background Task (Runs on Core 0)
// ==============================================================================
void spiSlaveTask(void *pvParameters) {
  Serial.println("[SPI SLAVE] Task running on Core 0");

  // Allocate DMA-capable memory
  spi_tx_buf = (uint8_t*)heap_caps_malloc(SPI_BUFFER_SIZE, MALLOC_CAP_DMA);
  spi_rx_buf = (uint8_t*)heap_caps_malloc(SPI_BUFFER_SIZE, MALLOC_CAP_DMA);

  if (!spi_tx_buf || !spi_rx_buf) {
    Serial.println("[SPI SLAVE ERROR] Failed to allocate DMA buffers!");
    vTaskDelete(NULL);
    return;
  }

  // Pre-load default ACK response: ASCII "ACK"
  memset(spi_tx_buf, 0, SPI_BUFFER_SIZE);
  spi_tx_buf[0] = 'A';
  spi_tx_buf[1] = 'C';
  spi_tx_buf[2] = 'K';
  for (int i = 3; i < SPI_BUFFER_SIZE; i++) {
    spi_tx_buf[i] = (uint8_t)i;
  }

  // Configure SPI Bus
  spi_bus_config_t buscfg = {};
  buscfg.mosi_io_num = SPI_SLAVE_MOSI;
  buscfg.miso_io_num = SPI_SLAVE_MISO;
  buscfg.sclk_io_num = SPI_SLAVE_SCLK;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = SPI_BUFFER_SIZE;

  // Configure SPI Slave Interface (Mode 0: CPOL=0, CPHA=0)
  spi_slave_interface_config_t slvcfg = {};
  slvcfg.spics_io_num = SPI_SLAVE_CS;
  slvcfg.flags = 0;
  slvcfg.queue_size = 3;
  slvcfg.mode = 0;
  slvcfg.post_setup_cb = NULL;
  slvcfg.post_trans_cb = NULL;

  // Enable internal pull-ups on input pins
  gpio_set_pull_mode((gpio_num_t)SPI_SLAVE_MOSI, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode((gpio_num_t)SPI_SLAVE_SCLK, GPIO_PULLDOWN_ONLY); // Mode 0 idles low
  gpio_set_pull_mode((gpio_num_t)SPI_SLAVE_CS,   GPIO_PULLUP_ONLY);   // CS idles high

  // Initialize SPI Slave with DMA Channel
  esp_err_t ret = spi_slave_initialize(SPI3_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    Serial.printf("[SPI SLAVE ERROR] Initialization failed with error: 0x%X\n", ret);
    vTaskDelete(NULL);
    return;
  }

  Serial.println("[SPI SLAVE] Hardware interface initialized successfully!");
  Serial.printf("[SPI SLAVE] Pins -> MOSI:%d, MISO:%d, SCK:%d, CS:%d\n",
                SPI_SLAVE_MOSI, SPI_SLAVE_MISO, SPI_SLAVE_SCLK, SPI_SLAVE_CS);

  // Main SPI Slave Transaction Loop
  while (1) {
    memset(spi_rx_buf, 0, SPI_BUFFER_SIZE);

    // Keep ACK in TX buffer
    spi_tx_buf[0] = 'A';
    spi_tx_buf[1] = 'C';
    spi_tx_buf[2] = 'K';

    spi_slave_transaction_t t = {};
    t.length = SPI_BUFFER_SIZE * 8; // length in bits
    t.tx_buffer = spi_tx_buf;
    t.rx_buffer = spi_rx_buf;

    // Block waiting for STM32 Master to clock in a transfer
    ret = spi_slave_transmit(SPI3_HOST, &t, portMAX_DELAY);
    if (ret == ESP_OK) {
      size_t bytes = t.trans_len / 8;
      if (bytes == 0) bytes = SPI_BUFFER_SIZE; // Fallback if trans_len not set by hardware

      // Copy to shared buffer for Core 1 loop processing
      memcpy(lastSpiRxPacket, spi_rx_buf, SPI_BUFFER_SIZE);
      lastSpiRxLength = bytes;
      spiPacketPending = true;
    }
  }
}

// ==============================================================================
// Setup Routine
// ==============================================================================
void setup() {
  // 1. Initialize Serial Monitor for debugging
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=======================================================");
  Serial.println("  Embedded Equipment Health & Communication Monitor    ");
  Serial.println("    ESP32 Unified SPI Slave & Telemetry Gateway        ");
  Serial.println("=======================================================");

  // 2. Initialize UART2 communication with STM32
  Serial2.begin(STM32_BAUD, SERIAL_8N1, STM32_RX, STM32_TX);
  Serial2.setTimeout(50); // Prevent loop stalling on partial lines
  Serial.printf("[UART] Listening for STM32 on RX:%d, TX:%d @ %d baud\n", STM32_RX, STM32_TX, STM32_BAUD);

  // 3. Connect to Wi-Fi
  Serial.printf("[WIFI] Connecting to SSID: %s ", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 8000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected Successfully!");
    Serial.print("[WIFI] ESP32 Gateway IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("[WIFI] Target Dashboard: %s\n", serverName);
  } else {
    Serial.println("\n[WIFI] Wi-Fi connection timed out. Gateway operating in standalone fallback mode.");
    Serial.println("[WIFI] Tip: Update 'ssid' and 'password' if you wish to forward to PC Dashboard.");
  }

  // 4. Setup On-board Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatusJson);
  server.begin();
  Serial.println("[HTTP] Local ESP32 diagnostic web server listening on port 80");

  // 5. Start Hardware SPI Slave Background Task on Core 0
  xTaskCreatePinnedToCore(
    spiSlaveTask,
    "spi_slave_task",
    4096,
    NULL,
    2,
    &spiSlaveTaskHandle,
    0
  );

  currentTelemetry.lastPacketTimestamp = millis();
  currentTelemetry.lastAckTimestamp = millis();
  Serial.println("[SYS] Ready. Monitoring both SPI and UART feeds.\n");
}

// ==============================================================================
// Main Loop (Runs on Core 1)
// ==============================================================================
void loop() {
  // Handle local HTTP requests (On-board diagnostic dashboard)
  server.handleClient();

  // Handle interactive commands from USB Serial
  handleSerialCommands();

  // 1. Check if an SPI packet was received from STM32 Master
  if (spiPacketPending) {
    spiPacketPending = false;
    processSpiPacket();
  }

  // 2. Check if UART telemetry is arriving from STM32 via UART2
  if (Serial2.available()) {
    String stm32Data = Serial2.readStringUntil('\n');
    stm32Data.trim();

    if (stm32Data.length() > 0) {
      Serial.print("[STM32 UART RX] ");
      Serial.println(stm32Data);

      // Verify that the received line looks like telemetry before processing
      if (stm32Data.indexOf("FLIT") != -1 || stm32Data.indexOf("{") != -1) {
        parseStm32Data(stm32Data);
        forwardToDashboard();
      } else {
        Serial.printf("[STM32 UART WARN] Raw or unexpected characters: '%s'\n", stm32Data.c_str());
      }
    }
  }

  // 3. Perform periodic heartbeat loss / timeout detection
  checkHeartbeatLoss();
}

// ==============================================================================
// Process Raw SPI Packet Received from STM32
// ==============================================================================
void processSpiPacket() {
  unsigned long now = millis();
  totalFlitsReceived++;
  totalAcksReceived++;

  // Calculate interval between SPI packets
  unsigned long intervalMs = 0;
  if (currentTelemetry.lastAckTimestamp > 0) {
    intervalMs = now - currentTelemetry.lastAckTimestamp;
  }
  currentTelemetry.lastAckTimestamp = now;
  currentTelemetry.lastPacketTimestamp = now;

  // Calculate Jitter
  if (prevIntervalMs > 0 && intervalMs > 0) {
    lastJitterMs = abs((long)intervalMs - (long)prevIntervalMs);
  }
  prevIntervalMs = intervalMs;

  // Extract 32-bit flit number and 32-bit STM32 RTT latency (microseconds)
  uint32_t flitNum = 0;
  uint32_t stmLatencyUs = 0;
  memcpy(&flitNum, &lastSpiRxPacket[0], sizeof(uint32_t));
  memcpy(&stmLatencyUs, &lastSpiRxPacket[4], sizeof(uint32_t));
  uint8_t faultFlag = lastSpiRxPacket[12];

  if (flitNum == 0) {
    flitNum = totalFlitsReceived;
  }

  currentTelemetry.flitNumber = flitNum;
  currentTelemetry.ackReceived = true;
  currentTelemetry.timeBetweenAcks = intervalMs;

  // Record STM32's microsecond RTT stopwatch measurement (ignore rollover glitch > 1 second = 1,000,000 us)
  if (stmLatencyUs > 0 && stmLatencyUs < 1000000) {
    currentTelemetry.latencyUs = (float)stmLatencyUs;
    if (minLatencyUs == 999999.0f || currentTelemetry.latencyUs < minLatencyUs) minLatencyUs = currentTelemetry.latencyUs;
    if (maxLatencyUs > 1000000.0f || currentTelemetry.latencyUs > maxLatencyUs) maxLatencyUs = currentTelemetry.latencyUs;
    avgLatencyUs = ((avgLatencyUs * latencySamples) + currentTelemetry.latencyUs) / (latencySamples + 1);
    latencySamples++;
  }

  // Update raw payload representation
  char hexBuf[128];
  snprintf(hexBuf, sizeof(hexBuf), "SPI Flit #%lu | STM32 RTT: %lu us | Interval: %lu ms | ACK sent",
           flitNum, stmLatencyUs, intervalMs);
  currentTelemetry.rawPayload = String(hexBuf);

  Serial.printf("[SPI SLAVE RX] Flit #%lu (%u bytes) | STM32 RTT: %lu us | Interval: %lu ms -> Sent ACK\n",
                flitNum, lastSpiRxLength, stmLatencyUs, intervalMs);

  // State Transition Logic
  if (faultFlag == 1) {
    currentTelemetry.state = STATE_FAULT;
    totalFaultEvents++;
    Serial.println("[HEALTH] State Transition: FAULT (Hardware Button Injected)");
  } else if (currentTelemetry.state == STATE_FAULT) {
    currentTelemetry.state = STATE_RECOVERED;
    lastStateTransitionTime = now;
    Serial.println("[HEALTH] State Transition: FAULT -> RECOVERED (SPI Link Restored)");
  } else if (currentTelemetry.state == STATE_RECOVERED) {
    if (now - lastStateTransitionTime > 3000) {
      currentTelemetry.state = STATE_NORMAL;
      Serial.println("[HEALTH] State Transition: RECOVERED -> NORMAL");
    }
  } else {
    currentTelemetry.state = STATE_NORMAL;
  }

  // Forward immediately to Web Dashboard
  forwardToDashboard();
}

// ==============================================================================
// STM32 Data Parser (For UART Telemetry Streams)
// ==============================================================================
void parseStm32Data(const String& raw) {
  unsigned long now = millis();
  currentTelemetry.rawPayload = raw;
  currentTelemetry.lastPacketTimestamp = now;
  totalFlitsReceived++;

  long flit = totalFlitsReceived;
  bool ack = true;
  unsigned long intervalMs = 0;
  float latency = 0.0f;

  if (raw.startsWith("{") && raw.endsWith("}")) {
    int flitIdx = raw.indexOf("\"flit");
    if (flitIdx != -1) {
      int colon = raw.indexOf(':', flitIdx);
      if (colon != -1) flit = raw.substring(colon + 1).toInt();
    }
    int ackIdx = raw.indexOf("\"ack");
    if (ackIdx != -1) {
      int colon = raw.indexOf(':', ackIdx);
      if (colon != -1) {
        String ackVal = raw.substring(colon + 1);
        ack = (ackVal.startsWith("1") || ackVal.startsWith("true") || ackVal.startsWith("\"true"));
      }
    }
    int timeIdx = raw.indexOf("\"time_between_acks");
    if (timeIdx == -1) timeIdx = raw.indexOf("\"interval");
    if (timeIdx != -1) {
      int colon = raw.indexOf(':', timeIdx);
      if (colon != -1) intervalMs = (unsigned long)raw.substring(colon + 1).toInt();
    }
    int latIdx = raw.indexOf("\"latency");
    if (latIdx == -1) latIdx = raw.indexOf("\"rtt");
    if (latIdx != -1) {
      int colon = raw.indexOf(':', latIdx);
      if (colon != -1) latency = raw.substring(colon + 1).toFloat();
    }
  } 
  else if (raw.indexOf(':') != -1 || raw.indexOf('=') != -1) {
    String upper = raw;
    upper.toUpperCase();

    int fPos = upper.indexOf("FLIT");
    if (fPos != -1) {
      int valStart = findFirstDigit(upper, fPos);
      if (valStart != -1) {
        flit = upper.substring(valStart).toInt();
      }
    }

    int aPos = upper.indexOf("ACK");
    if (aPos != -1) {
      if (upper.indexOf("ACK: 0") != -1 || upper.indexOf("ACK=0") != -1 || 
          upper.indexOf("ACK: FAIL") != -1 || upper.indexOf("NACK") != -1 || 
          upper.indexOf("ACK: FALSE") != -1) {
        ack = false;
      } else {
        ack = true;
      }
    }

    int tPos = upper.indexOf("TIME_BETWEEN_ACKS");
    if (tPos == -1) tPos = upper.indexOf("INTERVAL");
    if (tPos == -1) tPos = upper.indexOf("DELTA");
    if (tPos != -1) {
      int valStart = findFirstDigit(upper, tPos);
      if (valStart != -1) {
        intervalMs = (unsigned long)upper.substring(valStart).toInt();
      }
    }

    int lPos = upper.indexOf("LATENCY");
    if (lPos == -1) lPos = upper.indexOf("RTT");
    if (lPos != -1) {
      int valStart = findFirstDigit(upper, lPos);
      if (valStart != -1) {
        latency = upper.substring(valStart).toFloat();
      }
    }
  }
  else if (raw.indexOf(',') != -1) {
    int firstComma = raw.indexOf(',');
    int secondComma = raw.indexOf(',', firstComma + 1);
    int thirdComma = (secondComma != -1) ? raw.indexOf(',', secondComma + 1) : -1;

    flit = raw.substring(0, firstComma).toInt();
    if (secondComma != -1) {
      String ackPart = raw.substring(firstComma + 1, secondComma);
      ack = (ackPart.toInt() == 1 || ackPart.equalsIgnoreCase("true") || ackPart.equalsIgnoreCase("ack"));
      if (thirdComma != -1) {
        intervalMs = (unsigned long)raw.substring(secondComma + 1, thirdComma).toInt();
        latency = raw.substring(thirdComma + 1).toFloat();
      } else {
        latency = raw.substring(secondComma + 1).toFloat();
      }
    } else {
      latency = raw.substring(firstComma + 1).toFloat();
    }
  }
  else {
    int valStart = findFirstDigit(raw);
    if (valStart != -1) {
      latency = raw.substring(valStart).toFloat();
    }
  }

  if (ack) {
    if (intervalMs == 0 && currentTelemetry.lastAckTimestamp > 0) {
      intervalMs = now - currentTelemetry.lastAckTimestamp;
    }
    currentTelemetry.lastAckTimestamp = now;
    totalAcksReceived++;

    if (prevIntervalMs > 0 && intervalMs > 0) {
      lastJitterMs = abs((long)intervalMs - (long)prevIntervalMs);
    }
    prevIntervalMs = intervalMs;
  }

  currentTelemetry.flitNumber = flit;
  currentTelemetry.ackReceived = ack;
  currentTelemetry.timeBetweenAcks = intervalMs;
  currentTelemetry.latencyUs = latency;

  if (latency > 0) {
    if (latency < minLatencyUs) minLatencyUs = latency;
    if (latency > maxLatencyUs) maxLatencyUs = latency;
    avgLatencyUs = ((avgLatencyUs * latencySamples) + latency) / (latencySamples + 1);
    latencySamples++;
  }

  if (currentTelemetry.state == STATE_FAULT) {
    if (ack) {
      currentTelemetry.state = STATE_RECOVERED;
      lastStateTransitionTime = now;
      Serial.println("[HEALTH] State Transition: FAULT -> RECOVERED (Link Restored)");
    }
  } else if (currentTelemetry.state == STATE_RECOVERED) {
    if (now - lastStateTransitionTime > 3000) {
      currentTelemetry.state = STATE_NORMAL;
      Serial.println("[HEALTH] State Transition: RECOVERED -> NORMAL");
    }
  } else {
    currentTelemetry.state = STATE_NORMAL;
  }
}

// ==============================================================================
// Heartbeat Loss / Timeout Detector
// ==============================================================================
void checkHeartbeatLoss() {
  unsigned long now = millis();

  if (now - lastHeartbeatCheck < 250) return;
  lastHeartbeatCheck = now;

  if (currentTelemetry.lastPacketTimestamp > 0 && 
     (now - currentTelemetry.lastPacketTimestamp > HEARTBEAT_TIMEOUT_MS)) {
    
    if (currentTelemetry.state != STATE_FAULT) {
      currentTelemetry.state = STATE_FAULT;
      totalFaultEvents++;
      lastStateTransitionTime = now;
      Serial.printf("[FAULT ALERT] Heartbeat Timeout! No data from STM32 for %lu ms\n", 
                    now - currentTelemetry.lastPacketTimestamp);

      forwardToDashboard();
    }
  }
}

// ==============================================================================
// HTTP Telemetry Dispatcher (Forwards JSON to Python Web Dashboard)
// ==============================================================================
void forwardToDashboard() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  http.begin(serverName);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(1000);

  String stateStr = "NORMAL";
  if (currentTelemetry.state == STATE_FAULT) stateStr = "FAULT";
  else if (currentTelemetry.state == STATE_RECOVERED) stateStr = "RECOVERED";

  float packetLossPct = 0.0f;
  if (totalFlitsReceived > 0) {
    long drops = (long)totalFlitsReceived - (long)totalAcksReceived;
    if (drops > 0) {
      packetLossPct = ((float)drops / (float)totalFlitsReceived) * 100.0f;
    }
  }

  String safeRaw = currentTelemetry.rawPayload;
  safeRaw.replace("\"", "'");
  safeRaw.replace("\r", "");
  safeRaw.replace("\n", " ");

  String json = "{";
  json += "\"flit_number\":" + String(currentTelemetry.flitNumber) + ",";
  json += "\"ack_received\":" + String(currentTelemetry.ackReceived ? "true" : "false") + ",";
  json += "\"time_between_acks_ms\":" + String(currentTelemetry.timeBetweenAcks) + ",";
  json += "\"latency_us\":" + String(currentTelemetry.latencyUs, 2) + ",";
  json += "\"status\":\"" + stateStr + "\",";
  json += "\"uptime_ms\":" + String(millis()) + ",";
  json += "\"total_flits\":" + String(totalFlitsReceived) + ",";
  json += "\"total_acks\":" + String(totalAcksReceived) + ",";
  json += "\"packet_loss_pct\":" + String(packetLossPct, 1) + ",";
  json += "\"jitter_ms\":" + String(lastJitterMs, 1) + ",";
  json += "\"min_latency_us\":" + String(minLatencyUs == 999999.0f ? 0.0f : minLatencyUs, 1) + ",";
  json += "\"max_latency_us\":" + String(maxLatencyUs, 1) + ",";
  json += "\"avg_latency_us\":" + String(avgLatencyUs, 1) + ",";
  json += "\"raw_payload\":\"" + safeRaw + "\",";
  json += "\"gateway_ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";

  int httpCode = http.POST(json);
  if (httpCode > 0) {
    if (httpCode != HTTP_CODE_OK && httpCode != 201) {
      Serial.printf("[HTTP] POST returned code: %d\n", httpCode);
    }
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}

// ==============================================================================
// Local Web Server Endpoints
// ==============================================================================
void handleStatusJson() {
  String stateStr = "NORMAL";
  if (currentTelemetry.state == STATE_FAULT) stateStr = "FAULT";
  else if (currentTelemetry.state == STATE_RECOVERED) stateStr = "RECOVERED";

  String json = "{";
  json += "\"flit\":" + String(currentTelemetry.flitNumber) + ",";
  json += "\"ack\":" + String(currentTelemetry.ackReceived ? "true" : "false") + ",";
  json += "\"time_between_acks\":" + String(currentTelemetry.timeBetweenAcks) + ",";
  json += "\"latency_us\":" + String(currentTelemetry.latencyUs, 2) + ",";
  json += "\"status\":\"" + stateStr + "\",";
  json += "\"total_flits\":" + String(totalFlitsReceived) + ",";
  json += "\"total_acks\":" + String(totalAcksReceived) + ",";
  json += "\"avg_latency_us\":" + String(avgLatencyUs, 2) + ",";
  json += "\"uptime_s\":" + String(millis() / 1000);
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>ESP32 Gateway Telemetry</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0b0f19; color: #f1f5f9; padding: 20px; margin: 0; }";
  html += ".card { background: #131b2e; border: 1px solid #1e293b; border-radius: 12px; padding: 24px; max-width: 600px; margin: 0 auto; box-shadow: 0 10px 25px rgba(0,0,0,0.5); }";
  html += "h1 { font-size: 1.3rem; margin-top: 0; color: #38bdf8; letter-spacing: 0.5px; }";
  html += ".status { font-weight: bold; font-size: 1.1rem; padding: 6px 14px; border-radius: 20px; display: inline-block; }";
  html += ".NORMAL { background: #064e3b; color: #34d399; }";
  html += ".FAULT { background: #7f1d1d; color: #f87171; animation: pulse 1s infinite; }";
  html += ".RECOVERED { background: #78350f; color: #fbbf24; }";
  html += "@keyframes pulse { 0% { opacity: 1; } 50% { opacity: 0.5; } 100% { opacity: 1; } }";
  html += ".grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 20px; }";
  html += ".stat { background: #0f172a; padding: 12px; border-radius: 8px; border: 1px solid #1e293b; }";
  html += ".stat-label { font-size: 0.75rem; color: #94a3b8; text-transform: uppercase; }";
  html += ".stat-val { font-size: 1.4rem; font-weight: bold; color: #e2e8f0; margin-top: 4px; font-family: monospace; }";
  html += ".footer { margin-top: 20px; font-size: 0.8rem; color: #64748b; text-align: center; }";
  html += "</style>";
  html += "<script>setInterval(() => { fetch('/api/status').then(r=>r.json()).then(d=>{";
  html += "document.getElementById('flit').innerText = d.flit;";
  html += "document.getElementById('ack').innerText = d.ack ? 'YES' : 'NO';";
  html += "document.getElementById('lat').innerText = d.latency_us.toFixed(1) + ' µs';";
  html += "document.getElementById('int').innerText = d.time_between_acks + ' ms';";
  html += "document.getElementById('avg').innerText = d.avg_latency_us.toFixed(1) + ' µs';";
  html += "let st = document.getElementById('status');";
  html += "st.innerText = d.status; st.className = 'status ' + d.status;";
  html += "}); }, 500);</script></head><body>";
  html += "<div class='card'>";
  html += "<h1>Embedded Equipment Health Monitor</h1>";
  html += "<p>ESP32 Direct SPI Slave & Telemetry Feed</p>";
  
  String stateStr = "NORMAL";
  if (currentTelemetry.state == STATE_FAULT) stateStr = "FAULT";
  else if (currentTelemetry.state == STATE_RECOVERED) stateStr = "RECOVERED";

  html += "<div>Status: <span id='status' class='status " + stateStr + "'>" + stateStr + "</span></div>";
  html += "<div class='grid'>";
  html += "<div class='stat'><div class='stat-label'>Flit Number</div><div id='flit' class='stat-val'>" + String(currentTelemetry.flitNumber) + "</div></div>";
  html += "<div class='stat'><div class='stat-label'>ACK Status</div><div id='ack' class='stat-val'>" + String(currentTelemetry.ackReceived ? "YES (ACK Sent)" : "NO") + "</div></div>";
  html += "<div class='stat'><div class='stat-label'>Latency (RTT)</div><div id='lat' class='stat-val'>" + String(currentTelemetry.latencyUs, 1) + " &micro;s</div></div>";
  html += "<div class='stat'><div class='stat-label'>Time Between ACKs</div><div id='int' class='stat-val'>" + String(currentTelemetry.timeBetweenAcks) + " ms</div></div>";
  html += "<div class='stat'><div class='stat-label'>Avg Latency</div><div id='avg' class='stat-val'>" + String(avgLatencyUs, 1) + " &micro;s</div></div>";
  html += "<div class='stat'><div class='stat-label'>Total Flits</div><div class='stat-val'>" + String(totalFlitsReceived) + "</div></div>";
  html += "</div>";
  html += "<div class='footer'>Electronics & Robotics Club &bull; BITS Pilani &bull; Target: " + String(serverName) + "</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// ==============================================================================
// Interactive Serial Monitor Commands
// ==============================================================================
void handleSerialCommands() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.equalsIgnoreCase("sim") || cmd.equalsIgnoreCase("send")) {
      static int simFlit = 1;
      float simLatency = 42.0f + (random(0, 150) / 10.0f);
      unsigned long simInterval = 500 + random(-5, 6);
      String mock = "FLIT: " + String(simFlit++) + ", ACK: 1, TIME_BETWEEN_ACKS: " + 
                    String(simInterval) + ", LATENCY: " + String(simLatency, 1);
      Serial.println("[SIM INJECT] " + mock);
      parseStm32Data(mock);
      forwardToDashboard();
    } 
    else if (cmd.equalsIgnoreCase("fault")) {
      Serial.println("[SIM INJECT] Simulating link failure / heartbeat loss...");
      currentTelemetry.state = STATE_FAULT;
      currentTelemetry.ackReceived = false;
      totalFaultEvents++;
      forwardToDashboard();
    } 
    else if (cmd.equalsIgnoreCase("recover")) {
      Serial.println("[SIM INJECT] Simulating link recovery...");
      String mock = "FLIT: 999, ACK: 1, TIME_BETWEEN_ACKS: 500, LATENCY: 45.0";
      parseStm32Data(mock);
      currentTelemetry.state = STATE_RECOVERED;
      forwardToDashboard();
    } 
    else if (cmd.equalsIgnoreCase("status")) {
      Serial.printf("[STATUS] Flits: %lu | ACKs: %lu | State: %d | Avg RTT: %.2fus\n",
                    totalFlitsReceived, totalAcksReceived, currentTelemetry.state, avgLatencyUs);
    } 
    else if (cmd.equalsIgnoreCase("help")) {
      Serial.println("\n--- Interactive Serial Commands ---");
      Serial.println("  sim     - Inject simulated normal STM32 packet");
      Serial.println("  fault   - Force state machine into FAULT state");
      Serial.println("  recover - Inject recovery ACK packet");
      Serial.println("  status  - Print telemetry and statistics");
      Serial.println("  help    - Show this message\n");
    }
  }
}
