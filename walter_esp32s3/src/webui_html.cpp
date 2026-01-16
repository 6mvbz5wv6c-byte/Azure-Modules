/**
 * @file webui_html.cpp
 * @brief Embedded web UI HTML/JavaScript
 *
 * Complete oscilloscope interface with DSP filtering capabilities
 * including Butterworth bandpass, notch filters, and Lock-In amplifier.
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
    button.lockin { background: #a50; border-color: #c70; }

    #scope {
      flex: 1;
      min-height: 200px;
      border: 1px solid #333;
      background: #000;
    }

    .panel {
      background: #1a1a1a;
      border: 1px solid #333;
      border-radius: 4px;
      padding: 10px;
      margin-top: 8px;
      flex-shrink: 0;
    }

    .filter-bar {
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
      min-width: 40px;
      text-align: center;
    }

    .small-btn {
      padding: 4px 8px;
      font-size: 11px;
    }

    #stats {
      font-size: 10px;
      color: #666;
      margin-top: 8px;
    }

    /* Advanced panel */
    #advanced {
      display: none;
      margin-top: 8px;
    }
    #advanced.show { display: block; }

    .adv-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 8px;
    }

    .filter-section {
      background: #222;
      border-radius: 4px;
      padding: 8px;
    }
    .filter-section h4 {
      margin: 0 0 6px 0;
      font-size: 10px;
      color: #888;
      text-transform: uppercase;
      letter-spacing: 1px;
    }
    .param {
      display: flex;
      align-items: center;
      gap: 4px;
      margin: 4px 0;
    }
    .param label {
      flex: 0 0 80px;
      font-size: 10px;
      color: #aaa;
    }
    .param input[type="range"] {
      flex: 1;
      accent-color: #0a5;
    }
    .param .pval {
      flex: 0 0 45px;
      text-align: right;
      font-family: monospace;
      font-size: 10px;
      color: #0ff;
    }
    .param input[type="checkbox"] {
      accent-color: #0a5;
    }
    .param select {
      flex: 1;
      background: #333;
      color: #eee;
      border: 1px solid #444;
      border-radius: 3px;
      padding: 2px;
      font-size: 10px;
    }
  </style>
  <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
</head>
<body>
  <header>
    <span class="logo">Geode AGM</span>
    <span id="status" class="connecting">Connecting...</span>
  </header>

  <div id="container">
    <div id="controls">
      <button id="btn-live">Jump to Live</button>
      <span style="color:#666;font-size:11px">Buffer: 30s</span>
      <span id="view-time" style="font-family:monospace;font-size:11px;color:#888"></span>
    </div>

    <div id="scope"></div>

    <div class="panel">
      <div class="filter-bar">
        <button id="btn-filter">Enable Filter</button>
        <button id="btn-lockin">Lock-In Amp</button>

        <div class="control-group" id="freq-ctrl" style="display:none">
          <label>Freq:</label>
          <button class="small-btn" id="freq-down">-</button>
          <span class="value" id="freq-val">22</span>
          <span style="color:#666;font-size:10px">Hz</span>
          <button class="small-btn" id="freq-up">+</button>
        </div>

        <div class="control-group" id="notch-ctrl" style="display:none">
          <label>60Hz:</label>
          <button class="small-btn active" id="btn-notch">ON</button>
        </div>

        <div class="control-group" id="pga-ctrl">
          <label>PGA:</label>
          <button class="small-btn" id="pga-down">-</button>
          <span class="value" id="pga-val">4x</span>
          <button class="small-btn" id="pga-up">+</button>
        </div>

        <button id="btn-adv" style="margin-left:auto">Advanced</button>
      </div>

      <div id="advanced">
        <div class="adv-grid">
          <div class="filter-section">
            <h4>Bandpass Filter</h4>
            <div class="param">
              <label>Center:</label>
              <input type="range" id="bp-fc" min="10" max="50" value="22" step="1">
              <span class="pval" id="bp-fc-val">22 Hz</span>
            </div>
            <div class="param">
              <label>Bandwidth:</label>
              <input type="range" id="bp-bw" min="2" max="30" value="10" step="1">
              <span class="pval" id="bp-bw-val">10 Hz</span>
            </div>
            <div class="param">
              <label>Order:</label>
              <input type="range" id="bp-ord" min="2" max="8" value="4" step="2">
              <span class="pval" id="bp-ord-val">4</span>
            </div>
          </div>

          <div class="filter-section">
            <h4>60 Hz Notch</h4>
            <div class="param">
              <label>Enabled:</label>
              <input type="checkbox" id="notch-en" checked>
            </div>
            <div class="param">
              <label>Frequency:</label>
              <input type="range" id="notch-f" min="55" max="65" value="60" step="0.5">
              <span class="pval" id="notch-f-val">60 Hz</span>
            </div>
            <div class="param">
              <label>Q:</label>
              <input type="range" id="notch-q" min="10" max="100" value="35" step="5">
              <span class="pval" id="notch-q-val">35</span>
            </div>
            <div class="param">
              <label>Stages:</label>
              <input type="range" id="notch-n" min="1" max="4" value="2" step="1">
              <span class="pval" id="notch-n-val">2</span>
            </div>
          </div>

          <div class="filter-section">
            <h4>Lock-In Amplifier</h4>
            <div class="param">
              <label>Ref Freq:</label>
              <input type="range" id="lia-f" min="10" max="50" value="22" step="0.5">
              <span class="pval" id="lia-f-val">22 Hz</span>
            </div>
            <div class="param">
              <label>Time Const:</label>
              <input type="range" id="lia-tc" min="0.01" max="1" value="0.1" step="0.01">
              <span class="pval" id="lia-tc-val">100 ms</span>
            </div>
            <div class="param">
              <label>Output:</label>
              <select id="lia-out">
                <option value="mag">Magnitude</option>
                <option value="i">In-Phase</option>
                <option value="q">Quadrature</option>
                <option value="filt">Filtered</option>
              </select>
            </div>
          </div>
        </div>
      </div>

      <div id="stats"></div>
    </div>
  </div>

  <script>
    // =========================================================================
    // DSP LIBRARY
    // =========================================================================

    class Biquad {
      constructor(b0,b1,b2,a1,a2) {
        this.b0=b0; this.b1=b1; this.b2=b2; this.a1=a1; this.a2=a2;
        this.z1=0; this.z2=0;
      }
      reset() { this.z1=0; this.z2=0; }
      process(x) {
        const y = this.b0*x + this.z1;
        this.z1 = this.b1*x - this.a1*y + this.z2;
        this.z2 = this.b2*x - this.a2*y;
        return y;
      }
    }

    class FilterChain {
      constructor(sections) { this.sections = sections; }
      reset() { this.sections.forEach(s => s.reset()); }
      processArray(inp) {
        const out = new Float64Array(inp.length);
        for (let i=0; i<inp.length; i++) {
          let x = inp[i];
          for (const s of this.sections) x = s.process(x);
          out[i] = x;
        }
        return out;
      }
    }

    function designLP2(fc, Q, fs) {
      const w0 = 2*Math.PI*fc/fs;
      const alpha = Math.sin(w0)/(2*Q);
      const cw0 = Math.cos(w0);
      const b0 = (1-cw0)/2, b1 = 1-cw0, b2 = (1-cw0)/2;
      const a0 = 1+alpha, a1 = -2*cw0, a2 = 1-alpha;
      return new Biquad(b0/a0, b1/a0, b2/a0, a1/a0, a2/a0);
    }

    function designHP2(fc, Q, fs) {
      const w0 = 2*Math.PI*fc/fs;
      const alpha = Math.sin(w0)/(2*Q);
      const cw0 = Math.cos(w0);
      const b0 = (1+cw0)/2, b1 = -(1+cw0), b2 = (1+cw0)/2;
      const a0 = 1+alpha, a1 = -2*cw0, a2 = 1-alpha;
      return new Biquad(b0/a0, b1/a0, b2/a0, a1/a0, a2/a0);
    }

    function designNotch2(f0, Q, fs) {
      const w0 = 2*Math.PI*f0/fs;
      const alpha = Math.sin(w0)/(2*Q);
      const cw0 = Math.cos(w0);
      const a0 = 1+alpha;
      return new Biquad(1/a0, -2*cw0/a0, 1/a0, -2*cw0/a0, (1-alpha)/a0);
    }

    function designButterworthBP(fc, bw, fs, order) {
      const sections = [];
      const fLow = fc - bw/2, fHigh = fc + bw/2;
      const n = order/2;
      const Qs = [];
      for (let k=0; k<n; k++) Qs.push(1/(2*Math.cos(Math.PI*(2*k+1)/(4*n))));
      for (let k=0; k<n; k++) {
        sections.push(designHP2(fLow, Qs[k], fs));
        sections.push(designLP2(fHigh, Qs[k], fs));
      }
      return new FilterChain(sections);
    }

    function designNotchFilter(f0, Q, fs, stages) {
      const sections = [];
      for (let i=0; i<stages; i++) sections.push(designNotch2(f0, Q, fs));
      return new FilterChain(sections);
    }

    class LockInAmplifier {
      constructor(refFreq, tc, fs) {
        this.refFreq = refFreq;
        this.fs = fs;
        this.omega = 2*Math.PI*refFreq/fs;
        this.phase = 0;
        this.setTC(tc);
      }
      setTC(tc) {
        this.tc = tc;
        const lpFreq = 1/(2*Math.PI*tc);
        this.lpI = designLP2(lpFreq, 0.707, this.fs);
        this.lpQ = designLP2(lpFreq, 0.707, this.fs);
      }
      setFreq(f) { this.refFreq = f; this.omega = 2*Math.PI*f/this.fs; }
      reset() { this.phase = 0; this.lpI.reset(); this.lpQ.reset(); }
      processArray(inp, outType) {
        const n = inp.length;
        const out = new Float64Array(n);
        for (let i=0; i<n; i++) {
          const sinR = Math.sin(this.phase);
          const cosR = Math.cos(this.phase);
          this.phase += this.omega;
          if (this.phase > 2*Math.PI) this.phase -= 2*Math.PI;
          const I = this.lpI.process(inp[i]*sinR*2);
          const Q = this.lpQ.process(inp[i]*cosR*2);
          switch(outType) {
            case 'i': out[i] = I; break;
            case 'q': out[i] = Q; break;
            case 'filt': out[i] = I*Math.sin(this.phase) + Q*Math.cos(this.phase); break;
            default: out[i] = Math.sqrt(I*I + Q*Q);
          }
        }
        return out;
      }
    }

    // =========================================================================
    // FILTER STATE
    // =========================================================================

    const filt = {
      enabled: false,
      lockIn: false,
      fs: 1000,
      bpFc: 22, bpBw: 10, bpOrd: 4,
      notchEn: true, notchF: 60, notchQ: 35, notchN: 2,
      liaF: 22, liaTc: 0.1, liaOut: 'mag',
      bp: null, notch: null, lia: null
    };

    function rebuildFilters() {
      filt.bp = designButterworthBP(filt.bpFc, filt.bpBw, filt.fs, filt.bpOrd);
      filt.notch = filt.notchEn ? designNotchFilter(filt.notchF, filt.notchQ, filt.fs, filt.notchN) : null;
      if (!filt.lia) filt.lia = new LockInAmplifier(filt.liaF, filt.liaTc, filt.fs);
      else { filt.lia.setFreq(filt.liaF); filt.lia.setTC(filt.liaTc); }
    }

    function applyFilters(samples) {
      let result = samples;
      if (filt.lockIn) {
        if (filt.notch) result = filt.notch.processArray(result);
        result = filt.lia.processArray(result, filt.liaOut);
        return Array.from(result);
      }
      if (filt.enabled) {
        if (filt.notch) result = filt.notch.processArray(result);
        if (filt.bp) result = filt.bp.processArray(result);
      }
      return Array.from(result);
    }

    // =========================================================================
    // CHART
    // =========================================================================

    const BUFFER_SEC = 30;
    const SAMPLE_RATE = 1000;
    const MAX_PTS = BUFFER_SEC * SAMPLE_RATE;

    let plotInit = false;
    let followLive = true;
    let suppressRelayout = false;
    let nextTime = null;
    let lastTime = null;
    let frameCount = 0;

    function initPlot() {
      Plotly.newPlot('scope', [{
        x: [], y: [],
        mode: 'lines',
        line: { width: 1, color: '#00ff88' }
      }], {
        margin: { l: 50, r: 10, t: 5, b: 25 },
        paper_bgcolor: '#000',
        plot_bgcolor: '#000',
        showlegend: false,
        xaxis: { title: 'Time (s)', showgrid: true, gridcolor: '#222', tickfont: { color: '#888', size: 9 } },
        yaxis: { title: 'Counts', showgrid: true, gridcolor: '#222', tickfont: { color: '#888', size: 9 }, autorange: true }
      }, { responsive: true, displaylogo: false, modeBarButtonsToRemove: ['select2d', 'lasso2d'] }).then(gd => {
        gd.on('plotly_relayout', () => { if (!suppressRelayout) followLive = false; });
      });
      plotInit = true;
    }

    function setLiveView() {
      if (lastTime == null) return;
      suppressRelayout = true;
      Plotly.relayout('scope', { 'xaxis.range': [lastTime - BUFFER_SEC, lastTime] }).then(() => { suppressRelayout = false; });
    }

    function appendFrame(msg) {
      if (!plotInit) initPlot();
      let geo = msg.geo;
      if (!geo || !geo.length) return;

      const dt = 1.0 / SAMPLE_RATE;
      if (nextTime === null) nextTime = msg.t0 || 0;

      if (filt.enabled || filt.lockIn) geo = applyFilters(geo);

      const xs = new Array(geo.length);
      for (let i = 0; i < geo.length; i++) xs[i] = nextTime + i * dt;
      nextTime += geo.length * dt;
      lastTime = xs[geo.length - 1];
      frameCount++;

      Plotly.extendTraces('scope', { x: [xs], y: [geo] }, [0], MAX_PTS);

      const mode = filt.lockIn ? ' [LOCK-IN]' : (filt.enabled ? ' [FILTERED]' : '');
      document.getElementById('view-time').textContent = lastTime.toFixed(1) + 's' + mode;

      if (followLive) setLiveView();

      document.getElementById('stats').textContent = 'Frames: ' + frameCount;
    }

    // =========================================================================
    // UI
    // =========================================================================

    const PGA = [1,2,4,8,16,32,64];
    let pgaIdx = 2;

    function initUI() {
      const btnFilter = document.getElementById('btn-filter');
      const btnLockin = document.getElementById('btn-lockin');
      const freqCtrl = document.getElementById('freq-ctrl');
      const notchCtrl = document.getElementById('notch-ctrl');

      btnFilter.onclick = () => {
        filt.enabled = !filt.enabled;
        filt.lockIn = false;
        btnFilter.classList.toggle('active', filt.enabled);
        btnLockin.classList.remove('lockin');
        freqCtrl.style.display = filt.enabled ? 'flex' : 'none';
        notchCtrl.style.display = filt.enabled ? 'flex' : 'none';
        btnFilter.textContent = filt.enabled ? 'Filter ON' : 'Enable Filter';
        rebuildFilters();
      };

      btnLockin.onclick = () => {
        filt.lockIn = !filt.lockIn;
        filt.enabled = false;
        btnLockin.classList.toggle('lockin', filt.lockIn);
        btnFilter.classList.remove('active');
        btnFilter.textContent = 'Enable Filter';
        freqCtrl.style.display = filt.lockIn ? 'flex' : 'none';
        notchCtrl.style.display = filt.lockIn ? 'flex' : 'none';
        btnLockin.textContent = filt.lockIn ? 'Lock-In ON' : 'Lock-In Amp';
        rebuildFilters();
      };

      document.getElementById('freq-down').onclick = () => {
        filt.bpFc = Math.max(10, filt.bpFc - 1);
        filt.liaF = filt.bpFc;
        document.getElementById('freq-val').textContent = filt.bpFc;
        document.getElementById('bp-fc').value = filt.bpFc;
        document.getElementById('bp-fc-val').textContent = filt.bpFc + ' Hz';
        document.getElementById('lia-f').value = filt.liaF;
        document.getElementById('lia-f-val').textContent = filt.liaF + ' Hz';
        rebuildFilters();
      };

      document.getElementById('freq-up').onclick = () => {
        filt.bpFc = Math.min(50, filt.bpFc + 1);
        filt.liaF = filt.bpFc;
        document.getElementById('freq-val').textContent = filt.bpFc;
        document.getElementById('bp-fc').value = filt.bpFc;
        document.getElementById('bp-fc-val').textContent = filt.bpFc + ' Hz';
        document.getElementById('lia-f').value = filt.liaF;
        document.getElementById('lia-f-val').textContent = filt.liaF + ' Hz';
        rebuildFilters();
      };

      document.getElementById('btn-notch').onclick = function() {
        filt.notchEn = !filt.notchEn;
        this.classList.toggle('active', filt.notchEn);
        this.textContent = filt.notchEn ? 'ON' : 'OFF';
        document.getElementById('notch-en').checked = filt.notchEn;
        rebuildFilters();
      };

      document.getElementById('pga-down').onclick = () => {
        pgaIdx = Math.max(0, pgaIdx - 1);
        document.getElementById('pga-val').textContent = PGA[pgaIdx] + 'x';
      };
      document.getElementById('pga-up').onclick = () => {
        pgaIdx = Math.min(PGA.length - 1, pgaIdx + 1);
        document.getElementById('pga-val').textContent = PGA[pgaIdx] + 'x';
      };

      document.getElementById('btn-adv').onclick = () => {
        document.getElementById('advanced').classList.toggle('show');
      };

      document.getElementById('btn-live').onclick = () => { followLive = true; setLiveView(); };

      // Advanced controls
      const bindSlider = (id, obj, prop, suffix, cb) => {
        const el = document.getElementById(id);
        const valEl = document.getElementById(id + '-val');
        el.oninput = () => {
          obj[prop] = parseFloat(el.value);
          valEl.textContent = (prop === 'liaTc' ? (obj[prop]*1000).toFixed(0) + ' ms' : obj[prop] + (suffix||''));
          if (cb) cb();
          rebuildFilters();
        };
      };

      bindSlider('bp-fc', filt, 'bpFc', ' Hz', () => {
        filt.liaF = filt.bpFc;
        document.getElementById('freq-val').textContent = filt.bpFc;
        document.getElementById('lia-f').value = filt.liaF;
        document.getElementById('lia-f-val').textContent = filt.liaF + ' Hz';
      });
      bindSlider('bp-bw', filt, 'bpBw', ' Hz');
      bindSlider('bp-ord', filt, 'bpOrd', '');
      bindSlider('notch-f', filt, 'notchF', ' Hz');
      bindSlider('notch-q', filt, 'notchQ', '');
      bindSlider('notch-n', filt, 'notchN', '');
      bindSlider('lia-f', filt, 'liaF', ' Hz', () => {
        filt.bpFc = filt.liaF;
        document.getElementById('freq-val').textContent = filt.liaF;
        document.getElementById('bp-fc').value = filt.bpFc;
        document.getElementById('bp-fc-val').textContent = filt.bpFc + ' Hz';
      });
      bindSlider('lia-tc', filt, 'liaTc');

      document.getElementById('notch-en').onchange = function() {
        filt.notchEn = this.checked;
        document.getElementById('btn-notch').classList.toggle('active', filt.notchEn);
        document.getElementById('btn-notch').textContent = filt.notchEn ? 'ON' : 'OFF';
        rebuildFilters();
      };

      document.getElementById('lia-out').onchange = function() {
        filt.liaOut = this.value;
      };

      rebuildFilters();
    }

    // =========================================================================
    // WEBSOCKET
    // =========================================================================

    const statusEl = document.getElementById('status');

    function connectWS() {
      const ws = new WebSocket('ws://' + location.host + '/ws');
      statusEl.textContent = 'Connecting...';
      statusEl.className = 'connecting';

      ws.onopen = () => {
        statusEl.textContent = 'Connected';
        statusEl.className = 'connected';
      };

      ws.onclose = () => {
        statusEl.textContent = 'Reconnecting...';
        statusEl.className = 'connecting';
        setTimeout(connectWS, 2000);
      };

      ws.onerror = () => {
        statusEl.textContent = 'Error';
        statusEl.className = 'error';
      };

      ws.onmessage = (e) => {
        try { appendFrame(JSON.parse(e.data)); } catch (err) { console.error(err); }
      };
    }

    // =========================================================================
    // INIT
    // =========================================================================

    document.addEventListener('DOMContentLoaded', () => {
      initPlot();
      initUI();
      connectWS();
    });
  </script>
</body>
</html>
)rawliteral";

const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;
