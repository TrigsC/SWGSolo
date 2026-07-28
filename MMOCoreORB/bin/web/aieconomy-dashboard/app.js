/* ============================================================
   AI ECONOMY COMMAND - single-file SPA
   Served by RESTServer.serveDashboardRequest (only index.html,
   styles.css, app.js are whitelisted - everything lives here).
   Data: GET /v1/aieconomy/dashboard/  (bearer token)
         GET /v1/admin/stats/, /v1/version/ (Mainframe page)
   ============================================================ */
(() => {
  "use strict";

  /* ---------------- state ---------------- */

  const state = {
    token: localStorage.getItem("core3_api_token") || "",
    data: null,            // last dashboard snapshot
    stats: null,           // last /v1/admin/stats payload
    version: null,         // /v1/version payload
    lastOkMs: 0,           // last successful dashboard fetch
    lastErr: "",
    fetching: false,
    failures: 0,
    backoffUntil: 0,
    pollMs: 5000,
    statsPollMs: 30000,
    lastStatsMs: 0,
    mapZone: localStorage.getItem("core3_cmd_zone") || "",
    route: (location.hash || "#/command").replace(/^#\//, "")
  };

  /* ---------------- history (trend buffer) ---------------- */

  const HIST_KEY = "core3_cmd_hist_v1";
  const HIST_MIN_GAP_MS = 15000;
  const HIST_MAX = 960;
  let hist = [];
  try { hist = JSON.parse(localStorage.getItem(HIST_KEY) || "[]"); } catch (e) { hist = []; }
  if (!Array.isArray(hist)) hist = [];
  let histDirty = 0;

  function histPush(d) {
    const now = Date.now();
    const last = hist[hist.length - 1];
    if (last && now - last.t < HIST_MIN_GAP_MS) return;
    const acq = d.simulatedAcquisition || {};
    const ma = d.minerActivity || {};
    const pvp = d.pvpActivity || {};
    hist.push({
      t: now,
      qty: Number(acq.resourcesAcquired || 0),
      acq: Number(acq.acquisitions || 0),
      st: Number(ma.stationed || 0),
      mv: Number(ma.moving || 0),
      eng: Number(pvp.playerEngagementsTotal || 0) + Number(pvp.botEngagementsTotal || 0),
      dth: Number(pvp.deathsTotal || 0),
      bat: Number((d.hiveCrafters || {}).batchesCompleted || 0)
    });
    if (hist.length > HIST_MAX) hist = hist.slice(hist.length - HIST_MAX);
    if (++histDirty >= 8) {
      histDirty = 0;
      try { localStorage.setItem(HIST_KEY, JSON.stringify(hist)); } catch (e) { /* quota */ }
    }
  }

  /* ---------------- utils ---------------- */

  const $ = (id) => document.getElementById(id);

  const esc = (v) => String(v == null ? "" : v)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");

  const nf = new Intl.NumberFormat();
  const num = (v) => nf.format(Math.round(Number(v || 0)));

  function compact(v) {
    const n = Number(v || 0);
    if (Math.abs(n) >= 1e6) return (n / 1e6).toFixed(n % 1e6 === 0 ? 0 : 1) + "M";
    if (Math.abs(n) >= 1e4) return (n / 1e3).toFixed(n % 1e3 === 0 ? 0 : 1) + "k";
    return nf.format(Math.round(n));
  }

  const labelize = (v) => String(v == null || v === "" ? "none" : v)
    .replace(/_/g, " ")
    .replace(/([a-z])([A-Z])/g, "$1 $2")
    .toLowerCase()
    .replace(/\b\w/g, (c) => c.toUpperCase());

  function ago(sec) {
    const s = Math.max(0, Math.round(Number(sec || 0)));
    if (s < 90) return s + "s";
    if (s < 5400) return Math.round(s / 60) + "m";
    if (s < 172800) return (s / 3600).toFixed(1) + "h";
    return Math.round(s / 86400) + "d";
  }

  function hms(totalSec) {
    const s = Math.max(0, Math.floor(totalSec));
    const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), ss = s % 60;
    return String(h).padStart(2, "0") + ":" + String(m).padStart(2, "0") + ":" + String(ss).padStart(2, "0");
  }

  const clamp01 = (x) => Math.max(0, Math.min(1, Number(x) || 0));

  const onoff = (b) => b ? "ON" : "OFF";
  const yesno = (b) => b ? "YES" : "NO";

  /* ---------------- generic components ---------------- */

  function panel(title, tag, body, span) {
    return `<section class="panel ${span || "span-12"}">
      <header><h3>${esc(title)}</h3><span class="panel-tag">${tag || ""}</span></header>
      <div class="panel-body">${body}</div>
    </section>`;
  }

  function metric(label, value, opts = {}) {
    return `<div class="metric ${opts.tone || ""}">
      <span class="m-label">${esc(label)}</span>
      <span class="m-value">${opts.raw ? value : compact(value)}</span>
      ${opts.sub ? `<span class="m-sub">${opts.sub}</span>` : ""}
    </div>`;
  }

  function chip(text, tone) {
    return `<span class="chip ${tone || ""}">${esc(text)}</span>`;
  }

  function kvRows(pairs) {
    return `<div class="kv">` + pairs.map(([k, v, tone]) =>
      `<div class="kv-row"><span class="k">${esc(k)}</span><span class="v ${tone || ""}">${v}</span></div>`
    ).join("") + `</div>`;
  }

  function bars(rows, opts = {}) {
    const vals = rows.map((r) => Number(r.value || 0));
    const max = Math.max(1, ...vals);
    return `<div class="bars">` + rows.map((r) => {
      const w = Math.max(1, Math.round((Number(r.value || 0) / max) * 100));
      return `<div class="bar-row">
        <span class="b-label" title="${esc(r.label)}">${esc(r.label)}</span>
        <div class="bar-track"><div class="bar-fill ${r.tone || opts.tone || ""}" style="width:${w}%"></div></div>
        <span class="b-value">${compact(r.value)}</span>
      </div>`;
    }).join("") + `</div>`;
  }

  function sparkline(series, opts = {}) {
    if (!series || series.length < 2) {
      return `<div class="chart-note">COLLECTING TREND DATA&hellip;</div>`;
    }
    const w = 300, h = 46, pad = 2;
    const vals = series.map((p) => p.v);
    let min = Math.min(...vals), max = Math.max(...vals);
    if (max === min) { max += 1; min -= 1; }
    const pts = series.map((p, i) => {
      const x = pad + (i / (series.length - 1)) * (w - pad * 2);
      const y = h - pad - ((p.v - min) / (max - min)) * (h - pad * 2);
      return x.toFixed(1) + "," + y.toFixed(1);
    });
    const fillPts = `${pad},${h - pad} ` + pts.join(" ") + ` ${w - pad},${h - pad}`;
    const spanMin = Math.round((series[series.length - 1].t - series[0].t) / 60000);
    return `<svg class="spark" viewBox="0 0 ${w} ${h}" preserveAspectRatio="none">
        <polygon class="spark-fill" points="${fillPts}"></polygon>
        <polyline points="${pts.join(" ")}" style="stroke:${opts.color || "var(--amber)"}"></polyline>
      </svg>
      <div class="chart-note">${esc(opts.note || "")} · LAST ${spanMin}MIN · MIN ${compact(min)} / MAX ${compact(max)}</div>`;
  }

  function histSeries(key, { delta = false, tail = 240 } = {}) {
    const rows = hist.slice(-tail);
    if (delta) {
      const out = [];
      for (let i = 1; i < rows.length; i++) {
        out.push({ t: rows[i].t, v: Math.max(0, rows[i][key] - rows[i - 1][key]) });
      }
      return out;
    }
    return rows.map((r) => ({ t: r.t, v: r[key] }));
  }

  function emptyRow(cols, text) {
    return `<tr class="empty-row"><td colspan="${cols}">${esc(text || "No records")}</td></tr>`;
  }

  function table(headers, rowsHtml, opts = {}) {
    return `<div class="tbl-wrap ${opts.tall ? "tall" : ""}" data-scroll="${esc(opts.scrollKey || headers.join("-"))}">
      <table class="tbl">
        <thead><tr>${headers.map((h) => `<th>${esc(h)}</th>`).join("")}</tr></thead>
        <tbody>${rowsHtml || emptyRow(headers.length)}</tbody>
      </table>
    </div>`;
  }

  // Resilient auto-table: renders array-of-objects by key union (schema-drift safe).
  function autoTable(rows, opts = {}) {
    const list = Array.isArray(rows) ? rows.slice(0, opts.maxRows || 20) : [];
    if (!list.length) return table(["No Data"], emptyRow(1, opts.empty || "No rows"), opts);
    const keys = [];
    for (const r of list) {
      for (const k of Object.keys(r)) {
        if (!keys.includes(k) && typeof r[k] !== "object") keys.push(k);
      }
    }
    const cols = (opts.cols || keys).slice(0, opts.maxCols || 9);
    const body = list.map((r) => `<tr>${cols.map((k) => {
      const v = r[k];
      if (typeof v === "number") return `<td class="t-num">${num(v)}</td>`;
      if (typeof v === "boolean") return `<td>${chip(yesno(v), v ? "ok" : "ghost")}</td>`;
      const s = String(v == null ? "" : v);
      return `<td title="${esc(s)}">${esc(s.length > 42 ? s.slice(0, 40) + "…" : s)}</td>`;
    }).join("")}</tr>`).join("");
    return table(cols.map(labelize), body, opts);
  }

  function autoKV(obj, opts = {}) {
    const skip = new Set(opts.skip || []);
    const pairs = [];
    for (const [k, v] of Object.entries(obj || {})) {
      if (skip.has(k) || v == null || typeof v === "object") continue;
      if (typeof v === "boolean") pairs.push([labelize(k), yesno(v), v ? (opts.boolTone || "") : ""]);
      else if (typeof v === "number") pairs.push([labelize(k), num(v)]);
      else pairs.push([labelize(k), esc(String(v).slice(0, 60))]);
      if (pairs.length >= (opts.max || 24)) break;
    }
    return kvRows(pairs);
  }

  function alertRow(tone, title, body) {
    return `<div class="alert ${tone}"><span class="a-title">${esc(title)}</span><span class="a-body">${body}</span></div>`;
  }

  /* ---------------- zone map ---------------- */

  // SWG zones span roughly -8192..8192 on X (east) and Y (north).
  function zoneMapSVG(zone, dots) {
    const S = 320, HALF = 8192;
    const px = (x) => ((Number(x) + HALF) / (HALF * 2)) * S;
    const py = (y) => S - ((Number(y) + HALF) / (HALF * 2)) * S;
    let inner = "";
    // faint range rings + crosshair
    inner += `<line x1="${S / 2}" y1="0" x2="${S / 2}" y2="${S}" stroke="#182230" stroke-width="1"/>`;
    inner += `<line x1="0" y1="${S / 2}" x2="${S}" y2="${S / 2}" stroke="#182230" stroke-width="1"/>`;
    for (const r of [S * 0.17, S * 0.34]) {
      inner += `<circle cx="${S / 2}" cy="${S / 2}" r="${r}" fill="none" stroke="#141d29" stroke-width="1"/>`;
    }
    for (const d of dots) {
      const mx = px(d.mx), my = py(d.my);
      if (d.tx != null) {
        const tx = px(d.tx), ty = py(d.ty);
        inner += `<line x1="${mx.toFixed(1)}" y1="${my.toFixed(1)}" x2="${tx.toFixed(1)}" y2="${ty.toFixed(1)}"
          stroke="rgba(242,163,60,0.35)" stroke-width="1" stroke-dasharray="3 3"/>`;
        inner += `<circle cx="${tx.toFixed(1)}" cy="${ty.toFixed(1)}" r="4" fill="none" stroke="#4fd1c5" stroke-width="1.3"/>`;
      }
      const tone = d.stuck ? "#e5484d" : "#f2a33c";
      inner += `<circle cx="${mx.toFixed(1)}" cy="${my.toFixed(1)}" r="3" fill="${tone}">
        <title>${esc(d.label || "")}</title></circle>`;
    }
    inner += `<text x="6" y="14" fill="#4b5661" font-size="9" font-family="monospace" letter-spacing="2">${esc(String(zone).toUpperCase())}</text>`;
    return `<svg class="zone-map" viewBox="0 0 ${S} ${S}">${inner}</svg>`;
  }

  /* ---------------- data fetch ---------------- */

  async function apiGet(path, timeoutMs) {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), timeoutMs || 9000);
    try {
      const res = await fetch(path, {
        headers: { "Authorization": "Bearer " + state.token },
        signal: ctrl.signal,
        cache: "no-store"
      });
      if (!res.ok) throw new Error("HTTP " + res.status);
      return await res.json();
    } finally {
      clearTimeout(timer);
    }
  }

  async function doPoll() {
    if (state.fetching || document.hidden) return;
    if (!state.token) { setLink("down", "NO TOKEN"); return; }
    if (Date.now() < state.backoffUntil) return;
    state.fetching = true;
    try {
      const d = await apiGet("/v1/aieconomy/dashboard/");
      // REST envelope: payload sections live under .result ({debug,result,status,...})
      state.data = (d && d.result) || d;
      state.lastOkMs = Date.now();
      state.failures = 0;
      state.lastErr = "";
      histPush(state.data);
      render();
    } catch (e) {
      state.failures++;
      state.lastErr = e && e.name === "AbortError" ? "TIMEOUT" : String(e && e.message || e);
      const backoff = Math.min(60000, 10000 * Math.pow(2, Math.min(3, state.failures - 1)));
      state.backoffUntil = Date.now() + backoff;
      if (!state.data) render(); // keep boot screen informative
    } finally {
      state.fetching = false;
      updateLink();
    }

    // Secondary, cheaper feeds for the Mainframe page.
    if (Date.now() - state.lastStatsMs > state.statsPollMs) {
      state.lastStatsMs = Date.now();
      apiGet("/v1/admin/stats/", 6000).then((s) => { state.stats = s; if (state.route === "mainframe") render(); }).catch(() => {});
      if (!state.version) {
        apiGet("/v1/version/", 6000).then((v) => { state.version = v; }).catch(() => {});
      }
    }
  }

  let lastPollAttempt = 0;
  function poll() {
    const now = Date.now();
    if (state.data && now - lastPollAttempt < state.pollMs) return;
    lastPollAttempt = now;
    doPoll();
  }

  function setLink(mode, text, age) {
    const dot = $("link-dot"), txt = $("link-text"), da = $("data-age");
    if (!dot) return;
    dot.className = "link-dot " + (mode === "up" ? "up" : mode === "stale" ? "stale" : "down");
    txt.textContent = text;
    da.textContent = age || "";
  }

  function updateLink() {
    if (!state.token) return setLink("down", "NO TOKEN");
    if (!state.lastOkMs) return setLink("down", state.lastErr ? "LINK DOWN" : "CONNECTING", state.lastErr);
    const ageS = (Date.now() - state.lastOkMs) / 1000;
    if (ageS < 15) setLink("up", "UPLINK LIVE", "T-" + Math.round(ageS) + "s");
    else if (ageS < 60) setLink("stale", "UPLINK SLOW", "T-" + Math.round(ageS) + "s");
    else setLink("down", "LINK DOWN", "T-" + ago(ageS) + (state.lastErr ? " · " + state.lastErr : ""));
  }

  /* ================= PAGES ================= */

  /* ---------------- COMMAND (overview) ---------------- */

  function pageCommand(d) {
    const pop = d.population || {};
    const ai = d.aiPopulation || {};
    const ma = d.minerActivity || {};
    const acq = d.simulatedAcquisition || {};
    const rec = d.minerRecovery || {};
    const pvp = d.pvpActivity || {};
    const hc = d.hiveCrafters || {};
    const fg = d.finishedGoods || {};
    const audit = d.economyDecisionAudit || {};
    const safety = d.safetyBoundaries || {};
    const dispatch = d.planetDispatch || {};
    const demand = (d.demand || {}).profiles || [];

    // ------- alerts -------
    const alerts = [];
    const dataAgeS = state.lastOkMs ? (Date.now() - state.lastOkMs) / 1000 : 1e9;
    if (dataAgeS > 60) alerts.push(alertRow("red", "STALE FEED", `No telemetry for ${ago(dataAgeS)} - server may be down or REST wedged.`));
    if (ma.emergencyDisabled) alerts.push(alertRow("red", "EMERGENCY STOP", "Intelligent targeting emergency-disabled. Miner activations halted."));
    const unsafe = ["realResourceCreation", "resourceContainerCreation", "marketMutation", "inventoryMutation"]
      .filter((k) => String(safety[k] || "no").toLowerCase() === "yes");
    if (unsafe.length) alerts.push(alertRow("red", "SIM BREACH", "Safety boundary reports live mutation: " + unsafe.map(labelize).join(", ")));
    if (Number(rec.needsAttention || 0) > 0) alerts.push(alertRow("amber", "RECOVERY", `${num(rec.needsAttention)} miner(s) flagged for attention · ${num(rec.actionsTaken)} recovery actions taken so far.`));
    if (Number(ma.pathFailures || 0) > 0) alerts.push(alertRow("amber", "PATHING", `${num(ma.pathFailures)} path failures recorded this session.`));
    if (!alerts.length) alerts.push(alertRow("green", "ALL CLEAR", "No anomalies. The hive is operating within normal parameters."));

    // ------- briefing -------
    const stationedShare = Number(ai.miners || pop.activeMiners || 0) > 0
      ? Math.round((Number(ma.stationed || 0) / Number(ai.miners || pop.activeMiners)) * 100) : 0;
    const topPlanet = [...(acq.quantityByPlanet || [])].sort((a, b) => b.quantity - a.quantity)[0];
    const topRes = [...(acq.exactResourceTotals || [])].sort((a, b) => b.quantity - a.quantity)[0];
    const briefing = `
      <p class="briefing">
        <strong>${num(pop.totalControllers || ai.total)}</strong> AI operatives on the grid -
        <strong>${num(pop.activeMiners || ai.miners)}</strong> extraction units
        (${num(ma.stationed)} stationed · ${num(ma.moving)} in transit · ${num(ma.sampling)} sampling, ${stationedShare}% station rate)
        and <strong>${num(pop.activePvpBots || ai.pvp)}</strong> combat operatives.
        Session haul: <strong>${compact(acq.resourcesAcquired)}</strong> units over
        <strong>${num(acq.acquisitions)}</strong> simulated acquisitions across
        <strong>${num(acq.uniqueResources)}</strong> distinct resources${topPlanet ? `, led by <strong>${esc(labelize(topPlanet.planet))}</strong>` : ""}${topRes ? ` - top take <strong>${esc(topRes.exactResource)}</strong> (${compact(topRes.quantity)})` : ""}.
        Crafting hive: <strong>${num(hc.batchesCompleted)}</strong> batches, <strong>${compact(fg.totalQuantity)}</strong> finished goods on hand.
        ${dispatch.enabled ? `Cross-planet dispatch <strong>${dispatch.dryRun ? "ARMED (dry-run)" : "ACTIVE"}</strong> - ${num(dispatch.boarded)} boardings.` : ""}
        <span class="b-dim">Audit verdict: ${esc(labelize(audit.status || "no_data"))} - ${esc(audit.summary || "no summary")}</span>
      </p>`;

    // ------- galaxy heat -------
    const zoneRows = [...(ai.byZone || [])];
    const dispatchByPlanet = {};
    for (const p of (dispatch.byPlanet || [])) dispatchByPlanet[p.zone] = p;
    for (const p of (dispatch.byPlanet || [])) {
      if (!zoneRows.some((z) => z.zone === p.zone)) zoneRows.push({ zone: p.zone, activeMiners: p.current, pvp: 0, stationed: 0, moving: 0 });
    }
    const maxAct = Math.max(1, ...zoneRows.map((z) => Number(z.activeMiners || 0) + Number(z.pvp || 0)));
    const galaxy = `<div class="galaxy">` + zoneRows
      .sort((a, b) => (Number(b.activeMiners || 0) + Number(b.pvp || 0)) - (Number(a.activeMiners || 0) + Number(a.pvp || 0)))
      .map((z) => {
        const act = Number(z.activeMiners || 0) + Number(z.pvp || 0);
        const heat = clamp01(act / maxAct) * 0.32;
        const dis = dispatchByPlanet[z.zone];
        return `<div class="planet-tile">
          <div class="heat" style="opacity:${heat.toFixed(2)}"></div>
          ${dis && dis.home ? `<span class="p-flag">${chip("HOME", "teal")}</span>` : ""}
          <div class="p-name">${esc(z.zone || "?")}</div>
          <div class="p-stats">
            <span class="p-stat">MINERS <b>${num(z.activeMiners)}</b></span>
            <span class="p-stat">COMBAT <b>${num(z.pvp)}</b></span>
            <span class="p-stat">STATIONED <b>${num(z.stationed)}</b></span>
            ${dis ? `<span class="p-stat">QUOTA <b>${num(dis.current)}/${num(dis.desired)}</b></span>` : ""}
            ${dis && Number(dis.demandWeight || 0) ? `<span class="p-stat">DEMAND <b>${compact(dis.demandWeight)}</b></span>` : ""}
          </div>
        </div>`;
      }).join("") + `</div>`;

    // ------- demand pressure -------
    const demandBars = bars(
      [...demand].sort((a, b) => Number(b.pressureScore || 0) - Number(a.pressureScore || 0)).slice(0, 8)
        .map((p) => ({
          label: labelize(p.profile),
          value: p.pressureScore,
          tone: p.stateGroup === "shortage" ? "red" : p.stateGroup === "surplus" ? "teal" : ""
        }))
    );

    // ------- audit table -------
    const auditRows = (audit.profileAudit || []).map((r) => `<tr>
      <td><span class="t-main">${esc(labelize(r.profile))}</span><span class="t-sub">pressure ${num(r.pressureScore)}</span></td>
      <td>${chip(labelize(r.demandState), r.demandState === "shortage" ? "red" : "blue")}</td>
      <td class="t-num">${num(r.coveredOpportunities)} / ${num(Number(r.coveredOpportunities || 0) + Number(r.uncoveredOpportunities || 0))}</td>
      <td class="t-num">${compact(r.recentYieldQuantity)}</td>
      <td><span class="t-main">${esc(labelize(r.status))}</span><span class="t-sub">${esc(r.reason || "")}</span></td>
    </tr>`).join("");

    return `
      <div class="page-title"><h2>COMMAND</h2><span class="page-sub">daily briefing - the whole operation at a glance</span></div>
      <div class="grid">
        ${panel("SITUATION BRIEFING", (d.metadata || {}).asOfTime || "", briefing)}
        ${panel("ALERTS", `${alerts.length} channel(s)`, `<div class="alerts">${alerts.join("")}</div>`)}
        <section class="panel span-12"><header><h3>FORCE DISPOSITION</h3><span class="panel-tag">population &amp; output</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("AI Total", pop.totalControllers || ai.total, { tone: "accent" })}
            ${metric("Miners", pop.activeMiners || ai.miners)}
            ${metric("Stationed", ma.stationed, { tone: "good" })}
            ${metric("In Transit", ma.moving)}
            ${metric("PvP Units", pop.activePvpBots || ai.pvp)}
            ${metric("Acquisitions", acq.acquisitions)}
            ${metric("Units Gathered", acq.resourcesAcquired, { tone: "accent" })}
            ${metric("Unique Resources", acq.uniqueResources)}
            ${metric("Craft Batches", hc.batchesCompleted)}
            ${metric("Finished Goods", fg.totalQuantity)}
          </div></div>
        </section>
        ${panel("EXTRACTION TREND", "units gathered / interval", sparkline(histSeries("qty", { delta: true }), { note: "GATHER RATE" }), "span-6")}
        ${panel("STATIONED MINERS", "count over time", sparkline(histSeries("st"), { note: "STATIONED", color: "var(--teal)" }), "span-6")}
        ${panel("GALACTIC HEAT MAP", "activity by planet", galaxy)}
        ${panel("DEMAND PRESSURE", "top profiles", demandBars, "span-6")}
        <section class="panel span-6"><header><h3>SAFETY BOUNDARIES</h3><span class="panel-tag">${unsafe.length ? "REVIEW" : "sim-only holding"}</span></header>
          <div class="panel-body">${kvRows([
            ["Real resource creation", chip(String(safety.realResourceCreation || "no").toUpperCase(), unsafe.includes("realResourceCreation") ? "red" : "ok")],
            ["ResourceContainer creation", chip(String(safety.resourceContainerCreation || "no").toUpperCase(), unsafe.includes("resourceContainerCreation") ? "red" : "ok")],
            ["Market mutation", chip(String(safety.marketMutation || "no").toUpperCase(), unsafe.includes("marketMutation") ? "red" : "ok")],
            ["Inventory mutation", chip(String(safety.inventoryMutation || "no").toUpperCase(), unsafe.includes("inventoryMutation") ? "red" : "ok")],
            ["Audit recommendation", esc(labelize(audit.recommendation || "none"))],
            ["Audit mode", esc(audit.mode || "read-only")]
          ])}</div>
        </section>
        ${panel("PROFILE AUDIT", labelize(audit.status || "no data"),
          table(["Profile", "Demand", "Coverage", "Recent Yield", "Status"], auditRows, { scrollKey: "cmd-audit" }))}
      </div>
      <p class="footer-note">SIMULATION-ONLY CLEARANCE · NO REAL ECONOMY MUTATION · SNAPSHOT ${esc((d.metadata || {}).asOfTime || "n/a")}</p>`;
  }

  /* ---------------- EXTRACTION (miners) ---------------- */

  function pageExtraction(d) {
    const ma = d.minerActivity || {};
    const cp = d.coveragePlanner || {};
    const acq = d.simulatedAcquisition || {};
    const rec = d.minerRecovery || {};
    const ready = d.acquisitionReadiness || {};
    const pathd = d.pathValidationDiagnostics || {};

    const stateBars = bars([
      { label: "Stationed", value: ma.stationed, tone: "green" },
      { label: "Moving", value: ma.moving, tone: "" },
      { label: "Sampling", value: ma.sampling, tone: "teal" },
      { label: "Validated", value: ma.validated, tone: "blue" },
      { label: "Queued", value: ma.queued, tone: "dim" },
      { label: "Candidate", value: ma.candidate, tone: "dim" },
      { label: "Failed", value: ma.failed, tone: "red" },
      { label: "Expired", value: ma.expired, tone: "red" }
    ]);

    const assignments = (ma.assignments || []).map((a) => `<tr>
      <td class="t-num">#${esc(a.minerId)}<span class="t-sub">gen ${esc(a.assignmentGenerationId || "?")}</span></td>
      <td>${chip(labelize(a.lifecycleStatus || a.status), a.lifecycleStatus === "stationed" ? "ok" : a.lifecycleStatus === "failed" ? "red" : "amber")}</td>
      <td><span class="t-main">${esc(a.targetResource || "unknown")}</span><span class="t-sub">${esc(a.targetType || "")} · ${esc(a.targetZone || "")}</span></td>
      <td>${esc(labelize(a.profile))}</td>
      <td>${chip(labelize(a.activationPathTrustStatus || a.pathTrustStatus || "n/a"),
        /verified|direct_overland|directOverland/i.test(String(a.activationPathTrustStatus || a.pathTrustStatus)) ? "ok" : "ghost")}</td>
      <td class="t-num">${num(a.stationSampleCount)}<span class="t-sub">${compact(a.stationYieldQuantity)} yield</span></td>
      <td class="t-num">${ago(a.stationDurationSeconds)}</td>
    </tr>`).join("");

    // zone ops map from path validation rows
    const rows = pathd.rows || [];
    const zones = [...new Set(rows.map((r) => r.targetZone).filter(Boolean))].sort();
    if (!state.mapZone || !zones.includes(state.mapZone)) state.mapZone = zones[0] || "";
    const dots = rows
      .filter((r) => r.targetZone === state.mapZone && (r.minerPositionAvailable === undefined || r.minerPositionAvailable))
      .map((r) => ({
        mx: r.minerX, my: r.minerY,
        tx: r.targetX, ty: r.targetY,
        stuck: /fail|stuck/i.test(String(r.explanationKey || "")),
        label: `#${r.minerId} → ${r.targetResource || "?"} (${labelize(r.latestPathTrustStatus || "")})`
      }));
    const mapBlock = `<div class="zone-map-box">
        ${zoneMapSVG(state.mapZone || "no zone", dots)}
        <div class="zone-map-legend">
          <label class="small muted">SECTOR
            <select class="map-select" onchange="AECD.setMapZone(this.value)">
              ${zones.map((z) => `<option value="${esc(z)}" ${z === state.mapZone ? "selected" : ""}>${esc(z.toUpperCase())}</option>`).join("")}
            </select>
          </label>
          <span><span class="legend-dot" style="background:#f2a33c"></span>miner position</span>
          <span><span class="legend-dot" style="background:none;border:1px solid #4fd1c5"></span>assigned target</span>
          <span><span class="legend-dot" style="background:#e5484d"></span>pathing anomaly</span>
          <span class="muted small">${dots.length} tracked assignment(s) in sector · dashed = planned route (straight-line)</span>
        </div>
      </div>`;

    const profileRows = (cp.coverageByProfile || []).map((r) => `<tr>
      <td><span class="t-main">${esc(labelize(r.demandProfile))}</span><span class="t-sub">pressure ${num(r.pressureScore)}</span></td>
      <td class="t-num">${num(r.desiredMiners)}</td>
      <td class="t-num">${num(r.assignedMinerCount)}</td>
      <td class="t-num">${num(r.activeCoverageMinerCount)}<span class="t-sub">${num(r.stationedMinerCount)} stationed</span></td>
      <td>${chip(num(r.coverageGap), Number(r.coverageGap || 0) ? "amber" : "ok")}</td>
    </tr>`).join("");

    const slotRows = (cp.coverageSlots || []).slice(0, 16).map((r) => `<tr>
      <td><span class="t-main">${esc(r.resourceName || "unknown")}</span><span class="t-sub">${esc(r.resourceType || "")}</span></td>
      <td>${esc(labelize(r.demandProfile))}</td>
      <td>${esc(labelize(r.zone))}</td>
      <td>${chip(labelize(r.coverageState || "?"), /full/i.test(String(r.coverageState)) ? "ok" : /uncov/i.test(String(r.coverageState)) ? "red" : "amber")}</td>
      <td class="t-num">${compact(r.stockpileKnownQuantity)} / ${compact(r.desiredReserve)}</td>
      <td class="t-num">${num(r.stationedMinerCount)}st ${num(r.movingMinerCount)}mv</td>
    </tr>`).join("");

    const recRows = (rec.rows || [])
      .sort((a, b) => Number(Boolean(b.needsAttention)) - Number(Boolean(a.needsAttention)))
      .slice(0, 20).map((r) => `<tr>
        <td class="t-num">#${esc(r.minerId)}</td>
        <td>${chip(labelize(r.status), r.needsAttention ? "red" : r.healthy ? "ok" : "amber")}<span class="t-sub">${esc(labelize(r.lifecycleStatus))}</span></td>
        <td><span class="t-main">${esc(r.resourceName || "-")}</span><span class="t-sub">${esc(labelize(r.demandProfile))}</span></td>
        <td>${esc(labelize(r.currentZone))} → ${esc(labelize(r.targetZone))}</td>
        <td class="t-num">${num(r.distanceToTarget)}m</td>
        <td><span class="t-main">${esc(labelize(r.recoveryRecommendation))}</span><span class="t-sub">${esc(labelize(r.stuckReason))}</span></td>
      </tr>`).join("");

    const acqEvents = (acq.events || []).slice(0, 25).map((e) => `<tr>
      <td class="t-num">${ago(e.ageSeconds)}</td>
      <td class="t-num">#${esc(e.minerId)}</td>
      <td><span class="t-main">${esc(e.resourceName || "?")}</span><span class="t-sub">${esc(e.resourceType || "")}</span></td>
      <td>${esc(labelize(e.planet))}</td>
      <td class="t-num">${num(e.quantity)}</td>
      <td class="t-num">${num(e.density)}</td>
      <td>${esc(labelize(e.demandProfile))}</td>
    </tr>`).join("");

    const planetBars = bars([...(acq.quantityByPlanet || [])].sort((a, b) => b.quantity - a.quantity)
      .map((r) => ({ label: labelize(r.planet), value: r.quantity })));
    const typeBars = bars([...(acq.quantityByResourceType || [])].sort((a, b) => b.quantity - a.quantity).slice(0, 10)
      .map((r) => ({ label: r.resourceType, value: r.quantity, tone: "teal" })));

    const dur = cp.stationDurationSummary || {};
    const samp = cp.stationSampleSummary || {};

    return `
      <div class="page-title"><h2>EXTRACTION</h2><span class="page-sub">mining operations - assignments, coverage, recovery, haul</span></div>
      <div class="grid">
        <section class="panel span-12"><header><h3>OPERATIONAL STATUS</h3>
          <span class="panel-tag">${ma.intelligentTargetingEnabled ? "intelligent targeting " + esc(ma.mode || "") : "targeting off"}
          ${ma.emergencyDisabled ? " · EMERGENCY DISABLED" : ""}</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Active", ma.currentIntelligentActiveCount)}
            ${metric("Stationed", ma.stationed, { tone: "good" })}
            ${metric("Moving", ma.moving)}
            ${metric("Sampling", ma.sampling)}
            ${metric("Validated", ma.validated)}
            ${metric("Activation Fails", ma.activationFailures, { tone: Number(ma.activationFailures || 0) ? "warn" : "" })}
            ${metric("Path Fails", ma.pathFailures, { tone: Number(ma.pathFailures || 0) ? "warn" : "" })}
            ${metric("Cap", ma.maxActiveIntelligentMiners)}
          </div></div>
        </section>
        ${panel("STATE DISTRIBUTION", "live", stateBars, "span-5")}
        <section class="panel span-7"><header><h3>SECTOR OPERATIONS MAP</h3><span class="panel-tag">live assignment plot</span></header>
          <div class="panel-body">${mapBlock}</div></section>
        ${panel("LIVE ASSIGNMENTS", `${(ma.assignments || []).length} active`,
          table(["Miner", "Lifecycle", "Target Resource", "Profile", "Path Trust", "Samples", "On Station"], assignments, { scrollKey: "ext-assign" }))}
        ${panel("COVERAGE BY PROFILE", `${num(cp.desiredCoverageSlots)} slots · gap ${num(cp.totalCoverageGap)}`,
          table(["Profile", "Desired", "Assigned", "Active", "Gap"], profileRows, { scrollKey: "ext-covprof" }), "span-6")}
        <section class="panel span-6"><header><h3>STATION DISCIPLINE</h3><span class="panel-tag">sampling cadence</span></header>
          <div class="panel-body">${kvRows([
            ["Stationed miners", num(dur.stationedCount)],
            ["Avg station duration", ago(dur.averageStationDurationSeconds)],
            ["Max station duration", ago(dur.maxStationDurationSeconds)],
            ["Station sample ticks", num(cp.stationedSampleTicks || samp.stationedSampleTicks)],
            ["Station yield", compact(samp.stationYieldQuantity)],
            ["Sample interval", num(cp.sampleIntervalSeconds) + "s · " + esc(cp.sampleIntervalSource || "?")],
            ["Last stationed sample", ago(cp.lastStationedSampleAgeSeconds) + " ago"],
            ["Unassigned miners", num(cp.unassignedMiners)]
          ])}</div>
        </section>
        ${panel("COVERAGE SLOTS", "top 16 by planner order",
          table(["Resource", "Profile", "Zone", "State", "Reserve", "Miners"], slotRows, { scrollKey: "ext-slots" }))}
        <section class="panel span-6"><header><h3>RECOVERY WATCH</h3>
          <span class="panel-tag">${rec.enabled ? (rec.dryRun ? "dry-run" : "live · clear-only") : "disabled"} · ${num(rec.actionsTaken)} actions</span></header>
          <div class="panel-body flush">${table(["Miner", "Status", "Resource", "Route", "Dist", "Recommendation"], recRows, { scrollKey: "ext-rec" })}</div>
        </section>
        <section class="panel span-6"><header><h3>ACQUISITION READINESS</h3>
          <span class="panel-tag">real acquisition ${ready.realResourceAcquisitionEnabled ? "ON" : "OFF"}</span></header>
          <div class="panel-body">
            <div class="metric-row">
              ${metric("Stationed", ready.stationedMiners)}
              ${metric("Ready", ready.acquisitionReadyMiners, { tone: "good" })}
              ${metric("Blocked", ready.acquisitionBlockedMiners, { tone: Number(ready.acquisitionBlockedMiners || 0) ? "warn" : "" })}
            </div>
            <div class="section-gap"></div>
            ${kvRows((ready.acquisitionBlockedReasons || []).slice(0, 8).map((r) => [labelize(r.reason), num(r.count)]))}
          </div>
        </section>
        ${panel("HAUL BY PLANET", "simulated units", planetBars, "span-6")}
        ${panel("HAUL BY RESOURCE TYPE", "top 10", typeBars, "span-6")}
        ${panel("ACQUISITION LEDGER", `${num(acq.acquisitions)} total · avg ${num(acq.averageQuantity)}/tx`,
          table(["Age", "Miner", "Resource", "Planet", "Qty", "Density", "Profile"], acqEvents, { scrollKey: "ext-ledger", tall: true }))}
      </div>`;
  }

  /* ---------------- SUPPLY CHAIN (economy) ---------------- */

  function pageSupply(d) {
    const demand = d.demand || {};
    const supply = d.supply || {};
    const stock = d.stockpileInspection || {};
    const aware = d.resourceAwareStockpile || {};
    const hr = d.hiveReservations || {};
    const hc = d.hiveCrafters || {};
    const fg = d.finishedGoods || {};
    const scout = d.resourceScout || {};

    const demandRows = (demand.profiles || []).map((p) => {
      const opp = p.activeOpportunityResource || {};
      const ratio = clamp01(p.reserveRatio);
      return `<tr>
        <td><span class="t-main">${esc(labelize(p.profile))}</span></td>
        <td>${chip(labelize(p.stateGroup || p.state), p.stateGroup === "shortage" ? "red" : p.stateGroup === "surplus" ? "teal" : "blue")}</td>
        <td class="t-num">${compact(p.knownSupply)} / ${compact(p.desiredReserve)}
          <div class="bar-track" style="width:90px;margin-top:3px"><div class="bar-fill ${ratio >= 1 ? "green" : ratio > 0.5 ? "" : "red"}" style="width:${Math.round(ratio * 100)}%"></div></div></td>
        <td class="t-num">${num(p.pressureScore)}</td>
        <td class="t-num">${compact(p.shortageUnits)}</td>
        <td>${opp.available
          ? `<span class="t-main">${esc(opp.resourceName)}</span><span class="t-sub">${esc(opp.resourceType || "")} · ${esc(opp.zones || "")} · score ${num(opp.demandScore)}</span>`
          : `<span class="muted">${esc(labelize(opp.reason || "none"))}</span>`}</td>
      </tr>`;
    }).join("");

    const labelRows = [...(stock.labelSummaries || [])]
      .sort((a, b) => Number(b.totalKnownQuantity || 0) - Number(a.totalKnownQuantity || 0))
      .slice(0, 14).map((r) => `<tr>
        <td><span class="t-main">${esc(labelize(r.label))}</span><span class="t-sub">${esc(labelize(r.identityConfidence))}</span></td>
        <td class="t-num">${compact(r.currentSessionQuantity)}</td>
        <td class="t-num">${compact(r.persistedQuantity)}</td>
        <td class="t-num">${compact(r.totalKnownQuantity)}</td>
        <td><span class="t-sub">${esc(String(r.demandProfiles || "none").split(",").map(labelize).join(" · "))}</span></td>
      </tr>`).join("");

    const awareRows = [...(aware.rows || [])]
      .sort((a, b) => Number(b.quantity || 0) - Number(a.quantity || 0))
      .slice(0, 14).map((r) => `<tr>
        <td><span class="t-main">${esc(r.sourceResourceName || labelize(r.conceptualLabel))}</span><span class="t-sub">${esc(r.sourceResourceType || "")}</span></td>
        <td class="t-num">${compact(r.quantity)}</td>
        <td>${esc(labelize(r.sourcePlanet || r.sourceZone))}</td>
        <td>${esc(labelize(r.selectedProfile || r.selectedDemandProfile))}</td>
        <td class="t-num">${ago(r.lastObservedAgeSeconds)} ago</td>
      </tr>`).join("");

    const goodRows = (fg.lots || []).map((g) => `<tr>
      <td><span class="t-main">${esc(g.goodName || g.goodKey || "?")}</span><span class="t-sub">${esc(g.goodClassChain || "")}</span></td>
      <td>${esc(labelize(g.producingProfile))}</td>
      <td class="t-num">${num(g.quantity)}<span class="t-sub">${num(g.availableQuantity)} free</span></td>
      <td>${chip(labelize(g.qualityTier || "?"), /high|exceptional/i.test(String(g.qualityTier)) ? "ok" : "ghost")}<span class="t-sub">score ${num(g.qualityScore)}</span></td>
      <td class="t-num">${ago(g.lastCraftedAgeSeconds)} ago</td>
    </tr>`).join("");

    const recipeRows = (fg.recipes || []).map((r) => `<tr>
      <td>${esc(labelize(r.profile))}</td>
      <td class="t-num">${num(r.inputUnitsPerCraft)} in</td>
      <td class="t-num">${num(r.outputUnitsPerCraft)} out</td>
      <td class="t-num">${compact(r.finishedGoodTargetUnits)}</td>
    </tr>`).join("");

    const oppRows = [...(scout.demandOpportunities || [])]
      .sort((a, b) => Number((b.demand || {}).priority || 0) - Number((a.demand || {}).priority || 0))
      .slice(0, 12).map((r) => {
        const dem = r.demand || {};
        return `<tr>
          <td><span class="t-main">${esc(r.resourceName || "?")}</span><span class="t-sub">${esc(r.resourceType || "")} · ${esc(r.zones || r.planet || "")}</span></td>
          <td>${esc(labelize(dem.profile || r.bestUse))}</td>
          <td>${chip(labelize(dem.stateGroup || "target"), dem.stateGroup === "shortage" ? "red" : "blue")}</td>
          <td class="t-num">${num(dem.priority || r.score)}</td>
          <td class="t-num">${compact(dem.shortageUnits)}</td>
        </tr>`;
      }).join("");

    const producedBars = bars((hc.producedByProfile || []).map((r) => ({ label: labelize(r.profile), value: r.craftedUnits, tone: "teal" })));

    return `
      <div class="page-title"><h2>SUPPLY CHAIN</h2><span class="page-sub">demand, stockpiles, hive crafting, finished goods</span></div>
      <div class="grid">
        <section class="panel span-12"><header><h3>SUPPLY LEDGER</h3><span class="panel-tag">${esc(demand.serverPhase || "")} phase · ${esc(demand.supplyMode || "")}</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Session Gathered", supply.currentSessionConceptualTotalQuantity)}
            ${metric("Persistent Baseline", supply.persistentBaselineStockpileQuantity)}
            ${metric("Total Known", supply.totalKnownConceptualQuantity, { tone: "accent" })}
            ${metric("Stockpile Lots", stock.loadedLots)}
            ${metric("Available", stock.availableQuantity)}
            ${metric("Reserved", stock.reservedQuantity)}
            ${metric("Finished Goods", fg.totalQuantity, { tone: "good" })}
            ${metric("Craft Batches", hc.batchesCompleted)}
          </div></div>
        </section>
        ${panel("DEMAND BOARD", `${(demand.profiles || []).length} profiles`,
          table(["Profile", "State", "Reserve", "Pressure", "Shortage", "Active Opportunity"], demandRows, { scrollKey: "sup-demand", tall: true }))}
        ${panel("HIVE STOCKPILE - BY LABEL", `${labelize(stock.status || "?")} · persistence ${stock.persistenceReady ? "ready" : "off"}`,
          table(["Label", "Session", "Persisted", "Known", "Feeds Profiles"], labelRows, { scrollKey: "sup-labels" }), "span-6")}
        ${panel("RESOURCE-AWARE STOCKPILE", `${num(aware.rowCount)} rows · ${compact(aware.totalQuantity)} units`,
          table(["Source Resource", "Qty", "Planet", "Profile", "Seen"], awareRows, { scrollKey: "sup-aware" }), "span-6")}
        <section class="panel span-4"><header><h3>HIVE RESERVATIONS</h3><span class="panel-tag">${hr.apiReady ? "api ready" : "api pending"}</span></header>
          <div class="panel-body">${kvRows([
            ["Active reservations", num(hr.activeReservations)],
            ["Reserved quantity", compact(hr.reservedQuantity)],
            ["Granted", num(hr.granted)],
            ["Consumed", num(hr.consumed)],
            ["Released", num(hr.released)],
            ["Self-test", onoff(hr.selfTestEnabled)]
          ])}</div></section>
        <section class="panel span-4"><header><h3>HIVE CRAFTERS</h3><span class="panel-tag">${hc.enabled ? "enabled" : "disabled"} · every ${num(hc.intervalSeconds)}s</span></header>
          <div class="panel-body">${kvRows([
            ["Batches completed", num(hc.batchesCompleted)],
            ["Units consumed", compact(hc.unitsConsumed)],
            ["Units produced", compact(hc.unitsProduced)],
            ["Last profile", labelize(hc.lastProfile)],
            ["Last matched tier", labelize(hc.lastMatchedTier)],
            ["Fallback used", yesno(hc.fallbackUsed), hc.fallbackUsed ? "warn" : ""]
          ])}</div></section>
        ${panel("CRAFT OUTPUT BY PROFILE", "units", producedBars || "", "span-4")}
        ${panel("FINISHED GOODS", `${num(fg.goodLots)} lots · ${compact(fg.totalQuantity)} units`,
          table(["Good", "Profile", "Qty", "Quality", "Last Craft"], goodRows, { scrollKey: "sup-goods" }), "span-8")}
        ${panel("RECIPES", "per-profile io", table(["Profile", "Input", "Output", "Target"], recipeRows, { scrollKey: "sup-recipes" }), "span-4")}
        ${panel("SCOUT - TOP OPPORTUNITIES", `${num(scout.activeResourceCount)} active resources scanned`,
          table(["Resource", "Use", "State", "Score", "Shortage"], oppRows, { scrollKey: "sup-opps" }))}
      </div>`;
  }

  /* ---------------- TRANSIT (logistics) ---------------- */

  function pageTransit(d) {
    const st = d.stationTravel || {};
    const pd = d.planetDispatch || {};
    const ai = d.aiPopulation || {};
    const rush = d.resourceRush || {};
    const sim = d.travelPlanSimulation || {};
    const veh = d.vehicleMechanics || {};

    const byPlanet = (pd.byPlanet || []).map((p) => {
      const cur = Number(p.current || 0), des = Number(p.desired || 0);
      const max = Math.max(1, cur, des);
      return `<div class="bar-row">
        <span class="b-label">${esc((p.zone || "?").toUpperCase())}${p.home ? " ·H" : ""}</span>
        <div class="bar-track" style="height:14px">
          <div class="bar-fill ${cur >= des ? "green" : ""}" style="width:${Math.round((cur / max) * 100)}%"></div>
          <div style="position:absolute;top:-2px;bottom:-2px;left:${Math.round((des / max) * 100)}%;width:2px;background:var(--teal)"></div>
        </div>
        <span class="b-value">${cur}/${des}</span>
      </div>`;
    }).join("");

    const zoneRows = [...(ai.byZone || [])]
      .sort((a, b) => Number(b.activeMiners || 0) - Number(a.activeMiners || 0))
      .map((z) => `<tr>
        <td><span class="t-main">${esc(labelize(z.zone))}</span>${z.configuredMinerSpawnZone ? `<span class="t-sub">spawn zone</span>` : ""}</td>
        <td class="t-num">${num(z.activeMiners)}</td>
        <td class="t-num">${num(z.pvp)}</td>
        <td class="t-num">${num(z.stationed)} / ${num(z.moving)} / ${num(z.sampling)}</td>
        <td class="t-num">${num(z.idle)}</td>
        <td class="t-num">${num(z.blocked)}</td>
      </tr>`).join("");

    const plans = (sim.plans || []).slice(0, 15).map((p) => `<tr>
      <td>${chip(labelize(p.planType), p.planType === "hub_return" ? "blue" : "amber")}</td>
      <td class="t-num">#${esc(p.minerId || "-")}</td>
      <td><span class="t-sub">${esc(labelize(p.currentZone))} → ${esc(labelize(p.targetZone || p.targetZones))}</span></td>
      <td><span class="t-main">${esc(p.targetResource || p.targetHub || "?")}</span><span class="t-sub">${esc(labelize(p.selectedProfile || p.purpose))}</span></td>
      <td><span class="t-main">${esc(labelize(p.recommendedAction || "watch"))}</span><span class="t-sub">${esc(p.reason || "")}</span></td>
    </tr>`).join("");

    const topRemote = rush.topRemoteOpportunity || {};

    return `
      <div class="page-title"><h2>TRANSIT</h2><span class="page-sub">station travel, cross-planet dispatch, fleet distribution</span></div>
      <div class="grid">
        <section class="panel span-12"><header><h3>TRANSIT NETWORK</h3><span class="panel-tag">mechanism: ${esc(st.mechanism || "switchZone")}</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Station Travels", st.travels, { tone: "accent" })}
            ${metric("Avg Meters Saved", st.avgOverlandMetersSaved)}
            ${metric("Total Saved", st.totalOverlandMetersSaved)}
            ${metric("Dispatches", pd.dispatches)}
            ${metric("Boardings", pd.boarded)}
            ${metric("In Progress", pd.inProgress)}
            ${metric("Fleet Size", pd.totalMiners)}
          </div></div>
        </section>
        <section class="panel span-6"><header><h3>PLANET DISPATCH - QUOTAS</h3>
          <span class="panel-tag">${pd.enabled ? (pd.dryRun ? chip("DRY RUN", "amber") : chip("LIVE", "ok")) : chip("OFF", "ghost")} current vs <span style="color:var(--teal)">desired</span></span></header>
          <div class="panel-body"><div class="bars">${byPlanet || `<span class="muted small">No dispatch data</span>`}</div>
          <div class="section-gap"></div>
          ${kvRows([
            ["Last target zone", labelize(pd.lastTargetZone || "none")],
            ["Last donor", pd.lastDonorId ? "#" + esc(pd.lastDonorId) + " from " + esc(labelize(pd.lastDonorFromZone)) : "none"],
            ["Last boarding", pd.lastBoardedFromZone ? esc(labelize(pd.lastBoardedFromZone)) + " → " + esc(labelize(pd.lastBoardedToZone)) : "none"],
            ["Last skip reason", labelize(pd.lastSkipReason || "none")],
            ["Home floor / remote cap", num(pd.minMinersPerHomePlanet) + " / " + num(pd.maxMinersPerRemotePlanet)]
          ])}</div>
        </section>
        <section class="panel span-6"><header><h3>STATION TRAVEL</h3><span class="panel-tag">${st.enabled ? "enabled" : "disabled"} · same-planet ${yesno(st.samePlanetOnly)}</span></header>
          <div class="panel-body">${kvRows([
            ["Travels fired", num(st.travels)],
            ["Min saving to trigger", num(st.minSavingMeters) + "m"],
            ["Avg overland saved", num(st.avgOverlandMetersSaved) + "m"],
            ["Total overland saved", compact(st.totalOverlandMetersSaved) + "m"]
          ])}
          <div class="section-gap"></div>
          <header style="padding:10px 0 6px;border:none"><h3 style="color:var(--muted)">RESOURCE RUSH</h3></header>
          ${rush.topRemoteOpportunityAvailable ? kvRows([
            ["Top remote target", esc(topRemote.resourceName || "?")],
            ["Type", esc(topRemote.resourceType || "?")],
            ["Zone", labelize(topRemote.zone || topRemote.zones)],
            ["Demand score", num(topRemote.demandScore || topRemote.pressureScore)],
            ["Local miners there", num(topRemote.localMiners)],
            ["Remote high-priority", num(rush.remoteHighPriorityCount)]
          ]) : `<span class="muted small">${num(rush.localHighPriorityCount)} local / ${num(rush.remoteHighPriorityCount)} remote high-priority opportunities</span>`}</div>
        </section>
        ${panel("FLEET DISTRIBUTION", `${num(ai.total)} AI across ${(ai.byZone || []).length} zones`,
          table(["Zone", "Miners", "PvP", "St/Mv/Sm", "Idle", "Blocked"], zoneRows, { scrollKey: "tr-zones" }), "span-7")}
        <section class="panel span-5"><header><h3>VEHICLE MECHANICS</h3><span class="panel-tag">${veh.enabled ? "enabled" : chip("SHELVED", "ghost")}</span></header>
          <div class="panel-body">${kvRows([
            ["Deploys / Mounts", num(veh.deploys) + " / " + num(veh.mounts)],
            ["Dismounts / Stores", num(veh.dismounts) + " / " + num(veh.stores)],
            ["Failures", num(veh.failures), Number(veh.failures || 0) ? "bad" : ""],
            ["Active vehicles", num(veh.activeVehicles)],
            ["Self-test", onoff(veh.selfTestEnabled)]
          ])}
          <p class="muted small">NPC RIDER-slot mounting requires template arrangement descriptors - program shelved in favor of station travel.</p></div>
        </section>
        ${panel("TRAVEL PLAN SIMULATION", `${num(sim.totalPlans)} plans · travel ${sim.travelImplemented ? "implemented" : "not implemented"}`,
          table(["Type", "Miner", "Route", "Target", "Recommendation"], plans, { scrollKey: "tr-plans" }))}
      </div>`;
  }

  /* ---------------- WARFRONT (pvp) ---------------- */

  function pageWarfront(d) {
    const pvp = d.pvpActivity || {};
    const scouts = pvp.scouts || {};
    const comms = pvp.comms || {};
    const routed = pvp.routedTravel || {};
    const squads = pvp.squads || [];

    const factionCard = (name, cls) => {
      const list = squads.filter((s) => String(s.faction || "").toLowerCase() === name);
      const alive = list.reduce((a, s) => a + Number(s.membersAlive || 0), 0);
      const deaths = list.reduce((a, s) => a + Number(s.deaths || 0), 0);
      const eng = list.reduce((a, s) => a + Number(s.engagements || 0), 0);
      return `<div class="faction-card ${cls}">
        <h4>${name.toUpperCase()} COMMAND</h4>
        <div class="metric-row">
          ${metric("Squads", list.length)}
          ${metric("Operatives", alive)}
          ${metric("Engagements", eng)}
          ${metric("Losses", deaths, { tone: deaths ? "bad" : "" })}
        </div>
        <div class="section-gap"></div>
        ${kvRows(list.slice(0, 6).map((s) => [
          `Squad ${s.squadId} · ${labelize(s.planet || "?")}${s.city ? " / " + labelize(s.city) : ""}`,
          `${num(s.membersAlive)}/${num(s.desiredSize)} · ${esc(labelize(s.leaderPhase || "?"))}`
        ]))}
      </div>`;
    };

    const squadRows = squads.map((s) => `<tr>
      <td class="t-num">#${esc(s.squadId)}</td>
      <td>${chip(labelize(s.faction), String(s.faction).toLowerCase() === "rebel" ? "amber" : "blue")}</td>
      <td>${esc(labelize(s.planet))}<span class="t-sub">${esc(labelize(s.city || ""))}</span></td>
      <td class="t-num">${num(s.membersAlive)}/${num(s.desiredSize)}${Number(s.pendingReplacements || 0) ? `<span class="t-sub">+${num(s.pendingReplacements)} inbound</span>` : ""}</td>
      <td><span class="t-main">${esc(labelize(s.leaderPhase || "?"))}</span><span class="t-sub">${ago(s.leaderPhaseAgeSeconds)}</span></td>
      <td class="t-num">${num(s.engagements)}</td>
      <td class="t-num">${num(s.deaths)}</td>
      <td class="t-num">${num(s.travels)}</td>
      <td>${s.reforming ? chip("REFORMING", "amber") : s.convergePending ? chip("CONVERGING", "teal") : chip("PATROL", "ok")}</td>
    </tr>`).join("");

    const contacts = (scouts.activeContacts || []).map((c) => `<div class="feed-row">
      <span class="f-age">${ago(c.ageSeconds)}</span>
      <span class="f-body">${chip(labelize(c.faction), String(c.faction).toLowerCase() === "rebel" ? "amber" : "blue")}
        contact on <b>${esc(labelize(c.planet))}</b>${c.city ? " near " + esc(labelize(c.city)) : ""}
        ${c.targetWasPlayer ? chip("PLAYER", "red") : chip("BOT", "ghost")}
        <small>reported by squad ${esc(c.reporterSquadId)} · ${num(c.reports)} report(s)</small>
      </span>
    </div>`).join("");

    return `
      <div class="page-title"><h2>WARFRONT</h2><span class="page-sub">combat operatives, squad status, scout intelligence</span></div>
      <div class="grid">
        <section class="panel span-12"><header><h3>THEATER SUMMARY</h3>
          <span class="panel-tag">${pvp.enabled ? "active" : "stand-down"} · bot-vs-bot ${yesno(pvp.allowBotVsBotCombat)} · scan ${num(pvp.scanRadiusMeters)}m</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Squads", squads.length, { tone: "accent" })}
            ${metric("Player Engagements", pvp.playerEngagementsTotal)}
            ${metric("Bot Engagements", pvp.botEngagementsTotal)}
            ${metric("Total Losses", pvp.deathsTotal, { tone: Number(pvp.deathsTotal || 0) ? "warn" : "" })}
            ${metric("Travels", pvp.travelsTotal)}
            ${metric("Promotions", pvp.promotionsTotal)}
            ${metric("Squad Reforms", pvp.squadReformsTotal)}
            ${metric("Recovery Actions", pvp.recoveryActionsTotal)}
          </div>
          <div class="section-gap"></div>
          ${sparkline(histSeries("eng", { delta: true }), { note: "ENGAGEMENT RATE", color: "var(--red)" })}
          </div>
        </section>
        <div class="faction-row" style="grid-column: span 12">
          ${factionCard("rebel", "rebel")}
          ${factionCard("imperial", "imperial")}
        </div>
        ${panel("SQUAD ROSTER", `${squads.length} squads · size ${num(pvp.squadSize)} · ${num(pvp.squadsPerFaction)}/faction`,
          table(["Squad", "Faction", "Theater", "Strength", "Leader Phase", "Eng", "Losses", "Travels", "Status"], squadRows, { scrollKey: "war-squads" }))}
        <section class="panel span-7"><header><h3>SCOUT NETWORK</h3>
          <span class="panel-tag">${scouts.enabled ? "reporting" : "offline"} · ${num(scouts.contactsReportedTotal)} contacts · ${num(scouts.convergencesTotal)} convergences</span></header>
          <div class="panel-body"><div class="feed" data-scroll="war-contacts">${contacts || `<span class="muted small">No active contacts - sectors quiet.</span>`}</div></div>
        </section>
        <section class="panel span-5"><header><h3>COMMS TRAFFIC</h3><span class="panel-tag">faction channels</span></header>
          <div class="panel-body">${kvRows([
            ["Spatial announcements", onoff(comms.spatialAnnouncements)],
            ["Announcements sent", num(comms.announcementsTotal)],
            ["Faction rooms", onoff(comms.factionRooms)],
            ["Rooms created", num(comms.factionRoomsCreated)],
            ["Room posts", num(comms.factionRoomPostsTotal)],
            ["Joins blocked (covert)", num(comms.factionRoomJoinsBlockedTotal)],
            ["Announce cooldown", num(comms.announceCooldownSeconds) + "s"]
          ])}</div>
        </section>
        <section class="panel span-12"><header><h3>ROUTED TRAVEL</h3>
          <span class="panel-tag">${routed.enabled ? "routed" : "direct"} · collector boarding ${onoff(routed.useCollectorBoarding)} · ship-wait ${onoff(routed.boardOnActualShuttle)}</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Routes Planned", routed.routesPlannedTotal)}
            ${metric("Legs Executed", routed.routeLegsExecutedTotal)}
            ${metric("Hop Routes", routed.hopRoutesTotal)}
            ${metric("Transit Stops", routed.transitStopsTotal)}
            ${metric("Collector Boardings", routed.collectorBoardingsTotal, { tone: "accent" })}
            ${metric("Tactical Arrivals", routed.tacticalArrivalsTotal, { tone: "accent" })}
            ${metric("Plan Fallbacks", routed.fallbacksTotal, { tone: Number(routed.fallbacksTotal || 0) ? "warn" : "" })}
            ${metric("Collector Fallbacks", routed.collectorFallbacksTotal, { tone: Number(routed.collectorFallbacksTotal || 0) ? "warn" : "" })}
            ${metric("Break-offs", routed.breakOffsTotal, { tone: Number(routed.breakOffsTotal || 0) ? "warn" : "" })}
            ${metric("Stalemate Breaks", routed.stalemateBreaksTotal)}
          </div>
          <div class="section-gap"></div>
          ${kvRows([
            ["Collector cache", `${num(routed.collectorCacheResolvedCount)} resolved · ${num(routed.collectorCacheFallbackCount)} pad-fallback`],
            ["City hangouts", (routed.cityLocations || []).length
              ? ["manual", "cantina", "fallback"].map((k) =>
                  `${(routed.cityLocations || []).filter((c) => String(c.hangoutSource) === k).length} ${k}`).join(" · ")
              : "not resolved yet"],
            ["Staging", `${esc(labelize(routed.stagingRebel || "?"))} (rebel) · ${esc(labelize(routed.stagingImperial || "?"))} (imperial)`],
            ["Orphan sweep", `${num(routed.orphanBotsLastSweep)} last · ${num(routed.orphanBotsDetectedTotal)} total`],
            ["En route", squads.filter((s) => s.routeDest).map((s) =>
              `#${esc(s.squadId)} → ${esc(labelize(String(s.routeDest).split(":").pop() || ""))} (${num(s.routeLegsRemaining)} leg${Number(s.routeLegsRemaining) === 1 ? "" : "s"}${s.departureTargetKind === "collector" ? " · collector" : ""})`
            ).join(" · ") || "none"]
          ])}</div>
        </section>
      </div>`;
  }

  /* ---------------- WILDS (pve hunter positions) ---------------- */

  function pageWilds(d) {
    const pve = d.pveActivity || {};
    const roster = Array.isArray(pve.roster) ? pve.roster : [];
    const offers = Array.isArray((pve.missionBoard || {}).offers)
      ? pve.missionBoard.offers : [];
    const offerFor = (r) => {
      const matches = offers.filter((o) =>
        Number(o.identityId) === Number(r.identityId) &&
        (!r.bodyOid || !o.bodyOid || Number(o.bodyOid) === Number(r.bodyOid)) &&
        o.completed !== true);
      return matches.length ? matches[0] : null;
    };
    const attached = roster.filter((r) => r.bodyAttached).length;
    const active = roster.filter((r) => String(r.phase || "IDLE_HOME") !== "IDLE_HOME").length;
    const rows = roster.map((r) => {
      const phase = String(r.phase || "IDLE_HOME");
      const planet = String(r.posPlanet || "");
      const coords = [r.posX, r.posY, r.posZ].map(Number);
      const hasPosition = Boolean(planet) && coords.every((v) => Number.isFinite(v));
      const name = r.name || [r.firstName, r.lastName].filter(Boolean).join(" ") || "Hunter #" + (r.identityId || "?");
      const way = hasPosition
        ? "/way " + coords.map((v) => v.toFixed(1)).join(" ")
        : "";
      const position = hasPosition
        ? `<span class="t-main">${coords[0].toFixed(1)} / ${coords[1].toFixed(1)}</span><span class="t-sub">z ${coords[2].toFixed(1)}</span>`
        : `<span class="muted">position unavailable</span>`;
      const offer = offerFor(r);
      const offerBearing = offer && Number.isFinite(Number(offer.bearingDeg))
        ? `${Number(offer.bearingDeg).toFixed(0)}°` : "—";
      const offerPos = offer ? [offer.advertisedX, offer.advertisedY].map(Number) : [];
      const offerDistance = offer && hasPosition && offer.planet === planet &&
        offerPos.every((v) => Number.isFinite(v))
        ? `${Math.hypot(coords[0] - offerPos[0], coords[1] - offerPos[1]).toFixed(0)}m`
        : "—";
      const offerLabel = offer
        ? `<span class="t-main">${esc(offerBearing)}</span><span class="t-sub">${esc(offerDistance)}</span>`
        : `<span class="muted">no held offer</span>`;
      const lairAlive = offer ? offer.lairAlive === true : r.lairAlive === true;
      const wavesSeen = offer && offer.wavesSeen != null ? offer.wavesSeen : r.wavesSeen;
      const wavesLabel = wavesSeen == null ? "—" : String(wavesSeen);
      const realBuffsOn = pve.realBuffsEnabled === true;
      const medicalBuffed = r.medicalBuffed === true;
      const entertainerBuffed = r.entertainerBuffed === true;
      const buffLabel = medicalBuffed && entertainerBuffed
        ? "full"
        : medicalBuffed ? "medical" : entertainerBuffed ? "entertainer" : "needed";
      const buffTone = medicalBuffed && entertainerBuffed ? "ok" : "amber";
      const buffStatus = realBuffsOn
        ? `${chip(buffLabel, buffTone)}<span class="t-sub">${num(r.minBuffSecondsLeft)}s · ${esc(r.lastBuffSource || "none")}</span>`
        : `${chip("disabled", "ghost")}`;
      return `<tr>
        <td><span class="t-main">${esc(name)}</span><span class="t-sub">${r.bodyAttached ? "body attached" : "identity only"}</span></td>
        <td>${chip(labelize(phase), /HUNT|TRAVEL|RETREAT|MISSION/i.test(phase) ? "teal" : "ghost")}</td>
        <td class="wilds-offer">${offerLabel}</td>
        <td>${chip(lairAlive ? "alive" : "clear", lairAlive ? "amber" : "ghost")}</td>
        <td class="t-num wilds-waves">${esc(wavesLabel)}</td>
        <td>${hasPosition ? esc(labelize(planet)) : `<span class="muted">in transit</span>`}</td>
        <td class="t-num">${position}</td>
        <td>${hasPosition ? `<code class="way-code">${esc(way)}</code>` : `<span class="muted">n/a</span>`}</td>
        <td>${buffStatus}</td>
      </tr>`;
    }).join("");

    return `
      <div class="page-title"><h2>WILDS</h2><span class="page-sub">live PvE hunter positions and movement phases</span></div>
      <div class="grid">
        <section class="panel span-12"><header><h3>HUNTER FIELD SUMMARY</h3>
          <span class="panel-tag">${pve.enabled ? "active" : "stand-down"} · refreshes with telemetry</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Hunters", roster.length, { tone: "accent" })}
            ${metric("Attached Bodies", attached)}
            ${metric("Active Orders", active)}
            ${metric("Completed Missions", pve.missionsCompletedTotal, { tone: "accent" })}
            ${metric("Kills", pve.hunterKillsTotal)}
            ${metric("Harvest Units", pve.hunterHarvestUnitsTotal)}
            ${metric("Deaths", pve.hunterDeathsTotal, { tone: Number(pve.hunterDeathsTotal || 0) ? "warn" : "" })}
          </div></div>
        </section>
        ${panel("HUNTER POSITION FEED", `${roster.length} identities · ${active} active`,
          table(["Hunter", "Phase", "Offer bearing / distance", "Lair", "Waves", "Planet", "X / Y", "/way", "Buffs"], rows, { scrollKey: "wilds-roster", tall: true }))}
        <section class="panel span-12"><div class="panel-body wilds-note">
          <span class="muted small">Coordinates are read-only live positions. Select the visible <code class="way-code">/way</code> text and paste it in-game; unavailable positions indicate a body or zone transition.</span>
        </div></section>
      </div>`;
  }

  /* ---------------- TELEMETRY (diagnostics) ---------------- */

  function pageTelemetry(d) {
    const pathd = d.pathValidationDiagnostics || {};
    const cal = d.reachabilityCalibration || {};
    const mem = d.reachabilityMemory || {};
    const nav = d.navAreaDensitySelection || {};
    const align = d.coverageAlignmentDiagnostics || {};
    const mr = d.movementReadiness || {};
    const histRows = (d.recentAssignmentHistory || {}).rows || [];

    const pathRows = (pathd.rows || []).slice(0, 20).map((r) => `<tr>
      <td class="t-num">#${esc(r.minerId)}<span class="t-sub">${esc(labelize(r.lifecycleStatus || r.assignmentStatus))}</span></td>
      <td><span class="t-main">${esc(r.targetResource || "?")}</span><span class="t-sub">${esc(r.targetZone || "")}</span></td>
      <td>${chip(labelize(r.explanationKey || "unknown"), /verified|direct_overland/i.test(String(r.explanationKey)) ? "ok" : /fail|reject/i.test(String(r.explanationKey)) ? "red" : "amber")}
        <span class="t-sub">${esc(labelize(r.latestPathTrustStatus || ""))}</span></td>
      <td class="t-num">${num(r.straightLineDistance)}m<span class="t-sub">${num(r.pathNodes)} nodes</span></td>
      <td><span class="t-sub">${r.targetInNavmeshAvailable ? (r.targetInNavmesh ? "target on mesh" : "target off mesh") : "mesh n/a"} · ${esc(labelize(r.overlandRejectReason || "none"))}</span></td>
    </tr>`).join("");

    const feed = histRows.slice(0, 20).map((r) => `<div class="feed-row">
      <span class="f-age">${ago(r.ageSeconds)}</span>
      <span class="f-body">#${esc(r.minerId)} ${esc(labelize(r.clearReason || r.lifecycleStatus))} - ${esc(r.targetResource || "?")}
        <small>${esc(labelize(r.selectedProfile))} · ${esc(r.targetZone || "")} · ${num(r.stationSampleCount)} samples · ${compact(r.stationYieldQuantity)} yield</small>
      </span>
    </div>`).join("");

    return `
      <div class="page-title"><h2>TELEMETRY</h2><span class="page-sub">pathing, reachability, alignment - deep diagnostics</span></div>
      <div class="grid">
        <section class="panel span-12"><header><h3>PATH VALIDATION</h3>
          <span class="panel-tag">trust: ${esc(labelize(pathd.pathTrustRequired || "verifiedPath"))}${pathd.pathTrustRelaxed ? " (relaxed)" : ""}</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Candidates", pathd.candidateAssignments)}
            ${metric("Verified Paths", pathd.verifiedPaths, { tone: "good" })}
            ${metric("Overland OK", pathd.overlandReachable, { tone: "good" })}
            ${metric("Failed", pathd.failedValidations, { tone: Number(pathd.failedValidations || 0) ? "bad" : "" })}
            ${metric("Water Reject", pathd.overlandUnsafeWater)}
            ${metric("Off-Mesh Targets", pathd.targetOutsideNavmesh)}
            ${metric("Stale", pathd.staleValidations)}
            ${metric("Ready", pathd.forceMovementReadinessPassedCount)}
          </div>
          <div class="section-gap"></div>
          ${table(["Miner", "Target", "Explanation", "Distance", "Terrain"], pathRows, { scrollKey: "tel-path" })}</div>
        </section>
        <section class="panel span-6"><header><h3>REACHABILITY CALIBRATION</h3><span class="panel-tag">${esc(labelize(cal.status || "n/a"))}</span></header>
          <div class="panel-body">
            ${autoTable(cal.validationOutcomes, { maxRows: 8, scrollKey: "tel-outcomes", empty: "No outcome data" })}
            <div class="section-gap"></div>
            ${autoTable(cal.byPlanet, { maxRows: 10, scrollKey: "tel-calplanet", empty: "No per-planet data" })}
          </div>
        </section>
        <section class="panel span-6"><header><h3>FAILURE REASONS &amp; DISTANCE BANDS</h3><span class="panel-tag">calibration</span></header>
          <div class="panel-body">
            ${autoTable(cal.topFailureReasons, { maxRows: 8, scrollKey: "tel-fail", empty: "No failures recorded" })}
            <div class="section-gap"></div>
            ${autoTable(cal.byDistanceBand, { maxRows: 8, scrollKey: "tel-band", empty: "No distance band data" })}
          </div>
        </section>
        <section class="panel span-6"><header><h3>REACHABILITY MEMORY</h3><span class="panel-tag">shadow cache</span></header>
          <div class="panel-body">${typeof mem === "object" ? autoKV(mem, { max: 12 }) : ""}
            ${autoTable(mem.byPlanet || mem.planetMemory, { maxRows: 8, scrollKey: "tel-memplanet", empty: "No planet memory" })}
          </div>
        </section>
        <section class="panel span-6"><header><h3>NAVAREA DENSITY + MOVEMENT GATES</h3><span class="panel-tag">shadow mode</span></header>
          <div class="panel-body">
            ${autoKV(nav, { max: 10 })}
            <div class="section-gap"></div>
            ${kvRows([
              ["Readiness status", labelize(mr.movementReadinessStatus || "n/a")],
              ["Readiness reason", labelize(mr.movementReadinessReason || "n/a")],
              ["Gates passed / blocked", num(mr.forceMovementReadinessPassedCount) + " / " + num(mr.forceMovementBlockedCount)],
              ["Arrival timeouts", num(mr.movementArrivalTimeoutCount)],
              ["Sample timeouts", num(mr.sampleTimeoutCount)]
            ])}
          </div>
        </section>
        <section class="panel span-6"><header><h3>COVERAGE ALIGNMENT</h3><span class="panel-tag">${esc(labelize(align.status || "n/a"))}</span></header>
          <div class="panel-body">${autoKV(align.counts, { max: 14 })}</div>
        </section>
        <section class="panel span-6"><header><h3>ASSIGNMENT HISTORY</h3><span class="panel-tag">${num((d.recentAssignmentHistory || {}).rowCount)} records</span></header>
          <div class="panel-body"><div class="feed" data-scroll="tel-hist">${feed || `<span class="muted small">No cleared assignments yet.</span>`}</div></div>
        </section>
      </div>`;
  }

  /* ---------------- MAINFRAME (server) ---------------- */

  function pageMainframe(d) {
    const s = ((state.stats || {}).result) || {};
    const core = s.core || {};
    const players = s.players || {};
    const missions = s.missions || {};
    const ver = ((state.version || {}).result) || state.version || {};
    const meta = d.metadata || {};

    const upSec = core.coreStartTimeMs ? (Date.now() - Number(core.coreStartTimeMs)) / 1000 : 0;

    const missionBars = bars(Object.entries(missions)
      .filter(([k]) => k.startsWith("creditsGeneratedFromMissions"))
      .map(([k, v]) => ({ label: k.replace("creditsGeneratedFromMissions", ""), value: v }))
      .sort((a, b) => b.value - a.value));

    const completedBars = bars(Object.entries(missions)
      .filter(([k]) => k.startsWith("numberOfCompletedMissions"))
      .map(([k, v]) => ({ label: k.replace("numberOfCompletedMissions", ""), value: v, tone: "teal" }))
      .sort((a, b) => b.value - a.value));

    return `
      <div class="page-title"><h2>MAINFRAME</h2><span class="page-sub">core status, population, mission economy</span></div>
      <div class="grid">
        <section class="panel span-12"><header><h3>CORE STATUS</h3><span class="panel-tag">${esc(ver.core3_version || "version n/a")}</span></header>
          <div class="panel-body"><div class="metric-row">
            ${metric("Uptime", upSec ? hms(upSec) : "n/a", { raw: true, tone: "accent" })}
            ${metric("Players Online", players.onlineCount)}
            ${metric("Online Peak", players.onlineMax)}
            ${metric("Accounts", players.accountsCount)}
            ${metric("Distinct IPs", players.distinctIPsCount)}
            ${metric("Core Load Time", core.coreLoadMs ? Math.round(core.coreLoadMs / 1000) + "s" : "n/a", { raw: true })}
          </div>
          ${state.stats ? "" : `<p class="muted small">Waiting for /v1/admin/stats - refreshes every 30s.</p>`}</div>
        </section>
        ${panel("MISSION CREDITS GENERATED", "player mission economy", missionBars, "span-6")}
        ${panel("MISSIONS COMPLETED", "by type", completedBars, "span-6")}
        <section class="panel span-6"><header><h3>SNAPSHOT FEED</h3><span class="panel-tag">dashboard uplink</span></header>
          <div class="panel-body">${kvRows([
            ["Snapshot time", esc(meta.asOfTime || "n/a")],
            ["Feed age", state.lastOkMs ? ago((Date.now() - state.lastOkMs) / 1000) : "never", state.lastOkMs && (Date.now() - state.lastOkMs) < 15000 ? "ok" : "warn"],
            ["Poll interval", (state.pollMs / 1000) + "s"],
            ["Consecutive failures", String(state.failures), state.failures ? "bad" : ""],
            ["Last error", esc(state.lastErr || "none")],
            ["Trend samples", String(hist.length)]
          ])}</div>
        </section>
        <section class="panel span-6"><header><h3>UPLINK ENDPOINTS</h3><span class="panel-tag">read-only feeds in use</span></header>
          <div class="panel-body">${kvRows([
            ["/v1/aieconomy/dashboard/", "34 sections · 5s"],
            ["/v1/admin/stats/", "core + players + missions · 30s"],
            ["/v1/version/", "once per session"]
          ])}
          <p class="muted small">All calls are GET-only with the stored bearer token. This console never mutates server state.</p></div>
        </section>
      </div>`;
  }

  /* ---------------- router ---------------- */

  const ICONS = {
    command: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><circle cx="8" cy="8" r="6"/><circle cx="8" cy="8" r="1.6" fill="currentColor"/><path d="M8 2v2.4M8 11.6V14M2 8h2.4M11.6 8H14"/></svg>`,
    extraction: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><path d="M3 13L8 3l5 10z"/><path d="M6 13l2-4 2 4"/></svg>`,
    supply: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><rect x="2.5" y="6" width="11" height="7"/><path d="M2.5 6L8 2.5 13.5 6M8 6v7"/></svg>`,
    transit: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><path d="M2 11c3-6 9-6 12 0"/><circle cx="8" cy="5" r="1.4" fill="currentColor"/><path d="M4 13.5h8"/></svg>`,
    warfront: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><path d="M8 2l5 2v4c0 3-2.2 5-5 6-2.8-1-5-3-5-6V4z"/><path d="M5.7 8l1.6 1.6L10.6 6"/></svg>`,
    wilds: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><path d="M2 12.5l3.2-4 2.1 2 2.6-5 4.1 7"/><path d="M2 14h12"/><circle cx="11.9" cy="3.2" r="1.2"/></svg>`,
    telemetry: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><path d="M2 8h3l1.5-4 3 8L11 8h3"/></svg>`,
    mainframe: `<svg class="nav-icon" viewBox="0 0 16 16" fill="none" stroke="currentColor"><rect x="3" y="2.5" width="10" height="11"/><path d="M6 5.5h4M6 8h4M6 10.5h2"/></svg>`
  };

  const ROUTES = [
    { id: "command", label: "COMMAND", render: pageCommand },
    { id: "extraction", label: "EXTRACTION", render: pageExtraction },
    { id: "supply", label: "SUPPLY", render: pageSupply },
    { id: "transit", label: "TRANSIT", render: pageTransit },
    { id: "warfront", label: "WARFRONT", render: pageWarfront },
    { id: "wilds", label: "WILDS", render: pageWilds },
    { id: "telemetry", label: "TELEMETRY", render: pageTelemetry },
    { id: "mainframe", label: "MAINFRAME", render: pageMainframe }
  ];

  function alertCount() {
    const d = state.data || {};
    let n = 0;
    if ((d.minerActivity || {}).emergencyDisabled) n++;
    if (Number((d.minerRecovery || {}).needsAttention || 0) > 0) n++;
    if (state.lastOkMs && (Date.now() - state.lastOkMs) / 1000 > 60) n++;
    return n;
  }

  function renderNav() {
    const badge = alertCount();
    $("nav").innerHTML = ROUTES.map((r) => `
      <a class="nav-item ${state.route === r.id ? "active" : ""}" href="#/${r.id}">
        ${ICONS[r.id] || ""}<span>${r.label}</span>
        ${r.id === "command" && badge ? `<span class="nav-badge">${badge}</span>` : ""}
      </a>`).join("");
  }

  function captureScroll() {
    const map = {};
    document.querySelectorAll("[data-scroll]").forEach((el) => {
      if (el.scrollTop || el.scrollLeft) map[el.dataset.scroll] = [el.scrollTop, el.scrollLeft];
    });
    return map;
  }

  function restoreScroll(map) {
    document.querySelectorAll("[data-scroll]").forEach((el) => {
      const s = map[el.dataset.scroll];
      if (s) { el.scrollTop = s[0]; el.scrollLeft = s[1]; }
    });
  }

  function render() {
    updateLink();
    renderNav();
    const view = $("view");
    if (!state.token) {
      view.innerHTML = `<div class="boot-screen"><div class="boot-frame">
        <p class="boot-line">CLEARANCE REQUIRED</p>
        <p class="boot-sub">Press <b>KEY</b> in the top bar and enter the REST API bearer token.</p>
      </div></div>`;
      return;
    }
    if (!state.data) {
      view.innerHTML = `<div class="boot-screen"><div class="boot-frame">
        <p class="boot-line">${state.lastErr ? "UPLINK FAILED" : "ESTABLISHING UPLINK…"}</p>
        <p class="boot-sub">${state.lastErr ? esc(state.lastErr) + " - retrying with backoff. The game server may be starting or the REST listener busy." : "Awaiting first telemetry snapshot from the hive."}</p>
      </div></div>`;
      return;
    }
    const route = ROUTES.find((r) => r.id === state.route) || ROUTES[0];
    const scroll = captureScroll();
    try {
      view.innerHTML = route.render(state.data);
    } catch (e) {
      view.innerHTML = `<div class="error-panel">RENDER FAULT [${esc(route.id)}]: ${esc(e && e.message || e)}<br>
        The data feed is alive but this page hit an unexpected shape. Other pages should still work.</div>`;
    }
    restoreScroll(scroll);
  }

  /* ---------------- wiring ---------------- */

  window.AECD = {
    setMapZone(z) {
      state.mapZone = z;
      try { localStorage.setItem("core3_cmd_zone", z); } catch (e) {}
      render();
    },
    // headless smoke hook: renders every page against a snapshot, throws on fault
    __smoke(data) {
      return ROUTES.map((r) => [r.id, r.render(data || {}).length]);
    }
  };

  window.addEventListener("hashchange", () => {
    state.route = (location.hash || "#/command").replace(/^#\//, "");
    render();
    window.scrollTo(0, 0);
  });

  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) { state.backoffUntil = 0; poll(); }
  });

  $("token-toggle").addEventListener("click", () => {
    $("token-form").classList.toggle("hidden");
    $("token-input").value = state.token;
    $("token-input").focus();
  });

  $("token-form").addEventListener("submit", (ev) => {
    ev.preventDefault();
    state.token = $("token-input").value.trim();
    try { localStorage.setItem("core3_api_token", state.token); } catch (e) {}
    $("token-form").classList.add("hidden");
    state.backoffUntil = 0;
    state.failures = 0;
    render();
    poll();
  });

  function tickClock() {
    const el = $("clock");
    if (el) {
      const t = new Date();
      el.textContent = String(t.getHours()).padStart(2, "0") + ":" +
        String(t.getMinutes()).padStart(2, "0") + ":" +
        String(t.getSeconds()).padStart(2, "0");
    }
    updateLink();
  }

  setInterval(tickClock, 1000);
  setInterval(poll, 1000); // gate inside poll() enforces pollMs cadence + backoff

  tickClock();
  render();
  poll();
})();
