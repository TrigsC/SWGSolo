# Live Verification — F_0.9.5 (P.10f skill training + novice PlayerBots)

**Run ID**: `20260906-223926-p10f-skill-training`
**Date**: 2026-09-06
**Plan**: `docs/1-plans/F_0.9.5_p10f-skill-training-and-novice-playerbots.plan.md`
**Branch**: `feat/p10f-skill-training`
**Verdict**: **LIVE_VERIFICATION_PASS** — 54/54 scenarios, both boot phases.
**Supersedes**: runs `20260906-172801`, `-213203` and `-215131`, each of which
also passed 54/54 but was invalidated by a subsequent post-review fix. This run
re-proves the matrix against the final code and harness.

## Result

```
PHASE = B      PASS = 54      FAIL = 0      of 54
records = 9    orphanRecords = 0    rosterWithoutRecord = 0
plans: brawler -> 23 boxes, 92 skill points, loadError ""
       invalid_weapon_probe -> 0 boxes, loadError "invalid_weapon_type"
```

## Static gate

| Check | Result |
| --- | --- |
| Incremental build | PASS — warning-clean, links |
| `luac -p` (3 changed Lua files) | PASS |
| `LuaMobileTest` | 11 failures, **all pre-existing** (`light_jedi_padawan`, `smart_doctor_buffer`, `tusken_guard`, `tusken_king`); 0 new, proven by a baseline run with the new template and registration removed |

## Assertion results

| # | Assertion | Result | Evidence |
| --- | --- | --- | --- |
| A1 | Clean startup, process alive, no new fatal/backtrace | PASS | 13 restarts, `who.json` fresh in 7-15 s each; 0 crash signatures |
| A2 | All 54 scenarios PASS | **PASS** | phase A 53 PASS + probe `AWAITING_RESTART`; phase B 54/54 |
| A3 | Scenario 18 real combat, unarmed XP == oracle | **PASS** | `kills=1`, `attackersConsidered=1`, `awardsGranted=2`, `totalAwarded=93` = 85 direct + 8 `combat_general`, the exact `dwarf_nuna` oracle at tier 1 |
| A4 | Persistence round-trip + two-boot probe | **PASS** | `flush_and_reload_roundtrip`, `tier_persistence_and_self_heal`, and `retained_record_survives_restart` completing in phase B |
| A5 | `orphanRecords == 0`, `rosterWithoutRecord == 0` | PASS | 0 / 0 at completion |
| A6 | 2000-identity scale budgets | PASS | `training_scale_2000_budget` (3) and `training_scale_2000_live_roster` (17) |
| A7 | Governance exact-cap | PASS | `max_hunters_contention_and_displacement` (22), `multi_replacement_convergence` (23) |
| A8 | Forbidden mutations zero for non-roster actors | PASS | scenarios 5, 6, 52 |

## Defects found and fixed during verification

Thirteen runs were required. Each blocked in a different layer; the list is the
substance of this verification, since none of these was reachable by build,
unit tests, or three rounds of code review.

| Run | Reached | Defect | Layer |
| --- | --- | --- | --- |
| 1 | 8/54 | `playerBotTrainingGateEnabled()` checked the per-capability flag **before** the parity override, so the harness could never enable training while the gate shipped default-off | implementation |
| 2 | 16/54 | four scenarios called `trainSkill` without arming the gate | harness authoring |
| 3 | 18/54 | novice template was 1-2 damage / 100 HAM — needed ~750 s to kill a `dwarf_nuna` that killed it in ~17 s | game data |
| 4 | 20/54 | `retirements.completed` had no dotted alias; the lookup silently returned 0 | harness wiring |
| 5 | 20/54 | five scenarios asserted an **absolute** `orphanRecords == 0` on a global gauge | assertion design |
| 6-7 | 24/54 | `expect="cap"` compares `availableXp == effectiveCap`, unsatisfiable once a box of that type is trained (`available = cap − cost`); the per-type cap was unobservable | observability |
| 8 | 38/54 | new scenarios left orphans, breaking the reaper pair's exact delta | harness state |
| 9 | 40/54 | **every** mint appends to the positional identity list, so added scenarios drifted `identityIndex` in two pre-existing scenarios | harness state |
| 10 | stalled | server reached 141 % CPU with REST unresponsive ~30 min, alive and still writing logs. **Root cause never established.** The run-8 fix (four added `runReaper force=true` calls) was reverted to targeted `deleteProgressionRow` and the matrix then completed, but the reaper scans `progressionRecords` (~9-12 entries here), so that explanation does not hold up — see "Corrections" below | unexplained |
| 11 | 44/54 | **the desired-active-set governor drained bodies it did not own**, destroying parity-harness bodies | implementation regression |
| 12 | 22/54 | `assertBodyMaxBound` and the governor's spawn budget both counted harness bodies against `pveMaxHunters` | implementation |
| 13 | 54/54 | — (first full pass) | — |
| 14 | 54/54 | Codex post-verification review found the run-11 ownership fix derived `governorManaged` from `isPveHuntEligible`, which reads the **runtime-refreshable** `pveHuntingProfessions`. Removing a profession at runtime would drop its live production bodies out of the drain *and* the population count at once - stranded, uncounted, replacements spawning past `pveMaxHunters`. Ownership is now `isGovernorManagedIdentity` (`profession != "harness"`), independent of eligibility; eligibility governs only desired-set membership | implementation |

| 15 | 37/54 | Codex found the novice **distribution** pass never applied `isPveHuntEligible`, which the general-fill pass did - a profession removed from runtime `huntingProfessions` would keep its bodies desired and undrainable. Fixed **structurally**: eligibility is computed once into an `eligible` set and both passes filter to it, so they decide ordering only, never membership | implementation |
| 16 | 54/54 | scenario 38 asserted an exact reap delta of 1 while novice minting now creates a real production brawler, inflating the orphan count; it now establishes its own zero-orphan baseline | harness state |

| 17 | **54/54** | scenario 38's forced reap did not rebase its delta assertions - counter baselines are captured before a scenario's first step, so with N inherited orphans the delta read `1-N` while the absolute value was correctly 1. Run 16 passed only because N happened to be 0, i.e. **by luck rather than by the fix**. Both assertions changed to `assertCounterValue`, which the forced reap makes valid | harness assertion |

### The two that matter most

**Run 1 — gate precedence.** The award gate checks only the master flag and lets
the parity override decide; the training gate folded the capability flag into
that pre-override check. Because the capability correctly ships `false`, the
override was unreachable and the entire training capability was unverifiable.
The fix makes the two helpers symmetric.

**Run 11 — governor ownership.** The original `governPvePopulation` only ever
*added* bodies, so it never had to answer which bodies it owned. Adding a drain
made ownership load-bearing, and three separate places disagreed: the drain, the
spawn budget, and `assertBodyMaxBound`. All three now scope to governor-managed
(hunt-eligible) identities; harness bodies sit outside the population budget.
This was caught only by a **pre-existing F_0.9.1 scenario** that assumed a body
would still exist — an argument for re-running old scenarios unchanged rather
than adapting them to new behaviour.

## Recorded harness hazards (not fixed here)

1. **The "2000 identity" scale scenarios create nothing.** `seedSyntheticRoster`
   (`SimPlayerManager.cpp:12507`) validates configuration and returns PASS - its
   own comment states it "intentionally avoids SQL/body creation" - and
   `assertTrainingScale` (`:12464`) is a pure in-memory arithmetic simulation of
   the cursor walk and pending drain. Together they prove the sweep/drain
   **bounds** are correct and terminating. They are NOT a load test, and nothing
   in this verification exercised 2000 concurrent identities, bodies, or rows.
2. **The matrix carries order-dependent global state** — orphan counts, reaper
   deltas, and the positional identity index space. Three separate failures
   (runs 5, 8, 9) came from new scenarios disturbing state that older scenarios
   silently depended on. New scenarios must leave global state as they found it.
3. **An unknown counter name silently resolves to 0**, so a typo'd assertion
   reads as a failed feature rather than a broken test (run 4).

## Corrections to this report

Two claims in the first draft were wrong and are corrected above:

1. **Run 10 was not diagnosed.** The original text asserted the stall came from
   `runReaper` rescanning a persisting 2000-identity roster. No such roster ever
   existed (see hazard 1). The reverted change coincided with the matrix
   completing, but no causal link was established. The stall remains unexplained
   and could recur.
2. **The 2000-identity scale claim is narrower than stated.** It is an
   arithmetic bounds check, not evidence that the server sustains 2000 bots.

## Known limitation

Per-bot runtime combat state is not on the dashboard (only combat *config*), so
a combat failure cannot be distinguished between "engaged but too weak" and
"never engaged" without arithmetic on template stats. This cost a diagnosis
cycle in run 3.

## Evidence

- `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260906-223926-p10f-skill-training-dashboard.jsonl`
- Earlier run captures retained: `...-104819-...`, `...-123610-...`, `...-130712-...`
- Static gate build log: `/tmp/_trip-verify-build.txt`

## Cleanup — confirmed

- `playerBotParityTest.enabled` restored to `false`
- `playerBotProgression.enabled` and `awardKillXp` restored to `false`;
  `trainingEnabled` and `novice.enabled` unchanged at `false`
- 0 `TRIP-verify TEMPORARY` markers remain; `luac -p` clean
- `orphanRecords = 0`, `rosterWithoutRecord = 0`; production roster and its
  progression untouched; no database cleared

## Verdict

**LIVE_VERIFICATION_PASS** — receipt recorded; release may proceed.
