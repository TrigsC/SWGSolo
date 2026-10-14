# Code Review: F_0.9.3 — Loot from kills and the hive item vault (P.10d)

**Review Date**: 2026-10-14  
**Version**: 0.9.10  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/sql/swgemu.sql`
- `MMOCoreORB/src/server/db/ServerDatabase.cpp`
- `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyData.idl`
- `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyManager.cpp`
- `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyManager.h`
- `MMOCoreORB/src/server/zone/managers/creature/CreatureManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotItemReconcile.h` (new)
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotLootStep.h` (new)
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotItemReconcileTest.cpp`
- `MMOCoreORB/src/tests/SimBotLootStepTest.cpp`

**Plan**: `docs/1-plans/F_0.9.3_p10d-loot-and-vault.plan.md`
**Review loop**: Codex, 17 rounds; final review round APPROVED. The consolidated synthesis below flagged one open item (two untracked headers), resolved at release; see "Release resolution".

---

## Executive Summary

The implementation now satisfies the planned loot, vault, reconciliation, observability, and travel-resumption behavior in the tested working tree. All previously identified production correctness and callback-race defects were addressed, and the final build passed 118 in-binary tests, 14/14 live scenarios, and the 23/23 verification receipt at `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:6`.

Promotion remains blocked because two required production headers are still untracked and absent from `git diff HEAD`. A commit made from the current tracked change set would omit headers included by production sources and fail from a clean checkout.

**Verdict: NEEDS REVISION**

---

## Changes Overview

The change adds corpse-loot claims, bounded autonomous loot walks, cash and item deposits, per-identity vault bags, durable item records, boot reconciliation, teardown handling, diagnostics, dashboard exposure, unit tests, and live verification scenarios.

Loot walks can interrupt eligible travel legs and safely resume them afterward. Arrival and path-result callbacks carry their captured work-loop generation into a serialized classifier so callbacks from replaced movement cannot execute mission-phase logic.

The final live verification exercised fail-closed vault initialization, cash and item paths, cap and fault behavior, death handling, reaping, restart reconciliation, and a real roster hunter looting while on `TRAVEL_TO_MISSION`; see `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:57`.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Required production headers are not included in the tracked change — resolved at release.**  
   `SimBotLootStep.h` and `SimBotItemReconcile.h` remain `??` in `git status`, while tracked production sources include them at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:12`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:19`, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:20`. Add `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotLootStep.h:1` and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotItemReconcile.h:1` to the commit.

2. **“Loot callbacks could execute normal phase arrival/failure logic” — addressed.**  
   Arrival and path failure classify the callback before all travel, flee, traversal, and phase handling at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2343` and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2558`. Current path failures set `lootStepPathFailed`, which the tick converts to `abandoned.pathFailed` at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3786`.

3. **“Loot outcomes were not consistently recorded, and failures could retry indefinitely” — addressed.**  
   Every disposition is counted and emits `LOOT_RESULT` at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12266`. Only `TOO_FAR` returns the claim; transfer and bag failures are final at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3826`.

4. **“Failed or losing bag creation paths leaked persistent bags” — addressed.**  
   Invalid bags, refused transfers, and concurrent mapping losers all call the destructive cleanup path at `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyManager.cpp:250`, with unlink-and-destroy implemented at `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyManager.cpp:302`.

5. **“Loaded item timestamps did not match the epoch-millisecond representation used for new rows” — addressed.**  
   Both timestamps are loaded through `UNIX_TIMESTAMP(...)*1000` at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:10882`.

6. **“Death and teardown left queued claims associated with a body that no longer existed” — addressed.**  
   `dropPlayerBotLootClaims` removes the entire identity queue and counts it at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12250`; death and teardown invoke it at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:4254` and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:7045`.

7. **“The vault failed closed because `creature_inventory.iff` is `GENERIC`, not `VOLUME`” — addressed after live discovery.**  
   Item containers now accept either type, while bags are distinguished by template CRC at `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyManager.cpp:286`. A created vault that fails validation is destroyed at `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyManager.cpp:177`, and reconciliation recognizes unmapped bags by template at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:13416`.

8. **“Production hunters never became idle long enough to start the loot walk” — addressed after live discovery.**  
   Eligible bots may now interrupt a walking travel leg at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3855`, record the interruption at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3891`, and resume the appropriate travel leg at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3747`. The real-hunter scenario passed with the expected claim, walk, loot, and resume sequence at `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:76`.

9. **“Finishing a loot step could leave the corpse walk alive or strand the interrupted leg” — addressed.**  
   Finish now serializes against callbacks, advances the movement generation, sets in-flight movement states to idle, and only then clears the active guard at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3726`. New-attacker exits also resume the interrupted leg immediately at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3802`.

10. **“Arrival/path callbacks could cross the loot-step boundary through TOCTOU races” — addressed through successive revisions.**  
    The final implementation covers all previously identified variants:

    - Arrival and path tasks carry their own captured generation via `callbackTaskGeneration` at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:50`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:759`, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:879`.
    - Callback classification and finish share `lootArrivalMutex` at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2318` and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3738`.
    - Active state and the first-generation boundary are published together at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3893`.
    - Pre-step callbacks are classified stale while active, and every callback dispatched below the finish generation remains stale afterward at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2324`.
    - Both current and stale callbacks are swallowed before phase handling at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2343` and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:2558`.
    - New orders and teardown exit through the common finish path at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:409` and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:7043`.

11. **“A successful path result can pass its generation check and then be applied after movement replacement” — accepted override; open shared-mover debt.**  
    The window remains between the check at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:745` and callback application at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp:766`. This predates the loot feature and affects every shared mover consumer; widening this release to redesign shared path-result acceptance was explicitly declined and recorded at `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:106`.

### Minor Issues

1. **“The planned claim/step diagnostics were missing” — addressed.**  
   `LOOT_CLAIM`, `LOOT_STEP begin/end`, and `LOOT_RESULT` are emitted at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12156`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3729`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:3902`, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12277`.

2. **“Bot death did not pair an active `LOOT_STEP begin` with an end event” — addressed.**  
   The death branch now calls the common finish path at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp:4249`.

3. **“The two new unit-test sources were untracked” — addressed for the test files.**  
   Both now appear as added files at `MMOCoreORB/src/tests/SimBotLootStepTest.cpp:1` and `MMOCoreORB/src/tests/SimBotItemReconcileTest.cpp:1`. This does not resolve the separate open production-header omission above.

4. **“The production-walk harness used default step timeouts incompatible with its long wait” — addressed.**  
   Explicit 1,200,000 ms, 300,000 ms, and 120,000 ms step budgets are defined at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2186` and enforced by the runner at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15992`.

5. **“A timed-out or disabled production-walk scenario could leave its provoked creature behind” — addressed.**  
   Idempotent cleanup is implemented at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14725` and called from cleanup initiation, final cleanup, and the disabled runner path at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15387`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15490`, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15661`.

6. **“Destroying a live provoked creature could leave its hunter in stale combat state” — addressed.**  
   Cleanup retains the identity and whether the creature was alive, then clears only that hunter’s combat state at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14743` and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14760`.

7. **“The sum of production-walk step budgets exceeds the scenario budget” — withdrawn as a defect.**  
   The outer 1,500,000 ms deadline remains authoritative at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15911`; individual budgets merely bound whichever step is active. The exact scenario passed live at `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:76`.

8. **“Live verification had not exercised the real production loot walk” — addressed.**  
   The harness now selects a governor-managed roster hunter, provokes a real fight, awaits a result for that corpse, and confirms travel resumes at `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17812`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17896`, and `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17925`.

### Suggestions

1. **Crash-window durability — accepted planned behavior.**  
   A fresh item can be lost if the server restarts before the normal five-minute object delta save, after which reconciliation correctly marks the SQL row orphaned. This was verified and documented at `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:47`.

2. **Empty mapped bags — non-blocking operational observation.**  
   The live run left two empty mapped bags, including one created before discovering a corpse contained cash only. No bag discard or reaper failure accompanied them; retain as follow-up telemetry at `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:95`.

3. **Production item roll — verification caveat, not a code finding.**  
   The real roster-hunter scenario looted cash because the spawned creature’s item roll was empty. Item transfer used the same `lootPlayerBotCorpse` path and was exercised by the component/weapon harness scenarios at `docs/4-unit-tests/live_f093-loot-vault_20261014-161558-f093-loot-vault.md:85`.

---

## Checklist

- [x] 1. Functional Requirements — behavior passes in the verified workspace; the two production headers are staged in the release commit.
- [x] 2. Code Quality — ownership, locking, failure accounting, cleanup, and diagnostics are explicit.
- [x] 3. Architectural Compliance — conforms to existing manager/controller patterns, with the accepted shared-mover path-result race retained as documented debt.
- [x] 4. IDL / Serialization — vault and bag mappings follow the project’s managed-data patterns.
- [x] 5. Lua Boundary — configuration and harness scenario data are parsed and syntax-checked cleanly.
- [x] 6. Simulation Safety — 118/118 in-binary tests and 14/14 live scenarios passed; no crash or BDB signature was observed.
- [x] 7. Error Handling — vault initialization fails closed, invalid records are rejected individually, and transfer/cleanup failures are bounded and counted.
- [x] 8. Security — corpse ownership, distance, gate, identity, and container checks are enforced before transfer.
- [x] 9. Performance — claim queues, retries, vault capacity, and walk duration are bounded; the former persistent transfer retry loop is removed.

---

## Verdict

**APPROVED**

The final Codex review round returned APPROVED. The consolidated synthesis returned NEEDS REVISION for one reason only, Major issue 1 (the new headers were untracked), which is a packaging step rather than a code defect.

## Release resolution

`SimBotLootStep.h` and `SimBotItemReconcile.h` were staged with `git add` before the release commit; `git diff --cached --name-only` lists both, together with the force-added `SimBotLootStepTest.cpp` and `SimBotItemReconcileTest.cpp` (the tests directory is ignored by `.gitignore`). No source file changed after the live-verification receipt (`receipt.py check`: PASS, 23/23). `MMOCoreORB/utils/engine3` appears dirty in the working tree but is excluded from the commit, as in every release of this line.
