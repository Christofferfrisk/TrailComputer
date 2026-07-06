'use strict';
const D = window.TC_DATA;
const R2D = 180 / Math.PI, D2R = Math.PI / 180;

// --- persisted settings -----------------------------------------------------
const S = Object.assign({ start: -1, end: -1, dayH: 6, awake: false },
  JSON.parse(localStorage.getItem('tc') || '{}'));
const save = () => localStorage.setItem('tc', JSON.stringify(S));

// --- geo --------------------------------------------------------------------
function haversineM(la1, lo1, la2, lo2) {
  const r = 6371000, dla = (la2 - la1) * D2R, dlo = (lo2 - lo1) * D2R;
  const a = Math.sin(dla / 2) ** 2 + Math.cos(la1 * D2R) * Math.cos(la2 * D2R) * Math.sin(dlo / 2) ** 2;
  return 2 * r * Math.asin(Math.sqrt(a));
}
function bearing(la1, lo1, la2, lo2) {
  const y = Math.sin((lo2 - lo1) * D2R) * Math.cos(la2 * D2R);
  const x = Math.cos(la1 * D2R) * Math.sin(la2 * D2R) -
            Math.sin(la1 * D2R) * Math.cos(la2 * D2R) * Math.cos((lo2 - lo1) * D2R);
  return (Math.atan2(y, x) * R2D + 360) % 360;
}
// Snap (lat,lon) onto a polyline; returns nearest segment, along-distance, lateral, cum.
function snap(poly, lat, lon) {
  const kx = 111320 * Math.cos(lat * D2R), ky = 110540;
  let best = { seg: 0, along: 0, lat: 1e12, cum: poly[0][2] };
  for (let i = 0; i < poly.length - 1; i++) {
    const a = poly[i], b = poly[i + 1];
    const ax = (a[1] - lon) * kx, ay = (a[0] - lat) * ky;
    const bx = (b[1] - lon) * kx, by = (b[0] - lat) * ky;
    const dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    let t = len2 > 0 ? -(ax * dx + ay * dy) / len2 : 0;
    t = Math.max(0, Math.min(1, t));
    const px = ax + t * dx, py = ay + t * dy, d = Math.hypot(px, py);
    if (d < best.lat) {
      const along = Math.hypot(t * dx, t * dy);
      best = { seg: i, along, lat: d, cum: a[2] + along };
    }
  }
  return best;
}

// --- hike section helpers ---------------------------------------------------
// codes: main hut slot 0..N-1; spur stop = 1000 + j  (j: 0 Kebnekaise, 1 Nikkaluokta)
function codeName(c) {
  if (c < 0) return '—';
  return c >= 1000 ? D.spur.stops[c - 1000].name : D.hutNames[c];
}
function mainSlotOfCode(c) {                       // where a code sits on the main line
  if (c < 1000) return c;
  return D.hutNames.indexOf(D.spur.afterHut);      // spur attaches after this hut
}
function hikeActive() { return S.start >= 0 && S.end >= 0; }
function hikeBounds() {                            // [startSlot, endSlot] on the main route
  if (!hikeActive()) return [0, D.huts.length - 1];
  let a = mainSlotOfCode(S.start), b = mainSlotOfCode(S.end);
  if (S.start >= 1000) a = a + 1;                  // main portion begins after the junction
  if (a > b) [a, b] = [b, a];
  return [a, b];
}
function cumKm(slot) { return D.route[D.huts[slot]][2] / 1000; }

function naismithMin(km, ascM) {
  return Math.round(km / 4.5 * 60 + Math.max(0, ascM) / 600 * 60);
}
function ascentBetween(idxA, idxB) {               // cumAsc field on route points
  return Math.max(0, D.route[idxB][4] - D.route[idxA][4]);
}
const fmtETA = m => (m < 0 ? '—' : `${Math.floor(m / 60)}:${String(m % 60).padStart(2, '0')}`);

// --- live position ----------------------------------------------------------
let pos = null;   // {lat,lon,acc,spd,ts}

function computeNow() {
  const [sSlot, eSlot] = hikeBounds();
  const out = { hasFix: !!pos, off: false, onSpur: false };
  if (!pos) return out;

  const sm = snap(D.route, pos.lat, pos.lon);
  const sp = snap(D.spur.route, pos.lat, pos.lon);
  out.onSpur = sp.lat < sm.lat && sp.lat < 3000;
  out.off = Math.min(sm.lat, sp.lat) > 150;

  if (out.onSpur) {
    const keb = D.spur.route[D.spur.kebIdx][2], end = D.spur.route[D.spur.route.length - 1][2];
    const toKeb = keb - sp.cum, kebDone = sp.cum >= keb;
    out.next = kebDone ? D.spur.junction : 'Kebnekaise';
    out.remKm = (kebDone ? (end - sp.cum) : toKeb) / 1000;
    out.etaMin = naismithMin(out.remKm, 0);
    out.frac = 0; out.doneKm = 0;
    out.totalKm = (end / 1000) + (cumKm(eSlot) - cumKm(sSlot));
    out.approach = `${(sp.cum / 1000).toFixed(1)} km from Nikkaluokta`;
    return out;
  }

  const cur = sm.cum / 1000;
  // next hut ahead within the section
  let ns = -1;
  for (let k = sSlot; k <= eSlot; k++) if (cumKm(k) > cur + 0.02) { ns = k; break; }
  if (ns < 0) ns = eSlot;
  out.next = D.hutNames[ns];
  out.remKm = Math.max(0, cumKm(ns) - cur);
  out.remAsc = ascentBetween(sm.seg, D.huts[ns]);
  out.etaMin = naismithMin(out.remKm, out.remAsc);
  out.altM = Math.round(D.route[sm.seg][3]);
  out.arrived = out.remKm < 0.06 && ns === eSlot;

  const c0 = cumKm(sSlot), c1 = cumKm(eSlot);
  out.totalKm = Math.max(0.1, c1 - c0);
  out.doneKm = Math.max(0, Math.min(out.totalKm, cur - c0));
  out.frac = out.doneKm / out.totalKm;
  out.snap = sm;
  return out;
}

// --- rendering: Now ---------------------------------------------------------
function ring(frac) {
  const C = 289, off = C * (1 - Math.max(0, Math.min(1, frac)));
  return `<svg class="dial" viewBox="0 0 110 110">
    <circle cx="55" cy="55" r="46" fill="none" stroke="#e3e7ec" stroke-width="9"/>
    <circle cx="55" cy="55" r="46" fill="none" stroke="#2e7d4f" stroke-width="9"
      stroke-linecap="round" transform="rotate(-90 55 55)"
      stroke-dasharray="${C}" stroke-dashoffset="${off.toFixed(1)}"/>
    <text x="55" y="52" class="ringpct">${Math.round(frac * 100)}%</text>
    <text x="55" y="70" class="ringlbl">of hike</text></svg>`;
}
// Schematic north-up map of nearby route points + you.
function miniMap(n) {
  if (!pos || !n.snap) return '';
  const poly = D.route, seg = n.snap.seg, span = 40;
  const lo = Math.max(0, seg - span), hi = Math.min(poly.length - 1, seg + span);
  const kx = 111320 * Math.cos(pos.lat * D2R), ky = 110540;
  const P = [];
  for (let i = lo; i <= hi; i++) P.push([(poly[i][1] - pos.lon) * kx, (poly[i][0] - pos.lat) * ky]);
  let R = 200;
  P.forEach(p => R = Math.max(R, Math.abs(p[0]), Math.abs(p[1])));
  const W = 320, H = 240, sc = (Math.min(W, H) / 2 - 16) / R;
  const X = e => (W / 2 + e[0] * sc).toFixed(1), Y = e => (H / 2 - e[1] * sc).toFixed(1);
  let pts = P.map(p => `${X(p)},${Y(p)}`).join(' ');
  let huts = '';
  for (let k = 0; k < D.huts.length; k++) {
    const hi2 = D.huts[k];
    if (hi2 < lo || hi2 > hi) continue;
    const e = [(poly[hi2][1] - pos.lon) * kx, (poly[hi2][0] - pos.lat) * ky];
    huts += `<rect x="${X(e) - 3}" y="${Y(e) - 3}" width="6" height="6" fill="#1b2430"/>
      <text x="${X(e)}" y="${(+Y(e) - 6)}" font-size="9" fill="#444" text-anchor="middle">${D.hutNames[k]}</text>`;
  }
  return `<svg class="mapbox" viewBox="0 0 ${W} ${H}">
    <polyline points="${pts}" fill="none" stroke="#2e7d4f" stroke-width="3"/>
    ${huts}
    <circle cx="${W / 2}" cy="${H / 2}" r="6" fill="#c0392b"/>
    <circle cx="${W / 2}" cy="${H / 2}" r="10" fill="none" stroke="#c0392b" stroke-width="2"/>
    <text x="10" y="16" font-size="11" font-weight="700" fill="#1b2430">▲N</text></svg>`;
}
function renderNow() {
  const n = computeNow(), el = document.getElementById('p-now');
  if (!n.hasFix) {
    el.innerHTML = `<div class="card"><div class="hero">${ring(0)}
      <div class="heronum"><div class="dist" style="font-size:24px">Waiting for GPS…</div>
      <div class="sub">go outside with a clear view of the sky</div></div></div>
      <p class="muted">GPS works with no phone signal. Position may take a minute on a cold start.</p></div>`;
    return;
  }
  const acc = pos.acc ? `±${Math.round(pos.acc)} m` : '—';
  const spd = pos.spd != null ? `${(pos.spd * 3.6).toFixed(1)} km/h` : '—';
  let h = `<div class="card"><div class="hero">${ring(n.frac || 0)}
    <div class="heronum">
      <div class="dist">${n.remKm.toFixed(1)}<span>km</span></div>
      <div class="sub">to <b>${n.next}</b></div>
      <div class="sub">ETA <b>${fmtETA(n.etaMin)}</b>${n.remAsc ? ` · climb <b>${Math.round(n.remAsc)} m</b>` : ''}</div>
      ${n.totalKm ? `<div class="sub"><b>${Math.round(n.doneKm)}</b> of ${Math.round(n.totalKm)} km done</div>` : ''}
    </div></div>`;
  if (n.onSpur) h += `<p class="muted">On the Kebnekaise approach · ${n.approach}.</p>`;
  if (n.off) h += `<div class="warn">⚠ You seem to be more than 150 m off the trail line.</div>`;
  if (n.arrived) h += `<div class="warn" style="background:#e6f2ea;color:#1d5c39">✓ At ${n.next} — end of your section.</div>`;
  h += `<div class="tiles">
    <div class="tile"><div class="tv">${n.altM != null ? n.altM : '—'}</div><div class="tl">alt m</div></div>
    <div class="tile"><div class="tv">${spd}</div><div class="tl">speed</div></div>
    <div class="tile"><div class="tv">${acc}</div><div class="tl">GPS acc</div></div>
  </div>${miniMap(n)}
  <p class="muted">Schematic — line + huts only, no terrain. Use a topo-map app for the ground detail.</p></div>`;
  el.innerHTML = h;
}

// --- rendering: Plan --------------------------------------------------------
function hikeOptions(sel) {
  let o = '';
  for (let k = 0; k < D.hutNames.length; k++) {
    o += `<option value="${k}"${sel === k ? ' selected' : ''}>${D.hutNames[k]}</option>`;
    if (D.spur.afterHut === D.hutNames[k])
      D.spur.stops.forEach((s, j) => {
        const c = 1000 + j;
        o += `<option value="${c}"${sel === c ? ' selected' : ''}>&nbsp;&nbsp;↳ ${s.name}</option>`;
      });
  }
  return o;
}
function chips(name) {
  let c = '';
  const st = D.stores[name];
  if (st === 2) c += `<span class="chip shop">🛒 shop</span>`;
  else if (st === 1) c += `<span class="chip sm">🛒 small</span>`;
  if (D.sauna.includes(name)) c += `<span class="chip amen">🧖 bastu</span>`;
  if (D.station.includes(name)) c += `<span class="chip amen">🍽 station</span>`;
  return c;
}
function planDays() {
  if (!hikeActive()) return [];
  const [sSlot, eSlot] = hikeBounds();
  const legs = [];
  let from = codeName(S.start);
  if (S.start >= 1000) {                       // spur approach legs first
    for (let j = S.start - 1000; j >= 0; j--) {
      const s = D.spur.stops[j];
      const to = j > 0 ? D.spur.stops[j - 1].name : D.spur.junction;
      legs.push({ to, km: s.legKm, hrs: parseHrs(s.time) });
    }
  }
  for (let k = sSlot + 1; k <= eSlot; k++) {
    const st = D.stages[D.hutNames[k]];
    const km = st ? st.km : (cumKm(k) - cumKm(k - 1));
    const asc = ascentBetween(D.huts[k - 1], D.huts[k]);
    const hrs = st ? parseHrs(st.time) : (km / 4.5 + asc / 600);
    legs.push({ to: D.hutNames[k], km, hrs, boat: st && st.boat });
  }
  const days = []; let day = { from, to: from, km: 0, hrs: 0, boat: false, n: 0 };
  const tgt = S.dayH || 6;
  for (const lg of legs) {
    if (day.n > 0 && day.hrs + lg.hrs > tgt) { days.push(day); day = { from: day.to, to: day.to, km: 0, hrs: 0, boat: false, n: 0 }; }
    day.km += lg.km; day.hrs += lg.hrs; day.boat = day.boat || lg.boat; day.to = lg.to; day.n++;
  }
  if (day.n > 0) days.push(day);
  return days;
}
const parseHrs = t => { const m = /(\d+)/.exec(t || ''); return m ? +m[1] : 0; };

function elevProfile() {
  const [sSlot, eSlot] = hikeBounds();
  const i0 = D.huts[sSlot], i1 = D.huts[eSlot];
  let e0 = 1e9, e1 = -1e9;
  for (let i = i0; i <= i1; i++) { e0 = Math.min(e0, D.route[i][3]); e1 = Math.max(e1, D.route[i][3]); }
  if (e1 - e0 < 1) e1 = e0 + 1;
  const km0 = D.route[i0][2] / 1000, km1 = D.route[i1][2] / 1000;
  const W = 520, H = 170, pL = 34, pT = 10, pB = 26, pw = W - pL - 8, ph = H - pT - pB, base = H - pB;
  const X = km => pL + (km - km0) / (km1 - km0) * pw;
  const Y = e => pT + (1 - (e - e0) / (e1 - e0)) * ph;
  let pts = '';
  for (let i = i0; i <= i1; i++) pts += `${X(D.route[i][2] / 1000).toFixed(1)},${Y(D.route[i][3]).toFixed(1)} `;
  let huts = '';
  for (let k = sSlot; k <= eSlot; k++) {
    const x = X(cumKm(k)), y = Y(D.route[D.huts[k]][3]);
    huts += `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="2.5" fill="#1d5c39"/>`;
  }
  return `<svg viewBox="0 0 ${W} ${H}" style="width:100%;height:auto;display:block">
    <polyline points="${pts}" fill="none" stroke="#2e7d4f" stroke-width="1.6"/>
    <line x1="${pL}" y1="${base}" x2="${pL + pw}" y2="${base}" stroke="#c7ccd2"/>
    <text x="2" y="${pT + 6}" font-size="9" fill="#697483">${Math.round(e1)} m</text>
    <text x="2" y="${base}" font-size="9" fill="#697483">${Math.round(e0)} m</text>${huts}
    <text x="${pL}" y="${base + 15}" font-size="11" font-weight="bold">▲ ${codeName(S.start)}</text>
    <text x="${pL + pw}" y="${base + 15}" font-size="11" font-weight="bold" text-anchor="end">${codeName(S.end)} ▲</text>
  </svg>`;
}
function renderPlan() {
  const [sSlot, eSlot] = hikeBounds();
  const n = computeNow();
  let h = `<div class="card"><h2><span class="ic">🥾</span>This hike</h2>
    ${hikeActive() ? `<p class="prog"><b>${codeName(S.start)}</b> → <b>${codeName(S.end)}</b> · ≈ ${Math.round(cumKm(eSlot) - cumKm(sSlot))} km</p>` : ''}
    <div class="field"><label>Start</label><select id="selS">${hikeOptions(S.start)}</select></div>
    <div class="field"><label>End</label><select id="selE">${hikeOptions(S.end)}</select></div>
    <p class="muted"><a href="#" id="fullRoute">Use full route</a> · the plan uses only this section.</p></div>`;

  // day planner
  const days = planDays();
  h += `<div class="card"><h2><span class="ic">📅</span>Day planner</h2>
    <div class="field"><label>Target hours / day</label><input id="dayH" inputmode="numeric" value="${S.dayH}"></div>`;
  if (!days.length) h += `<p class="muted">Set a Start &amp; End to plan daily stages.</p>`;
  days.forEach((d, i) => {
    h += `<div class="day"><div class="daynum">${i + 1}</div><div class="dayb">
      <div class="dayr"><b>${d.from}</b> → <b>${d.to}</b></div>
      <div class="daym"><span class="m">${Math.round(d.km)} km</span><span class="m">~${Math.round(d.hrs)} h</span>
      ${d.boat ? '<span class="m bt">⚓ boat</span>' : ''}</div></div></div>`;
  });
  if (days.length) h += `<p class="muted">${days.length} days at ~${S.dayH} h/day · STF stage figures.</p>`;
  h += `</div>`;

  // elevation
  h += `<div class="card"><h2><span class="ic">⛰️</span>Elevation profile</h2>${elevProfile()}</div>`;

  // route timeline
  h += `<div class="card"><h2><span class="ic">🗺️</span>Route</h2>`;
  const cur = n.hasFix && !n.onSpur && n.snap ? n.snap.cum / 1000 : -1;
  h += `<ul class="tl">`;
  for (let k = 0; k < D.hutNames.length; k++) {
    const inHike = k >= sSlot && k <= eSlot;
    const st = D.stages[D.hutNames[k]];
    const done = cur >= 0 && cumKm(k) < cur;
    const cls = (!inHike ? 'dim' : '') + (done ? ' done' : '');
    const leg = st ? `${st.km} km · ${st.time}` : (k > 0 ? `${(cumKm(k) - cumKm(k - 1)).toFixed(1)} km` : 'start');
    h += `<li class="${cls}"><span class="node">${k + 1}</span>
      <span class="name">${D.hutNames[k]}${chips(D.hutNames[k])}</span>
      <span class="leg">${leg} · ${Math.round(cumKm(k))} km${D.transport[D.hutNames[k]] ? ' · 🚌 ' + D.transport[D.hutNames[k]] : ''}</span></li>`;
    if (D.spur.afterHut === D.hutNames[k])
      D.spur.stops.forEach(s => {
        h += `<li class="spur dim"><span class="node"></span><span class="name">${s.name}${chips(s.name)}</span>
          <span class="leg">${s.legKm} km · ${s.time}${s.note ? ' · ' + s.note : ''}${D.transport[s.name] ? ' · 🚌 ' + D.transport[s.name] : ''}</span></li>`;
      });
  }
  h += `</ul></div>`;
  document.getElementById('p-plan').innerHTML = h;

  document.getElementById('selS').onchange = e => { S.start = +e.target.value; save(); renderAll(); };
  document.getElementById('selE').onchange = e => { S.end = +e.target.value; save(); renderAll(); };
  document.getElementById('dayH').onchange = e => { S.dayH = Math.max(2, Math.min(16, +e.target.value || 6)); save(); renderAll(); };
  document.getElementById('fullRoute').onclick = ev => { ev.preventDefault(); S.start = -1; S.end = -1; save(); renderAll(); };
}

// --- rendering: Info --------------------------------------------------------
function renderInfo() {
  document.getElementById('p-info').innerHTML = `
  <div class="card"><h2><span class="ic">ℹ️</span>About</h2>
    <p style="font-size:14px">Offline Kungsleden navigator. Route, huts, STF stages and day
    planning all live on your phone — no signal needed. Position comes from your phone's GPS,
    which works with no reception.</p>
    <label class="toggle" style="margin-top:12px"><input type="checkbox" id="awake" ${S.awake ? 'checked' : ''}>
    Keep screen awake while open</label>
    <p class="muted">GPS + screen drain the battery fast — bring a power bank, and only open the
    app when you need it. This is a navigation aid, not a safety device: carry a paper map, a
    compass, and a satellite messenger.</p>
    <p class="muted">Map detail: this app draws a schematic line only. For terrain, use a topo
    app (Fjällkartan / Lantmäteriet, Topo GPS, OsmAnd) with the region downloaded.</p></div>
  <div class="card"><h2><span class="ic">📡</span>Status</h2><div class="grid" id="stat"></div></div>`;
  document.getElementById('awake').onchange = e => { S.awake = e.target.checked; save(); applyWakeLock(); };
  updateStatus();
}
function updateStatus() {
  const g = document.getElementById('stat'); if (!g) return;
  const rows = [
    ['GPS', pos ? 'live' : 'no fix'],
    ['Position', pos ? `${pos.lat.toFixed(5)}, ${pos.lon.toFixed(5)}` : '—'],
    ['Accuracy', pos && pos.acc ? `±${Math.round(pos.acc)} m` : '—'],
    ['Offline data', `${D.route.length} route pts · ${D.huts.length} huts`],
    ['Installed', navigator.serviceWorker && navigator.serviceWorker.controller ? 'cached for offline' : 'loading…'],
  ];
  g.innerHTML = rows.map(r => `<div class="k">${r[0]}</div><div class="v">${r[1]}</div>`).join('');
}

// --- glue -------------------------------------------------------------------
function renderAll() {
  renderNow();
  if (document.getElementById('p-plan').classList.contains('active')) renderPlan();
  if (document.getElementById('p-info').classList.contains('active')) renderInfo();
}
document.querySelectorAll('.tabbar button').forEach(b => b.onclick = () => {
  document.querySelectorAll('.tabbar button').forEach(x => x.classList.toggle('active', x === b));
  document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === 'p-' + b.dataset.p));
  if (b.dataset.p === 'plan') renderPlan();
  if (b.dataset.p === 'info') renderInfo();
});

// GPS
function setGps(state, txt) {
  const d = document.getElementById('gpsdot'); d.className = 'dot ' + state;
  document.getElementById('gpstxt').textContent = txt;
}
if ('geolocation' in navigator) {
  navigator.geolocation.watchPosition(p => {
    pos = { lat: p.coords.latitude, lon: p.coords.longitude, acc: p.coords.accuracy,
            spd: p.coords.speed, ts: p.timestamp };
    setGps('ok', pos.acc ? `±${Math.round(pos.acc)} m` : 'fix');
    renderNow(); updateStatus();
  }, err => setGps('off', err.code === 1 ? 'permission denied' : 'no GPS'),
  { enableHighAccuracy: true, maximumAge: 5000, timeout: 60000 });
} else setGps('off', 'no GPS');

// Wake lock
let wl = null;
async function applyWakeLock() {
  try {
    if (S.awake && 'wakeLock' in navigator) wl = await navigator.wakeLock.request('screen');
    else if (wl) { wl.release(); wl = null; }
  } catch (e) {}
}
document.addEventListener('visibilitychange', () => { if (S.awake && document.visibilityState === 'visible') applyWakeLock(); });

// Service worker
if ('serviceWorker' in navigator)
  navigator.serviceWorker.register('sw.js').then(updateStatus).catch(() => {});

renderNow();
applyWakeLock();
