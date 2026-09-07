# Live Verification — F_0.9.5 (P.10f skill training + novice PlayerBots)

**Run ID**: `20260906-104819-p10f-skill-training`
**Date**: 2026-09-06
**Plan**: `docs/1-plans/F_0.9.5_p10f-skill-training-and-novice-playerbots.plan.md`
**Branch**: `feat/p10f-skill-training`
**Verdict**: **LIVE_VERIFICATION_FAIL** — no receipt recorded.

## Changed subsystems

`SimPlayerManager.{h,cpp}` (training store, plan derivation, evaluation loop,
body overlay, eligibility, governance, retirement), `Skill.h` (preclusion
accessor), `ServerDatabase.cpp` (schema 1013), `sim_player_manager.lua`
(gates, plans, novice block, 54-scenario matrix),
`mobile/sim_playerbot_novice.lua` (new), `mobile/serverobjects.lua`,
`aieconomy-dashboard/app.js`.

## Static gate

| Check | Result |
| --- | --- |
| Incremental build | PASS — warning-clean, links |
| `luac -p` x 3 changed Lua files | PASS |
| `LuaMobileTest` | 11 failures, **all pre-existing** (`light_jedi_padawan`, `smart_doctor_buffer`, `tusken_guard`, `tusken_king`); 0 new — proven by a baseline run with the new template and its registration removed |

## Restart / readiness

Pre-restart the server was already wedged: `core3` alive but the REST endpoint
returned 0 bytes, no `who.json`, and a `[core3] <defunct>` zombie was present.
`server-cycle.sh restart` recovered it — `preflight: connected test players=0`,
`ready: core3 running; fresh who.json after 15s`. No pre-restart dashboard
baseline was capturable; the harness supplies its own counter baselines via
`assertCounterDelta`, so no assertion depended on one.

## Scenario configuration

Vehicle: `playerBotParityTest`, 54 scenarios, tatooine (3467, -4890, 5),
kill target `dwarf_nuna`. Temporary settings: `playerBotParityTest.enabled` and
the `playerBotProgression` master gate set true; the three shipped capability
gates left false in file for the harness to drive per scenario.

## Assertion results

| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| A1 | Clean startup, process alive, no new fatal/backtrace | yes | yes — 0 crash signatures in `screenlog.0`; `core3` alive at end | PASS |
| A2 | All 54 scenarios PASS | 54/54 | **7 PASS, 1 FAIL, 46 never ran** (runner is fail-fast) | **FAIL** |
| A3 | Scenario 18 real combat progress (nuna dies, unarmed XP == oracle) | pass | never reached — blocked by A2 | NOT RUN |
| A4 | Persistence round-trip (flush/reload, two-boot probe) | pass | never reached | NOT RUN |
| A5 | `orphanRecords == 0`, `rosterWithoutRecord == 0` | 0 / 0 | **0 / 0** | PASS |
| A6 | 2000-identity scale budgets | pass | `training_scale_2000_budget` (scenario 3) PASSED; live-roster variant never reached | PARTIAL |
| A7 | Governance exact-cap (scenarios 22, 23) | pass | never reached | NOT RUN |
| A8 | Forbidden mutations zero for non-roster actors (5, 6) | pass | both PASSED | PASS |

Scenarios 1-7 PASS: `store_boot_invariant`,
`harness_identity_minted_with_record`, `training_scale_2000_budget`,
`grant_xp_accepted`, `grant_xp_unknown_identity_rejected`,
`award_to_non_roster_body_rejected`, `credits_grant_and_spend`.

## Failure — scenario 8 `record_skill`

```
setTrainingGate  PASS
recordSkill      FAIL  record_skill_rejected
recordSkill      PENDING
assertSkill      PENDING

training.enabled     = False   (after setTrainingGate reported PASS)
training.configured  = False
gates.trainingEnabled= False
refusals             = { gate: 1, everything else: 0 }
```

**Root cause — gate precedence in `playerBotTrainingGateEnabled()`.**

```cpp
bool SimPlayerManager::playerBotProgressionAwardGateEnabled() {
    if (!progressionEnabled)                       // MASTER gate only
        return false;
    Locker parityLock(&playerBotParityTestMutex);
    if (playerBotParityTestGateOverrideActive)
        return playerBotParityTestGateOverride;    // override decides
    return true;
}

bool SimPlayerManager::playerBotTrainingGateEnabled() {
    if (!progressionEnabled || !progressionTrainingEnabled)
        return false;                              // CAPABILITY checked first
    Locker parityLock(&playerBotParityTestMutex);
    if (playerBotParityTestTrainingGateOverrideActive)
        return playerBotParityTestTrainingGateOverride;  // unreachable
    return true;
}
```

The training helper folds the per-capability flag into the pre-override check.
Because `playerBotProgression.trainingEnabled` correctly ships **false**, the
harness's `setTrainingGate` override can never take effect and every call to
`trainPlayerBotSkill` is refused with `refusals.gate`.

Blast radius is wider than one scenario: the novice mint path grants its first
box through `trainPlayerBotSkill(freeGrant = true)` and hits the same gate, so
training, novice minting, and the scenario-18 unarmed kill are all blocked by
this single defect.

Required fix (implementation mode, not verification): make
`playerBotTrainingGateEnabled()` mirror the award gate — check only
`progressionEnabled` before consulting the parity override, and treat
`progressionTrainingEnabled` as the value returned when no override is active.

## Why earlier gates missed it

The build, the unit suites and three rounds of Codex code review all read the
code as written. This defect only appears when the code is *run with the shipped
default-off configuration*, which is exactly the condition live verification
creates and the earlier gates do not.

## Evidence

- `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260906-104819-p10f-skill-training-dashboard.jsonl` (285 samples, 2 s interval, `result.playerBotProgression` + `result.pveActivity`)
- Static gate build log: `/tmp/_trip-verify-build.txt`

An initial capture attempt produced no rows because the selectors omitted the
`result.` envelope prefix; re-run with `result.`-prefixed paths.

## Cleanup — confirmed

- `playerBotParityTest.enabled` restored to `false`
- `playerBotProgression.enabled` restored to `false`; `awardKillXp`,
  `trainingEnabled`, `novice.enabled` all remain `false`
- 0 `TRIP-verify` markers left in `sim_player_manager.lua`; `luac -p` clean
- Store end state: 6 records (the production hunters), `orphanRecords = 0`,
  `rosterWithoutRecord = 0`, no harness identities left behind
- Production roster and its progression untouched; no database cleared

## Verdict

**LIVE_VERIFICATION_FAIL** — one blocking defect. No receipt recorded; release
remains blocked. Return to `TRIP-2-implement` to fix the gate precedence, then
repeat the testing gate, Codex review, and this verification in full.
