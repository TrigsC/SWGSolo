# Code Review: Skill training, novice PlayerBots, and the neutral body template (P.10f)

**Review Date**: 2026-09-06  
**Version**: 0.9.5  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/scripts/mobile/serverobjects.lua`
- `MMOCoreORB/bin/scripts/mobile/sim_playerbot_novice.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/db/ServerDatabase.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/variables/Skill.h`
- `MMOCoreORB/utils/engine3`
- `VERSION`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`
- `docs/ARCHI.md`
- `docs/4-unit-tests/live_p10f-skill-training_20260906-223926-p10f-skill-training.md`

**Plan**: `docs/1-plans/F_0.9.5_p10f-skill-training-and-novice-playerbots.plan.md`

---

## Executive Summary

This change turns roster PlayerBot kill XP into durable skill training, derives XP caps and combat tier from trained skills, introduces gated novice brawler identities and a neutral body template, and reconciles the governed PvE population through a bounded desired-active-set model. All code-review findings were addressed and the final code and harness passed 54/54 live scenarios across both boot phases. **APPROVED with observations**.

---

## Changes Overview

The progression store now treats trained skills as durable authority, derives spend and tier, repairs cached state, and evaluates training through event-driven work plus a bounded sweep. Lua supplies default-off training and novice configuration, while C++ handles plan derivation, body overlays, retirement, population ownership, and dashboard observability. The final verification receipt is `docs/4-unit-tests/live_p10f-skill-training_20260906-223926-p10f-skill-training.md:3-27`.

---

## Findings

### Critical Issues

None.

### Major Issues

1. **Unchanged config refresh discarded training work** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:7764`. Rebuilding plans on every maintenance refresh reset the round-robin cursor and pending identities, starving identities beyond the first batch. **Disposition: addressed** at `SimPlayerManager.cpp:7769-7775`; unchanged signatures now preserve the existing plans, cursor, and pending set.

2. **Novice minting caused unbounded legacy-hunter churn** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:13797`. The novice distribution was followed by the legacy capacity filler, which reminted the planless identities retirement removed. **Disposition: addressed** at `SimPlayerManager.cpp:13816-13824`; novice distribution is now the complete novice-mode population policy.

3. **Training parity override was unreachable under shipped defaults** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9742`. The training capability flag short-circuited before the verification override, making the default-off feature impossible to exercise. **Disposition: addressed** at `SimPlayerManager.cpp:9743-9755`; only the master gate precedes the override.

4. **Novice parity override reverted during refresh and then leaked state** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:9042`. A bare harness assignment was overwritten by recurring Lua refresh; the first override fix then lacked a complete restore path and could leave novice behavior enabled after fail-fast cleanup. **Disposition: addressed** at `SimPlayerManager.cpp:9045-9056`, `:9733-9739`, `:11731-11734`, and `:11872-11875`; the prior value is latched once and restored outside the parity mutex on config reset, scenario restore, and harness cleanup.

5. **Population reconciliation destroyed and counted harness-owned bodies** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14621`. The desired-active-set drain initially treated every out-of-set body as governor-owned, while the body budget included parity bodies. **Disposition: addressed** at `SimPlayerManager.cpp:13842-13850`, `:14629-14669`, and `:12754-12777`; production ownership is explicit and consistently scopes drain, spawn budget, and the bound assertion.

6. **Governor ownership depended on runtime-refreshable eligibility** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:13842`. Deriving ownership from `pveHuntingProfessions` could make live production bodies simultaneously undrainable and uncounted after a config change. **Disposition: addressed** at `SimPlayerManager.cpp:13844-13850`; all non-harness production identities remain governor-managed while eligibility only determines desired membership.

7. **Novice distribution bypassed hunt eligibility** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:14571`. Distribution-selected identities could remain desired after their profession was removed from runtime hunting configuration. **Disposition: addressed structurally** at `SimPlayerManager.cpp:14578-14618`; eligibility is computed once and both distribution-first and general-fill passes consume the same set.

8. **Live verification was outstanding** — the initial review could not approve a SimPlayer behavior change on static evidence alone. **Disposition: addressed** by `docs/4-unit-tests/live_p10f-skill-training_20260906-223926-p10f-skill-training.md:3-27`, which records a warning-clean build, zero new unit-test failures, and 54/54 scenarios passing in both boot phases; cleanup and default-off restoration are recorded at `:127-138`.

### Minor Issues

1. **Plan signature survived a full plan-map reset** — `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:6175`. The signature fast path could leave the rebuilt plan map permanently empty after `loadLuaConfig()`. **Disposition: addressed** at `SimPlayerManager.cpp:6175-6180`; the map and signature are cleared together under `progressionMutex`.

2. **XP-cap scenario asserted an impossible available-XP value** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1282`. Available XP is earned minus spend, so it cannot equal the raised cap immediately after buying a box of that XP type. **Disposition: addressed** with direct cap observability at `SimPlayerManager.cpp:12621-12642` and direct before/after assertions at `sim_player_manager.lua:1284-1292`.

3. **Forced orphan cleanup did not rebase delta assertions** — `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1366`. Scenario baselines are captured before the first step (`SimPlayerManager.cpp:12084-12109`), so a forced reap followed by a delta assertion still depended on inherited state through `SimPlayerManager.cpp:11470-11479`. **Disposition: addressed** at `sim_player_manager.lua:1373-1383`; both checks now assert the absolute orphan count after forced cleanup. The final rerun is recorded as run 17 at `docs/4-unit-tests/live_p10f-skill-training_20260906-223926-p10f-skill-training.md:64-67`.

### Suggestions

1. **Verification state isolation** — accepted as follow-up. The matrix still has shared global counters and positional identity state; the limitation and recommendation are recorded at `docs/4-unit-tests/live_p10f-skill-training_20260906-223926-p10f-skill-training.md:95-100`. This does not invalidate the final receipt after the concrete scenario-38 defect was fixed and rerun.

2. **Scale evidence scope** — observation only. The 2000-identity scenarios prove cursor and pending-drain bounds arithmetically; they are not a concurrent 2000-body or 2000-row load test (`docs/4-unit-tests/live_p10f-skill-training_20260906-223926-p10f-skill-training.md:88-94`).

3. **Unexplained verification stall** — observation only. One earlier run reached high CPU and stopped serving REST, but no causal explanation was established; the correction is preserved at `docs/4-unit-tests/live_p10f-skill-training_20260906-223926-p10f-skill-training.md:104-110`. A complete fresh two-phase run passed afterward.

4. **Real-combat kill-XP setting** — accepted verification setup, not a defect. Real combat intentionally checks the production gate at `MMOCoreORB/src/server/zone/managers/player/PlayerManagerImplementation.cpp:2096`, while the committed `awardKillXp` value remains default-off at `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:1565`.

---

## Checklist

- [x] 1. Functional Requirements — Passed; implementation matches the plan and all review defects were rerun live.
- [x] 2. Code Quality — Passed; shared eligibility and ownership abstractions remove the predicate drift found during review.
- [x] 3. Architectural Compliance — Passed; persistence, maintenance-lane work, dashboard evidence, and documentation follow `docs/ARCHI.md` patterns.
- [x] 4. Distributed Object / IDL Discipline — Passed; no autogen edits or IDL changes, managed references are preserved, and new cross-mutex restore paths avoid the identified ABBA order.
- [x] 5. Lua/C++ Boundary — Passed; tunables, gates, plans, distribution, and scenarios remain in Lua while bounded runtime mechanics remain in C++.
- [x] 6. AI-Economy / Simulation Safety — Passed; behavior is simulation-only, ships default-off, and the final dashboard/in-game matrix passed 54/54 across both boot phases.
- [x] 7. Error Handling — Passed; database, plan-load, invalid-input, cleanup, and refusal paths remain explicit and observable.
- [x] 8. Security — Passed; no credentials or authentication bypasses were introduced and dashboard access remains on the existing authenticated surface.
- [x] 9. Performance — Passed with recorded observations; hot work is bounded and the 2000-identity arithmetic checks pass, while a true 2000-entity load test and diagnosis of one earlier transient stall remain follow-up work.

---

## Verdict

**APPROVED with observations**

No Critical, Major, or Minor findings remain open. The final warning-clean build, unchanged pre-existing unit-test failures, default-off cleanup, and 54/54 two-phase live result satisfy the approval gate. The archived observations concern future harness isolation, load-test breadth, and diagnosis of one non-reproduced stall; none contradicts the final verification receipt.
