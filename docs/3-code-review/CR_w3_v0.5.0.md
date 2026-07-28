# Code Review: P.8.7 — Market-Driven Mission Hunting (Real Missions, Galactic Travel)

**Review Date**: 2026-07-26 (addendum 2026-07-28)
**Version**: 0.5.0
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/bin/web/aieconomy-dashboard/index.html`
- `MMOCoreORB/bin/web/aieconomy-dashboard/styles.css`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/MissionDiagLog.h` (new)
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/TravelDiagLog.h` (new)
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ARCHI.md`
- `docs/ai-pve-playerbot-design.md`

**Excluded (pre-existing owner changes, not part of this feature):**
`death_watch_wraith.lua`, `rifle_t21.lua`, and the `engine3` submodule.

**Plan**: `docs/1-plans/F_0.5.0_p87-market-driven-mission-hunting.plan.md`

---

## Executive Summary

This change replaces scripted single-kill PvE hunts with market-driven,
level-appropriate destroy missions, routed interplanetary travel, real
three-wave lairs, optional buff trips, and expanded dashboard telemetry. Eight
findings were tracked across the review loop: six were addressed, one
performance optimization was accepted as deferred, and owner-controlled live
activation remains an explicit operational handoff. **APPROVED with
observations**

A post-review addendum (below) covers three follow-ups that landed after the
2026-07-26 loop and are part of this v0.5.0 release: the C1–C4 tick-concurrency
hardening (separately Codex-reviewed and converged), the bounded offer-retry,
and the meat allocation-ceiling saturation fix (trivial config).

---

## Changes Overview

The implementation moves shared travel mechanics into the base SimPlayer
controller, adds location-based mission-terminal discovery, constructs real
mission lairs, and introduces a market matchmaker that ranks resource demand
against level-qualified lair yield. It also adds race-safe hunter telemetry,
mission lifecycle counters, wave progress, and dashboard presentation, with all
new behavior behind default-off Lua gates.

Build, Lua parsing, JavaScript syntax, and whitespace checks were reported clean;
no existing unit suite exercises the SimPlayer layer, and the new pure-helper
coverage debt is recorded at `docs/4-unit-tests/COVERAGE-DEBT.md:17`.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Routing-only cities incorrectly blocked deliberate PvE destinations** — `SimPlayerManager.h:858`, `SimPlayerManager.cpp:8661`
   The initial implementation applied `routingOnly` broadly enough to exclude the newly added market planets from terminal discovery and market work. **Disposition: addressed.** Routing-only now means excluded from random/automatic placement while remaining valid for deliberate PvE work; PvE callers explicitly allow these cities at `SimPlayerManager.cpp:9555` and `SimPlayerManager.cpp:9673`, while automatic PvP destinations still reject them at `SimPlayerManager.cpp:31235`.

2. **Matchmaker reservation could disagree with the generated mission yield** — `SimPlayerManager.cpp:9125`, `SimPlayerManager.cpp:10850`
   The original selector chose randomly within a tier after the matchmaker reserved the maximum yield. **Disposition: addressed after refinement.** The index records `minLevelCeiling` at `SimPlayerManager.cpp:9131` and sums weighted-mobile family yields at `:9153`; the selector sums matching yields at `:9223` with strict-first fallback at `:9297`; per-identity levels resolve at `:10735`; the matchmaker mirrors the same strict-first, upper-bound-only fallback at `:10850`; the stored offer uses the summed exact-type yield at `:9334`.

3. **Shared hunter travel polluted miner-only telemetry** — `SimPlayerController.cpp:2556`
   **Disposition: addressed.** The base class exposes an empty per-controller boarding hook at `SimPlayerController.h:247`; the shared travel path invokes it at `:2561`, and miner telemetry is confined to the miner override at `:1605`.

4. **Mission-lair capacity failures leaked objects and abandoned valid missions** — `SimPlayerManager.cpp:9962`
   **Disposition: addressed.** Capacity is reserved before creation at `:9965`, the lair is retained via `ManagedReference` at `:9991`, and released/destroyed on late failure at `:10071`. The tri-state result is declared at `SimPlayerManager.h:2294`, and the hunter defers/retries at `SimHunterController.cpp:1863`.

5. **Engaged-telemetry registration raced with cleanup** — `SimHunterController.cpp:2686`
   **Disposition: addressed.** A dedicated mutex protects the map at `SimHunterController.h:53`; cleanup snapshots and clears under it before dropping creature observers at `:2689`; registration reserves the key before observer work at `:2723` and only fills a surviving reservation at `:2782`.

6. **Live verification incomplete for newly gated behavior** — plan `:678`, `:840`
   **Disposition: accepted with override.** Restarting the live server is owner-controlled. The unverified paths remain inert behind default-off gates; final owner restart + observation is a deployment handoff. *(Update: the real-mission, three-wave, offer-retry, and saturation paths were subsequently live-verified — see addendum.)*

### Minor Issues

1. **Mission reveal message was invisible and `wavesSeen` was never populated** — `SimHunterController.cpp:1882`, `SimPlayerManager.cpp:13058`
   **Disposition: addressed.** Reveal uses the spatial hunter announcement path at `SimHunterController.cpp:1885`; the wave observer is stored at `SimPlayerManager.cpp:10065`, its real spawn number emitted at `:13070`, and the frontend renders it at `app.js:1034`.

### Suggestions

1. **Market matchmaking repeats multiplicative scans** — `SimPlayerManager.cpp:10784..10864`
   **Disposition: open, accepted as deferred.** Default-off, bounded roster-maintenance cadence, small hunter population. Restructuring/extracting the ranking helpers is deferred alongside the pure-helper test seam at `docs/4-unit-tests/COVERAGE-DEBT.md:17`.

---

## Addendum (2026-07-28) — post-review follow-ups in v0.5.0

**A1. C1–C4 tick-concurrency hardening — Codex-reviewed separately, converged.**
The original single-driver proposal was rejected by Codex as insufficient
(`activeTickGeneration` was latest-wins, not single-flight, and a plain-`uint64`
data race; `generate` commits internally). The shipped fix adopts: **C1**
`std::atomic<uint64> activeTickGeneration`; **C2** non-blocking `tickRunning`
single-flight guard; **C3** route the terminal→accept transition through the
single tick lane; **C4** a globally-monotonic terminal-visit epoch
(`openTerminalVisit`/`pveTerminalVisitEpoch`) captured on board-open and
compared at commit, invalidated at order-clear, config reload, and
world-presence drain (ABA-safe — the sequence is never reset). A world-presence
drain epoch gap found during that review was closed. The full per-controller
lifecycle-event strand was reviewed as incomplete/risky and **capped at C1–C4**
by explicit owner decision (documented debt). Live result: offer churn 558→0, 0
`staleVisit`, 0 crashes/deadlocks.

**A2. Bounded offer-retry — low-risk, Codex-reviewed with the fixes batch.**
An empty board dwells and re-generates for `offerMaxAttempts`×`offerRetrySeconds`
(default 3×25s), retry span clamped ≤480s. Four accompanying low-risk fixes
(log-under-lock, retry-span clamp + prune-expired, city-region reveal rejection,
log-under-agent-lock) were applied and reviewed.

**A3. Meat allocation-ceiling saturation fix — trivial config, live-verified.**
Hunters idled across restarts because meat's dispatch `effectiveCeiling`
defaulted to 25% of `desiredReserve` and accumulated meat exceeded it →
`signalUnits`/`pressure` = 0 → no orders. Meat is the sole hunter family, so
`familyAllocationCeilingFraction.meat` 0.25→1.0. Live reload: ceiling 25k→70.6k,
`signalUnits` 0→41k, hunters dispatched immediately. No code path changed.

---

## Checklist

- [x] 1. Functional Requirements — Passed; correctness findings addressed, final yield selection conforms to the plan.
- [x] 2. Code Quality — Passed; concurrency, routing, and tier-selection invariants documented.
- [x] 3. Architectural Compliance — Passed; manager/controller and dashboard patterns follow `docs/ARCHI.md`.
- [x] 4. Distributed Object / IDL Discipline — Passed; no IDL/autogen changes, managed references retained, lock ordering documented.
- [x] 5. Lua/C++ Boundary — Passed; tunables and gates remain in Lua, no competing behavior-tree driver.
- [x] 6. AI-Economy / Simulation Safety — Passed; simulation-only and default-off; live activation owner-controlled (now performed).
- [x] 7. Error Handling — Passed; lair failures clean up safely, capacity exhaustion defers, actionable state logged.
- [x] 8. Security — Passed; reuses the Bearer-authenticated dashboard facade; no new endpoint/credential exposure.
- [x] 9. Performance — Passed with observation; bounded default-off matchmaker scans remain an accepted follow-up optimization.

---

## Verdict

**APPROVED with observations**

No actionable Critical or Major code issue remains. The bounded matchmaker scan
is accepted deferred work; the C1–C4 concurrency cap and the meat-consumer
economy phase are documented debt carried into the release handoff.
