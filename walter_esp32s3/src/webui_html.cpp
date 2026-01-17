/**
 * @file webui_html.cpp
 * @brief Embedded web UI HTML/JavaScript
 *
 * Complete oscilloscope interface with Canvas-based rendering.
 * No external dependencies - works completely offline when connected to AP.
 */

#include "webui.h"

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Geode AGM Oscilloscope</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: #111;
      color: #eee;
      display: flex;
      flex-direction: column;
      height: 100vh;
      overflow: hidden;
    }
    header {
      padding: 8px 12px;
      background: #1a1a1a;
      border-bottom: 1px solid #333;
      display: flex;
      justify-content: space-between;
      align-items: center;
      flex-shrink: 0;
    }
    .logo { font-weight: bold; color: #0f0; }
    #status { font-size: 12px; }
    #status.connected { color: #0f0; }
    #status.connecting { color: #f80; }
    #status.error { color: #f00; }

    #container {
      flex: 1;
      display: flex;
      flex-direction: column;
      padding: 8px;
      overflow: hidden;
    }

    #controls {
      display: flex;
      gap: 8px;
      align-items: center;
      flex-wrap: wrap;
      margin-bottom: 8px;
      flex-shrink: 0;
    }

    button {
      background: #333;
      color: #eee;
      border: 1px solid #444;
      border-radius: 4px;
      padding: 6px 12px;
      cursor: pointer;
      font-size: 12px;
    }
    button:hover { background: #444; }
    button.active { background: #0a5; border-color: #0c7; }

    #scope-container {
      flex: 1;
      min-height: 200px;
      border: 1px solid #333;
      background: #000;
      position: relative;
    }

    #scope {
      width: 100%;
      height: 100%;
    }

    .panel {
      background: #1a1a1a;
      border: 1px solid #333;
      border-radius: 4px;
      padding: 10px;
      margin-top: 8px;
      flex-shrink: 0;
    }

    .control-bar {
      display: flex;
      gap: 12px;
      align-items: center;
      flex-wrap: wrap;
    }

    .control-group {
      display: flex;
      align-items: center;
      gap: 4px;
    }

    .control-group label {
      font-size: 11px;
      color: #888;
    }

    .value {
      font-family: monospace;
      font-size: 14px;
      color: #0ff;
      min-width: 50px;
      text-align: center;
    }

    .small-btn {
      padding: 4px 8px;
      font-size: 11px;
    }

    #stats {
      font-size: 11px;
      color: #888;
      margin-top: 8px;
      font-family: monospace;
    }

    #last-val {
      color: #0f0;
      font-size: 14px;
      font-family: monospace;
    }
  </style>
</head>
<body>
  <header>
    <span class="logo">Geode AGM</span>
    <span id="status" class="connecting">Connecting...</span>
  </header>

  <div id="container">
    <div id="controls">
      <button id="btn-pause">Pause</button>
      <button id="btn-clear">Clear</button>
      <span style="color:#666;font-size:11px">|</span>
      <span id="last-val">--</span>
    </div>

    <div id="scope-container">
      <canvas id="scope"></canvas>
    </div>

    <div class="panel">
      <div class="control-bar">
        <div class="control-group">
          <label>Time/div:</label>
          <button class="small-btn" id="time-down">-</button>
          <span class="value" id="time-val">100ms</span>
          <button class="small-btn" id="time-up">+</button>
        </div>

        <div class="control-group">
          <label>V/div:</label>
          <button class="small-btn" id="volt-down">-</button>
          <span class="value" id="volt-val">Auto</span>
          <button class="small-btn" id="volt-up">+</button>
        </div>

        <div class="control-group">
          <label>PGA:</label>
          <button class="small-btn" id="pga-down">-</button>
          <span class="value" id="pga-val">4x</span>
          <button class="small-btn" id="pga-up">+</button>
        </div>
      </div>

      <div id="stats">
        Frames: 0 | Samples: 0 | Rate: -- Hz
      </div>
    </div>
  </div>

  <script>
    // =========================================================================
    // CANVAS OSCILLOSCOPE
    // =========================================================================

    const canvas = document.getElementById('scope');
    const ctx = canvas.getContext('2d');
    const statusEl = document.getElementById('status');
    const statsEl = document.getElementById('stats');
    const lastValEl = document.getElementById('last-val');

    // Data buffer - circular buffer of samples
    const BUFFER_SIZE = 30000; // 30 seconds at 1kHz
    const dataBuffer = new Float32Array(BUFFER_SIZE);
    let writeIdx = 0;
    let sampleCount = 0;
    let frameCount = 0;
    let lastFrameTime = 0;
    let fps = 0;

    // Display settings
    const TIME_DIVS = [10, 20, 50, 100, 200, 500, 1000, 2000, 5000]; // ms per division
    let timeIdx = 3; // 100ms default
    const VOLT_DIVS = [0, 100000, 500000, 1000000, 2000000, 5000000]; // 0 = auto
    let voltIdx = 0; // auto
    let paused = false;

    // PGA settings
    const PGA = [1, 2, 4, 8, 16, 32, 64];
    let pgaIdx = 2; // 4x default

    // Convert raw ADC count to voltage (2.5V ref, gain from PGA)
    function countsToVolts(counts) {
      return (counts / 8388607.0) * 2.5 / PGA[pgaIdx];
    }

    // Resize canvas to match container
    function resizeCanvas() {
      const container = document.getElementById('scope-container');
      const rect = container.getBoundingClientRect();
      canvas.width = rect.width;
      canvas.height = rect.height;
    }

    // Draw oscilloscope
    function draw() {
      if (!canvas.width || !canvas.height) {
        resizeCanvas();
      }

      const w = canvas.width;
      const h = canvas.height;

      // Clear
      ctx.fillStyle = '#000';
      ctx.fillRect(0, 0, w, h);

      // Grid
      const divisions = 10;
      ctx.strokeStyle = '#222';
      ctx.lineWidth = 1;
      ctx.beginPath();
      for (let i = 1; i < divisions; i++) {
        const x = (w / divisions) * i;
        ctx.moveTo(x, 0);
        ctx.lineTo(x, h);
      }
      for (let i = 1; i < 8; i++) {
        const y = (h / 8) * i;
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
      }
      ctx.stroke();

      // Center line
      ctx.strokeStyle = '#333';
      ctx.beginPath();
      ctx.moveTo(0, h / 2);
      ctx.lineTo(w, h / 2);
      ctx.stroke();

      // No data yet
      if (sampleCount === 0) {
        ctx.fillStyle = '#444';
        ctx.font = '14px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('Waiting for data...', w / 2, h / 2);
        requestAnimationFrame(draw);
        return;
      }

      // Calculate how many samples to show
      const msPerDiv = TIME_DIVS[timeIdx];
      const totalMs = msPerDiv * divisions;
      const samplesToShow = Math.min(totalMs, BUFFER_SIZE, sampleCount);

      // Get data range for auto-scaling
      let minVal = Infinity, maxVal = -Infinity;
      const startIdx = (writeIdx - samplesToShow + BUFFER_SIZE) % BUFFER_SIZE;
      for (let i = 0; i < samplesToShow; i++) {
        const idx = (startIdx + i) % BUFFER_SIZE;
        const v = dataBuffer[idx];
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
      }

      // Scale
      let yRange;
      if (voltIdx === 0) {
        // Auto scale
        const range = maxVal - minVal;
        yRange = range > 0 ? range * 1.2 : 1000000; // 20% margin
      } else {
        yRange = VOLT_DIVS[voltIdx] * 8; // 8 vertical divisions
      }
      const yCenter = (minVal + maxVal) / 2;

      // Draw waveform
      ctx.strokeStyle = '#0f8';
      ctx.lineWidth = 1;
      ctx.beginPath();

      for (let i = 0; i < samplesToShow; i++) {
        const idx = (startIdx + i) % BUFFER_SIZE;
        const x = (i / samplesToShow) * w;
        const y = h / 2 - ((dataBuffer[idx] - yCenter) / yRange) * h;

        if (i === 0) {
          ctx.moveTo(x, y);
        } else {
          ctx.lineTo(x, y);
        }
      }
      ctx.stroke();

      // Scale labels
      ctx.fillStyle = '#666';
      ctx.font = '10px monospace';
      ctx.textAlign = 'left';
      ctx.fillText(countsToVolts(yCenter + yRange / 2).toFixed(4) + 'V', 4, 12);
      ctx.fillText(countsToVolts(yCenter - yRange / 2).toFixed(4) + 'V', 4, h - 4);

      ctx.textAlign = 'right';
      ctx.fillText(totalMs + 'ms', w - 4, h - 4);

      if (!paused) {
        requestAnimationFrame(draw);
      }
    }

    // Add samples to buffer
    function addSamples(samples) {
      if (paused) return;

      for (let i = 0; i < samples.length; i++) {
        dataBuffer[writeIdx] = samples[i];
        writeIdx = (writeIdx + 1) % BUFFER_SIZE;
        sampleCount++;
      }

      // Update last value display
      const lastVal = samples[samples.length - 1];
      const volts = countsToVolts(lastVal);
      lastValEl.textContent = volts.toFixed(5) + ' V (' + lastVal + ')';

      // Update stats
      frameCount++;
      const now = performance.now();
      if (now - lastFrameTime > 1000) {
        fps = Math.round(frameCount * 1000 / (now - lastFrameTime));
        frameCount = 0;
        lastFrameTime = now;
      }
      statsEl.textContent = 'Frames: ' + Math.floor(sampleCount / 1000) +
        ' | Samples: ' + sampleCount +
        ' | Rate: ' + (fps * 1000) + ' sps';
    }

    // Clear buffer
    function clearBuffer() {
      dataBuffer.fill(0);
      writeIdx = 0;
      sampleCount = 0;
      frameCount = 0;
    }

    // =========================================================================
    // WEBSOCKET
    // =========================================================================

    function connectWS() {
      const ws = new WebSocket('ws://' + location.host + '/ws');
      statusEl.textContent = 'Connecting...';
      statusEl.className = 'connecting';

      ws.onopen = () => {
        statusEl.textContent = 'Connected';
        statusEl.className = 'connected';
        console.log('[WS] Connected');
      };

      ws.onclose = () => {
        statusEl.textContent = 'Reconnecting...';
        statusEl.className = 'connecting';
        console.log('[WS] Disconnected, reconnecting in 2s...');
        setTimeout(connectWS, 2000);
      };

      ws.onerror = (e) => {
        statusEl.textContent = 'Error';
        statusEl.className = 'error';
        console.error('[WS] Error:', e);
      };

      ws.onmessage = (e) => {
        try {
          const msg = JSON.parse(e.data);
          if (msg.geo && msg.geo.length > 0) {
            addSamples(msg.geo);
          }
        } catch (err) {
          console.error('[WS] Parse error:', err);
        }
      };
    }

    // =========================================================================
    // UI EVENT HANDLERS
    // =========================================================================

    document.getElementById('btn-pause').onclick = function() {
      paused = !paused;
      this.textContent = paused ? 'Resume' : 'Pause';
      this.classList.toggle('active', paused);
      if (!paused) {
        requestAnimationFrame(draw);
      }
    };

    document.getElementById('btn-clear').onclick = () => {
      clearBuffer();
    };

    document.getElementById('time-down').onclick = () => {
      timeIdx = Math.max(0, timeIdx - 1);
      document.getElementById('time-val').textContent = TIME_DIVS[timeIdx] + 'ms';
    };

    document.getElementById('time-up').onclick = () => {
      timeIdx = Math.min(TIME_DIVS.length - 1, timeIdx + 1);
      document.getElementById('time-val').textContent = TIME_DIVS[timeIdx] + 'ms';
    };

    document.getElementById('volt-down').onclick = () => {
      voltIdx = Math.max(0, voltIdx - 1);
      document.getElementById('volt-val').textContent = voltIdx === 0 ? 'Auto' : (VOLT_DIVS[voltIdx] / 1000000).toFixed(1) + 'M';
    };

    document.getElementById('volt-up').onclick = () => {
      voltIdx = Math.min(VOLT_DIVS.length - 1, voltIdx + 1);
      document.getElementById('volt-val').textContent = voltIdx === 0 ? 'Auto' : (VOLT_DIVS[voltIdx] / 1000000).toFixed(1) + 'M';
    };

    document.getElementById('pga-down').onclick = () => {
      pgaIdx = Math.max(0, pgaIdx - 1);
      document.getElementById('pga-val').textContent = PGA[pgaIdx] + 'x';
      // TODO: Send PGA change to device
    };

    document.getElementById('pga-up').onclick = () => {
      pgaIdx = Math.min(PGA.length - 1, pgaIdx + 1);
      document.getElementById('pga-val').textContent = PGA[pgaIdx] + 'x';
      // TODO: Send PGA change to device
    };

    // Handle window resize
    window.addEventListener('resize', () => {
      resizeCanvas();
    });

    // =========================================================================
    // INIT
    // =========================================================================

    resizeCanvas();
    connectWS();
    requestAnimationFrame(draw);

    console.log('[Geode] Oscilloscope initialized (Canvas mode - no external dependencies)');
  </script>
</body>
</html>
)rawliteral";

const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;
