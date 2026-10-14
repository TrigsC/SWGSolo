# Code Review: F_0.9.2 — Mission credits and the first credit sink (P.10c)

**Review Date**: 2026-10-14  
**Version**: 0.9.9  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/sql/swgemu.sql`
- `MMOCoreORB/src/server/db/ServerDatabase.cpp`
- `MMOCoreORB/src/server/zone/managers/mission/MissionManager.idl`
- `MMOCoreORB/src/server/zone/managers/mission/MissionManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotMissionCredits.h` (new)
- `MMOCoreORB/src/tests/SimBotMissionCreditsTest.cpp` (new)
- `docs/1-plans/F_0.9.2_p10c-mission-credits.plan.md`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`

**Plan**: `docs/1-plans/F_0.9.2_p10c-mission-credits.plan.md`

---

## Executive Summary

The change adds mission-credit awards, paid doctor buffs, and a durable holdings journal, with dashboard diagnostics and verification coverage. All actionable review findings were addressed; one critical working-tree configuration concern was accepted under an explicit owner override requiring partial staging. **APPROVED with observations**

---

## Changes Overview

Mission rewards now use the shared mission-manager formula and credit the bot’s progression identity, while doctor buffs reserve and charge a configurable fee. Holdings mutations are journaled durably, reconciled with stored balances, pruned according to retention configuration, and exposed through the AI-economy dashboard. Post-review verification completed successfully: 104 gtests passed with the server stopped, all 16 live checks passed, and all 17 harness scenarios passed.

---

## Findings

### Critical Issues

#### Working-tree live settings could be accidentally promoted

- **Location**: `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:388`, `:401`, `:420`, `:433`, `:725`, `:2063-2073`, `:2100`, `:2316-2340`
- **Description**: The working-tree diff contains enabled progression gates, mission diagnostics, elevated reserves, novice-roster activation, and legacy-hunter retirement. Committing the complete working-tree hunk could change live behavior and delete legacy identities.
- **Disposition**: **Accepted with override.** The owner identified these as long-standing live-only settings that are deliberately excluded from release commits. The release blob is constructed by partial staging and carries only this chunk’s safe defaults: `awardMissionCredits=false`, `buffPayment.enabled=false`, `doctorFeeCredits=0`, `ledgerRetentionDays=30`, plus the harness scenarios. This override depends on preserving that partial-staging boundary.

### Major Issues

#### Zero-valued doctor fee and ledger retention were ignored

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:8681-8684`, `:8961-8964`
- **Description**: `doctorFeeCredits` and `ledgerRetentionDays` originally used a clamping helper whose fallback behavior prevented configured zero values from taking effect.
- **Disposition**: **Addressed.** Both settings now use `clampIntRange` with zero as a valid lower bound. A zero doctor fee means no charge, and zero retention means retain indefinitely.

#### Zero-divisor unit-test expectation was incorrect

- **Location**: `MMOCoreORB/src/tests/SimBotMissionCreditsTest.cpp:24-28`
- **Description**: The original test expected `withinBounds(3, 10, 11, 0)` to succeed even though a zero divisor is normalized to one, making `3` out of bounds.
- **Disposition**: **Addressed.** The test now verifies that `withinBounds(10, 10, 11, 0)` succeeds and `withinBounds(3, 10, 11, 0)` fails.

### Minor Issues

#### Dashboard omitted new ledger and credit diagnostics

- **Location**: `MMOCoreORB/bin/web/aieconomy-dashboard/app.js:1206-1220`, `:1329-1351`
- **Description**: The backend exposed recent holdings changes and mission-credit, ledger, and buff-payment counters, but the dashboard did not render them.
- **Disposition**: **Addressed.** The dashboard now renders per-bot recent-ledger entries, summary metrics, retention and mismatch state, mission-award diagnostics, and doctor-buff payment diagnostics. Dynamic ledger text is escaped before HTML insertion.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; implementation matches the plan and all live and harness scenarios passed.
- [x] 2. Code Quality — Passed.
- [x] 3. Architectural Compliance — Passed; progression ownership and database writes remain manager-controlled.
- [x] 4. Distributed Object / IDL Discipline — Passed.
- [x] 5. Lua/C++ Boundary — Passed; Lua contains configuration and scenarios while C++ owns mechanics and state transitions.
- [ ] 6. AI-Economy / Simulation Safety — Passed with caveat: owner-controlled live settings at `sim_player_manager.lua:388-2340` must remain excluded from the release blob through partial staging.
- [x] 7. Error Handling — Passed; failed journal writes remain observable and retryable.
- [x] 8. Security — Passed; no new authorization surface was introduced and dashboard-provided text is escaped.
- [x] 9. Performance — Passed; recent-ledger output and pending work are bounded with no material hot-path regression.

---

## Verdict

**APPROVED with observations**

All actionable findings are resolved. The post-approval test-only refinements improve ledger failure diagnostics at `SimPlayerManager.cpp:14921-14927`, make `ledger_requeued_after_failed_insert` assert the observed retry behavior at `sim_player_manager.lua:2000-2020`, and force final cleanup in `ledger_reconciles_mismatch` at `sim_player_manager.lua:2036-2054`. Final verification passed 104 gtests, 16/16 live checks, and 17/17 harness scenarios. Promotion remains contingent on the owner’s documented partial-staging procedure excluding unrelated live-only configuration.
