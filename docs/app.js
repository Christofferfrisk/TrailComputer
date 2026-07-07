'use strict';
const D = window.TC_DATA;
const R2D = 180 / Math.PI, D2R = Math.PI / 180;

// --- persisted settings -----------------------------------------------------
const S = Object.assign({ start: -1, end: -1, dayH: 6, dayFlex: 1, awake: false, sim: false, simF: 0 },
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

function naismithMin(km, ascM, kmh) {
  return Math.round(km / (kmh || 4.5) * 60 + Math.max(0, ascM) / 600 * 60);
}
function ascentBetween(idxA, idxB) {               // cumAsc field on route points
  return Math.max(0, D.route[idxB][4] - D.route[idxA][4]);
}
const fmtETA = m => (m < 0 ? '—' : `${Math.floor(m / 60)}:${String(m % 60).padStart(2, '0')}`);

// --- on-demand position -----------------------------------------------------
// One fix at a time (the GPS radio powers down between fixes) to spare battery,
// the same "glance, don't stare" idea the hardware used.
let pos = null;        // {lat,lon,acc,spd}
let lastFixMs = 0;
let locating = false;
function agoStr(ms) {
  if (!ms) return '';
  const s = (Date.now() - ms) / 1000 | 0;
  if (s < 45) return 'just now';
  if (s < 5400) return Math.round(s / 60) + ' min ago';
  return Math.round(s / 3600) + ' h ago';
}
const hhmm = ms => { const d = new Date(ms); return String(d.getHours()).padStart(2, '0') + ':' + String(d.getMinutes()).padStart(2, '0'); };

// --- simulation (bench testing off the trail) -------------------------------
function tripPolyline() {
  const [sSlot, eSlot] = hikeBounds();
  const startMain = hikeActive() ? mainSlotOfCode(S.start) : sSlot;
  const pts = [];
  if (hikeActive() && S.start >= 1000) D.spur.route.forEach(p => pts.push([p[0], p[1]]));
  for (let i = D.huts[startMain]; i <= D.huts[eSlot]; i++) pts.push([D.route[i][0], D.route[i][1]]);
  return pts;
}
function simPoint(f) {
  const pts = tripPolyline(), cum = [0];
  for (let i = 1; i < pts.length; i++)
    cum.push(cum[i - 1] + haversineM(pts[i - 1][0], pts[i - 1][1], pts[i][0], pts[i][1]));
  const target = f * cum[cum.length - 1];
  let i = 1; while (i < cum.length && cum[i] < target) i++;
  if (i >= pts.length) return pts[pts.length - 1];
  const t = (target - cum[i - 1]) / Math.max(1, cum[i] - cum[i - 1]);
  return [pts[i - 1][0] + (pts[i][0] - pts[i - 1][0]) * t, pts[i - 1][1] + (pts[i][1] - pts[i - 1][1]) * t];
}
function applySim() {
  const p = simPoint(S.simF);
  pos = { lat: p[0], lon: p[1], acc: 8, spd: 1.2 };
  lastFixMs = Date.now();
  setGps('ok', 'sim ' + Math.round(S.simF * 100) + '%');
  renderNow();
}

function computeNow() {
  const [sSlot, eSlot] = hikeBounds();
  const startMain = hikeActive() ? mainSlotOfCode(S.start) : sSlot;   // Singi for a spur start
  const spurTotalKm = (hikeActive() && S.start >= 1000) ? D.spur.route[D.spur.route.length - 1][2] / 1000 : 0;
  const tripKm = Math.max(0.1, spurTotalKm + (cumKm(eSlot) - cumKm(startMain)));
  const out = { hasFix: !!pos, off: false, onSpur: false, totalKm: hikeActive() ? tripKm : 0 };
  if (!pos) return out;

  const sm = snap(D.route, pos.lat, pos.lon);
  const sp = snap(D.spur.route, pos.lat, pos.lon);
  out.onSpur = sp.lat < sm.lat && sp.lat < 3000;
  out.off = Math.min(sm.lat, sp.lat) > 150;

  const spdKmh = pos.spd > 0 ? pos.spd * 3.6 : 0;   // adjust ETA to your real pace when moving
  out.measuredPace = spdKmh >= 1.5 && spdKmh <= 9;  // else fall back to Naismith 4.5 km/h
  const pace = out.measuredPace ? spdKmh : 4.5;
  out.pace = pace;

  if (out.onSpur) {
    const keb = D.spur.route[D.spur.kebIdx][2], end = D.spur.route[D.spur.route.length - 1][2];
    const toKeb = keb - sp.cum, kebDone = sp.cum >= keb;
    out.next = kebDone ? D.spur.junction : 'Kebnekaise';
    out.remKm = (kebDone ? (end - sp.cum) : toKeb) / 1000;
    const tgtIdx = kebDone ? D.spur.route.length - 1 : D.spur.kebIdx;
    out.remAsc = Math.max(0, D.spur.route[tgtIdx][4] - D.spur.route[sp.seg][4]);
    out.etaMin = naismithMin(out.remKm, out.remAsc, pace);
    out.doneKm = sp.cum / 1000;
    out.totalKm = tripKm;
    out.frac = out.doneKm / tripKm;
    out.legFrac = kebDone ? (end > keb ? (sp.cum - keb) / (end - keb) : 1) : (keb > 0 ? sp.cum / keb : 0);
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
  const pv = ns >= 1 ? cumKm(ns - 1) : 0;                 // leg = previous hut -> next hut
  out.legFrac = Math.max(0, Math.min(1, (cur - pv) / Math.max(0.01, cumKm(ns) - pv)));
  out.remAsc = ascentBetween(sm.seg, D.huts[ns]);
  out.etaMin = naismithMin(out.remKm, out.remAsc, pace);
  out.altM = Math.round(D.route[sm.seg][3]);
  out.arrived = out.remKm < 0.06 && ns === eSlot;

  out.doneKm = Math.max(0, Math.min(tripKm, spurTotalKm + (cur - cumKm(startMain))));
  out.totalKm = tripKm;
  out.frac = out.doneKm / tripKm;
  out.snap = sm;
  return out;
}

// --- rendering: Now ---------------------------------------------------------
function ring(frac, label) {
  const C = 289, off = C * (1 - Math.max(0, Math.min(1, frac)));
  return `<svg class="dial" viewBox="0 0 110 110">
    <circle cx="55" cy="55" r="46" fill="none" stroke="#e3e7ec" stroke-width="9"/>
    <circle cx="55" cy="55" r="46" fill="none" stroke="#2e7d4f" stroke-width="9"
      stroke-linecap="round" transform="rotate(-90 55 55)"
      stroke-dasharray="${C}" stroke-dashoffset="${off.toFixed(1)}"/>
    <text x="55" y="52" class="ringpct">${Math.round(frac * 100)}%</text>
    <text x="55" y="70" class="ringlbl">${label}</text></svg>`;
}
// Schematic north-up map of the whole trip (hike section + spur approach) + you.
function tripMap() {
  const [sSlot, eSlot] = hikeBounds();
  const startMain = hikeActive() ? mainSlotOfCode(S.start) : sSlot;   // include the junction hut
  const spurTrip = hikeActive() && S.start >= 1000;
  const i0 = D.huts[startMain], i1 = D.huts[eSlot];

  const all = [];
  if (spurTrip) D.spur.route.forEach(p => all.push([p[0], p[1]]));
  for (let i = i0; i <= i1; i++) all.push([D.route[i][0], D.route[i][1]]);
  if (pos) all.push([pos.lat, pos.lon]);
  let laMin = 1e9, laMax = -1e9, loMin = 1e9, loMax = -1e9;
  all.forEach(p => { laMin = Math.min(laMin, p[0]); laMax = Math.max(laMax, p[0]); loMin = Math.min(loMin, p[1]); loMax = Math.max(loMax, p[1]); });

  const midLa = (laMin + laMax) / 2, coslat = Math.cos(midLa * D2R);
  const W = 320, H = 320, pad = 20;
  const spanLo = (loMax - loMin) * coslat || 1e-6, spanLa = (laMax - laMin) || 1e-6;
  const sc = Math.min((W - 2 * pad) / spanLo, (H - 2 * pad) / spanLa);
  const offX = (W - spanLo * sc) / 2, offY = (H - spanLa * sc) / 2;
  const X = (la, lo) => (offX + (lo - loMin) * coslat * sc).toFixed(1);
  const Y = (la, lo) => (offY + (laMax - la) * sc).toFixed(1);

  let svg = '';
  if (spurTrip) {
    const sp = D.spur.route.map(p => `${X(p[0], p[1])},${Y(p[0], p[1])}`).join(' ');
    svg += `<polyline points="${sp}" fill="none" stroke="#2e7d4f" stroke-width="2.5" stroke-dasharray="5 3"/>`;
  }
  let mn = '';
  for (let i = i0; i <= i1; i++) mn += `${X(D.route[i][0], D.route[i][1])},${Y(D.route[i][0], D.route[i][1])} `;
  svg += `<polyline points="${mn}" fill="none" stroke="#2e7d4f" stroke-width="3"/>`;

  let huts = '';
  for (let k = startMain; k <= eSlot; k++) {
    const hi = D.huts[k], x = X(D.route[hi][0], D.route[hi][1]), y = Y(D.route[hi][0], D.route[hi][1]);
    huts += `<rect x="${x - 3}" y="${y - 3}" width="6" height="6" fill="#1b2430"/>
      <text x="${+x + 6}" y="${+y + 3}" font-size="9" fill="#444">${D.hutNames[k]}</text>`;
  }
  if (spurTrip) D.spur.stops.forEach(s => {
    const x = X(s.lat, s.lon), y = Y(s.lat, s.lon);
    huts += `<rect x="${x - 3}" y="${y - 3}" width="6" height="6" fill="#3d6a8a"/>
      <text x="${+x + 6}" y="${+y + 3}" font-size="9" fill="#3d6a8a">${s.name}</text>`;
  });

  const you = pos ? `<circle cx="${X(pos.lat, pos.lon)}" cy="${Y(pos.lat, pos.lon)}" r="6" fill="#c0392b"/>
    <circle cx="${X(pos.lat, pos.lon)}" cy="${Y(pos.lat, pos.lon)}" r="10" fill="none" stroke="#c0392b" stroke-width="2"/>` : '';
  return `<svg class="mapbox" viewBox="0 0 ${W} ${H}">${svg}${huts}${you}
    <text x="10" y="16" font-size="11" font-weight="700" fill="#1b2430">▲N</text></svg>`;
}
function wireRefresh() { const b = document.getElementById('refresh'); if (b) b.onclick = () => getFix(true); }
function renderNow() {
  const n = computeNow(), el = document.getElementById('p-now');
  if (!n.hasFix) {
    const msg = locating ? 'Locating…' : 'No position yet';
    const sub = locating ? 'reading GPS — may take a moment' : 'tap below to get your position';
    el.innerHTML = `<div class="card"><div class="hero">${ring(0, 'of leg')}
      <div class="heronum"><div class="dist" style="font-size:24px">${msg}</div>
      <div class="sub">${sub}</div></div></div>
      <button class="btn" id="refresh" style="width:100%"${locating ? ' disabled' : ''}>${locating ? 'Locating…' : '⟳ Get my position'}</button>
      <p class="muted">GPS works with no phone signal; a cold start can take a minute. The GPS
      turns off between checks to save battery.</p></div>`;
    wireRefresh();
    return;
  }
  const acc = pos.acc ? `±${Math.round(pos.acc)} m` : '—';
  const spd = pos.spd != null ? `${(pos.spd * 3.6).toFixed(1)} km/h` : '—';
  let h = `<div class="card"><div class="hero">${ring(n.legFrac || 0, 'of leg')}
    <div class="heronum">
      <div class="dist">${n.remKm.toFixed(1)}<span>km</span></div>
      <div class="sub">to <b>${n.next}</b></div>
      <div class="sub">ETA <b>${fmtETA(n.etaMin)}</b>${n.measuredPace ? ` <span class="dim">at ${n.pace.toFixed(1)} km/h</span>` : ''}${n.remAsc ? ` · climb <b>${Math.round(n.remAsc)} m</b>` : ''}</div>
      <div class="sub">arrive <b>~${hhmm(lastFixMs + n.etaMin * 60000)}</b> <span class="dim">· as of ${hhmm(lastFixMs)}</span></div>
    </div></div>
    ${n.totalKm ? `<div class="legbar"><i style="width:${Math.round((n.frac || 0) * 100)}%"></i></div>
    <div class="sub2"><b>${Math.round((n.frac || 0) * 100)}%</b> of hike · ${Math.round(n.doneKm)} of ${Math.round(n.totalKm)} km</div>` : ''}`;
  if (n.onSpur) h += `<p class="muted">On the Kebnekaise approach · ${n.approach}.</p>`;
  if (n.off) h += `<div class="warn">⚠ You seem to be more than 150 m off the trail line.</div>`;
  if (n.arrived) h += `<div class="warn" style="background:#e6f2ea;color:#1d5c39">✓ At ${n.next} — end of your section.</div>`;
  h += `<div class="tiles">
    <div class="tile"><div class="tv">${n.altM != null ? n.altM : '—'}</div><div class="tl">alt m</div></div>
    <div class="tile"><div class="tv">${spd}</div><div class="tl">speed</div></div>
    <div class="tile"><div class="tv">${acc}</div><div class="tl">GPS acc</div></div>
  </div>${tripMap()}
  <button class="btn sec" id="refresh" style="width:100%;margin-top:12px"${locating ? ' disabled' : ''}>${locating ? 'Locating…' : '⟳ Update position'}</button>
  <p class="muted">Updated ${agoStr(lastFixMs)} · schematic map (line + huts only) — use a topo app for terrain. GPS is off between checks.</p></div>`;
  el.innerHTML = h;
  wireRefresh();
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
  const depart = (D.info && D.info.depart) || {};
  const legs = [];
  let from = codeName(S.start);
  if (S.start >= 1000) {                       // spur approach legs first
    for (let j = S.start - 1000; j >= 0; j--) {
      const s = D.spur.stops[j];
      const to = j > 0 ? D.spur.stops[j - 1].name : D.spur.junction;
      legs.push({ from, to, km: s.legKm, hrs: parseHrs(s.time), hrsHi: parseHrsHi(s.time), boat: false });
      from = to;
    }
  }
  // spur legs end at the junction hut; main walking resumes from there, not sSlot (= junction+1)
  const startK = S.start >= 1000 ? mainSlotOfCode(S.start) : sSlot;
  for (let k = startK + 1; k <= eSlot; k++) {
    const st = D.stages[D.hutNames[k]];
    const km = st ? st.km : (cumKm(k) - cumKm(k - 1));
    const asc = ascentBetween(D.huts[k - 1], D.huts[k]);
    const hrs = st ? parseHrs(st.time) : (km / 4.5 + asc / 600);
    const hrsHi = st ? parseHrsHi(st.time) : hrs;
    const boat = !!(st && st.boat);
    // "always take the bus": a scheduled leg boarded at its origin is a ride, not a walking day
    const transit = boat && !!depart[from];
    legs.push({ from, to: D.hutNames[k], km, hrs, hrsHi, boat, transit });
    from = D.hutNames[k];
  }
  const newDay = f => ({ from: f, to: f, km: 0, hrs: 0, hrsHi: 0, boat: false, walkBoat: false, n: 0, board: null });
  const days = []; let day = null;
  const tgt = (S.dayH || 6) + (S.dayFlex || 0);  // allow days to run up to target + flex before splitting
  for (const lg of legs) {
    if (!day) day = newDay(lg.from);
    else if (!lg.transit && day.n > 0 && day.hrs + lg.hrs > tgt) { days.push(day); day = newDay(day.to); }
    if (lg.transit && depart[lg.from])          // board the bus/boat this day (walking already done)
      day.board = Object.assign({ stage: 'transfer', hut: lg.from, walkLo: day.hrs, walkHi: day.hrsHi }, depart[lg.from]);
    day.boat = day.boat || lg.boat; day.to = lg.to; day.n++;
    if (lg.transit) continue;                   // ride adds no walking distance or time
    day.km += lg.km; day.hrs += lg.hrs; day.hrsHi += lg.hrsHi;
    if (lg.boat) day.walkBoat = true;           // a boat on a walking leg (e.g. Teusajaure crossing)
  }
  if (day && day.n > 0) days.push(day);
  const last = days[days.length - 1];           // finishing at a hut you bus out from
  if (last && !last.board && depart[last.to])
    last.board = Object.assign({ stage: 'exit', hut: last.to, walkLo: last.hrs, walkHi: last.hrsHi }, depart[last.to]);
  days.forEach(d => {                           // suggested start time for boat/bus days
    if (!d.board) return;
    const buf = (d.board.buffer || 15) / 60;
    if (d.board.walkHi < 0.25) { d.beBy = timeMinus(d.board.time, buf); return; }
    const startMin = toMin(d.board.time) - Math.round((d.board.walkHi + buf + 0.5) * 60);
    if (startMin < 5 * 60) d.infeasible = true;  // too much walking to still catch it that day
    else d.startSug = clock(startMin);
  });
  return days;
}
const DAY_START = '08:00';                       // assumed daily start when no service to catch
const parseHrs = t => { const m = /(\d+)/.exec(t || ''); return m ? +m[1] : 0; };
const parseHrsHi = t => { const m = /(\d+)\s*-\s*(\d+)/.exec(t || ''); return m ? +m[2] : parseHrs(t); };
const toMin = s => { const [h, m] = s.split(':').map(Number); return h * 60 + m; };
const clock = t => { t = ((t % 1440) + 1440) % 1440; return String(Math.floor(t / 60)).padStart(2, '0') + ':' + String(t % 60).padStart(2, '0'); };
const timeMinus = (hhmm, h) => clock(toMin(hhmm) - Math.round(h * 60));
const timePlus = (hhmm, h) => clock(toMin(hhmm) + Math.round(h * 60));

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
function dayNote(d) {
  let h = '';
  if (d.board) {
    const b = d.board, buf = (b.buffer || 15) / 60;
    const tail = b.stage === 'transfer' && b.arrive ? `, in ${d.to} ~${b.arrive}` : '';
    if (d.infeasible)
      h += `<div class="daynote sched"><b>⚠ Too far for the ${b.time} ${b.mode}</b> — ~${b.walkLo}–${b.walkHi} h to ${b.hut} won't fit before ${b.time}. Split this day (lower the target or flexibility) or overnight at ${b.hut}.</div>`;
    else if (b.stage === 'exit')
      h += `<div class="daynote sched"><b>🕗 Start by ~${d.startSug}</b> — reach ${b.hut} for the ${b.time} bus out (be at the stop by ~${timeMinus(b.time, buf)}).</div>`;
    else if (d.beBy)
      h += `<div class="daynote sched"><b>🚌 Catch the ${b.time} ${b.mode} at ${b.hut}</b> — be at the stop by ~${d.beBy}. ${b.label}${tail}.</div>`;
    else
      h += `<div class="daynote sched"><b>🕗 Start by ~${d.startSug}</b> — ~${b.walkLo}–${b.walkHi} h to ${b.hut} for the ${b.time} ${b.mode}. ${b.label}${tail}.</div>`;
  }
  if (d.walkBoat)
    h += `<div class="daynote sched">⚓ A boat crossing this day — ask the hut warden the evening before (warden shuttle or self-service rowboat).</div>`;
  return h;
}
function renderPlan() {
  const [sSlot, eSlot] = hikeBounds();
  const n = computeNow();
  let h = `<div class="card"><h2><span class="ic">🥾</span>This hike</h2>
    ${hikeActive() ? `<p class="prog"><b>${codeName(S.start)}</b> → <b>${codeName(S.end)}</b> · ≈ ${Math.round(n.totalKm)} km</p>` : ''}
    <div class="field"><label>Start</label><select id="selS">${hikeOptions(S.start)}</select></div>
    <div class="field"><label>End</label><select id="selE">${hikeOptions(S.end)}</select></div>
    <p class="muted"><a href="#" id="fullRoute">Use full route</a> · the plan uses only this section.</p></div>`;

  // day planner
  const days = planDays();
  const flexOpts = [0, 1, 2, 3].map(v =>
    `<option value="${v}"${(S.dayFlex || 0) === v ? ' selected' : ''}>${v ? '± ' + v + ' h' : 'Strict (±0 h)'}</option>`).join('');
  h += `<div class="card"><h2><span class="ic">📅</span>Day planner</h2>
    <div class="row2">
      <div class="field"><label>Target hours / day</label><input id="dayH" inputmode="numeric" value="${S.dayH}"></div>
      <div class="field"><label>Flexibility</label><select id="dayFlex">${flexOpts}</select></div>
    </div>`;
  if (!days.length) h += `<p class="muted">Set a Start &amp; End to plan daily stages.</p>`;
  days.forEach((d, i) => {
    const hLo = Math.round(d.hrs), hHi = Math.round(d.hrsHi);
    const hTxt = hHi > hLo ? `${hLo}–${hHi} h` : `~${hLo} h`;
    h += `<div class="day"><div class="daynum">${i + 1}</div><div class="dayb">
      <div class="dayr"><b>${d.from}</b> → <b>${d.to}</b></div>
      <div class="daym"><span class="m">${Math.round(d.km)} km</span><span class="m">${hTxt}</span>
      ${d.boat ? '<span class="m bt">⚓ boat</span>' : ''}</div>${dayNote(d)}</div></div>`;
  });
  if (days.length) h += `<p class="muted">${days.length} days at ~${S.dayH} h/day${S.dayFlex ? ` (up to ~${S.dayH + S.dayFlex} h)` : ''} · boat/bus days show a suggested start · STF stage figures.</p>`;
  h += `</div>`;

  // elevation
  h += `<div class="card"><h2><span class="ic">⛰️</span>Elevation profile</h2>${elevProfile()}</div>`;

  // route timeline
  h += `<div class="card"><h2><span class="ic">🗺️</span>Route</h2>`;
  const cur = n.hasFix && !n.onSpur && n.snap ? n.snap.cum / 1000 : -1;
  h += `<ul class="tl">`;
  const spurActive = hikeActive() && S.start >= 1000;
  const dispStart = spurActive ? mainSlotOfCode(S.start) : sSlot;   // include the junction hut (Singi)
  for (let k = 0; k < D.hutNames.length; k++) {
    const inHike = k >= dispStart && k <= eSlot;
    const st = D.stages[D.hutNames[k]];
    const done = cur >= 0 && cumKm(k) < cur;
    const cls = (!inHike ? 'dim' : '') + (done ? ' done' : '');
    const leg = st ? `${st.km} km · ${st.time}` : (k > 0 ? `${(cumKm(k) - cumKm(k - 1)).toFixed(1)} km` : 'start');
    const note = inHike && k < eSlot && D.notes && D.notes[D.hutNames[k]];  // leg-ahead note
    const tip = inHike && D.tips && D.tips[D.hutNames[k]];
    h += `<li class="${cls}"><span class="node">${k + 1}</span>
      <span class="name">${D.hutNames[k]}${chips(D.hutNames[k])}</span>
      <span class="leg">${leg} · ${Math.round(cumKm(k))} km${D.transport[D.hutNames[k]] ? ' · 🚌 ' + D.transport[D.hutNames[k]] : ''}</span>
      ${note ? `<span class="stagenote">ℹ️ ${note}</span>` : ''}
      ${tip ? `<span class="tip">💡 ${tip}</span>` : ''}</li>`;
    if (D.spur.afterHut === D.hutNames[k])
      D.spur.stops.forEach(s => {
        const snote = spurActive && D.notes && D.notes[s.name];
        h += `<li class="spur${spurActive ? '' : ' dim'}"><span class="node"></span><span class="name">${s.name}${chips(s.name)}</span>
          <span class="leg">${s.legKm} km · ${s.time}${s.note ? ' · ' + s.note : ''}${D.transport[s.name] ? ' · 🚌 ' + D.transport[s.name] : ''}</span>
          ${snote ? `<span class="stagenote">ℹ️ ${snote}</span>` : ''}</li>`;
      });
  }
  h += `</ul></div>`;
  document.getElementById('p-plan').innerHTML = h;

  document.getElementById('selS').onchange = e => { S.start = +e.target.value; save(); renderAll(); };
  document.getElementById('selE').onchange = e => { S.end = +e.target.value; save(); renderAll(); };
  document.getElementById('dayH').onchange = e => { S.dayH = Math.max(2, Math.min(16, +e.target.value || 6)); save(); renderAll(); };
  document.getElementById('dayFlex').onchange = e => { S.dayFlex = +e.target.value || 0; save(); renderAll(); };
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
  <div class="card"><h2><span class="ic">🧪</span>Test mode</h2>
    <label class="toggle"><input type="checkbox" id="sim" ${S.sim ? 'checked' : ''}>
    Simulate a position on the route</label>
    <div id="simctl" style="${S.sim ? '' : 'display:none'};margin-top:10px">
      <label>Drag along the trip · <b id="simPct">${Math.round(S.simF * 100)}%</b></label>
      <input type="range" id="simSlider" min="0" max="100" value="${Math.round(S.simF * 100)}">
    </div>
    <p class="muted">Off the trail (e.g. in town), your real GPS isn't on the route. Turn this on
    to drop a fake hiker anywhere along your hike and watch the Now tab react. Turn it off before
    you actually head out.</p></div>
  ${D.info ? `<div class="card"><h2><span class="ic">🛟</span>Season &amp; safety</h2>
    <p class="isec"><b>Season</b> ${D.info.season}</p>
    <p class="isec"><b>Emergency</b> ${D.info.emergency}</p>
    <p class="isec"><b>Markings &amp; boats</b> ${D.info.marking}</p>
    ${D.info.boatShortcut ? `<p class="isec"><b>Nikkaluokta boat</b> ${D.info.boatShortcut}</p>` : ''}
    ${D.info.lastLeg ? `<p class="isec"><b>Last leg: bus + boat</b> ${D.info.lastLeg}</p>` : ''}</div>` : ''}
  <div class="card"><h2><span class="ic">📡</span>Status</h2><div class="grid" id="stat"></div></div>`;
  document.getElementById('awake').onchange = e => { S.awake = e.target.checked; save(); applyWakeLock(); };
  const sim = document.getElementById('sim'), ctl = document.getElementById('simctl');
  sim.onchange = e => {
    S.sim = e.target.checked; save();
    ctl.style.display = S.sim ? '' : 'none';
    if (S.sim) applySim(); else getFix();
  };
  document.getElementById('simSlider').oninput = e => {
    S.simF = e.target.value / 100; save();
    document.getElementById('simPct').textContent = e.target.value + '%';
    applySim();
  };
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
function getFix(force) {
  if (S.sim) { applySim(); return; }
  if (locating) return;
  if (!('geolocation' in navigator)) { setGps('off', 'no GPS'); return; }
  locating = true; setGps('', 'locating…'); renderNow();
  navigator.geolocation.getCurrentPosition(p => {
    pos = { lat: p.coords.latitude, lon: p.coords.longitude, acc: p.coords.accuracy, spd: p.coords.speed };
    lastFixMs = Date.now(); locating = false;
    setGps('ok', pos.acc ? `±${Math.round(pos.acc)} m` : 'fix');
    renderNow(); updateStatus();
  }, err => {
    locating = false;
    setGps('off', err.code === 1 ? 'permission denied' : (err.code === 3 ? 'timed out' : 'no GPS'));
    renderNow();
  }, { enableHighAccuracy: true, timeout: 30000, maximumAge: force ? 0 : 20000 });
}
document.querySelector('header .gps').onclick = () => getFix(true);   // tap status to refresh
// Refresh when reopened if the last fix is stale; never poll in the background.
document.addEventListener('visibilitychange', () => {
  if (!S.sim && document.visibilityState === 'visible' && Date.now() - lastFixMs > 120000) getFix();
});
if (S.sim) applySim(); else getFix();   // one fix on open

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
