/* OSLTT Web Controller — app.js */

/* ===== FEATURE DETECTION ===== */
if (!('serial' in navigator)) {
  document.body.innerHTML = `
    <div style="max-width: 600px; margin: 80px auto; padding: 24px; background: #161b22; border: 1px solid #30363d; border-radius: 12px; color: #c9d1d9; font-family: sans-serif;">
      <h1 style="color: #f85149;">Web Serial not available</h1>
      <p>The OSLTT Web Controller requires the <strong>Web Serial API</strong>, which is only available in Chromium-based browsers (Chrome, Edge, Brave, Opera).</p>
      <p>If you're already using a supported browser:</p>
      <ul>
        <li>Make sure you're on <strong>Chrome 89+</strong> / <strong>Edge 89+</strong></li>
        <li>Serve this page via <code>http://localhost</code> instead of opening the file directly</li>
        <li>On Linux, you may need to add your user to the <code>dialout</code> or <code>uucp</code> group</li>
      </ul>
      <p style="margin-top: 16px;">
        <strong>Quick start:</strong><br>
        <code style="background: #0d1117; padding: 4px 8px; border-radius: 6px;">cd webapp && python3 -m http.server 8080</code><br>
        Then open <a href="http://localhost:8080" style="color: #58a6ff;">http://localhost:8080</a>
      </p>
    </div>
  `;
  throw new Error('Web Serial API not available');
}

/* ===== STATE ===== */
let port = null;
let reader = null;
let writer = null;
let readBuffer = '';
let connected = false;
let running = false;
let armed = false;
let rawResults = [];
let currentShot = 1;
let totalShots = 1;
let compareChart = null;
let savedRuns = [];
let compareTableRuns = [];
let compareTableSort = { key: null, dir: 1 };

/* ===== DOM ===== */
const $ = id => document.getElementById(id);

/* ===== CHARTS ===== */
let liveChart = null;
let resultsChart = null;

function initCharts() {
  if (typeof Chart === 'undefined') {
    $('livePanel').innerHTML += '<div class="info" style="color:#f85149">Chart.js failed to load. Check your internet connection.</div>';
    return;
  }

  Chart.defaults.color = '#8b949e';
  Chart.defaults.borderColor = '#30363d';

  liveChart = new Chart($('liveChart'), {
    type: 'line',
    data: { labels: [], datasets: [
      { label: 'Light Sensor', data: [], borderColor: '#58a6ff', backgroundColor: 'rgba(88,166,255,0.12)', fill: true, pointRadius: 0, tension: 0 },
      { label: 'Upper threshold', data: [], borderColor: '#f85149', borderDash: [6, 4], pointRadius: 0, fill: false, tension: 0 },
      { label: 'Lower threshold', data: [], borderColor: '#f85149', borderDash: [6, 4], pointRadius: 0, fill: false, tension: 0 },
      { label: 'Baseline', data: [], borderColor: '#3fb950', borderDash: [2, 4], pointRadius: 0, fill: false, tension: 0 },
      { label: 'Trigger', data: [], borderColor: '#d29922', backgroundColor: '#d29922', pointRadius: 0, showLine: false }
    ]},
    options: {
      responsive: true, maintainAspectRatio: false,
      animation: false,
      interaction: { intersect: false },
      plugins: { legend: { display: false } },
      scales: {
        x: { display: false },
        y: { beginAtZero: true }
      }
    }
  });

  resultsChart = new Chart($('resultsChart'), {
    type: 'bar',
    data: { labels: [], datasets: [{ label: 'Latency (ms)', data: [], backgroundColor: '#a371f7', borderRadius: 4 }] },
    options: {
      responsive: true, maintainAspectRatio: false,
      plugins: { legend: { display: false } },
      scales: {
        x: { grid: { display: false } },
        y: { beginAtZero: true, title: { display: true, text: 'ms' } }
      }
    }
  });
}

/* ===== SAVED RUNS ===== */
function loadSavedRuns() {
  try {
    savedRuns = JSON.parse(localStorage.getItem('osltt_runs') || '[]');
  } catch (e) {
    savedRuns = [];
  }
  renderSavedRuns();
}

function saveRunsToStorage() {
  localStorage.setItem('osltt_runs', JSON.stringify(savedRuns));
}

function saveRun() {
  if (!rawResults.length) return;
  const name = prompt('Save run as:', `Run ${new Date().toLocaleString()}`);
  if (!name) return;
  const latencies = rawResults.map(r => r.latencyUs / 1000);
  const avg = latencies.reduce((a, b) => a + b, 0) / latencies.length;
  const min = Math.min(...latencies);
  const max = Math.max(...latencies);
  const sd = Math.sqrt(latencies.reduce((s, v) => s + (v - avg) ** 2, 0) / latencies.length);
  const run = {
    id: Date.now(),
    name,
    date: new Date().toISOString(),
    latencies,
    settings: {
      shots: parseInt($('shots').value, 10),
      delayMs: parseInt($('delayMs').value, 10),
      movePx: parseInt($('movePx').value, 10),
      windowMs: parseInt($('windowMs').value, 10),
      threshold: parseInt($('threshold').value, 10),
      intervalUs: parseInt($('intervalUs').value, 10)
    },
    stats: { avg, min, max, sd }
  };
  savedRuns.push(run);
  saveRunsToStorage();
  renderSavedRuns();
}

function deleteSavedRun(id) {
  savedRuns = savedRuns.filter(r => r.id !== id);
  saveRunsToStorage();
  renderSavedRuns();
}

function renameRun(id) {
  const run = savedRuns.find(r => r.id === id);
  if (!run) return;
  const newName = prompt('Rename run:', run.name);
  if (newName && newName.trim()) {
    run.name = newName.trim();
    saveRunsToStorage();
    renderSavedRuns();
  }
}

function renderSavedRuns() {
  const container = $('savedRunsList');
  if (!savedRuns.length) {
    container.innerHTML = '<div class="info">No saved runs yet. Complete a test and click "Save Run".</div>';
    $('compareSelectedBtn').disabled = true;
    return;
  }
  container.innerHTML = savedRuns.map(r => `
    <div class="run-card">
      <input type="checkbox" value="${r.id}" />
      <div class="run-info">
        <div class="run-name">${escapeHtml(r.name)}</div>
        <div class="run-meta">${r.latencies.length} shots &middot; Avg ${r.stats.avg.toFixed(3)} ms &middot; ${new Date(r.date).toLocaleString()}</div>
      </div>
      <button class="btn btn-secondary btn-sm" data-ren="${r.id}">Rename</button>
      <button class="btn btn-secondary btn-sm" data-del="${r.id}">Delete</button>
    </div>
  `).join('');
  container.querySelectorAll('input[type="checkbox"]').forEach(cb => {
    cb.addEventListener('change', updateCompareButton);
  });
  container.querySelectorAll('button[data-ren]').forEach(btn => {
    btn.addEventListener('click', e => renameRun(parseInt(e.target.dataset.ren, 10)));
  });
  container.querySelectorAll('button[data-del]').forEach(btn => {
    btn.addEventListener('click', e => deleteSavedRun(parseInt(e.target.dataset.del, 10)));
  });
  updateCompareButton();
}

function updateCompareButton() {
  const checked = document.querySelectorAll('#savedRunsList input[type="checkbox"]:checked');
  $('compareSelectedBtn').disabled = checked.length < 2;
}

function escapeHtml(str) {
  return str.replace(/[&<>"']/g, m => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[m]));
}

function compareSelected() {
  const checked = Array.from(document.querySelectorAll('#savedRunsList input[type="checkbox"]:checked'))
    .map(cb => savedRuns.find(r => r.id === parseInt(cb.value, 10)))
    .filter(Boolean);
  if (checked.length < 2) return;

  const maxShots = Math.max(...checked.map(r => r.latencies.length));
  const labels = Array.from({ length: maxShots }, (_, i) => i + 1);

  const colors = ['#58a6ff', '#a371f7', '#3fb950', '#d29922', '#f85149', '#79c0ff'];
  const datasets = checked.map((r, i) => ({
    label: escapeHtml(r.name),
    data: r.latencies,
    borderColor: colors[i % colors.length],
    backgroundColor: colors[i % colors.length],
    pointRadius: 2,
    borderWidth: 2,
    tension: 0.1,
    fill: false
  }));

  if (compareChart) {
    compareChart.data.labels = labels;
    compareChart.data.datasets = datasets;
    compareChart.update();
  } else {
    compareChart = new Chart($('compareChart'), {
      type: 'line',
      data: { labels, datasets },
      options: {
        responsive: true, maintainAspectRatio: false,
        plugins: {
          legend: { labels: { color: '#c9d1d9' } },
          tooltip: { mode: 'index', intersect: false }
        },
        scales: {
          x: { grid: { color: '#30363d' }, ticks: { color: '#8b949e' } },
          y: { beginAtZero: false, grid: { color: '#30363d' }, ticks: { color: '#8b949e' }, title: { display: true, text: 'Latency (ms)', color: '#8b949e' } }
        },
        interaction: { mode: 'nearest', axis: 'x', intersect: false }
      }
    });
  }

  compareTableRuns = checked.map((r, i) => ({ ...r, _color: colors[i % colors.length] }));
  renderCompareTable();
}

function renderCompareTable() {
  if (compareTableRuns.length < 2) return;
  const rows = [...compareTableRuns];
  if (compareTableSort.key) {
    rows.sort((a, b) => {
      let va, vb;
      switch (compareTableSort.key) {
        case 'name': va = a.name.toLowerCase(); vb = b.name.toLowerCase(); break;
        case 'shots': va = a.latencies.length; vb = b.latencies.length; break;
        case 'avg': va = a.stats.avg; vb = b.stats.avg; break;
        case 'min': va = a.stats.min; vb = b.stats.min; break;
        case 'max': va = a.stats.max; vb = b.stats.max; break;
        case 'sd': va = a.stats.sd; vb = b.stats.sd; break;
        default: return 0;
      }
      return (va > vb ? 1 : va < vb ? -1 : 0) * compareTableSort.dir;
    });
  }

  const getInd = (key) => {
    if (compareTableSort.key !== key) return '';
    return compareTableSort.dir === 1 ? ' ▲' : ' ▼';
  };

  let minAll = Infinity, maxAll = -Infinity;
  rows.forEach(r => { minAll = Math.min(minAll, r.stats.min); maxAll = Math.max(maxAll, r.stats.max); });
  const range = maxAll - minAll || 1;

  const html = `
    <table class="compare-table">
      <thead>
        <tr>
          <th onclick="sortCompareTable('name')" style="text-align:left">Run<span class="sort-indicator">${getInd('name')}</span></th>
          <th onclick="sortCompareTable('shots')">Shots<span class="sort-indicator">${getInd('shots')}</span></th>
          <th onclick="sortCompareTable('avg')">Avg<span class="sort-indicator">${getInd('avg')}</span></th>
          <th onclick="sortCompareTable('min')">Min<span class="sort-indicator">${getInd('min')}</span></th>
          <th onclick="sortCompareTable('max')">Max<span class="sort-indicator">${getInd('max')}</span></th>
          <th onclick="sortCompareTable('sd')">Std Dev<span class="sort-indicator">${getInd('sd')}</span></th>
        </tr>
      </thead>
      <tbody>
        ${rows.map(r => {
          const best = r.stats.min === minAll;
          return `
          <tr class="${best ? 'row-best' : ''}">
            <td style="text-align:left">
              <div class="name-cell">
                <span class="row-bar" style="background:${r._color}"></span>
                ${escapeHtml(r.name)}
                ${best ? '<span class="best-star" title="Lowest minimum latency">★</span>' : ''}
              </div>
            </td>
            <td class="num">${r.latencies.length}</td>
            <td class="num">${r.stats.avg.toFixed(3)} ms</td>
            <td class="num ${best ? 'best-cell' : ''}">${r.stats.min.toFixed(3)} ms</td>
            <td class="num">${r.stats.max.toFixed(3)} ms</td>
            <td class="num">${r.stats.sd.toFixed(3)} ms</td>
          </tr>
          `;
        }).join('')}
      </tbody>
    </table>
  `;
  $('compareStats').innerHTML = html;
}

function sortCompareTable(key) {
  if (compareTableSort.key === key) {
    compareTableSort.dir *= -1;
  } else {
    compareTableSort = { key, dir: 1 };
  }
  renderCompareTable();
}

function clearSavedRuns() {
  if (!savedRuns.length) return;
  if (!confirm('Delete all saved runs?')) return;
  savedRuns = [];
  saveRunsToStorage();
  renderSavedRuns();
  if (compareChart) {
    compareChart.data.labels = [];
    compareChart.data.datasets = [];
    compareChart.update();
  }
  compareTableRuns = [];
  compareTableSort = { key: null, dir: 1 };
  $('compareStats').innerHTML = '';
}

/* ===== CONNECTION ===== */
async function connect() {
  try {
    try {
      port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x2886, usbProductId: 0x802f }, { usbVendorId: 0x239a }] });
    } catch (e) {
      if (e.name === 'NotFoundError') {
        port = await navigator.serial.requestPort();
      } else {
        throw e;
      }
    }
    await port.open({ baudRate: 2000000 });
    writer = port.writable.getWriter();
    reader = port.readable.getReader();
    connected = true;
    updateStatus('connected');
    setButtons(true);
    $('connectBtn').disabled = true;
    $('disconnectBtn').disabled = false;
    readLoop();
  } catch (e) {
    if (e.name === 'NotFoundError') {
      $('deviceInfo').textContent = 'No device selected or no compatible device found.';
    } else if (e.name === 'NotAllowedError') {
      $('deviceInfo').textContent = 'Permission denied or picker cancelled.';
    } else if (e.name === 'NetworkError') {
      $('deviceInfo').textContent = 'Failed to open port. Is another app using it?';
    } else {
      $('deviceInfo').textContent = 'Connection failed: ' + e.message;
    }
    console.error(e);
  }
}

async function disconnect() {
  connected = false;
  running = false;
  armed = false;
  try { reader.cancel(); } catch {}
  try { writer.close(); } catch {}
  try { await port.close(); } catch {}
  port = null; reader = null; writer = null;
  updateStatus('disconnected');
  setButtons(false);
  $('connectBtn').disabled = false;
  $('disconnectBtn').disabled = true;
}

async function readLoop() {
  while (connected) {
    try {
      const { value, done } = await reader.read();
      if (done) break;
      readBuffer += new TextDecoder().decode(value);
      processLines();
    } catch (e) {
      if (connected) {
        console.error('Read error:', e);
        $('deviceInfo').textContent = 'Read error: ' + e.message;
        break;
      }
    }
  }
}

/* ===== PARSER ===== */
function processLines() {
  let idx;
  while ((idx = readBuffer.indexOf('\n')) !== -1) {
    const line = readBuffer.slice(0, idx).trim();
    readBuffer = readBuffer.slice(idx + 1);
    handleLine(line);
  }
}

function handleLine(line) {
  if (!line) return;

  if (line === 'OK') {
    $('deviceInfo').textContent = 'Settings synced.';
    return;
  }
  if (line === 'ARMED') {
    armed = true;
    updateStatus('running');
    $('progressArea').textContent = 'Armed — press the device button to start';
    $('runTestBtn').textContent = 'Disarm';
    return;
  }
  if (line === 'DISARMED') {
    armed = false;
    updateStatus('connected');
    $('progressArea').textContent = 'Disarmed';
    $('runTestBtn').textContent = 'Arm Full Test';
    return;
  }
  if (line === 'START') {
    running = true;
    updateStatus('running');
    $('progressArea').textContent = `Running test (${currentShot}/${totalShots})...`;
    $('runTestBtn').textContent = 'Running...';
    $('runTestBtn').disabled = true;
    return;
  }
  if (line === 'DONE') {
    running = false;
    armed = false;
    updateStatus('connected');
    $('progressArea').textContent = 'Test complete.';
    $('runTestBtn').textContent = 'Arm Full Test';
    $('runTestBtn').disabled = false;
    finalizeResults();
    return;
  }
  if (line === 'ABORTED') {
    running = false;
    armed = false;
    updateStatus('connected');
    $('progressArea').textContent = 'Aborted.';
    $('runTestBtn').textContent = 'Arm Full Test';
    $('runTestBtn').disabled = false;
    return;
  }
  if (line === 'TIMEOUT') {
    $('progressArea').textContent = `Shot ${currentShot}: TIMEOUT — no signal detected`;
    currentShot++;
    return;
  }
  if (line.startsWith('FW:')) {
    $('deviceInfo').textContent = `Connected. Firmware ${line.slice(3)}`;
    return;
  }
  if (line.startsWith('BOARD:')) {
    $('deviceInfo').textContent += ` | Board: ${line.slice(6)}`;
    return;
  }
  if (line.startsWith('ERR:')) {
    $('deviceInfo').textContent = 'Error: ' + line;
    return;
  }

  if (line.startsWith('R,')) {
    const parts = line.split(',');
    if (parts.length >= 7) {
      currentSamples = {
        latencyUs: parseInt(parts[1]),
        latencyIndex: parseInt(parts[2]),
        intervalUs: parseInt(parts[3]),
        numSamples: parseInt(parts[4]),
        baseline: parseInt(parts[5]),
        threshold: parseInt(parts[6]),
        samples: []
      };
    }
    return;
  }

  if (currentSamples && currentSamples.samples.length === 0) {
    const nums = line.split(',').map(s => parseInt(s, 10)).filter(n => !isNaN(n));
    currentSamples.samples = nums;
    recordShot(currentSamples);
    currentSamples = null;
  }
}

let currentSamples = null;

function recordShot(data) {
  const latencyMs = data.latencyUs / 1000;
  const latencyIndex = data.latencyIndex ?? Math.round(data.latencyUs / data.intervalUs);
  rawResults.push(data);
  updateStats();
  plotLive(data.samples, data.baseline, data.threshold, latencyIndex);
  if (running) {
    currentShot++;
    $('progressArea').textContent = `Shot ${currentShot}/${totalShots} — latency: ${latencyMs.toFixed(3)} ms`;
  }
}

/* ===== UI UPDATES ===== */
function setButtons(on) {
  $('armBtn').disabled = !on;
  $('runTestBtn').disabled = !on ? true : running; // running state controls this
  $('abortBtn').disabled = !on ? true : !running && !armed;
  $('saveSettingsBtn').disabled = !on;
  $('exportCsvBtn').disabled = rawResults.length === 0;
  $('exportRawBtn').disabled = rawResults.length === 0;
  $('saveRunBtn').disabled = rawResults.length === 0;
}

function updateStatus(state) {
  const el = $('connectionStatus');
  el.className = 'status ' + state;
  if (state === 'connected') el.textContent = 'Connected';
  else if (state === 'disconnected') el.textContent = 'Disconnected';
  else if (state === 'running') el.textContent = 'Running...';
}

function updateStats() {
  if (!rawResults.length) { $('stats').innerHTML = ''; return; }
  const latencies = rawResults.map(r => r.latencyUs / 1000);
  const avg = latencies.reduce((a, b) => a + b, 0) / latencies.length;
  const min = Math.min(...latencies);
  const max = Math.max(...latencies);
  const sd = Math.sqrt(latencies.reduce((s, v) => s + (v - avg) ** 2, 0) / latencies.length);

  const boxes = [
    { l: 'Shots', v: latencies.length },
    { l: 'Average', v: avg.toFixed(3) + ' ms' },
    { l: 'Min', v: min.toFixed(3) + ' ms' },
    { l: 'Max', v: max.toFixed(3) + ' ms' },
    { l: 'Std Dev', v: sd.toFixed(3) + ' ms' },
  ];

  $('stats').innerHTML = boxes.map(b =>
    `<div class="stat-box"><div class="label">${b.l}</div><div class="value">${b.v}</div></div>`
  ).join('');
}

function plotLive(samples, baseline, threshold, latencyIndex) {
  if (!liveChart) return;
  const labels = samples.map((_, i) => i);
  liveChart.data.labels = labels;
  liveChart.data.datasets[0].data = samples;
  liveChart.data.datasets[1] = {
    label: 'Upper threshold',
    data: new Array(samples.length).fill(baseline + threshold),
    borderColor: '#f85149',
    borderDash: [6, 4],
    pointRadius: 0,
    fill: false,
    tension: 0
  };
  liveChart.data.datasets[2] = {
    label: 'Lower threshold',
    data: new Array(samples.length).fill((baseline > threshold) ? baseline - threshold : 0),
    borderColor: '#f85149',
    borderDash: [6, 4],
    pointRadius: 0,
    fill: false,
    tension: 0
  };
  liveChart.data.datasets[3] = {
    label: 'Baseline',
    data: new Array(samples.length).fill(baseline),
    borderColor: '#3fb950',
    borderDash: [2, 4],
    pointRadius: 0,
    fill: false,
    tension: 0
  };
  if (latencyIndex != null && latencyIndex < samples.length) {
    const markerData = new Array(samples.length).fill(null);
    markerData[latencyIndex] = samples[latencyIndex];
    liveChart.data.datasets[4] = {
      label: 'Trigger',
      data: markerData,
      borderColor: '#d29922',
      backgroundColor: '#d29922',
      pointRadius: 6,
      pointStyle: 'circle',
      showLine: false,
    };
  }
  liveChart.update('none');
}

function finalizeResults() {
  if (!resultsChart) return;
  const labels = rawResults.map((_, i) => i + 1);
  const data = rawResults.map(r => r.latencyUs / 1000);
  resultsChart.data.labels = labels;
  resultsChart.data.datasets[0].data = data;
  resultsChart.update();
  updateStats();
  setButtons(true);
}

/* ===== SERIAL SEND ===== */
async function send(str) {
  if (!writer) {
    console.warn('[send] no writer (not connected)');
    return;
  }
  const enc = new TextEncoder();
  await writer.write(enc.encode(str + '\n'));
}

/* ===== ACTIONS ===== */
async function saveSettings() {
  const shots  = parseInt($('shots').value, 10) || 100;
  const delay  = parseInt($('delayMs').value, 10) || 500;
  const move   = parseInt($('movePx').value, 10) || 20;
  const win    = parseInt($('windowMs').value, 10) || 80;
  const thr    = parseInt($('threshold').value, 10) || 0;
  const intv   = parseInt($('intervalUs').value, 10) || 10;
  const cmd = `C,${shots},${delay},${move},${thr},${win},${intv}`;
  $('deviceInfo').textContent = 'Saving settings...';
  await send(cmd);
}

async function armSingle() {
  currentShot = 1; totalShots = 1;
  await send('S');
}

async function runTest() {
  if (armed) {
    // Currently armed — disarm
    await send('D');
    return;
  }

  totalShots = parseInt($('shots').value, 10) || 100;
  currentShot = 1;
  rawResults = [];

  await saveSettings();
  await new Promise(r => setTimeout(r, 150));
  await send('A');
}

async function abort() {
  if (armed || running) {
    await send('X');
    armed = false;
  }
}

/* ===== EXPORT ===== */
function download(filename, text) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type: 'text/csv' }));
  a.download = filename;
  a.click();
}

function exportCsv() {
  if (!rawResults.length) return;
  const lines = ['Shot,LatencyUs,LatencyMs,Baseline,Threshold,IntervalUs,NumSamples'];
  rawResults.forEach((r, i) => {
    lines.push(`${i+1},${r.latencyUs},${(r.latencyUs/1000).toFixed(4)},${r.baseline},${r.threshold},${r.intervalUs},${r.numSamples}`);
  });
  const avg = rawResults.reduce((s, r) => s + r.latencyUs, 0) / rawResults.length;
  const latencies = rawResults.map(r => r.latencyUs / 1000);
  const min = Math.min(...latencies);
  const max = Math.max(...latencies);
  lines.push(`AVERAGE,${avg.toFixed(2)},${(avg/1000).toFixed(4)},,,,`);
  lines.push(`MIN,,${min.toFixed(4)},,,,`);
  lines.push(`MAX,,${max.toFixed(4)},,,,`);
  download('osltt-results.csv', lines.join('\n'));
}

function exportRaw() {
  if (!rawResults.length) return;
  const lines = rawResults.map((r, i) => {
    return `Shot ${i+1}: ` + r.samples.join(',');
  });
  download('osltt-raw.csv', lines.join('\n'));
}

function clearResults() {
  rawResults = [];
  updateStats();
  if (liveChart) {
    liveChart.data.labels = [];
    liveChart.data.datasets.forEach(ds => { ds.data = []; });
    liveChart.update();
  }
  if (resultsChart) {
    resultsChart.data.labels = [];
    resultsChart.data.datasets[0].data = [];
    resultsChart.update();
  }
  $('progressArea').textContent = '';
  setButtons(connected);
}

/* ===== EVENTS ===== */
$('connectBtn').addEventListener('click', connect);
$('disconnectBtn').addEventListener('click', disconnect);
$('saveSettingsBtn').addEventListener('click', saveSettings);
$('armBtn').addEventListener('click', armSingle);
$('runTestBtn').addEventListener('click', runTest);
$('abortBtn').addEventListener('click', abort);
$('exportCsvBtn').addEventListener('click', exportCsv);
$('exportRawBtn').addEventListener('click', exportRaw);
$('clearResultsBtn').addEventListener('click', clearResults);
$('saveRunBtn').addEventListener('click', saveRun);
$('compareSelectedBtn').addEventListener('click', compareSelected);
$('clearComparedBtn').addEventListener('click', clearSavedRuns);

// Prevent space/enter from triggering buttons while typing in inputs
document.querySelectorAll('input').forEach(inp => {
  inp.addEventListener('keydown', e => e.stopPropagation());
});

initCharts();
loadSavedRuns();
