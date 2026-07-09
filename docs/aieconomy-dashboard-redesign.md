# AI Economy Dashboard — Andor Redesign (2026-07-06)

Complete rewrite of the web console at `MMOCoreORB/bin/web/aieconomy-dashboard/`
(`index.html`, `styles.css`, `app.js`). Replaces the single monolithic page of
~20 stacked diagnostic panels with a **hash-routed multi-page SPA** themed after
Star Wars *Andor* (ISB surveillance-desk aesthetic: dark slate panels, amber
telemetry, corner brackets, monospaced numerals). Mobile-friendly (bottom tab
bar ≤820px). The previous app is preserved at
`bin/web/aieconomy-dashboard.bak-20260707/`.

## Hard constraint (why it's a 3-file SPA)

`RESTServer::serveDashboardRequest` (src/server/web/RESTServer.cpp) whitelists
exactly three file names — `index.html`, `styles.css`, `app.js` — served from
`Core3.RESTServer.DashboardRoot` (default `web/aieconomy-dashboard`) with
`Cache-Control: no-store`, read from disk **per request**. Consequences:

- All "pages" are client-side hash routes (`#/command`, `#/extraction`, …).
- No frameworks, no fonts, no images — everything inline (server may be
  reachable only on the LAN; app must work fully offline of the internet).
- **Deploys are instant**: `docker cp` the three files in, `chown 44400:44400`,
  refresh the browser. No rebuild, no restart.

## Pages (hash routes)

| Route | Name | Content (dashboard JSON sections) |
|---|---|---|
| `#/command` | COMMAND | Plain-English situation briefing (auto-generated), alert channel (stale feed / emergency stop / sim-breach / recovery / pathing), force-disposition metrics, gather-rate + stationed sparklines, **galactic heat map** (per-planet tiles: miners, combat, stationed, dispatch quota, demand weight), demand-pressure bars, safety boundaries, profile audit. Sections: `population`, `aiPopulation`, `minerActivity`, `simulatedAcquisition`, `minerRecovery`, `pvpActivity`, `hiveCrafters`, `finishedGoods`, `economyDecisionAudit`, `safetyBoundaries`, `planetDispatch`, `demand`, `metadata`. |
| `#/extraction` | EXTRACTION | Miner op status metrics, state-distribution bars, **sector operations map** (SVG plot of miner→target vectors per zone from `pathValidationDiagnostics.rows` minerX/Y & targetX/Y, zone selector), live assignments, coverage by profile + slots, station discipline, recovery watch, acquisition readiness, haul-by-planet / by-type bars, acquisition ledger. Sections: `minerActivity`, `coveragePlanner`, `minerRecovery`, `acquisitionReadiness`, `simulatedAcquisition`, `pathValidationDiagnostics`. |
| `#/supply` | SUPPLY | Supply-ledger metrics, demand board (reserve-ratio bars per profile + active opportunity), hive stockpile by label, resource-aware stockpile, hive reservations, hive crafters, craft output by profile, finished goods + recipes, scout top opportunities. Sections: `demand`, `supply`, `stockpileInspection`, `resourceAwareStockpile`, `hiveReservations`, `hiveCrafters`, `finishedGoods`, `resourceScout`. |
| `#/transit` | TRANSIT | Station travel stats, **planet-dispatch quota chart** (current vs desired ticks per planet, dry-run/live chip), resource rush, fleet distribution by zone, vehicle mechanics (SHELVED chip), travel-plan simulation. Sections: `stationTravel`, `planetDispatch`, `aiPopulation`, `resourceRush`, `travelPlanSimulation`, `vehicleMechanics`. |
| `#/warfront` | WARFRONT | Theater summary + engagement-rate sparkline, rebel/imperial faction cards, squad roster (strength, leader phase, engagements, losses, reforming/converging status), scout contact feed, comms traffic. Section: `pvpActivity` (squads/scouts/comms — previously **never rendered** by the old UI). |
| `#/telemetry` | TELEMETRY | Path validation summary + rows, reachability calibration (outcomes, per-planet, failure reasons, distance bands), reachability memory, navArea shadow cache, movement gates, coverage-alignment counts, assignment-history feed. Sections: `pathValidationDiagnostics`, `reachabilityCalibration`, `reachabilityMemory`, `navAreaDensitySelection`, `coverageAlignmentDiagnostics`, `movementReadiness`, `recentAssignmentHistory`. |
| `#/mainframe` | MAINFRAME | Core uptime/players/accounts from **`/v1/admin/stats/`** (previously unused endpoint), `core3_version` from `/v1/version/`, mission credits generated/completed bars, snapshot-feed health, endpoint inventory. |

## Architecture notes (app.js)

- **REST envelope gotcha**: every API response is wrapped by APIRequest as
  `{debug, result, status, status_code, trx_id}` — the 34 dashboard sections
  live under **`.result`**, not at the top level (`/v1/version/` is the odd one
  out with fields at top level). `doPoll()` unwraps with `d.result || d`.
- **Feeds**: `GET /v1/aieconomy/dashboard/` every 5s (9s timeout, exponential
  backoff to 60s on failure, paused while `document.hidden`);
  `/v1/admin/stats/` every 30s; `/v1/version/` once. All GET-only with bearer
  token from `localStorage["core3_api_token"]` (same key as the old app, so the
  stored token carries over).
- **Trend buffer**: samples key counters (gathered qty, acquisitions,
  stationed, moving, engagements, deaths, craft batches) at ≥15s spacing into
  `localStorage["core3_cmd_hist_v1"]` (cap 960 points) → sparklines survive
  page reloads; deltas give rate charts.
- **Resilience**: every page render wrapped in try/catch (a bad field faults
  one page, not the app); all field access defensive with fallbacks; the deep
  diagnostic sections (calibration/memory/navArea/alignment) use a generic
  `autoTable`/`autoKV` that renders whatever keys exist → schema drift in
  `SimPlayerManager.cpp` won't break Telemetry.
- **Scroll preservation**: `[data-scroll]` wrappers keyed; positions captured
  before re-render, restored after — 5s refresh doesn't yank tables.
- **Headless smoke test**: `AECD.__smoke(snapshot)` renders every route;
  harness (`smoke.mjs`, kept in session scratchpad, trivial to recreate) stubs
  DOM + fetch and runs all pages against empty and populated snapshots.
- Zone maps assume SWG planet extent −8192..+8192 on X/Y.

## Deploy procedure

```bash
docker cp index.html swgemu_server:/home/swgemu/workspace/Core3/MMOCoreORB/bin/web/aieconomy-dashboard/
docker cp styles.css swgemu_server:/home/swgemu/workspace/Core3/MMOCoreORB/bin/web/aieconomy-dashboard/
docker cp app.js    swgemu_server:/home/swgemu/workspace/Core3/MMOCoreORB/bin/web/aieconomy-dashboard/
docker exec swgemu_server bash -lc 'chown 44400:44400 /home/swgemu/workspace/Core3/MMOCoreORB/bin/web/aieconomy-dashboard/*'
# no build, no restart needed — files are read per request
```

Rollback: copy the three files back from `aieconomy-dashboard.bak-20260707/`.

## Status / open items (2026-07-06)

- **Deployed to the container, NOT yet verified live** — at deploy time the
  REST listener on :44443 was wedged (connections accepted-queue backlog, even
  `/v1/version/` timing out) while `core3` itself ran (~3h uptime). Needs the
  owner's restart; first browser load after that verifies everything.
- Ideas for later: extend the C++ whitelist (favicon/manifest for
  add-to-homescreen), server-side history ring for long-horizon trends
  (localStorage only sees what a browser session sees), a WARFRONT planet map
  once PvP squads expose coordinates, Jedi (P.7) section when the snapshot
  grows one.
