/* ============================================================
   HOCHBEET — device web UI logic  (vanilla, offline)
   Polls the ESP32 firmware and renders the warm dashboard.
   Data contract:
     GET  /data.json     tempC humPerc lux pumpStatus MAINTENANCE_MODE
                         soilPerc[5] waterLow waterCritical
                         (waterLow/waterCritical bool: true = sufficient, false = tripped)
     GET  /systemStatus  wifi uptime
     GET  /diagnostics   soilRaw[5] temperature humidity light roofState
                         roofContact pumpStatus errorFlags commStatus ledPWM freeHeap
     GET  /loadConfig    moisture[5] luxTarget tempTarget lightProfile[24]
     POST /saveConfig    {moisture[5], luxTarget, tempTarget, lightProfile[24]}
     GET  /getPlants  ·  POST /addPlant  ·  DELETE /deletePlant
     POST /manual {command,value?}  ·  POST /ackErrors  ·  POST /setTime {timestamp}
     GET  /logs.json     [{ t, up, lvl, tag, msg }]  (oldest→newest; t = secs since midnight)
   ============================================================ */
'use strict';
const $ = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

/* ---- toast ------------------------------------------------- */
let toastT;
function toast(msg, bad) {
  const el = $('#toast');
  el.textContent = msg;
  el.style.background = bad ? 'var(--rose)' : 'var(--ink)';
  el.classList.add('show');
  clearTimeout(toastT);
  toastT = setTimeout(() => el.classList.remove('show'), 2200);
}

/* ---- motion : GSAP-driven, reduced-motion + offline aware ---
   Single source of truth for "should we animate". `on` is false when
   GSAP failed to load (offline edge) or the user prefers reduced motion;
   every animated feature falls back to an instant set in that case. */
const MOTION = {
  reduce: !!(window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches),
  get on() { return !!window.gsap && !this.reduce; },
};
(function () {
  const mm = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)');
  if (mm && mm.addEventListener) mm.addEventListener('change', (e) => {
    MOTION.reduce = e.matches;
    document.documentElement.classList.toggle('js-motion', MOTION.on);
  });
  if (window.gsap) {
    gsap.defaults({ ease: 'power4.out', duration: 0.6 });
    if (window.Flip) gsap.registerPlugin(Flip); // bed-reorder animation
  }
  document.documentElement.classList.toggle('js-motion', MOTION.on);
})();

/* Reveal an SVG line path by drawing it in. No-op when motion is off. */
function drawReveal(path, doIt) {
  if (!doIt || !MOTION.on || !path || typeof path.getTotalLength !== 'function') return;
  let len;
  try { len = path.getTotalLength(); } catch (e) { return; }
  if (!len) return;
  gsap.killTweensOf(path);
  gsap.set(path, { strokeDasharray: len, strokeDashoffset: len });
  gsap.to(path, {
    strokeDashoffset: 0, duration: 0.7, ease: 'power2.out',
    onComplete() { path.style.removeProperty('stroke-dasharray'); path.style.removeProperty('stroke-dashoffset'); },
  });
}

/* Stagger the direct children of a view into place on activation. */
function revealView(viewEl) {
  if (!MOTION.on || !viewEl) return;
  const items = [...viewEl.children].filter((el) => !el.hidden);
  if (!items.length) return;
  gsap.killTweensOf(items);
  gsap.from(items, { y: 8, opacity: 0, duration: 0.45, stagger: 0.05, overwrite: true, clearProps: 'transform,opacity' });
}

/* One-time calm emphasis on an indicator when a fault first appears. */
function faultCue(el) {
  if (!el || !MOTION.on) return;
  gsap.fromTo(el, { scale: 1 }, {
    scale: 1.35, duration: 0.18, yoyo: true, repeat: 3, ease: 'power2.inOut',
    transformOrigin: '50% 50%', overwrite: true, clearProps: 'transform',
  });
}

/* ---- icons : expand <svg class="ic" data-ic="x"> ----------- */
function initIcons(root = document) {
  $$('.ic[data-ic]', root).forEach((svg) => {
    if (svg.dataset.done) return;
    svg.dataset.done = '1';
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('fill', 'none');
    svg.setAttribute('stroke', 'currentColor');
    svg.setAttribute('stroke-width', '1.7');
    svg.setAttribute('stroke-linecap', 'round');
    svg.setAttribute('stroke-linejoin', 'round');
    /* Size comes from CSS (.ic = 16px, .tab svg = 19px) or per-element
       inline styles set in the markup. Do NOT force 100% here — it would
       override those rules (inline beats class) and balloon every icon. */
    const use = document.createElementNS('http://www.w3.org/2000/svg', 'use');
    use.setAttribute('href', '#i-' + svg.dataset.ic);
    svg.appendChild(use);
  });
}

/* ---- theme : auto-follow OS, with override ----------------- */
const THEME_KEY = 'hb_theme';                       // 'auto' | 'day' | 'dusk'
const darkMQ = window.matchMedia('(prefers-color-scheme: dark)');
function effectiveMode(pref) { return pref === 'auto' ? (darkMQ.matches ? 'dusk' : 'day') : pref; }
function applyTheme() {
  let pref = localStorage.getItem(THEME_KEY) || 'auto';
  if (!['auto', 'day', 'dusk'].includes(pref)) pref = 'auto'; // unknown stored value

  const mode = effectiveMode(pref);
  document.documentElement.setAttribute('data-mode', mode);
  const meta = $('meta[name="theme-color"]');
  if (meta) meta.setAttribute('content', mode === 'dusk' ? '#1A1611' : '#F1EBDD');
  const btn = $('#dusk-btn');
  if (btn) btn.textContent = pref === 'auto' ? ('Auto · ' + (mode === 'dusk' ? 'dusk' : 'light')) : (pref === 'dusk' ? 'On' : 'Off');
}
darkMQ.addEventListener('change', () => {
  if ((localStorage.getItem(THEME_KEY) || 'auto') === 'auto') applyTheme();
});

/* ---- tabs -------------------------------------------------- */
$$('.tab').forEach((t) => t.addEventListener('click', () => {
  const target = t.dataset.tab;
  $$('.tab').forEach((x) => x.classList.toggle('active', x === t));
  $$('.view').forEach((v) => v.classList.toggle('active', v.id === target));
  $('.scroll').scrollTop = 0;
  if (target === 'logs') fetchLogs(); // pull the journal on first open
  if (target === 'dash') { drawCharts = true; renderDash(); } // redraw charts/sparks on re-entry
  revealView($('#' + target));
}));

/* ---- sparkline (SVG path) ---------------------------------- */
function spark(el, data, color, h, opts) {
  h = h || 30;
  if (!data || data.length < 2) { el.innerHTML = ''; el._spark = null; return; }
  const W = 100, H = h;
  let lo = Math.min(...data), hi = Math.max(...data);
  if (lo === hi) { lo -= 1; hi += 1; }
  const pad = (hi - lo) * 0.12; lo -= pad; hi += pad;
  const pts = data.map((v, i) => [(i / (data.length - 1)) * W, H - ((v - lo) / (hi - lo)) * H]);
  const line = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ' ' + p[1].toFixed(1)).join(' ');
  const area = line + ` L${W} ${H} L0 ${H} Z`;
  const last = pts[pts.length - 1];

  // Build the SVG once per host, then update path data in place so the
  // line slides instead of flashing on every poll, and GSAP has a stable target.
  let c = el._spark;
  const firstBuild = !c || c.h !== H;
  if (firstBuild) {
    const gid = 'g' + Math.random().toString(36).slice(2, 7);
    el.innerHTML =
      `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" style="width:100%;height:${H}px;overflow:visible">` +
      `<defs><linearGradient id="${gid}" x1="0" y1="0" x2="0" y2="1">` +
      `<stop offset="0%" stop-color="${color}" stop-opacity="0.22"/>` +
      `<stop offset="100%" stop-color="${color}" stop-opacity="0"/></linearGradient></defs>` +
      `<path class="sp-area" d="" fill="url(#${gid})"/>` +
      `<path class="sp-line" d="" fill="none" stroke="${color}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" vector-effect="non-scaling-stroke"/>` +
      `<circle class="sp-dot" r="2.4" fill="${color}" vector-effect="non-scaling-stroke"/></svg>`;
    c = el._spark = { h: H, area: el.querySelector('.sp-area'), line: el.querySelector('.sp-line'), dot: el.querySelector('.sp-dot') };
  }
  c.area.setAttribute('d', area);
  c.line.setAttribute('d', line);
  c.dot.setAttribute('cx', last[0].toFixed(1));
  c.dot.setAttribute('cy', last[1].toFixed(1));
  drawReveal(c.line, firstBuild || !!(opts && opts.drawOn));
}
function areaChart(el, data, color, h, band, opts) {
  h = h || 110;
  if (!data || data.length < 2) {
    el.innerHTML = `<div class="note" style="height:${h}px;display:grid;place-items:center;text-align:center">Collecting trend… keep this page open.</div>`;
    el._ac = null;
    return;
  }
  const W = 100, H = h;
  let series = band ? data.concat([band.lo, band.hi]) : data;
  let lo = Math.min(...series), hi = Math.max(...series);
  if (lo === hi) { lo -= 1; hi += 1; }
  const pad = (hi - lo) * 0.12; lo -= pad; hi += pad;
  const yOf = (v) => H - ((v - lo) / (hi - lo)) * H;
  const pts = data.map((v, i) => [(i / (data.length - 1)) * W, yOf(v)]);
  const line = pts.map((p, i) => (i ? 'L' : 'M') + p[0].toFixed(1) + ' ' + p[1].toFixed(1)).join(' ');
  const area = line + ` L${W} ${H} L0 ${H} Z`;
  const last = pts[pts.length - 1];
  let bandSvg = '';
  if (band) {
    const t = yOf(band.hi), bh = yOf(band.lo) - yOf(band.hi);
    bandSvg = `<rect x="0" y="${t.toFixed(1)}" width="${W}" height="${bh.toFixed(1)}" fill="var(--leaf)" opacity="0.10"/>` +
      `<line x1="0" y1="${t.toFixed(1)}" x2="${W}" y2="${t.toFixed(1)}" stroke="var(--leaf)" stroke-width="1" stroke-dasharray="3 3" vector-effect="non-scaling-stroke" opacity="0.5"/>` +
      `<line x1="0" y1="${(t + bh).toFixed(1)}" x2="${W}" y2="${(t + bh).toFixed(1)}" stroke="var(--leaf)" stroke-width="1" stroke-dasharray="3 3" vector-effect="non-scaling-stroke" opacity="0.5"/>`;
  }

  // Build once per host; update paths + band (band geometry shifts with config) in place.
  let c = el._ac;
  const firstBuild = !c || c.h !== H;
  if (firstBuild) {
    const gid = 'g' + Math.random().toString(36).slice(2, 7);
    const grid = [0.25, 0.5, 0.75].map((g) => `<line x1="0" y1="${(H * g).toFixed(1)}" x2="${W}" y2="${(H * g).toFixed(1)}" stroke="var(--line)" stroke-width="1" vector-effect="non-scaling-stroke"/>`).join('');
    el.innerHTML =
      `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" style="width:100%;height:${H}px;overflow:visible">` +
      `<defs><linearGradient id="${gid}" x1="0" y1="0" x2="0" y2="1">` +
      `<stop offset="0%" stop-color="${color}" stop-opacity="0.26"/>` +
      `<stop offset="100%" stop-color="${color}" stop-opacity="0.01"/></linearGradient></defs>` +
      grid +
      `<g class="ac-band"></g>` +
      `<path class="ac-area" d="" fill="url(#${gid})"/>` +
      `<path class="ac-line" d="" fill="none" stroke="${color}" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" vector-effect="non-scaling-stroke"/>` +
      `<circle class="ac-dot" r="3" fill="${color}" vector-effect="non-scaling-stroke"/></svg>`;
    c = el._ac = { h: H, band: el.querySelector('.ac-band'), area: el.querySelector('.ac-area'), line: el.querySelector('.ac-line'), dot: el.querySelector('.ac-dot') };
  }
  c.band.innerHTML = bandSvg;
  c.area.setAttribute('d', area);
  c.line.setAttribute('d', line);
  c.dot.setAttribute('cx', last[0].toFixed(1));
  c.dot.setAttribute('cy', last[1].toFixed(1));
  drawReveal(c.line, firstBuild || !!(opts && opts.drawOn));
}

/* ---- client-side history buffer ---------------------------- */
const HIST_MAX = 150;
const hist = { temp: [], hum: [], lux: [], soil: [[], [], [], [], []] };
function pushHist(d) {
  if (typeof d.tempC === 'number') hist.temp.push(d.tempC);
  if (typeof d.humPerc === 'number') hist.hum.push(d.humPerc);
  if (typeof d.lux === 'number') hist.lux.push(d.lux);
  if (Array.isArray(d.soilPerc)) d.soilPerc.forEach((v, i) => hist.soil[i] && hist.soil[i].push(v));
  ['temp', 'hum', 'lux'].forEach((k) => { while (hist[k].length > HIST_MAX) hist[k].shift(); });
  hist.soil.forEach((s) => { while (s.length > HIST_MAX) s.shift(); });
}

/* ---- shared state ------------------------------------------ */
const PLANT_KEY = 'hb_bedplants';
let config = { moisture: [40, 50, 60, 70, 80], luxTarget: 800, tempTarget: 24, tempHyst: 2, lightProfile: new Array(24).fill(50), nextBed: 0, soilDryValue: 3500, soilWetValue: 800 };
let plants = [];
// Versioned envelope: a structurally different value (key reuse, bed-count
// or shape change in a future version) must not flow into rendering.
const PLANT_SCHEMA_V = 1;
function loadBedPlants() {
  try {
    const raw = JSON.parse(localStorage.getItem(PLANT_KEY) || 'null');
    if (raw && raw.v === PLANT_SCHEMA_V && Array.isArray(raw.data) && raw.data.length === 5) return raw.data;
    if (Array.isArray(raw) && raw.length === 5) return raw; // legacy pre-envelope array
  } catch (e) {}
  return ['', '', '', '', ''];
}
function saveBedPlants(arr) {
  localStorage.setItem(PLANT_KEY, JSON.stringify({ v: PLANT_SCHEMA_V, data: arr }));
}
let bedPlants = loadBedPlants();
let maintenance = false, lastData = {}, lastDiag = {};
let lastPollOk = false, lastDiagAt = 0, pollDelay = 2000;
let firstPollDone = false;
let drawCharts = false; // set when the dash view is (re)activated; consumed once by renderDash
let prevWaterCrit = false, prevEmerg = false; // rising-edge tracking for the fault attention cue
const valveState = [false, false, false, false, false];
let pumpOn = false;
const swatchColor = (name) => {
  let h = 0; for (let i = 0; i < (name || '?').length; i++) h = (h * 31 + name.charCodeAt(i)) % 360;
  return `hsl(${h} 38% 42%)`;
};

/* ============================================================
   RENDER — live (every poll)
   ============================================================ */
function renderDash() {
  const d = lastData;
  const loading = !firstPollDone;

  // Living greenhouse hero sits above everything and updates in both auto and
  // maintenance modes; wrapped so a scene error can never break the dashboard.
  try { updateHero(); } catch (e) {}
  const skelSpan = (w, h) => `<span class="skel" style="display:inline-block;width:${w};height:${h};border-radius:3px;vertical-align:middle"></span>`;

  // Roof/pump/light/water status and the temp/humidity/light/soil readouts now
  // live in the hero, so the dash keeps only the soil-bed detail below. Soil
  // average is still needed for the beds-card header / sort.
  const soilAvg = Array.isArray(d.soilPerc) && d.soilPerc.length
    ? d.soilPerc.reduce((a, b) => a + b, 0) / d.soilPerc.length : null;

  // dashboard beds
  $('#soil-avg-note').textContent = loading ? '' : 'avg ' + soilAvg + '%';
  // Auto mode → irrigation map replaces the sorted soil list.
  const autoMode = !loading && !lastData.MAINTENANCE_MODE;
  const map = $('#irrig-map'), list = $('#dash-beds'), soilH = $('#soil-card-h');
  if (autoMode) {
    map.hidden = false; list.hidden = true;
    soilH.innerHTML = '<svg class="ic" data-ic="drop"></svg> Irrigation · live';
    initIcons(soilH);
    renderIrrigationMap();
    return; // skip list render below
  } else {
    if (typeof irrKillFlow === 'function' && IRR.built) irrKillFlow();
    map.hidden = true; list.hidden = false;
    soilH.innerHTML = '<svg class="ic" data-ic="leaf"></svg> Soil moisture · 5 beds';
    initIcons(soilH);
  }
  if (loading) {
    $('#dash-beds').innerHTML = Array(5).fill(0).map(() =>
      `<div class="bed">` +
      `<div class="skel" style="width:30px;height:30px;border-radius:8px;flex-shrink:0"></div>` +
      `<div class="b-main"><div class="b-top">` +
      `${skelSpan('72px', '13px')} ${skelSpan('28px', '13px')}` +
      `</div><div class="skel" style="height:7px;margin-top:8px;border-radius:100px"></div></div></div>`
    ).join('');
  } else {
    // Capture current row positions before the rewrite so Flip can slide rows
    // to their new slots when urgency re-sorts them (matched by data-flip-id).
    const flipAble = MOTION.on && window.Flip;
    const beforeState = flipAble && $('#dash-beds').children.length
      ? Flip.getState('#dash-beds .bed') : null;
    // sort beds by urgency: driest relative to target first ("Bed N" labels keep identity clear)
    $('#dash-beds').innerHTML = (d.soilPerc || [])
      .map((m, i) => ({ m, i }))
      .sort((a, b) => (a.m - config.moisture[a.i]) - (b.m - config.moisture[b.i]))
      .map(({ m, i }) => {
        const target = config.moisture[i];
        const low = m < target - 8;
        const name = bedPlants[i] || ('Bed ' + (i + 1));
        return bedRow(i, name, m, target, low);
      }).join('');
    if (beforeState) Flip.from(beforeState, { duration: 0.5, ease: 'power2.inOut', absolute: true });
  }
}
const IRR = { built: false, beds: 5, W: 320, H: 230 };

function irrLayout() {
  // x-centres for 5 beds across the width; pump centred at top.
  const n = IRR.beds, pad = 26, span = (IRR.W - pad * 2) / (n - 1);
  const bedX = Array.from({ length: n }, (_, i) => pad + i * span);
  return { bedX, pumpX: IRR.W / 2, pumpY: 26, bedTop: 120, bedH: 64, bedW: 40 };
}

function buildIrrSvg() {
  const L = irrLayout();
  IRR.L = L;
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('viewBox', `0 0 ${IRR.W} ${IRR.H}`);
  svg.setAttribute('class', 'irr-svg');
  svg.setAttribute('role', 'img');
  svg.setAttribute('aria-label', 'Irrigation map');

  // pipes pump -> bed (built first so beds render above them)
  L.bedX.forEach((x, i) => {
    const p = document.createElementNS(ns, 'path');
    const d = `M ${L.pumpX} ${L.pumpY + 16} L ${L.pumpX} 70 L ${x} 70 L ${x} ${L.bedTop}`;
    p.setAttribute('d', d);
    p.setAttribute('class', 'irr-pipe');
    p.setAttribute('id', `irr-pipe-${i}`);
    svg.appendChild(p);
  });

  // droplet (one reused circle per bed, hidden until pumping)
  L.bedX.forEach((x, i) => {
    const c = document.createElementNS(ns, 'circle');
    c.setAttribute('r', '3'); c.setAttribute('class', 'irr-drop');
    c.setAttribute('id', `irr-drop-${i}`); c.setAttribute('opacity', '0');
    svg.appendChild(c);
  });

  // pump/tank node
  const tank = document.createElementNS(ns, 'rect');
  tank.setAttribute('x', L.pumpX - 34); tank.setAttribute('y', 8);
  tank.setAttribute('width', 68); tank.setAttribute('height', 26);
  tank.setAttribute('rx', 6); tank.setAttribute('class', 'irr-tank');
  tank.setAttribute('id', 'irr-tank');
  svg.appendChild(tank);
  const tlabel = document.createElementNS(ns, 'text');
  tlabel.setAttribute('x', L.pumpX); tlabel.setAttribute('y', 25);
  tlabel.setAttribute('class', 'irr-tank-t'); tlabel.setAttribute('id', 'irr-tank-t');
  tlabel.textContent = 'TANK';
  svg.appendChild(tlabel);

  // beds: outline, soil fill (anchored at bottom), target line, label, %
  L.bedX.forEach((x, i) => {
    const bx = x - L.bedW / 2;
    const outline = document.createElementNS(ns, 'rect');
    outline.setAttribute('x', bx); outline.setAttribute('y', L.bedTop);
    outline.setAttribute('width', L.bedW); outline.setAttribute('height', L.bedH);
    outline.setAttribute('rx', 5); outline.setAttribute('class', 'irr-bed');
    outline.setAttribute('id', `irr-bed-${i}`);
    svg.appendChild(outline);

    const fill = document.createElementNS(ns, 'rect');
    fill.setAttribute('x', bx); fill.setAttribute('width', L.bedW);
    fill.setAttribute('rx', 5); fill.setAttribute('class', 'irr-fill');
    fill.setAttribute('id', `irr-fill-${i}`);
    svg.appendChild(fill);

    const tgt = document.createElementNS(ns, 'line');
    tgt.setAttribute('x1', bx); tgt.setAttribute('x2', bx + L.bedW);
    tgt.setAttribute('class', 'irr-target'); tgt.setAttribute('id', `irr-tgt-${i}`);
    svg.appendChild(tgt);

    const lbl = document.createElementNS(ns, 'text');
    lbl.setAttribute('x', x); lbl.setAttribute('y', L.bedTop + L.bedH + 14);
    lbl.setAttribute('class', 'irr-bed-l');
    lbl.textContent = 'B' + (i + 1);
    svg.appendChild(lbl);

    const pct = document.createElementNS(ns, 'text');
    pct.setAttribute('x', x); pct.setAttribute('y', L.bedTop + L.bedH + 27);
    pct.setAttribute('class', 'irr-bed-p'); pct.setAttribute('id', `irr-pct-${i}`);
    pct.textContent = '—';
    svg.appendChild(pct);
  });

  // status line (countdown / state)
  const status = document.createElementNS(ns, 'text');
  status.setAttribute('x', IRR.W / 2); status.setAttribute('y', IRR.H - 4);
  status.setAttribute('class', 'irr-status'); status.setAttribute('id', 'irr-status');
  svg.appendChild(status);

  const host = $('#irrig-map');
  host.innerHTML = '';
  host.appendChild(svg);

  // click-to-select: bind after svg is in the DOM so getElementById resolves
  for (let i = 0; i < IRR.beds; i++) {
    document.getElementById(`irr-bed-${i}`).addEventListener('click', (e) => {
      e.stopPropagation();
      const idx = +e.currentTarget.id.replace('irr-bed-', '');
      const prev = selectedBed;
      selectedBed = (selectedBed === idx) ? -1 : idx;
      if (prev >= 0 && prev < IRR.beds) document.getElementById(`irr-bed-${prev}`).classList.remove('selected');
      if (selectedBed >= 0) document.getElementById(`irr-bed-${selectedBed}`).classList.add('selected');
    });
  }
  svg.addEventListener('click', () => {
    if (selectedBed >= 0) document.getElementById(`irr-bed-${selectedBed}`).classList.remove('selected');
    selectedBed = -1;
  });

  IRR.built = true;
}

function setBedFill(i, perc) {
  const L = IRR.L;
  const h = Math.max(0, Math.min(100, perc)) / 100 * L.bedH;
  const fill = document.getElementById(`irr-fill-${i}`);
  fill.setAttribute('height', h);
  fill.setAttribute('y', L.bedTop + L.bedH - h);
  const tgtY = L.bedTop + L.bedH - (config.moisture[i] / 100 * L.bedH);
  const tgt = document.getElementById(`irr-tgt-${i}`);
  tgt.setAttribute('y1', tgtY); tgt.setAttribute('y2', tgtY);
  document.getElementById(`irr-pct-${i}`).textContent = Math.round(perc) + '%';
}

const IRRA = { state: '', bed: -1, flow: null, drop: null };
let selectedBed = -1;

function irrKillFlow() {
  if (IRRA.flow) { IRRA.flow.kill(); IRRA.flow = null; }
  if (IRRA.drop) { IRRA.drop.kill(); IRRA.drop = null; }
  for (let i = 0; i < IRR.beds; i++) {
    const d = document.getElementById(`irr-drop-${i}`);
    if (d) d.setAttribute('opacity', '0');
    const p = document.getElementById(`irr-pipe-${i}`);
    if (p) p.classList.remove('flowing');
    const b = document.getElementById(`irr-bed-${i}`);
    if (b) b.classList.remove('active', 'absorbing', 'next');
  }
}

function irrStartFlow(bed) {
  const pipe = document.getElementById(`irr-pipe-${bed}`);
  const drop = document.getElementById(`irr-drop-${bed}`);
  document.getElementById(`irr-bed-${bed}`).classList.add('active');
  if (!MOTION.on) { pipe.classList.add('flowing'); return; } // static highlight only
  pipe.classList.add('flowing');
  const len = pipe.getTotalLength();
  drop.setAttribute('opacity', '1');
  const o = { t: 0 };
  IRRA.drop = gsap.to(o, {
    t: 1, duration: 1.1, ease: 'none', repeat: -1,
    onUpdate() {
      const pt = pipe.getPointAtLength(o.t * len);
      drop.setAttribute('cx', pt.x); drop.setAttribute('cy', pt.y);
    }
  });
}

function fmtMMSS(ms) {
  const s = Math.max(0, Math.round(ms / 1000));
  return String((s / 60) | 0).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0');
}

function nextDriestBed() {
  // mirror firmware round-robin from config.nextBed; first bed below target.
  const n = IRR.beds, soil = lastData.soilPerc || [], start = config.nextBed || 0;
  for (let k = 0; k < n; k++) {
    const i = (start + k) % n;
    if (soil[i] != null && soil[i] < config.moisture[i]) return i;
  }
  return -1;
}

function renderIrrigationMap() {
  if (!IRR.built) buildIrrSvg();
  const soil = lastData.soilPerc || [];
  for (let i = 0; i < IRR.beds; i++) setBedFill(i, soil[i] ?? 0);

  const locked = !lastData.waterCritical || lastData.EMERGENCY_STOP;
  const ps = (lastData.pumpStatus || 'IDLE').toUpperCase();
  const bed = (typeof lastData.activeBed === 'number') ? lastData.activeBed : -1;
  const validBed = bed >= 0 && bed < IRR.beds;
  const status = document.getElementById('irr-status');
  const tank = document.getElementById('irr-tank');
  const tankT = document.getElementById('irr-tank-t');

  // derive a single display state (PAUSING removed — use per-bed bedDiffuseMs instead)
  const ds = locked ? 'LOCKED'
           : (ps === 'PUMPING' && validBed) ? 'PUMPING' : 'IDLE';

  if (ds !== IRRA.state || bed !== IRRA.bed) {
    irrKillFlow();
    tank.classList.toggle('locked', ds === 'LOCKED');
    if (ds === 'PUMPING') irrStartFlow(bed);
    else if (ds === 'IDLE') { const nb = nextDriestBed(); if (nb >= 0) document.getElementById(`irr-bed-${nb}`).classList.add('next'); }
    IRRA.state = ds; IRRA.bed = bed;
  }

  // passive soak glow: driven per-bed by bedDiffuseMs, independent of global state
  const diffuse = Array.isArray(lastData.bedDiffuseMs) ? lastData.bedDiffuseMs : [];
  for (let i = 0; i < IRR.beds; i++) {
    const b = document.getElementById(`irr-bed-${i}`);
    if (b) b.classList.toggle('absorbing', (diffuse[i] || 0) > 0);
  }

  tankT.textContent = ds === 'LOCKED' ? 'STOP' : 'TANK';

  // status line: reflects selected bed only
  if (ds === 'LOCKED') {
    status.textContent = 'Water critical — locked';
  } else if (selectedBed < 0) {
    status.textContent = 'Tap a bed';
  } else if (lastData.activeBed === selectedBed) {
    status.textContent = `Watering B${selectedBed + 1} · ${fmtMMSS(lastData.irrigRemainMs)}`;
  } else if ((diffuse[selectedBed] || 0) > 0) {
    status.textContent = `Diffusion B${selectedBed + 1} · ${fmtMMSS(diffuse[selectedBed])}`;
  } else {
    status.textContent = `B${selectedBed + 1} · idle`;
  }
}
function bedRow(i, name, m, target, low) {
  return `<div class="bed" data-flip-id="bed-${i}"><div class="swatch" style="background:${swatchColor(name)}">${(name[0] || 'B').toUpperCase()}</div>` +
    `<div class="b-main"><div class="b-top"><span><span class="b-plant">${esc(name)}</span> <span class="b-id">Bed ${i + 1}</span></span>` +
    `<span class="b-val ${low ? 'low' : ''}">${m}% <span class="muted">/ ${target}</span></span></div>` +
    `<div class="moist-track"><div class="moist-fill ${low ? 'low' : ''}" style="width:${m}%"></div>` +
    `<div class="moist-target" style="left:${target}%"></div></div></div></div>`;
}

/* ============================================================
   LIVING GREENHOUSE HERO  (ambient scene, top of Garden tab)
   Sky/sun by timeOfDay, sliding roof by roof state, 5 planter beds whose
   plants wilt by soil-vs-target, watering droplets, narrative status line.
   Driven entirely by existing /data.json + /diagnostics; no firmware change.
   ============================================================ */
const HERO = { built: false };
const G = (id) => document.getElementById(id);

function lerpHex(a, b, t) {
  const pa = parseInt(a.slice(1), 16), pb = parseInt(b.slice(1), 16);
  const ar = pa >> 16, ag = (pa >> 8) & 255, ab = pa & 255;
  const br = pb >> 16, bg = (pb >> 8) & 255, bb = pb & 255;
  const r = Math.round(ar + (br - ar) * t), g = Math.round(ag + (bg - ag) * t), bl = Math.round(ab + (bb - ab) * t);
  return '#' + (1 << 24 | r << 16 | g << 8 | bl).toString(16).slice(1);
}

/* timeOfDay (s) + lux → sky gradient + day/night. Falls back to lux when the
   controller clock is unset (timeOfDay 0), so the scene is never stuck at midnight. */
function skyColors(tod, lux) {
  let h = tod > 0 ? tod / 3600 : (lux > 3000 ? 13 : lux > 500 ? 9 : 21);
  h = ((h % 24) + 24) % 24;
  // softer, warmer stops [top, bottom] so the sky harmonizes with the cream cards
  const stops = [
    [0, '#1b2440', '#2a3354'], [5.5, '#46557f', '#caa07a'], [7, '#9fc0d8', '#dfe6e0'],
    [12, '#a9cfe0', '#e6e9dd'], [17, '#b6cad8', '#e7d8c0'], [19, '#5a6789', '#d99a6a'],
    [20.5, '#2b3450', '#46415f'], [24, '#1b2440', '#2a3354'],
  ];
  let a = stops[0], b = stops[stops.length - 1];
  for (let i = 0; i < stops.length - 1; i++) {
    if (h >= stops[i][0] && h <= stops[i + 1][0]) { a = stops[i]; b = stops[i + 1]; break; }
  }
  const t = (h - a[0]) / ((b[0] - a[0]) || 1);
  const isDay = h >= 6 && h < 20;
  const nightness = clamp(h < 6 ? 1 : h < 7 ? 7 - h : h < 19 ? 0 : h < 20 ? h - 19 : 1, 0, 1);
  return { top: lerpHex(a[1], b[1], t), bot: lerpHex(a[2], b[2], t), h, isDay, nightness };
}

/* Data-forward status hero: time-of-day atmosphere band (CSS gradient + an HTML
   round sun/moon so it never distorts across widths) over a status headline,
   mode pill, sub-status line, and a live metric strip. Driven by /data.json +
   /diagnostics — no firmware change. */
const HERO_SKY_W = 600;
const HERO_ICON = {
  temp: '<path d="M10 13.5V4a2 2 0 0 0-4 0v9.5a3.5 3.5 0 1 0 4 0Z"/>',
  hum: '<path d="M8 1.5S3 7 3 10.2A5 5 0 0 0 13 10.2C13 7 8 1.5 8 1.5Z"/>',
  lux: '<circle cx="8" cy="8" r="3.2"/><path d="M8 1v2M8 13v2M1 8h2M13 8h2M3 3l1.4 1.4M11.6 11.6 13 13M13 3l-1.4 1.4M4.4 11.6 3 13"/>',
  soil: '<path d="M8 14V7M8 7C8 5 6.5 3.5 4 3.5 4 6 5.5 7 8 7ZM8 7c0-2.2 1.6-3.8 4.2-3.8C12.2 5.6 10.6 7 8 7Z"/>',
};
function heroMetric(icon, label, key, unit, want) {
  return `<div class="hero-metric"><div class="mh"><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round">${HERO_ICON[icon]}</svg><span class="ml">${label}</span></div>` +
    `<div class="mv"><span data-k="${key}">—</span><span class="u">${unit}</span>` +
    (want ? `<span class="mv-want" data-w="${key}"></span>` : '') + `</div></div>`;
}
function buildHeroSvg() {
  const host = $('#garden-hero'); if (!host) return;
  const stars = [[60, 30], [140, 52], [230, 24], [330, 44], [420, 30], [500, 54], [210, 64], [470, 20]]
    .map(([x, y]) => `<circle cx="${x}" cy="${y}" r="1.2" fill="#fdfaf0"/>`).join('');
  host.innerHTML =
    `<div class="hero-sky">` +
    `<svg class="hero-sky-bg" viewBox="0 0 ${HERO_SKY_W} 104" preserveAspectRatio="none" aria-hidden="true">` +
    `<path id="hero-arc" d="M40 88 Q ${HERO_SKY_W / 2} -40 ${HERO_SKY_W - 40} 88" fill="none" stroke="#fff" stroke-opacity=".2" stroke-width="1.5" stroke-dasharray="3 6"/>` +
    `<g id="hero-stars" opacity="0">${stars}</g>` +
    `</svg>` +
    `<div class="hero-cel sun" id="hero-cel"><span class="glow"></span><span class="disc"></span></div>` +
    `<div class="hero-sky-meta">` +
    `<span class="hero-place"><svg width="13" height="13" viewBox="0 0 16 16" fill="none" stroke="#fff" stroke-width="1.6" stroke-linejoin="round"><path d="M8 1.6 14 5v6L8 14.4 2 11V5Z"/><path d="M2 5l6 3 6-3M8 8v6.4"/></svg>Hochbeet</span>` +
    `<span class="hero-clock" id="hero-clock">--:--</span>` +
    `</div>` +
    `</div>` +
    `<div class="hero-body">` +
    `<div class="hero-headrow">` +
    `<div class="hero-headline" id="hero-headline">—</div>` +
    `<span class="hero-pill" id="hero-pill"><span class="dot"></span><span class="ptxt" id="hero-ptxt">Automatic</span></span>` +
    `</div>` +
    `<div class="hero-sub" id="hero-sub">—</div>` +
    `<div class="hero-metrics">` +
    heroMetric('temp', 'Temp', 'tC', '°C', true) + heroMetric('hum', 'Humidity', 'hum', '%') +
    heroMetric('lux', 'Light', 'lx', 'lx', true) + heroMetric('soil', 'Soil', 'soil', '%') +
    `</div>` +
    `</div>`;
  HERO.built = true;
  host.hidden = false;
}

/* Status headline: one human sentence, priority-ordered. */
function buildNarrative(d) {
  if (d.EMERGENCY_STOP) return 'Emergency stop — everything parked.';
  if (d.waterCritical === false) return 'Reservoir empty — pump locked.';
  // Maintenance is shown by the mode pill — keep the headline on real garden
  // status (sensors still read while regulation is paused) to avoid repeating it.
  const rs = (lastDiag.roofState || '').toUpperCase();
  const ps = (d.pumpStatus || '').toUpperCase();
  const ab = typeof d.activeBed === 'number' ? d.activeBed : -1;
  if (ps === 'PUMPING' && ab >= 0) {
    const nm = bedPlants[ab] || ('Bed ' + (ab + 1));
    return `${nm} watering · ${Math.max(0, Math.round((d.irrigRemainMs || 0) / 1000))}s left`;
  }
  if (rs === 'OPENING') return 'Roof opening to cool down';
  if (rs === 'CLOSING') return 'Roof closing';
  const soil = d.soilPerc || [];
  let thirsty = 0;
  for (let i = 0; i < 5; i++) if (typeof soil[i] === 'number' && soil[i] < (config.moisture[i] || 50) - 8) thirsty++;
  if (d.waterLow === false) return 'Low water — top up soon';
  if (thirsty === 1) {
    for (let i = 0; i < 5; i++) if (typeof soil[i] === 'number' && soil[i] < (config.moisture[i] || 50) - 8)
      return `${bedPlants[i] || ('Bed ' + (i + 1))} getting thirsty`;
  }
  if (thirsty > 1) return `${thirsty} beds getting thirsty`;
  return 'All beds healthy';
}

/* Mode pill state (text + severity class) from fault/maintenance flags. */
function heroMode(d) {
  if (d.EMERGENCY_STOP) return { txt: 'Emergency', cls: 'err' };
  if (d.waterCritical === false) return { txt: 'Pump locked', cls: 'err' };
  if (d.MAINTENANCE_MODE) return { txt: 'Maintenance', cls: 'warn' };
  return { txt: 'Automatic', cls: '' };
}

/* Sub-status line: roof · pump · light · water, plain language. */
function heroSub(d) {
  const rs = (lastDiag.roofState || 'IDLE').toUpperCase();
  const roofTxt = rs === 'OPEN' ? 'Roof open' : rs === 'OPENING' ? 'Roof opening'
    : rs === 'CLOSING' ? 'Roof closing' : 'Roof closed';
  const pumpTxt = (d.pumpStatus || '').toUpperCase() === 'PUMPING' ? 'pump running' : 'pump idle';
  const led = lastDiag.ledPWM || 0;
  const lightTxt = led > 0 ? `light ${Math.round((led / 255) * 100)}%` : 'light off';
  const waterTxt = d.waterCritical === false ? 'water critical' : d.waterLow === false ? 'water low' : 'water OK';
  return `${roofTxt} · ${pumpTxt} · ${lightTxt} · ${waterTxt}`;
}

function setHeroText(id, txt) {
  const el = G(id); if (!el || el.textContent === txt) return;
  if (MOTION.on) gsap.fromTo(el, { opacity: 0, y: 3 }, { opacity: 1, y: 0, duration: 0.35, overwrite: true, onStart() { el.textContent = txt; } });
  else el.textContent = txt;
}

function updateHero() {
  if (!firstPollDone) return;
  if (!HERO.built) buildHeroSvg();
  const d = lastData;
  const sc = skyColors(d.timeOfDay || 0, d.lux || 0);

  // atmosphere band: CSS gradient + arc/stars by time of day
  $('#garden-hero .hero-sky').style.background = `linear-gradient(180deg, ${sc.top}, ${sc.bot})`;
  G('hero-stars').setAttribute('opacity', sc.nightness.toFixed(2));
  G('hero-arc').setAttribute('stroke-opacity', (0.08 + (1 - sc.nightness) * 0.16).toFixed(2));

  // sun by day / moon by night, riding the arc (HTML element → always round)
  const f = sc.isDay ? clamp((sc.h - 6) / 14, 0, 1)
    : clamp(((sc.h < 6 ? sc.h + 24 : sc.h) - 20) / 10, 0, 1);
  const cx = 40 + f * (HERO_SKY_W - 80), cy = 88 - Math.sin(f * Math.PI) * 66;
  const cel = G('hero-cel');
  cel.className = 'hero-cel ' + (sc.isDay ? 'sun' : 'moon');
  cel.style.left = (cx / HERO_SKY_W * 100).toFixed(2) + '%';
  cel.style.top = cy.toFixed(1) + 'px';

  // clock from controller time-of-day (— when the clock is unset)
  const tod = d.timeOfDay || 0;
  G('hero-clock').textContent = tod > 0
    ? `${String(Math.floor(tod / 3600) % 24).padStart(2, '0')}:${String(Math.floor(tod / 60) % 60).padStart(2, '0')}`
    : '--:--';

  // headline + mode pill + sub-status
  setHeroText('hero-headline', buildNarrative(d));
  const mode = heroMode(d);
  G('hero-pill').className = 'hero-pill' + (mode.cls ? ' ' + mode.cls : '');
  setHeroText('hero-ptxt', mode.txt);
  setHeroText('hero-sub', heroSub(d));

  // live metric strip
  const soil = d.soilPerc || [];
  let ss = 0, sn = 0;
  for (let i = 0; i < 5; i++) if (typeof soil[i] === 'number') { ss += soil[i]; sn++; }
  $('#garden-hero [data-k="tC"]').textContent = fmt(d.tempC, 1);
  $('#garden-hero [data-k="hum"]').textContent = fmt(d.humPerc, 0);
  $('#garden-hero [data-k="lx"]').textContent = fmt(d.lux, 0);
  $('#garden-hero [data-k="soil"]').textContent = sn ? fmt(Math.round(ss / sn), 0) : '—';
  // target ("should") values next to current, pulled from saved config
  const tw = $('#garden-hero [data-w="tC"]');
  if (tw) tw.textContent = config.tempTarget != null ? `target ${fmt(config.tempTarget, 1)}°` : '';
  const lw = $('#garden-hero [data-w="lx"]');
  if (lw) {
    // current light setpoint = configured max scaled by this hour's profile %
    // (mirrors lightManagement.cpp: targetLux = luxTarget * profilePercent/100)
    const hour = Math.floor(tod / 3600) % 24;
    const prof = (config.lightProfile && config.lightProfile[hour] != null) ? config.lightProfile[hour] : 100;
    lw.textContent = config.luxTarget != null
      ? `target ${fmt(Math.round(config.luxTarget * prof / 100), 0)}`
      : '';
  }
}

function renderDiag() {
  const g = lastDiag, d = lastData;
  // Staleness cue: /diagnostics can fail independently of the main poll —
  // don't keep rendering an old payload as if it were fresh.
  const staleEl = $('#diag-stale');
  if (staleEl) {
    const age = Date.now() - lastDiagAt;
    const stale = lastDiagAt > 0 && age > 10000;
    staleEl.hidden = !stale;
    if (stale) staleEl.textContent = 'Sensor data ' + Math.round(age / 1000) + ' s old';
  }
  renderSensorFaults(d, g);
  $('#d-temp').textContent = fmt(g.temperature != null ? g.temperature : d.tempC, 1);
  $('#d-hum').textContent = fmt(g.humidity != null ? g.humidity : d.humPerc, 0);
  spark($('#spark-d-temp'), hist.temp, 'var(--terra)', 30);
  spark($('#spark-d-hum'), hist.hum, 'var(--sky)', 30);

  const roof = (g.roofState || 'IDLE').toUpperCase();
  $('#roof-label').textContent = roof;
  $('#roof-state-inline').textContent = roof;
  // /diagnostics sends roofContact as the string "Closed"/"Open" (not a bool) —
  // show it directly so it tracks the reed instead of sticking on OPEN.
  $('#roof-contact').textContent = g.roofContact ? String(g.roofContact).toUpperCase() : '—';
  $('#roof-thresholds').textContent = `${(config.tempTarget + config.tempHyst).toFixed(1)} / ${(config.tempTarget - config.tempHyst).toFixed(1)} °C`;

  // water
  const wState = !d.waterCritical ? 'Critical' : !d.waterLow ? 'Low' : 'Normal';
  const ws = $('#water-state'); ws.textContent = wState;
  ws.style.color = !d.waterCritical ? 'var(--rose)' : !d.waterLow ? 'var(--sun)' : 'var(--leaf)';
  $('#tank-fill').style.height = (!d.waterCritical ? 7 : !d.waterLow ? 21 : 78) + '%';
  setChip($('#chip-low'), !d.waterLow ? 'warn' : 'on', 'Low float ' + (!d.waterLow ? 'tripped' : 'clear'));
  setChip($('#chip-crit'), !d.waterCritical ? 'err' : 'on', 'Critical ' + (!d.waterCritical ? 'tripped' : 'clear'));

  // raw soil
  const raw = g.soilRaw || [];
  const soilBad = g.ERR_SOIL || [];
  const head = $('#soil-raw').querySelector('.note');
  $('#soil-raw').innerHTML = (raw.length ? raw : (d.soilPerc || []).map(() => '—')).map((rv, i) =>
    `<div class="kv${soilBad[i] ? ' kv-fault' : ''}"><span class="k">Bed ${i + 1}${bedPlants[i] ? ' · ' + esc(bedPlants[i]) : ''}` +
    (soilBad[i] ? '<span class="sec-flag">Fault</span>' : '') + `</span>` +
    `<span class="v">${esc(rv)} <span class="muted">raw → ${(d.soilPerc || [])[i] != null ? esc(d.soilPerc[i]) + '%' : '—'}</span></span></div>`).join('') +
    (head ? head.outerHTML : '');

  // light controller
  $('#lc-measured').textContent = fmt(g.light != null ? g.light : d.lux, 0) + ' lx';
  spark($('#spark-d-lux'), hist.lux, 'var(--sun)', 30);
  const pwm = g.ledPWM || 0;
  $('#lc-pwm').textContent = `${pwm} / 255 (${Math.round((pwm / 255) * 100)}%)`;
  $('#lc-bar').style.width = (pwm / 255 * 100) + '%';
}

/* Per-sensor fault surfacing on the Sensors tab: banner + inline section
   badges. Sources: data.json bools (BME/BH) + /diagnostics ERR_SOIL[] array. */
function renderSensorFaults(d, g) {
  const bme = d.ERR_SENSOR_BME === true, bh = d.ERR_SENSOR_BH === true;
  const soilBad = g.ERR_SOIL || [];
  const faults = [];
  if (bme) faults.push('Climate sensor (BME280) not responding — holding last-good reading');
  if (bh) faults.push('Light sensor (BH1750) offline — light runs feed-forward only');
  soilBad.forEach((bad, i) => { if (bad) faults.push('Soil sensor ' + (i + 1) + ' reading implausible (outside 50–4000)'); });
  toggleHidden('#flag-bme', !bme);
  toggleHidden('#flag-bh', !bh);
  const banner = $('#diag-faults');
  if (banner) {
    banner.hidden = !faults.length;
    banner.innerHTML = faults.map((t) => `<div class="fault-row"><span class="fault-dot"></span>${esc(t)}</div>`).join('');
  }
}
function toggleHidden(sel, hide) { const el = $(sel); if (el) el.hidden = hide; }

function renderSystem(sys) {
  if (sys) {
    $('#sys-uptime').textContent = sys.uptime || '—';
    $('#sys-link').textContent = (sys.wifi || 'Connected') + ' · firmware web UI';
    $('#sys-wifi').textContent = sys.wifi || '—';
  }
  const heap = lastDiag.freeHeap;
  if (typeof heap === 'number') {
    $('#sys-heap').textContent = (heap / 1024).toFixed(1) + ' KB';
    $('#heap-bar').style.width = clamp(heap / 327680 * 100, 0, 100) + '%';
  }
  $('#sys-comm').textContent = lastDiag.commStatus || '—';
  $('#sys-mode').textContent = maintenance ? 'Maintenance' : 'Automatic';
  $('#sys-water').textContent = !lastData.waterCritical ? 'Critical' : !lastData.waterLow ? 'Low' : 'Normal';
  const ef = (lastDiag.errorFlags || '').trim();
  $('#sys-fault').textContent = (!ef || /^none$/i.test(ef)) ? 'None' : humanizeFlags(ef);
  $('#sys-time-status').textContent = new Date().toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit' });
}

/* ---- helpers ---------------------------------------------- */
/* firmware flag → plain language (matches errorFlags strings in webBackend.cpp) */
const FLAG_TEXT = {
  EMERGENCY_STOP: 'Emergency stop active',
  WATER_CRITICAL: 'Reservoir empty — pump locked',
  ROOF_TIMEOUT: 'Roof did not close in time — check for obstruction',
  FS_MOUNT: 'Storage error — config may not persist',
  NO_BME: 'Climate sensor not responding',
  NO_BH: 'Light sensor offline — light runs without feedback',
};
function humanizeFlags(ef) {
  return ef.split(',').map((f) => {
    f = f.trim();
    const soil = f.match(/^SOIL(\d)$/);
    if (soil) return 'Soil sensor ' + soil[1] + ' reading implausible';
    return FLAG_TEXT[f] || f;
  }).join(' · ');
}
function fmt(v, dec) { return (typeof v === 'number' && !isNaN(v)) ? v.toFixed(dec) : '—'; }
function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
function setChip(el, cls, txt) { el.className = 'chip ' + cls; el.innerHTML = '<span class="dot"></span>' + txt; }

/* ============================================================
   FETCH LOOP
   ============================================================ */
function setOnline(ok) {
  const w = $('#led-wifi');
  w.className = 'led ' + (ok ? 'on' : 'err');
  w.setAttribute('aria-label', 'Link: ' + (ok ? 'connected' : 'offline'));
  if (!ok && firstPollDone) {
    $('#hdr-mode').textContent = 'OFFLINE';
    $('#hdr-mode').className = 'mode-badge offline';
  }
}
/* fetch with 5 s timeout — a stalled ESP32 must not block the poll loop */
function fetchT(url, opts) {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), 5000);
  return fetch(url, Object.assign({}, opts, { signal: ctrl.signal })).finally(() => clearTimeout(t));
}
let polling = false;
async function poll() {
  if (polling) return; // previous round still in flight
  polling = true;
  try { await pollOnce(); } finally { polling = false; }
}
async function pollOnce() {
  try {
    const d = await fetchT('/data.json').then((r) => r.json());
    lastData = d;
    maintenance = !!d.MAINTENANCE_MODE;
    if (!firstPollDone) {
      firstPollDone = true;
      document.documentElement.removeAttribute('data-loading');
    }
    pushHist(d);
    $('#hdr-mode').textContent = maintenance ? 'MAINT' : 'AUTO';
    $('#hdr-mode').className = 'mode-badge' + (maintenance ? ' maint' : '');
    const lw = $('#led-water');
    lw.className = 'led ' + (!d.waterCritical ? 'err' : !d.waterLow ? 'warn' : 'on');
    lw.setAttribute('aria-label', 'Water: ' + (!d.waterCritical ? 'critical' : !d.waterLow ? 'low' : 'OK'));
    // one-time calm cue on the rising edge of a critical condition (steady-state is the CSS pulse)
    const waterCrit = !d.waterCritical;
    if (waterCrit && !prevWaterCrit) faultCue(lw);
    prevWaterCrit = waterCrit;
    const emerg = !!d.EMERGENCY_STOP;
    if (emerg && !prevEmerg) faultCue($('#led-fault'));
    prevEmerg = emerg;
    updateMaintUI();
    setOnline(true);
    lastPollOk = true;
  } catch (e) { setOnline(false); lastPollOk = false; }

  try { lastDiag = await fetchT('/diagnostics').then((r) => r.json()); lastDiagAt = Date.now(); } catch (e) {}
  try { renderSystem(await fetchT('/systemStatus').then((r) => r.json())); } catch (e) { renderSystem(null); }

  const ef = (lastDiag.errorFlags || '').trim();
  const hasFault = ef && !/^none$/i.test(ef);
  const lf = $('#led-fault');
  lf.className = 'led' + (hasFault ? ' err' : '');
  lf.setAttribute('aria-label', hasFault ? 'Faults: ' + humanizeFlags(ef) : 'Faults: none');

  renderDash(); renderDiag(); renderLiveBeds();
  // keep the journal live while the Logs tab is open
  const logsView = $('#logs');
  if (logsView && logsView.classList.contains('active')) fetchLogs();
}

/* live moisture in planting-plan (without rebuilding controls) */
function renderLiveBeds() {
  (lastData.soilPerc || []).forEach((m, i) => {
    const fill = $('#pb-fill-' + i), val = $('#pb-val-' + i);
    if (!fill) return;
    const target = config.moisture[i], low = m < target - 8;
    fill.style.width = m + '%'; fill.className = 'moist-fill' + (low ? ' low' : '');
    if (val) { val.textContent = m; val.parentElement.style.color = low ? 'var(--terra)' : 'var(--ink)'; }
  });
}

/* ============================================================
   EVENT JOURNAL  (/logs.json)
   ============================================================ */
/* seconds-since-midnight → HH:MM:SS (controller clock; no date on device) */
function logClock(t) {
  const s = ((+t % 86400) + 86400) % 86400;
  const p = (n) => String(n).padStart(2, '0');
  return p(Math.floor(s / 3600)) + ':' + p(Math.floor(s / 60) % 60) + ':' + p(s % 60);
}
const LOG_CLS = { ERROR: 'err', WARN: 'warn' }; // INFO/DEBUG fall through to 'on'
const LOG_VIEW_MAX = 50;
let logCache = [];      // newest-first snapshot of the last fetch
let logFilter = 'ALL';  // ALL | WARN | ERROR
async function fetchLogs() {
  const host = $('#log-list');
  if (!host) return;
  try {
    const arr = await fetchT('/logs.json').then((r) => r.json());
    logCache = Array.isArray(arr) ? arr.slice().reverse() : []; // newest first
    renderLogs();
  } catch (err) {
    host.innerHTML = '<div class="note">Could not load logs — check connection.</div>';
    const c = $('#logs-count'); if (c) c.textContent = '';
  }
}
/* Render from logCache applying the active level filter. WARN keeps warnings
   and errors (severity floor); ERROR is errors only. */
function renderLogs() {
  const host = $('#log-list');
  if (!host) return;
  const pass = (lvl) => logFilter === 'ALL'
    || (logFilter === 'ERROR' && lvl === 'ERROR')
    || (logFilter === 'WARN' && (lvl === 'WARN' || lvl === 'ERROR'));
  const rows = logCache.filter((e) => pass(String(e.lvl || 'INFO').toUpperCase())).slice(0, LOG_VIEW_MAX);
  const count = $('#logs-count');
  if (count) count.textContent = logCache.length
    ? `${rows.length} shown · ${logCache.length} total`
    : '';
  if (!logCache.length) { host.innerHTML = '<div class="note">No events logged yet.</div>'; return; }
  if (!rows.length) { host.innerHTML = '<div class="note">No matching events.</div>'; return; }
  host.innerHTML = rows.map((e) => {
    const lvl = String(e.lvl || 'INFO').toUpperCase();
    const cls = LOG_CLS[lvl] || 'on';
    return `<div class="log-row ${cls}"><div class="log-meta">` +
      `<span class="log-time">${logClock(e.t || 0)}</span>` +
      `<span class="log-lvl ${cls}">${esc(lvl)}</span>` +
      `<span class="log-tag">${esc(e.tag || '')}</span></div>` +
      `<div class="log-msg">${esc(e.msg || '')}</div></div>`;
  }).join('');
}

/* ============================================================
   CONFIG : load / build / save
   ============================================================ */
async function loadConfig() {
  try {
    const c = await fetch('/loadConfig').then((r) => r.json());
    // Nullish, not falsy: 0 is a valid stored value (luxTarget 0 = lights
    // off). `||` would silently turn it into the default and the next save
    // would write the default back to the firmware.
    config.moisture = Array.isArray(c.moisture) && c.moisture.length === 5
      ? c.moisture.map((x) => x ?? 50)
      : [c.moisture1, c.moisture2, c.moisture3, c.moisture4, c.moisture5].map((x) => x ?? 50);
    config.luxTarget = c.luxTarget ?? c.lux ?? 800;
    if (c.tempTarget != null) config.tempTarget = c.tempTarget;
    // firmware serialises the hysteresis band as `tempHysteresis`; accept the legacy `tempHyst` too
    if (c.tempHysteresis != null) config.tempHyst = c.tempHysteresis;
    else if (c.tempHyst != null) config.tempHyst = c.tempHyst;
    if (Array.isArray(c.lightProfile) && c.lightProfile.length === 24) config.lightProfile = c.lightProfile.slice();
    if (c.nextBed != null) config.nextBed = c.nextBed;
    if (c.soilDryValue != null) config.soilDryValue = c.soilDryValue;
    if (c.soilWetValue != null) config.soilWetValue = c.soilWetValue;
  } catch (e) { /* keep defaults offline */ }
  buildBeds(); buildLightProfile(); buildSteppers(); buildSoilCalib();
}

function buildSteppers() {
  stepper($('#temp-stepper'), config.tempTarget, 12, 36, 0.5, '°C', 'var(--terra)', (v) => { config.tempTarget = v; $('#temp-thresholds').innerHTML = thresholdTxt(); markDirty(); });
  stepper($('#lux-stepper'), config.luxTarget, 50, 2000, 10, 'lx', 'var(--sun)', (v) => { config.luxTarget = v; markDirty(); });
  $('#temp-thresholds').innerHTML = thresholdTxt();
}
function thresholdTxt() {
  return `Roof opens above <strong>${(config.tempTarget + config.tempHyst).toFixed(1)}°C</strong>, closes below <strong>${(config.tempTarget - config.tempHyst).toFixed(1)}°C</strong>.`;
}
function stepper(host, value, min, max, step, unit, color, onChange) {
  host.innerHTML = `<button class="btn sm" data-d="-1">−</button>` +
    `<div class="readout" style="color:${color}"><span class="sv">${value}</span><span class="u">${unit}</span></div>` +
    `<button class="btn sm" data-d="1">+</button>`;
  let val = value;
  const sv = host.querySelector('.sv');
  const upd = () => {
    sv.textContent = val;
    host.querySelector('[data-d="-1"]').disabled = val <= min;
    host.querySelector('[data-d="1"]').disabled = val >= max;
  };
  host.querySelectorAll('button').forEach((b) => b.addEventListener('click', () => {
    val = clamp(+(val + step * (+b.dataset.d)).toFixed(2), min, max);
    upd(); onChange(val);
  }));
  upd();
}

function buildSoilCalib() {
  const dryEl = $('#soil-dry-input');
  const wetEl = $('#soil-wet-input');
  if (!dryEl || !wetEl) return;
  dryEl.value = config.soilDryValue;
  wetEl.value = config.soilWetValue;
  const onChange = () => {
    const dry = clamp(parseInt(dryEl.value, 10) || 0, 0, 4095);
    const wet = clamp(parseInt(wetEl.value, 10) || 0, 0, 4095);
    config.soilDryValue = dry;
    config.soilWetValue = wet;
    markDirty();
  };
  dryEl.addEventListener('change', onChange);
  wetEl.addEventListener('change', onChange);
}

function buildBeds() {
  $('#beds-list').innerHTML = config.moisture.map((target, i) => {
    const name = bedPlants[i] || '';
    const opts = '<option value="">— pick plant —</option>' + plants.map((p) =>
      `<option value="${esc(p.name)}" ${p.name === name ? 'selected' : ''}>${esc(p.name)}</option>`).join('');
    return `<div class="card" style="margin-bottom:var(--gap)">
      <div class="row" style="gap:11px; margin-bottom:11px">
        <div class="swatch" style="width:38px;height:38px;border-radius:10px;font-size:17px;background:${swatchColor(name || 'Bed')}">${(name[0] || (i + 1)).toString().toUpperCase()}</div>
        <div style="flex:1"><div class="b-id">Bed ${i + 1}</div>
          <div style="font-family:var(--serif);font-size:19px;font-weight:500;line-height:1.1">${esc(name || 'Unassigned')}</div></div>
        <div style="text-align:right"><div class="readout readout-lg"><span id="pb-val-${i}">—</span><span class="u">%</span></div><div class="note">now</div></div>
      </div>
      <div class="moist-track" style="margin-bottom:12px"><div class="moist-fill" id="pb-fill-${i}" style="width:0%"></div>
        <div class="moist-target" id="pb-tgt-${i}" style="left:${target}%"></div></div>
      <div class="row" style="gap:10px; align-items:flex-end">
        <div style="flex:1.3"><label class="field-label">Plant</label><select class="sel" data-bed="${i}">${opts}</select></div>
        <div style="flex:1"><label class="field-label">Target <span id="tlbl-${i}">${target}</span>%</label>
          <input class="rng" type="range" min="20" max="95" value="${target}" data-bedtarget="${i}"></div>
      </div></div>`;
  }).join('');

  $$('#beds-list select[data-bed]').forEach((sel) => sel.addEventListener('change', () => {
    const i = +sel.dataset.bed; bedPlants[i] = sel.value;
    saveBedPlants(bedPlants);
    const p = plants.find((x) => x.name === sel.value);
    if (p && p.targetMoisture != null) {
      config.moisture[i] = clamp(p.targetMoisture, 0, 100);
      const rng = $(`[data-bedtarget="${i}"]`); rng.value = config.moisture[i];
      $('#tlbl-' + i).textContent = config.moisture[i];
      $('#pb-tgt-' + i).style.left = config.moisture[i] + '%';
    }
    markDirty();
    buildBeds(); renderLiveBeds();
  }));
  $$('#beds-list [data-bedtarget]').forEach((rng) => rng.addEventListener('input', () => {
    const i = +rng.dataset.bedtarget; config.moisture[i] = +rng.value;
    $('#tlbl-' + i).textContent = rng.value; $('#pb-tgt-' + i).style.left = rng.value + '%';
    markDirty();
    renderLiveBeds();
  }));
  renderLiveBeds();
}

function buildLightProfile() {
  const nowH = new Date().getHours();
  const rows = [[0, 12], [12, 24]];
  $('#lp-grid').innerHTML = rows.map(([a, b]) =>
    `<div class="lp-grid">` + config.lightProfile.slice(a, b).map((v, k) => {
      const hour = a + k;
      return `<div class="lp-col ${hour === nowH ? 'now' : ''}"><div class="lp-bar-track" data-hour="${hour}">` +
        `<div class="lp-bar" style="height:${v}%"></div></div><span class="lp-h">${String(hour).padStart(2, '0')}</span></div>`;
    }).join('') + `</div>`).join('<div style="height:8px"></div>');

  $$('#lp-grid .lp-bar-track').forEach((track) => {
    const hour = +track.dataset.hour, bar = track.querySelector('.lp-bar');
    const set = (clientY) => {
      const r = track.getBoundingClientRect();
      const v = clamp(Math.round((1 - (clientY - r.top) / r.height) * 100), 0, 100);
      config.lightProfile[hour] = v; bar.style.height = v + '%'; markDirty();
    };
    track.addEventListener('pointerdown', (e) => {
      e.preventDefault(); track.setPointerCapture(e.pointerId); set(e.clientY);
      const mv = (ev) => set(ev.clientY);
      track.addEventListener('pointermove', mv);
      const up = () => { track.removeEventListener('pointermove', mv); track.removeEventListener('pointerup', up); };
      track.addEventListener('pointerup', up);
    });
  });
}

/* unsaved-changes tracking for the deferred config (temp, lux, bed targets, light profile).
   Plant add/delete is immediate API and intentionally excluded. */
let configDirty = false;
function markDirty() {
  if (configDirty) return;
  configDirty = true;
  const bar = $('#save-bar'), st = $('#save-status');
  if (bar) bar.classList.add('dirty');
  if (st) st.textContent = 'Unsaved changes';
}
function clearDirty() {
  configDirty = false;
  const bar = $('#save-bar'), st = $('#save-status');
  if (bar) bar.classList.remove('dirty');
  if (st) st.textContent = 'All changes saved';
}

let savingConfig = false;
async function saveConfig() {
  if (savingConfig) return;
  savingConfig = true;
  const btn = $('#save-config-btn'); btn.disabled = true;
  const body = {
    moisture: config.moisture.map((v) => Math.round(v)),
    luxTarget: Math.round(config.luxTarget),
    tempTarget: config.tempTarget,
    tempHysteresis: config.tempHyst, // round-trip the band the UI displays
    lightProfile: config.lightProfile.map((v) => clamp(Math.round(v), 0, 100)),
    soilDryValue: config.soilDryValue,
    soilWetValue: config.soilWetValue,
  };
  try {
    const r = await fetchT('/saveConfig', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    if (!r.ok) throw new Error(r.status);
    clearDirty();
    toast('Configuration saved');
  } catch (e) { toast('Save failed — check connection', true); }
  finally { savingConfig = false; btn.disabled = false; }
}

/* ============================================================
   PLANTS
   ============================================================ */
async function loadPlants() {
  try { plants = await fetch('/getPlants').then((r) => r.json()); }
  catch (e) { plants = []; }
  renderPlantLib(); buildBeds();
}
function renderPlantLib() {
  const inUse = (n) => bedPlants.includes(n);
  $('#plant-list').innerHTML = plants.length ? plants.map((p) => {
    const used = inUse(p.name);
    return `<div class="bed"><div class="swatch" style="width:26px;height:26px;border-radius:7px;font-size:12px;background:${swatchColor(p.name)}">${(p.name[0] || '?').toUpperCase()}</div>
      <div class="b-main"><div class="b-top"><span class="b-plant" style="font-size:14px">${esc(p.name)}</span>
        <span class="b-val">${p.targetMoisture}% <span class="muted">target</span></span></div>
        ${used ? '<span class="state-pill" style="margin-top:4px;display:inline-block">in use</span>' : ''}</div>
      <button class="btn sm" style="width:40px;padding:0;min-height:38px;${used ? '' : 'border-color:var(--rose);color:var(--rose)'}" data-del="${esc(p.name)}" ${used ? 'disabled' : ''}>
        <svg class="ic" data-ic="trash"></svg></button></div>`;
  }).join('') : '<div class="note">No plants yet — add one below.</div>';
  initIcons($('#plant-list'));
  // inline two-tap confirm (on-brand, no native dialog): first tap arms, second deletes; auto-reverts after 3 s
  $$('#plant-list [data-del]').forEach((b) => {
    let timer;
    const reset = () => { b.classList.remove('arm'); b.style.width = '40px'; b.innerHTML = '<svg class="ic" data-ic="trash"></svg>'; initIcons(b); };
    b.addEventListener('click', () => {
      if (b.classList.contains('arm')) { clearTimeout(timer); deletePlant(b.dataset.del); return; }
      b.classList.add('arm'); b.style.width = 'auto'; b.textContent = 'Delete?';
      timer = setTimeout(reset, 3000);
    });
  });
}
let addingPlant = false;
async function addPlant() {
  if (addingPlant) return;
  const name = $('#new-plant-name').value.trim();
  const target = +$('#new-plant-target').value;
  if (!name) return toast('Enter a plant name', true);
  if (plants.find((p) => p.name.toLowerCase() === name.toLowerCase())) return toast('Plant already exists', true);
  addingPlant = true;
  const btn = $('#add-plant-btn'); btn.disabled = true;
  try {
    const r = await fetch('/addPlant', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name, targetMoisture: target }) });
    if (r.status === 409) throw new Error('exists');
    if (!r.ok) throw new Error(r.status);
    $('#new-plant-name').value = '';
    toast('Plant added');
    loadPlants();
  } catch (e) {
    // offline fallback: keep it locally so the UI still works on the bench
    plants.push({ name, targetMoisture: target }); renderPlantLib(); buildBeds();
    toast('Added (offline — not yet on controller)', true);
  }
  finally { addingPlant = false; btn.disabled = false; }
}
async function deletePlant(name) {
  try {
    const r = await fetch('/deletePlant', { method: 'DELETE', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ name }) });
    if (!r.ok && r.status !== 404) throw new Error(r.status);
    toast('Plant deleted'); loadPlants();
  } catch (e) {
    plants = plants.filter((p) => p.name !== name); renderPlantLib(); buildBeds();
    toast('Removed (offline)', true);
  }
}

/* ============================================================
   MANUAL CONTROL
   ============================================================ */
function updateMaintUI() {
  const b = $('#maint-banner'), btn = $('#maint-btn'), st = $('#maint-status'), mc = $('#manual-controls');
  btn.textContent = maintenance ? 'Exit' : 'Enable';
  btn.className = 'btn sm' + (maintenance ? ' on' : '');
  b.className = 'banner' + (maintenance ? '' : ' live');
  st.innerHTML = maintenance
    ? '<strong>Maintenance mode.</strong> Automatic control paused — overrides below are live.'
    : '<strong>Automatic control.</strong> Enable maintenance to take manual control.';
  mc.style.opacity = maintenance ? '1' : '0.5';
  mc.style.pointerEvents = maintenance ? 'auto' : 'none';
}
async function manual(command, value) {
  const payload = { command };
  if (value != null) payload.value = value;
  try {
    const r = await fetchT('/manual', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
    if (!r.ok) throw new Error(r.status);
    return true;
  } catch (e) {
    toast(e.message === '409' ? 'Refused — water level critical' : 'Command failed', true);
    return false;
  }
}
function buildValves() {
  $('#valve-grid').innerHTML = [1, 2, 3, 4, 5].map((v) =>
    `<button class="btn sm" data-valve="${v}" style="flex-direction:column;gap:2px;padding:9px 0;min-height:52px">
      <svg class="ic" data-ic="drop" style="width:15px;height:15px;color:var(--sky)"></svg><span style="font-size:10px">V${v}</span></button>`).join('');
  initIcons($('#valve-grid'));
  // Confirmed-state only: mutate UI after the POST succeeds (fetchT bounds
  // the wait); button disabled during the round-trip to prevent double-fire.
  $$('#valve-grid [data-valve]').forEach((b) => b.addEventListener('click', async () => {
    const i = +b.dataset.valve - 1;
    const next = !valveState[i];
    b.disabled = true;
    const ok = await manual(next ? 'valve_open' : 'valve_close', +b.dataset.valve);
    if (ok) {
      valveState[i] = next;
      b.classList.toggle('on', next);
      b.querySelector('svg').style.color = next ? '#fff' : 'var(--sky)';
    }
    b.disabled = false;
  }));
}

/* ============================================================
   WIRE-UP
   ============================================================ */
function autoSyncTime() {
  const now = new Date();
  const ts = now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds();
  fetch('/setTime', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ timestamp: ts }) }).catch(() => {});
}

function init() {
  initIcons();
  applyTheme();
  buildValves();

  // theme override cycle
  $('#dusk-btn').addEventListener('click', () => {
    const order = ['auto', 'day', 'dusk'];
    const cur = localStorage.getItem(THEME_KEY) || 'auto';
    localStorage.setItem(THEME_KEY, order[(order.indexOf(cur) + 1) % 3]);
    applyTheme();
  });

  // setup sub-navigation (Climate / Plants / Light)
  $$('#setup-subnav .subtab').forEach((t) => t.addEventListener('click', () => {
    $$('#setup-subnav .subtab').forEach((x) => {
      const on = x === t;
      x.classList.toggle('active', on);
      x.setAttribute('aria-selected', on ? 'true' : 'false');
    });
    $$('#beds .subview').forEach((v) => v.classList.toggle('active', v.dataset.subview === t.dataset.sub));
    $('.scroll').scrollTop = 0;
  }));

  // beds / config
  $('#save-config-btn').addEventListener('click', saveConfig);
  $('#new-plant-target').addEventListener('input', (e) => { $('#new-plant-target-lbl').textContent = 'Target moisture ' + e.target.value + '%'; });
  $('#add-plant-btn').addEventListener('click', addPlant);
  $('#new-plant-name').addEventListener('keydown', (e) => { if (e.key === 'Enter') addPlant(); });

  // manual
  // Safety-state toggle: UI flips only after confirmed 200. Poll
  // reconciliation (MAINTENANCE_MODE in /data.json) remains authoritative.
  $('#maint-btn').addEventListener('click', async () => {
    const btn = $('#maint-btn');
    btn.disabled = true;
    const ok = await manual(maintenance ? 'maintenance_off' : 'maintenance_on');
    if (ok) { maintenance = !maintenance; updateMaintUI(); }
    btn.disabled = false;
  });
  $$('[data-roof]').forEach((b) => b.addEventListener('click', () => manual('roof_' + b.dataset.roof)));
  $('#pump-btn').addEventListener('click', async () => {
    const btn = $('#pump-btn');
    const next = !pumpOn;
    btn.disabled = true;
    const ok = await manual(next ? 'pump_on' : 'pump_off');
    if (ok) pumpOn = next; // confirmed state only
    btn.textContent = 'Pump ' + (pumpOn ? 'ON' : 'OFF');
    btn.className = 'btn sm' + (pumpOn ? ' sky' : '');
    btn.disabled = false;
  });
  $('#valve-closeall-btn').addEventListener('click', async () => {
    const btn = $('#valve-closeall-btn');
    btn.disabled = true;
    const ok = await manual('valve_closeAll');
    if (ok) {
      valveState.fill(false);
      $$('#valve-grid [data-valve]').forEach((b) => {
        b.classList.remove('on');
        b.querySelector('svg').style.color = 'var(--sky)';
      });
      toast('All valves closed');
    }
    btn.disabled = false;
  });
  $('#led-slider').addEventListener('input', (e) => { $('#led-pct').innerHTML = Math.round(e.target.value / 255 * 100) + '<span class="u">%</span>'; });
  $('#led-apply').addEventListener('click', () => { manual('led_pwm', +$('#led-slider').value); toast('Brightness applied'); });
  $('#emerg-btn').addEventListener('click', () => { manual('emergency_stop'); manual('maintenance_on'); maintenance = true; updateMaintUI(); toast('EMERGENCY STOP', true); });
  $('#ack-btn').addEventListener('click', async () => { try { await fetch('/ackErrors', { method: 'POST', headers: { 'Content-Type': 'application/json' } }); toast('Errors reset'); } catch (e) { toast('Reset failed', true); } });

  // logs
  $('#logs-refresh').addEventListener('click', fetchLogs);
  $('#logs-filter').addEventListener('click', (ev) => {
    const b = ev.target.closest('.lf'); if (!b) return;
    logFilter = b.dataset.lvl;
    $$('#logs-filter .lf').forEach((el) => el.classList.toggle('active', el === b));
    renderLogs(); // re-filter the cached snapshot, no refetch
  });

  loadPlants();
  loadConfig();
  autoSyncTime();
  drawCharts = true;            // first paint of the dash draws charts/sparks in
  revealView($('#dash'));       // stagger the default view on load
  poll();
  // Self-scheduling poll loop: pauses while the tab is hidden (cheap 1 s
  // idle check), backs off exponentially to 30 s while the device is
  // unreachable, resets to 2 s on first success or when the tab returns.
  (function pollLoop() {
    setTimeout(async () => {
      if (!document.hidden) {
        await poll();
        pollDelay = lastPollOk ? 2000 : Math.min(pollDelay * 2, 30000);
      }
      pollLoop();
    }, document.hidden ? 1000 : pollDelay);
  })();
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) { pollDelay = 2000; poll(); }
  });
}
document.addEventListener('DOMContentLoaded', init);
