# Code Review: P.10a — PlayerBot Progression Store Foundation

**Review Date**: 2026-09-02  
**Version**: 0.9.0  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/ai/templates.lua`
- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/sql/swgemu.sql`
- `MMOCoreORB/src/server/db/ServerDatabase.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/4-unit-tests/COVERAGE-DEBT.md`
- `docs/ARCHI.md`
- `docs/ai-pve-playerbot-design.md`

**Plan**: `/srv/games/swgemu-core3/workspace/Core3/docs/1-plans/F_0.9.0_p10a-progression-store.plan.md`

---

## Executive Summary

This change adds the persistent PlayerBot progression store, schema migrations, award APIs, reconciliation and orphan reaping, dashboard visibility, and a default-off live parity harness. Every review and live-verification defect was resolved; the final gate passed a warning-clean build, syntax checks, 20/20 unit tests, an 18/18 live scenario matrix, and 26/26 live assertions.

APPROVED

---

## Changes Overview

Progression records keyed by roster identity now persist XP, credits, skill-point state, trained skills, and award metadata through the existing `SimPlayerManager` maintenance lane. The change also introduces guarded award APIs, boot reconciliation, an optional orphan reaper, dashboard presentation, and a two-boot live harness covering rejection, concurrent flush, recovery, cleanup, and restart behavior.

All shipped feature and harness gates remain disabled. No production award caller was added, and final live verification confirmed that existing PvE, PvP, mining, and maintenance behavior remained intact.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Expected rejection was reported as a failed scenario step** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:200`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10249`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10891`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1176`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1221`.  
   **Disposition: Addressed.** `expectReject` is now first-class step state shared by XP, credit grant/spend, and skill operations. Expected refusal passes, unexpected acceptance fails, and the oracle remains unchanged.

2. **Cleanup reused one exhausted cursor for body destruction and row deletion** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:1892`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10229`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10289`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10299`.  
   **Disposition: Addressed.** Cleanup separately tracks completion of the body pass and resets the cursor before deleting identities. Live verification confirmed that all harness bodies, identities, and progression rows were removed.

3. **Terminal requests kept `requestsQueued` nonzero and appeared never to be consumed** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9502`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9520`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11346`.  
   **Disposition: Addressed with partial override.** The unbounded-retention portion of the finding was overridden because terminal entries were already limited by the five-minute prune. The actual accounting bug was fixed by counting only queued/running requests, and single-observer terminal requests are now consumed. Asynchronous requests intentionally remain available for repeated observation until pruning.

4. **`assertPersisted` looked up a request after the helper had consumed it** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:2170`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10777`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10865`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11081`.  
   **Disposition: Addressed.** The request helper returns the consumed terminal result through `completedOut`; `assertPersisted` no longer performs an invalid second lookup.

5. **Verification gates remained enabled in the working tree** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1142`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1280`.  
   **Disposition: Addressed.** `playerBotParityTest.enabled` and `playerBotProgression.enabled` are restored to `false`, and `flushIntervalSeconds` is restored to 60. A final gates-off boot confirmed the store remained inert.

6. **Lua string defaults silently produced empty award sources** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:7586`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:7690`.  
   **Disposition: Addressed.** Live verification exposed that `LuaObject::getStringField(key, default)` can construct an empty string for a missing field. Both new Core3 call sites now read the field directly and apply their fallback in C++. The three pre-existing traversal-harness call sites were intentionally left unchanged because their Lua always supplies the fields; that scope decision is accepted.

7. **`step.phase` conflated restart selection with asynchronous request state** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:210`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:7600`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10689`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1198`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1230`.  
   **Disposition: Addressed.** Restart gating now uses `restartPhase`, while `phase` retains the `queued`/`started`/`completed` request-state meaning. This prevents the concurrent-flush waits from being silently skipped.

8. **The phase-A verdict path was relative to the wrong working directory** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10084`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10100`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10179`.  
   **Disposition: Addressed.** The path is now `log/playerbotparity-phaseA.json`, matching the server’s `bin/` working directory. The two-boot verification successfully wrote, read, validated, and removed the verdict file.

9. **Phase A could be certified without every preceding scenario passing** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10071`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10638`.  
   **Disposition: Addressed.** Verdict generation now refuses to write unless every non-restart scenario is `PASS`. A failed live run confirmed that no invalid phase-A receipt was produced.

### Minor Issues

1. **Recovery scenarios asserted short-lived internal states that polling could miss** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1243`.  
   **Disposition: Addressed.** Assertions for transient `dbAvailable=false` and `dirty=true` states were removed. Durable checks still prove merge-back correctness: the flush-failure counter increments, recovery completes, and both persisted and reloaded XP must equal 210 rather than the pre-failure 150.

2. **The orphan-reaper scenario used a delta where the invariant required an absolute value** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1207`.  
   **Disposition: Addressed.** The follow-on scenario now asserts absolute `orphanRecords=0`, because it inherits the orphan created by the preceding scenario.

3. **Reconciliation and dashboard row construction used nested roster scans** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9240`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11266`.  
   **Disposition: Addressed.** Both paths now use identity-keyed `VectorMap` lookups, keeping reconciliation and dashboard construction linear as the roster grows.

### Suggestions

1. **A `FlushNow` request took approximately 32 seconds, suggesting a missed maintenance kick** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8803`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9355`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9703`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11361`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16008`.  
   **Disposition: Addressed.** The scheduler was functioning; global fault-injection knobs were being consumed by an unrelated routine flush. Fault parameters now pass only from the targeted `FlushNow` handler, while routine flushes receive none. Kick-outcome and maximum-request-wait telemetry were added. Re-verification measured a maximum wait of 1011 ms with 33 requested kicks accounted for as 16 immediate and 17 deferred.

---

## Checklist

- [x] 1. Functional Requirements — Passed
- [x] 2. Code Quality — Passed
- [x] 3. Architectural Compliance — Passed
- [x] 4. Distributed Object / IDL Discipline — Passed; no IDL or generated-source changes
- [x] 5. Lua/C++ Boundary — Passed
- [x] 6. AI-Economy / Simulation Safety — Passed; default-off behavior, production isolation, cleanup, and maintenance cadence verified live
- [x] 7. Error Handling — Passed
- [x] 8. Security — Passed
- [x] 9. Performance — Passed

---

## Verdict

**APPROVED**

All review findings are closed, with the bounded-retention clarification and the pre-existing Lua call-site scope decision recorded as accepted overrides. The final gate was warning-clean, Lua and JavaScript syntax checks passed, unit tests passed 20/20, and live verification passed 18/18 scenarios and 26/26 assertions across gates-off, phase-A, restart phase-B, cleanup, and restored-configuration boots. The existing `engine3` submodule dirt and untracked database-backup directory were confirmed pre-existing and excluded from this change.
