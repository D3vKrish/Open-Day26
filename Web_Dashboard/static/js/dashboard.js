/*
 ==============================================================================
 Embedded Equipment Health & Communication Monitor - Human-Centric Controller
 Electronics & Robotics Club | BITS Pilani
 ==============================================================================
*/

document.addEventListener("DOMContentLoaded", () => {
  // ============================================================================
  // 1. Theme Management (Light / Dark)
  // ============================================================================
  const themeToggleBtn = document.getElementById("themeToggleBtn");
  const themeIcon = document.getElementById("themeIcon");
  const themeLabel = document.getElementById("themeLabel");

  const savedTheme = localStorage.getItem("dashboard_theme") || "light";
  document.documentElement.setAttribute("data-theme", savedTheme);
  updateThemeButtonUI(savedTheme);

  themeToggleBtn.addEventListener("click", () => {
    const currentTheme = document.documentElement.getAttribute("data-theme") || "light";
    const newTheme = currentTheme === "light" ? "dark" : "light";
    document.documentElement.setAttribute("data-theme", newTheme);
    localStorage.setItem("dashboard_theme", newTheme);
    updateThemeButtonUI(newTheme);
    updateChartTheme(newTheme);
  });

  function updateThemeButtonUI(theme) {
    if (theme === "dark") {
      themeIcon.textContent = "🌙";
      themeLabel.textContent = "Dark";
    } else {
      themeIcon.textContent = "☀️";
      themeLabel.textContent = "Light";
    }
  }

  // ============================================================================
  // 2. Audio Chimes (Web Audio API)
  // ============================================================================
  let audioCtx = null;
  let isSoundEnabled = true;

  function initAudio() {
    if (!audioCtx) {
      const AudioContext = window.AudioContext || window.webkitAudioContext;
      if (AudioContext) audioCtx = new AudioContext();
    }
    if (audioCtx && audioCtx.state === "suspended") {
      audioCtx.resume();
    }
  }

  function playTone(freq, type = "sine", duration = 0.15, gainVal = 0.08) {
    if (!isSoundEnabled) return;
    try {
      initAudio();
      if (!audioCtx) return;
      const osc = audioCtx.createOscillator();
      const gain = audioCtx.createGain();
      osc.type = type;
      osc.frequency.setValueAtTime(freq, audioCtx.currentTime);
      gain.gain.setValueAtTime(gainVal, audioCtx.currentTime);
      gain.gain.exponentialRampToValueAtTime(0.0001, audioCtx.currentTime + duration);
      osc.connect(gain);
      gain.connect(audioCtx.destination);
      osc.start();
      osc.stop(audioCtx.currentTime + duration);
    } catch (e) {
      console.warn("Audio warning:", e);
    }
  }

  function playAlertSound(state) {
    if (state === "FAULT") {
      playTone(330, "sawtooth", 0.16, 0.10);
      setTimeout(() => playTone(220, "sawtooth", 0.24, 0.12), 150);
    } else if (state === "RECOVERED") {
      playTone(523.25, "sine", 0.12, 0.08); // C5
      setTimeout(() => playTone(659.25, "sine", 0.12, 0.08), 100); // E5
      setTimeout(() => playTone(783.99, "sine", 0.20, 0.09), 200); // G5
    }
  }

  const soundToggleBtn = document.getElementById("soundToggleBtn");
  soundToggleBtn.addEventListener("click", () => {
    initAudio();
    isSoundEnabled = !isSoundEnabled;
    soundToggleBtn.style.opacity = isSoundEnabled ? "1" : "0.5";
  });

  // ============================================================================
  // 3. Real-Time Chart.js Configuration
  // ============================================================================
  const MAX_POINTS = 30;
  let isChartPaused = false;

  const latencyCtx = document.getElementById("latencyChart").getContext("2d");
  const intervalCtx = document.getElementById("intervalChart").getContext("2d");

  function getThemeColors() {
    const isDark = document.documentElement.getAttribute("data-theme") === "dark";
    return {
      grid: isDark ? "rgba(255, 255, 255, 0.06)" : "rgba(0, 0, 0, 0.05)",
      tick: isDark ? "#94a3b8" : "#64748b",
      latencyLine: isDark ? "#38bdf8" : "#0284c7",
      latencyFillTop: isDark ? "rgba(56, 189, 248, 0.35)" : "rgba(2, 132, 199, 0.25)",
      latencyFillBottom: isDark ? "rgba(56, 189, 248, 0.0)" : "rgba(2, 132, 199, 0.0)",
      intervalLine: isDark ? "#fbbf24" : "#d97706",
      intervalFillTop: isDark ? "rgba(245, 158, 11, 0.3)" : "rgba(245, 158, 11, 0.2)"
    };
  }

  const colors = getThemeColors();

  const latencyGrad = latencyCtx.createLinearGradient(0, 0, 0, 250);
  latencyGrad.addColorStop(0, colors.latencyFillTop);
  latencyGrad.addColorStop(1, colors.latencyFillBottom);

  const latencyChart = new Chart(latencyCtx, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        {
          label: "Round-Trip Latency (RTT)",
          data: [],
          borderColor: colors.latencyLine,
          backgroundColor: latencyGrad,
          borderWidth: 2.2,
          pointRadius: 3,
          pointBackgroundColor: colors.latencyLine,
          tension: 0.3,
          fill: true
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 300 },
      plugins: {
        legend: { display: false },
        tooltip: {
          padding: 8,
          callbacks: {
            label: (ctx) => ` Response Time: ${ctx.parsed.y.toFixed(1)} µs`
          }
        }
      },
      scales: {
        x: {
          grid: { color: colors.grid },
          ticks: { color: colors.tick, font: { family: "JetBrains Mono", size: 10 }, maxTicksLimit: 6 }
        },
        y: {
          min: 0,
          suggestedMax: 80,
          grid: { color: colors.grid },
          ticks: {
            color: colors.tick,
            font: { family: "JetBrains Mono", size: 10 },
            callback: (v) => `${v} µs`
          }
        }
      }
    }
  });

  const intervalGrad = intervalCtx.createLinearGradient(0, 0, 0, 250);
  intervalGrad.addColorStop(0, colors.intervalFillTop);
  intervalGrad.addColorStop(1, "rgba(245, 158, 11, 0)");

  const intervalChart = new Chart(intervalCtx, {
    type: "line",
    data: {
      labels: [],
      datasets: [
        {
          label: "Heartbeat Interval",
          data: [],
          borderColor: colors.intervalLine,
          backgroundColor: intervalGrad,
          borderWidth: 2,
          pointRadius: 3,
          pointBackgroundColor: colors.intervalLine,
          tension: 0.25,
          fill: true
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 300 },
      plugins: {
        legend: { display: false },
        tooltip: {
          padding: 8,
          callbacks: {
            label: (ctx) => ` Arrival Delta: ${ctx.parsed.y.toFixed(0)} ms`
          }
        }
      },
      scales: {
        x: {
          grid: { color: colors.grid },
          ticks: { color: colors.tick, font: { family: "JetBrains Mono", size: 10 }, maxTicksLimit: 6 }
        },
        y: {
          min: 0,
          suggestedMax: 800,
          grid: { color: colors.grid },
          ticks: {
            color: colors.tick,
            font: { family: "JetBrains Mono", size: 10 },
            callback: (v) => `${v} ms`
          }
        }
      }
    }
  });

  function updateChartTheme(theme) {
    const c = getThemeColors();
    latencyChart.options.scales.x.grid.color = c.grid;
    latencyChart.options.scales.y.grid.color = c.grid;
    latencyChart.options.scales.x.ticks.color = c.tick;
    latencyChart.options.scales.y.ticks.color = c.tick;
    latencyChart.data.datasets[0].borderColor = c.latencyLine;

    intervalChart.options.scales.x.grid.color = c.grid;
    intervalChart.options.scales.y.grid.color = c.grid;
    intervalChart.options.scales.x.ticks.color = c.tick;
    intervalChart.options.scales.y.ticks.color = c.tick;
    intervalChart.data.datasets[0].borderColor = c.intervalLine;

    latencyChart.update();
    intervalChart.update();
  }

  // Chart Pause/Reset
  document.getElementById("chart1PauseBtn").addEventListener("click", function() {
    isChartPaused = !isChartPaused;
    this.textContent = isChartPaused ? "Resume" : "Pause";
    this.classList.toggle("active", isChartPaused);
  });

  document.getElementById("chart1ResetBtn").addEventListener("click", () => {
    latencyChart.data.labels = [];
    latencyChart.data.datasets[0].data = [];
    latencyChart.update();
    intervalChart.data.labels = [];
    intervalChart.data.datasets[0].data = [];
    intervalChart.update();
  });

  // ============================================================================
  // 4. UI Elements Cache & Conversational Narrative
  // ============================================================================
  const humanStatusCard = document.getElementById("humanStatusCard");
  const statusAvatar = document.getElementById("statusAvatar");
  const narrativeHeading = document.getElementById("narrativeHeading");
  const narrativeText = document.getElementById("narrativeText");
  const systemStatusBadge = document.getElementById("systemStatusBadge");
  const statusLabel = document.getElementById("statusLabel");

  const rttQualityBadge = document.getElementById("rttQualityBadge");
  const valLatency = document.getElementById("valLatency");
  const latencyGaugeBar = document.getElementById("latencyGaugeBar");
  const valMinLatency = document.getElementById("valMinLatency");
  const valAvgLatency = document.getElementById("valAvgLatency");
  const valMaxLatency = document.getElementById("valMaxLatency");

  const valFlitNumber = document.getElementById("valFlitNumber");
  const valTotalFlits = document.getElementById("valTotalFlits");
  const transferRateBadge = document.getElementById("transferRateBadge");
  const valUptime = document.getElementById("valUptime");

  const valAckInterval = document.getElementById("valAckInterval");
  const ackStatusBadge = document.getElementById("ackStatusBadge");
  const cadenceText = document.getElementById("cadenceText");
  const valTotalAcks = document.getElementById("valTotalAcks");
  const valJitter = document.getElementById("valJitter");

  const valPacketLoss = document.getElementById("valPacketLoss");
  const healthScoreBadge = document.getElementById("healthScoreBadge");
  const healthFill = document.getElementById("healthFill");
  const lossText = document.getElementById("lossText");
  const valFaultCount = document.getElementById("valFaultCount");
  const linkIntegrity = document.getElementById("linkIntegrity");

  const stepNormal = document.getElementById("stepNormal");
  const stepFault = document.getElementById("stepFault");
  const stepRecovered = document.getElementById("stepRecovered");

  const nodeStm32Master = document.getElementById("nodeStm32Master");
  const nodeEsp32 = document.getElementById("nodeEsp32");
  const busLink1 = document.getElementById("busLink1");
  const nodeSlaveMetric = document.getElementById("nodeSlaveMetric");
  const gatewayIpDisplay = document.getElementById("gatewayIpDisplay");
  const stm32Conn = document.getElementById("stm32Conn");
  const esp32Conn = document.getElementById("esp32Conn");

  const rawPayloadView = document.getElementById("rawPayloadView");
  const logTableBody = document.getElementById("logTableBody");
  const autoScrollCheck = document.getElementById("autoScrollCheck");

  const btnToggleFault = document.getElementById("btnToggleFault");
  const toggleFaultEmoji = document.getElementById("toggleFaultEmoji");
  const toggleFaultTitle = document.getElementById("toggleFaultTitle");
  const toggleFaultDesc = document.getElementById("toggleFaultDesc");

  let totalFaultsCounter = 0;
  let previousState = "NORMAL";
  let flitCountForRate = 0;

  setInterval(() => {
    transferRateBadge.textContent = `${(flitCountForRate / 1.0).toFixed(1)} pkts/sec`;
    flitCountForRate = 0;
  }, 1000);

  // ============================================================================
  // 5. Handle Live Telemetry Updates
  // ============================================================================
  function handleTelemetryUpdate(point, state) {
    if (!point || !state) return;
    flitCountForRate++;

    const status = (state.status || "NORMAL").toUpperCase();
    const ack = Boolean(point.ack_received);
    const latency = parseFloat(point.latency_us || 0.0);
    const interval = parseFloat(point.time_between_acks_ms || 500.0);

    // 1. Update Human Narrative and Lifecycle Stepper
    humanStatusCard.setAttribute("data-status", status);
    systemStatusBadge.setAttribute("data-status", status);

    if (status === "FAULT") {
      statusAvatar.textContent = "🔴";
      statusLabel.textContent = "Communication Timeout (Fault)";
      narrativeHeading.textContent = "Communication Loss: Heartbeat Timeout Detected";
      narrativeText.textContent = `STM32 Master has stopped transmitting SPI packets (or Blue Button B1/PC13 is held). No heartbeat received for > 2.0s. System entered FAULT mode.`;

      stepNormal.classList.remove("active");
      stepFault.classList.add("active");
      stepRecovered.classList.remove("active");

      if (nodeEsp32) nodeEsp32.classList.add("node-fault");
      if (nodeStm32Master) nodeStm32Master.classList.add("node-fault");
      if (busLink1) busLink1.classList.add("fault-wire");
      if (nodeSlaveMetric) {
        nodeSlaveMetric.textContent = "Timeout / No ACK";
        nodeSlaveMetric.className = "status-mini-tag text-rose";
      }
      if (stm32Conn) stm32Conn.classList.add("offline");

      if (toggleFaultTitle) {
        toggleFaultEmoji.textContent = "🔁";
        toggleFaultTitle.textContent = "Restore / Clear Fault";
        toggleFaultDesc.textContent = "Click to restore link & clear heartbeat fault";
      }

      if (previousState !== "FAULT") {
        totalFaultsCounter++;
        valFaultCount.textContent = `${totalFaultsCounter} events`;
        playAlertSound("FAULT");
      }
    } else if (status === "RECOVERED") {
      statusAvatar.textContent = "🟡";
      statusLabel.textContent = "Communication Restored";
      narrativeHeading.textContent = "Connection Restored! Heartbeats Synchronized";
      narrativeText.textContent = `The high-speed SPI link between STM32 and ESP32 has recovered. Valid 64-byte flits and ACKs are once again flowing at 500 ms cadence.`;

      stepNormal.classList.remove("active");
      stepFault.classList.remove("active");
      stepRecovered.classList.add("active");

      if (nodeEsp32) nodeEsp32.classList.remove("node-fault");
      if (nodeStm32Master) nodeStm32Master.classList.remove("node-fault");
      if (busLink1) busLink1.classList.remove("fault-wire");
      if (nodeSlaveMetric) {
        nodeSlaveMetric.textContent = "Link Restored";
        nodeSlaveMetric.className = "status-mini-tag text-amber";
      }
      if (stm32Conn) stm32Conn.classList.remove("offline");

      if (toggleFaultTitle) {
        toggleFaultEmoji.textContent = "🔌";
        toggleFaultTitle.textContent = "Simulate Communication Fault";
        toggleFaultDesc.textContent = "Injects heartbeat timeout (FAULT) & triggers alert state";
      }

      if (previousState !== "RECOVERED") {
        playAlertSound("RECOVERED");
      }
    } else {
      statusAvatar.textContent = "🟢";
      statusLabel.textContent = "All Systems Healthy";
      narrativeHeading.textContent = "Communication Link is Active and Healthy";
      narrativeText.textContent = `STM32 Master is clocking sequenced 64-byte flits to ESP32 every 500 ms. Measured round-trip response time (RTT): ~${latency.toFixed(1)} µs. Zero packet loss.`;

      stepNormal.classList.add("active");
      stepFault.classList.remove("active");
      stepRecovered.classList.remove("active");

      if (nodeEsp32) nodeEsp32.classList.remove("node-fault");
      if (nodeStm32Master) nodeStm32Master.classList.remove("node-fault");
      if (busLink1) busLink1.classList.remove("fault-wire");
      if (nodeSlaveMetric) {
        nodeSlaveMetric.textContent = "ACK Confirmed";
        nodeSlaveMetric.className = "status-mini-tag text-emerald";
      }
      if (stm32Conn) stm32Conn.classList.remove("offline");

      if (toggleFaultTitle) {
        toggleFaultEmoji.textContent = "🔌";
        toggleFaultTitle.textContent = "Simulate Communication Fault";
        toggleFaultDesc.textContent = "Injects heartbeat timeout (FAULT) & triggers alert state";
      }
    }

    previousState = status;

    // 2. Latency Metric & Quality Badge
    valLatency.textContent = latency > 0 ? latency.toFixed(1) : "--";

    // Cleanly display min/avg/peak, suppressing any initial startup timer overflow
    const minLat = (state.min_latency_us > 0 && state.min_latency_us < 1000000) ? state.min_latency_us : latency;
    const avgLat = (state.avg_latency_us > 0 && state.avg_latency_us < 1000000) ? state.avg_latency_us : latency;
    const maxLat = (state.max_latency_us > 0 && state.max_latency_us < 1000000) ? state.max_latency_us : latency;
    valMinLatency.textContent = `${minLat.toFixed(1)} µs`;
    valAvgLatency.textContent = `${avgLat.toFixed(1)} µs`;
    valMaxLatency.textContent = `${maxLat.toFixed(1)} µs`;

    if (latency <= 15000 && latency > 0) {
      rttQualityBadge.textContent = "⚡ Optimal (< 15 ms)";
      rttQualityBadge.className = "badge badge-teal";
    } else if (latency <= 45000 && latency > 0) {
      rttQualityBadge.textContent = "🟢 Healthy Response";
      rttQualityBadge.className = "badge badge-emerald";
    } else if (latency <= 80000 && latency > 0) {
      rttQualityBadge.textContent = "🟡 Moderate Delay";
      rttQualityBadge.className = "badge badge-amber";
    } else {
      rttQualityBadge.textContent = "🔴 Congestion / Timeout";
      rttQualityBadge.className = "badge badge-rose";
    }

    const gaugePct = Math.min(100, Math.max(5, (latency / 60000.0) * 100));
    latencyGaugeBar.style.width = `${gaugePct}%`;
    latencyGaugeBar.style.background = latency > 80000 ? "var(--rose-500)" : latency > 45000 ? "var(--amber-500)" : "linear-gradient(90deg, #10b981, #34d399)";

    // 3. Flit Sequence
    valFlitNumber.textContent = point.flit_number || 0;
    valTotalFlits.textContent = `${(state.total_flits || 0).toLocaleString()} flits`;

    if (state.uptime_ms) {
      const s = Math.floor(state.uptime_ms / 1000);
      const hrs = String(Math.floor(s / 3600)).padStart(2, "0");
      const mins = String(Math.floor((s % 3600) / 60)).padStart(2, "0");
      const secs = String(s % 60).padStart(2, "0");
      valUptime.textContent = `${hrs}:${mins}:${secs}`;
    }

    // 4. Heartbeat & Cadence
    valAckInterval.textContent = interval > 0 ? interval.toFixed(0) : "--";
    if (ack && status !== "FAULT") {
      ackStatusBadge.textContent = "ACK Confirmed";
      ackStatusBadge.className = "badge badge-emerald";
      cadenceText.textContent = `🟢 Steady Heartbeat Cadence (~${interval.toFixed(0)} ms)`;
      cadenceText.style.color = "var(--emerald-600)";
    } else {
      ackStatusBadge.textContent = "No ACK (Lost)";
      ackStatusBadge.className = "badge badge-rose";
      cadenceText.textContent = "🔴 Timeout / Packet Unacknowledged";
      cadenceText.style.color = "var(--rose-600)";
    }
    valTotalAcks.textContent = `${(state.total_acks || 0).toLocaleString()} received`;
    valJitter.textContent = `±${(state.jitter_ms || 1.2).toFixed(1)} ms`;

    // 5. Health & Packet Loss
    const loss = parseFloat(state.packet_loss_pct || 0.0);
    valPacketLoss.textContent = loss.toFixed(1);
    const dropCount = Math.max(0, (state.total_flits || 0) - (state.total_acks || 0));
    lossText.textContent = `${dropCount} drops`;

    if (loss === 0.0 && status !== "FAULT") {
      healthScoreBadge.textContent = "100% Reliable";
      healthScoreBadge.className = "badge badge-emerald";
      healthFill.style.width = "100%";
      healthFill.style.background = "linear-gradient(90deg, var(--emerald-500), #34d399)";
      linkIntegrity.textContent = "100% (Optimal)";
      linkIntegrity.className = "pill-v text-emerald";
    } else if (loss < 5.0 && status !== "FAULT") {
      healthScoreBadge.textContent = `${(100 - loss).toFixed(1)}% Stable`;
      healthScoreBadge.className = "badge badge-amber";
      healthFill.style.width = `${100 - loss}%`;
      healthFill.style.background = "var(--amber-500)";
      linkIntegrity.textContent = `${(100 - loss).toFixed(1)}%`;
      linkIntegrity.className = "pill-v text-amber";
    } else {
      healthScoreBadge.textContent = "Degraded / Fault";
      healthScoreBadge.className = "badge badge-rose";
      healthFill.style.width = `${Math.max(10, 100 - loss)}%`;
      healthFill.style.background = "var(--rose-500)";
      linkIntegrity.textContent = `${Math.max(0, 100 - loss).toFixed(1)}%`;
      linkIntegrity.className = "pill-v text-rose";
    }

    if (state.gateway_ip) {
      gatewayIpDisplay.textContent = state.gateway_ip;
      esp32Conn.classList.remove("offline");
    }

    if (state.raw_payload) {
      rawPayloadView.textContent = state.raw_payload;
    }

    // 6. Push to Live Real-Time Charts
    if (!isChartPaused && point.timestamp) {
      const timeLabel = point.timestamp.split(".")[0];

      latencyChart.data.labels.push(timeLabel);
      latencyChart.data.datasets[0].data.push(latency);
      if (latencyChart.data.labels.length > MAX_POINTS) {
        latencyChart.data.labels.shift();
        latencyChart.data.datasets[0].data.shift();
      }
      latencyChart.update();

      intervalChart.data.labels.push(timeLabel);
      intervalChart.data.datasets[0].data.push(interval);
      if (intervalChart.data.labels.length > MAX_POINTS) {
        intervalChart.data.labels.shift();
        intervalChart.data.datasets[0].data.shift();
      }
      intervalChart.update();
    }
  }

  // ============================================================================
  // 6. Activity Log with Relative Timestamps
  // ============================================================================
  let currentFilter = "ALL";

  function appendEventRow(event) {
    if (!event) return;
    const tr = document.createElement("tr");
    tr.setAttribute("data-level", event.type);

    let badgeClass = "badge-teal";
    let badgeLabel = "INFO";
    if (event.type === "FAULT") {
      badgeClass = "badge-rose";
      badgeLabel = "FAULT";
    } else if (event.type === "RECOVERY") {
      badgeClass = "badge-amber";
      badgeLabel = "RESTORED";
    } else if (event.type === "WARNING") {
      badgeClass = "badge-amber";
      badgeLabel = "WARNING";
    }

    tr.innerHTML = `
      <td><span style="font-family: var(--font-code); font-size: 0.72rem;">${event.timestamp}</span></td>
      <td><span class="badge ${badgeClass}">${badgeLabel}</span></td>
      <td><strong>${event.title}</strong></td>
      <td style="color: var(--text-secondary);">${event.message}</td>
    `;

    if (currentFilter !== "ALL" && event.type !== currentFilter) {
      tr.style.display = "none";
    }

    logTableBody.insertBefore(tr, logTableBody.firstChild);

    if (logTableBody.children.length > 80) {
      logTableBody.removeChild(logTableBody.lastChild);
    }

    if (autoScrollCheck.checked) {
      logTableBody.parentElement.scrollTop = 0;
    }
  }

  document.querySelectorAll(".filter-btn").forEach(btn => {
    btn.addEventListener("click", () => {
      document.querySelectorAll(".filter-btn").forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      currentFilter = btn.getAttribute("data-filter");

      const rows = logTableBody.querySelectorAll("tr");
      rows.forEach(r => {
        const level = r.getAttribute("data-level");
        if (currentFilter === "ALL" || level === currentFilter) {
          r.style.display = "";
        } else {
          r.style.display = "none";
        }
      });
    });
  });

  document.getElementById("clearLogsBtn").addEventListener("click", () => {
    logTableBody.innerHTML = "";
  });

  // ============================================================================
  // 7. Interactive Fault Toggle Button
  // ============================================================================
  if (btnToggleFault) {
    btnToggleFault.addEventListener("click", () => {
      initAudio();
      btnToggleFault.style.transform = "scale(0.97)";
      setTimeout(() => btnToggleFault.style.transform = "", 150);

      fetch("/api/simulate", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "toggle" })
      }).catch(err => console.error("Fault toggle error:", err));
    });
  }

  // ============================================================================
  // 8. Server-Sent Events (SSE) Live Feed
  // ============================================================================
  function connectSSE() {
    const sse = new EventSource("/api/stream");

    sse.onmessage = (e) => {
      try {
        const msg = JSON.parse(e.data);
        if (msg.event === "sync") {
          if (msg.history && msg.history.length > 0) {
            msg.history.slice(-MAX_POINTS).forEach(pt => {
              const t = pt.timestamp ? pt.timestamp.split(".")[0] : "";
              latencyChart.data.labels.push(t);
              latencyChart.data.datasets[0].data.push(pt.latency_us);
              intervalChart.data.labels.push(t);
              intervalChart.data.datasets[0].data.push(pt.time_between_acks_ms);
            });
            latencyChart.update();
            intervalChart.update();
          }
          if (msg.events) {
            msg.events.reverse().forEach(ev => appendEventRow(ev));
          }
          if (msg.state) {
            handleTelemetryUpdate(msg.state, msg.state);
          }
        } else if (msg.event === "telemetry") {
          handleTelemetryUpdate(msg.data, msg.state);
        } else if (msg.event === "system_event") {
          appendEventRow(msg.data);
        }
      } catch (err) {
        console.error("SSE parse error:", err);
      }
    };

    sse.onerror = () => {
      sse.close();
      setTimeout(connectSSE, 2000);
    };
  }

  connectSSE();
});
