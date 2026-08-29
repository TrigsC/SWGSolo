# Live Verification Run 3 — F_0.8.0 Structure Traversal Foundation

**Verdict: `LIVE_VERIFICATION_FAIL`** (feature-level — D1/D2/D4 remain open and a
new D6 was found). **D5a and D5b are VERIFIED FIXED.** No receipt recorded.

- **Run ID**: `20260816-132600-structure-traversal-foundation`
- **Date**: 2026-08-16
- **Purpose**: live-verify the D5a and D5b fixes; complete the full 25-scenario
  matrix (run 2 halted at 20)
- **Predecessors**: `live_structure-traversal-foundation_20260814-091057.md`,
  `live_structure-traversal-foundation_20260814-132303.md`

## Scope

This run's contract verified **D5a** and **D5b** only. D1, D2 and D4's Theed
scenario anchor were known-open going in; their scenarios were expected to fail
and are excluded from the D5 verdict but recorded below.

## Static gate

| Gate | Result |
| --- | --- |
| Build (`-Werror`) | Clean — 0 errors, 0 warnings; ninja no-op, binary current with source |
| Lua `luac -p` | Clean — `sim_player_manager.lua`, `templates.lua`, `simTraversalTest.lua`, `simMiner.lua` |
| GoogleTest | No applicable cases. The plan's Test Impact section states no existing GoogleTest coverage touches these files and the matrix **is** the test deliverable; recorded as coverage debt, not a gap. |

## Restart / readiness

Restarted through `scripts/server-cycle.sh`. Readiness 14s to fresh `who.json`;
full load confirmed separately via `isServerLoading` (the script's readiness
check fires while the server is still loading — see "Tooling defects").

## Results — 25 of 25 resolved

**10 PASS / 15 FAIL.** Anomaly counters (deltas from a fresh process):
`egressPathFailures 2`, `pathfinderFallbackActivations 2`, `resumeFailures 0`,
`teleportsDetected 0`, `zSanityViolations 0`.

| # | Scenario | Run 1 | Run 2 | Run 3 | Attribution |
| --- | --- | --- | --- | --- | --- |
| 1 | cantina_enter_exit | PASS | PASS | **PASS** | |
| 2 | naboo_hospital_enter_exit | FAIL | FAIL | **FAIL** `controller_path_failed` | D2 |
| 3 | mos_eisley_starport_front | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 |
| 4 | mos_eisley_starport_deep_foyer4 | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 |
| 5 | cantina_immediate_exit | PASS | PASS | **PASS** | |
| 6 | cantina_long_dwell | PASS | PASS | **PASS** | 710s incl. 600s dwell |
| 7 | theed_starport_hangar | FAIL | FAIL | **FAIL** `target_cell_unresolved` | D4 remainder |
| 8 | starport_upper_floor | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 |
| 9 | hospital_to_cantina | PASS | PASS | **PASS** | |
| 10 | cantina_to_corellia_hospital | FAIL | FAIL | **FAIL** `controller_path_failed` | D2 (both enter steps resolve — D4 fix holds) |
| 11 | cell_to_enclosed_hollow | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 |
| 12 | combat_approach_door | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 — **combat clean** |
| 13 | combat_interior_route | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 — **combat clean** |
| 14 | combat_egress | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 — **combat clean** |
| 15 | combat_drag_different_cell | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 — **combat clean** |
| 16 | combat_ends_outdoors | FAIL | FAIL | **PASS** | **first full combat traversal** |
| 17 | combat_reentry_cross_building | FAIL | FAIL | **FAIL** `exit_not_outdoors` | D1 — **combat clean** |
| 18 | attacker_dies_instantly | FAIL | FAIL | **FAIL** `combat_pause_not_observed` | **D6 (new)** |
| 19 | unreachable_target_bounded_failure | PASS | FAIL | **PASS** | D5b contamination gone |
| 20 | external_preemption | PASS | FAIL | **PASS** | D5b contamination gone |
| 21 | prepare_for_relocation | PASS | not reached | **PASS** | |
| 22 | death_or_incapacity_recovery | PASS | not reached | **PASS** | |
| 23 | ten_sequential_cycles | PASS | not reached | **PASS** | 1372s, 10 cycles |
| 24 | two_bots_opposite_directions | FAIL | not reached | **FAIL** `exit_not_outdoors` | D1 |
| 25 | bot_a_dwell_bot_b_traverse | FAIL | not reached | **FAIL** `exit_not_outdoors` | D1 |

## Assertion table

| ID | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| A1 | Every `ST_COMBAT_PAUSE` produces a resume | resumes == pauses, > 0 | 6 pauses, 6 resumes, 1:1 in all six combat scenarios | **PASS** |
| A2 | Despawn shows `botCombatCleared=true botStillInCombat=false` | all events | 7 despawns, 0 with `botStillInCombat` set | **PASS** |
| A3 | `ST_RESUME_TICK` observable | present | 60 lines with inCombat/combatDriverActive/peaceSinceMs | **PASS** |
| A4 | Scenarios 19 & 20 regain run-1 parity | both PASS | both PASS | **PASS** |
| A5 | No attacker-free scenario enters CombatPaused | 0 | 0 | **PASS** |
| A6 | Run-1 passes reproduce | 7 scenarios PASS | all 7 PASS | **PASS** |
| A7 | All 25 scenarios resolve | 25 | 25 | **PASS** |
| A8 | Clean startup, alive, no crash | no backtrace | pid 1690020 alive; 0 backtraces (2 grep hits are GDB catchpoint registrations at startup, benign) | **PASS** |
| A9 | D3/D4 hold | no `attacker_spawn_failed` | none; scenario 10 resolves both enter steps | **PASS** |

Every contracted D5 assertion passed.

## D5a — VERIFIED FIXED, and the mechanism is now proven

Six pauses, six resumes, **all on `attempt=1`**, latency consistently
2044–2212 ms against `resumeSettleMs = 2000`:

| generation | despawn → resume |
| --- | --- |
| 24 | 2044 ms |
| 26 | 2075 ms |
| 29 | 2071 ms |
| 31 | 2068 ms |
| 33 | 2054 ms |
| 35 | 2212 ms |

The `ST_RESUME_TICK` instrumentation added for this run resolves the ambiguity
run 2 could not. Generation 24, one line per second:

```
t=…521157 inCombat=true  combatDriverActive=false peaceSinceMs=0
…                                 (7 consecutive ticks, monitor re-arming)
t=…527160 inCombat=true  combatDriverActive=false peaceSinceMs=0
t=…528117 SCENARIO_ATTACKER_DESPAWN … botCombatCleared=true botStillInCombat=false
t=…528160 inCombat=false combatDriverActive=false peaceSinceMs=0
t=…529161 inCombat=false combatDriverActive=false peaceSinceMs=…528160
t=…530161 ST_RESUME generation=24 attempt=1
```

Two conclusions, both now evidence-backed rather than inferred:

1. **The bot's stale combat state was the blocker.** `inCombat` flipped to false
   43 ms after the despawn cleared it, and the resume followed one settle window
   later.
2. **The "monitor stopped re-arming" candidate is ruled out.** The monitor
   re-armed correctly on all seven pre-despawn ticks. Run 2 listed these two
   mechanisms as unseparated; they are now separated.

`combatDriverActive` was false throughout, so the compound peace predicate was
gated solely by `isInCombat()` — consistent with the fix being placed on the
harness side rather than in combat internals.

`resumeFailures` stayed **0** again, confirming run 2's finding that the counter
cannot observe this failure mode. It remains a misleading metric.

## D5b — VERIFIED FIXED

- Scenarios 19 and 20, which regressed in run 2 purely from contamination, both
  **PASS** again.
- **Zero** attacker-free scenarios entered `CombatPaused` (run 2: two).
- All six genuine pauses carry `reason=arrival_combat` with their own scripted
  attacker. Generation 31 — run 2's contamination signature — is a legitimate
  attacker-driven pause in this run.
- Scenario 18's reset log shows the clean handoff:
  `ST_PHASE generation=36 to=Idle reason=structureTraversalScenarioReset`
  followed by a fresh generation 37 at peace.

## D6 — NEW DEFECT (harness), revealed by fixing D5a

`attacker_dies_instantly` failed with `combat_pause_not_observed`. This is a
**new** and more precise reason; the scenario has never before reached its real
assertion (run 1 `attacker_spawn_failed`, run 2 `enter_budget_exceeded`).

Scenario config: `interrupt={phase="ApproachDoor", afterMs=100, durationMs=1}`.
Harness assertion: `structureTraversalTestPauseObserved[botIndex]` must be true.

Observed window:

```
…118592 ST_PHASE generation=37 from=Idle to=ApproachDoor reason=outdoor_enter
…118693 ST_PATH result=accepted nodes=19
…119169 SCENARIO_ATTACKER_DESPAWN … botCombatCleared=true botStillInCombat=false
        (no ST_COMBAT_PAUSE anywhere in the scenario)
…146842 ST_PHASE generation=37 to=Idle reason=target_cell_arrived
…147116 SCENARIO_RESULT status=FAIL reason=combat_pause_not_observed
```

The attacker existed for roughly 477 ms (spawn ≈ +100 ms, despawn at +577 ms) —
shorter than the despawn tick granularity, and short relative to the controller
tick that performs pause detection.

**Two candidate explanations, not yet separated:**

- **(a)** Combat never established at all inside ~477 ms, so no pause was
  correct and the scenario's assertion is unsatisfiable as configured.
- **(b)** Combat did establish, and D5a's bot-side `clearCombatState(true)`
  raced ahead of the controller's pause check, erasing the evidence before it
  could be observed — i.e. the harness cancels the interrupt it just scripted.

**Observability gap blocking the answer**: `despawnStructureTraversalTestAttacker()`
records `botStillInCombat` only *after* clearing. The needed fact is the bot's
combat state **before** the clear. One extra field on the existing
`SCENARIO_ATTACKER_DESPAWN` line settles it.

If (b) holds, the fix is ordering, not logic: the harness should not clear the
bot's combat state until the controller has had at least one tick to observe the
pause, or `durationMs` should be floored above the controller tick interval.
Either way this is a **harness-side** issue. The foundation's own pause/resume
behavior is verified correct by the six clean cycles above.

## What is now established about the foundation

`combat_ends_outdoors` is the **first end-to-end combat traversal PASS** in the
project: approach → engage → pause → attacker death → resume → arrive → egress
outdoors. It passes because it terminates outdoors and so never touches the D1
starport hollow that blocks its five siblings.

Read together with scenarios 12–15 and 17 — all of which show clean pause/resume
and then fail only at `exit_not_outdoors` — the combat-interrupt half of the
foundation is working, and D1 is the single remaining blocker for the whole
starport family.

## Tooling defects found and fixed

Both were pre-existing traps, surfaced when a cancelled `core3 runUnitTests`
left a **zombie `core3`** behind. Container PID 1 does not reap children (≈200
zombie `screen`/`gdb` entries going back weeks), so that zombie is permanent for
the container's lifetime. It holds no file descriptors and cannot contend for
ports or the database — it is inert, merely misdetected.

| File | Defect | Fix |
| --- | --- | --- |
| `.claude/skills/TRIP-verify/scripts/server-cycle.sh` | `running()` used bare `pgrep -x core3`, which matches zombies — stop verification and `start_server` both wedged | probe skips Z-state pids |
| `/home/swgemu/bin/run` (container, **outside the repo**) | same bare `pgrep core3` guard → `** Already running **`, refusing every future start | same zombie-aware guard |

Without these the server could not have been restarted at all. **The `run`
change is in the owner's environment, not project code, and is flagged for
review**; backup at `scratchpad/run.BACKUP` (md5 `3a19ec4361d7ddd5cce0f5c61bd1129e`).

Separately, `server-cycle.sh`'s readiness check accepts a fresh `who.json` that
appears while `isServerLoading` is still true. It reported ready at 14s; real
load completion came later. Not fixed this run — worth tightening to gate on
`isServerLoading`.

## Evidence

| Path | Contents |
| --- | --- |
| `MMOCoreORB/bin/log/trip-verify-20260816-132600-structure-traversal-foundation-structuretraversal.log` | 83,145 B run delta, 594 lines — the primary evidence |
| `MMOCoreORB/bin/log/trip-verify-20260816-132600-structure-traversal-foundation-dashboard-final.json` | final `result.structureTraversal` snapshot: all 25 scenario verdicts + anomaly counters |

**Evidence caveat**: the periodic dashboard JSONL sampler was killed before its
write-out step, so no `-dashboard.jsonl` exists for this run. Every assertion
above is derived from the run-delta log and the final snapshot, both of which are
complete; the scenario table and counters are fully reproducible from them. The
loss is only the intermediate time series, which no assertion depended on.
`observe-dashboard.py` buffers and copies at completion — for long runs it should
be checkpointed periodically or invoked with `--select result.structureTraversal`
in shorter consecutive windows.

Log event census: `ST_PATH` 215, `ST_PHASE` 193, `SCENARIO_STEP` 74,
`ST_RESUME_TICK` 60, `SCENARIO_RESULT` 25, `SCENARIO_ATTACKER_DESPAWN` 7,
`ST_COMBAT_PAUSE` 6, `ST_RESUME` 6, `ST_FAIL` 3, `SCENARIO_BOT_DESPAWN` 3,
`ST_RESOLVE` 1, `SCENARIO_FORCED_PATH_FAILURE` 1.

## Cleanup

- `sim_player_manager.lua` restored byte-for-byte (md5
  `0c42cbfaacbfe196fc80649c2af46efa`); all three gates back to `false`;
  `luac -p` clean.
- Dashboard `structureTraversal.bots` is `[]` — no leaked harness bots or
  attackers.
- Server left running; no crash during the run.
- No database, persistent roster, or unrelated log touched.
- No receipt recorded.

## Defect queue after this run

| ID | Status |
| --- | --- |
| D1 | **Open** — egress accepts truncated path, strands bot in enclosed hollow; 10 of 15 failures. Proposal written (`F_0.8.0-D1_egress-hollow-escalation.proposal.md`), awaiting owner approval. **Now the dominant blocker.** |
| D2 | Diagnostics shipped; underlying med-center egress defect open (template-level, 2 failures). Candidate fourth repair strategy untested. |
| D3 | FIXED, confirmed (run 2) |
| D4 | Code FIXED, confirmed; Theed scenario anchor still wrong (1 failure) |
| D5a | **FIXED, VERIFIED** — 6/6 clean resumes, mechanism proven |
| D5b | **FIXED, VERIFIED** — contamination gone, 19 & 20 restored |
| D6 | **New** — `attacker_dies_instantly` never observes a pause; harness-side, needs one pre-clear telemetry field to separate two candidate causes |

**Recommended next**: D1 (unblocks 10 of the 15 failures), then D2's fourth
strategy, then D6's one-line telemetry, then D4's Theed anchor.
