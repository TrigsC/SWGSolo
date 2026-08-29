# Live Verification — F_0.8.0 D1 Egress Hollow Escalation

**Verdict: `LIVE_VERIFICATION_FAIL`.** No receipt recorded.

D1's escalation **never selected a target**: 11 triggers, 11 `no_candidate`,
0 targets chosen. The proposal's Option A and Option B both source their
candidates from data that lies *inside* the very region the escalation must
escape. This is a **design defect in the approved approach**, not a coding
defect in its implementation.

Two things did succeed and should be kept: the pre-existing hollow-geometry
correction shipped with **zero regression**, and D1's telemetry localised the
design defect precisely on the first run.

- **Run ID**: `20260817-180013-d1-hollow-escalation`
- **Date**: 2026-08-17
- **Plan**: `docs/1-plans/F_0.8.0-D1_egress-hollow-escalation.proposal.md` §4 (A+B+C)
- **Parent**: `docs/1-plans/F_0.8.0_structure-traversal-foundation.plan.md`
- **Predecessor**: `live_structure-traversal-foundation_20260816-132600.md` (run 3)

## Static gate

| Gate | Result |
| --- | --- |
| Build (`-Werror`) | Clean — 0 errors, 0 warnings, relinked |
| Lua `luac -p` | Clean |
| GoogleTest | No applicable cases (plan Test Impact); D1 coverage debt recorded with escape plan |

## Gate-off snapshot — PASS

Restarted on the D1 binary with all gates `false` before enabling anything:

```
dashboard roots: 38            (unchanged from run 3)
enabled: False  logging: False  testEnabled: False
hollowEscalationEnabled: False  cap: 1  preferTravelPoint: True
counters: all 7 == 0           (including both new counters)
bots: []                       scenarios: 25, all PENDING
```

## Results — 25 of 25 resolved

**10 PASS / 15 FAIL** — the *same* pass/fail count as run 3.
Counters: `hollowEscalationsTriggered 11`, `hollowEscalationsFailed 11`,
`teleportsDetected 0`, `zSanityViolations 0`, `egressPathFailures 2`,
`pathfinderFallbackActivations 2`, `resumeFailures 0`.

| # | Scenario | Run 3 | Run 4 (D1) | Note |
| --- | --- | --- | --- | --- |
| 1 | cantina_enter_exit | PASS | **PASS** | |
| 2 | naboo_hospital_enter_exit | FAIL | **FAIL** `controller_path_failed` | D2 |
| 3 | mos_eisley_starport_front | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1 escalation `no_candidate` |
| 4 | mos_eisley_starport_deep_foyer4 | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1 escalation `no_candidate` |
| 5 | cantina_immediate_exit | PASS | **PASS** | |
| 6 | cantina_long_dwell | PASS | **PASS** | 710s |
| 7 | theed_starport_hangar | FAIL | **FAIL** `target_cell_unresolved` | D4 remainder |
| 8 | starport_upper_floor | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1 escalation `no_candidate` |
| 9 | hospital_to_cantina | PASS | **PASS** | |
| 10 | cantina_to_corellia_hospital | FAIL | **FAIL** `controller_path_failed` | D2 |
| 11 | cell_to_enclosed_hollow | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1 escalation `no_candidate` |
| 12 | combat_approach_door | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1; combat clean |
| 13 | combat_interior_route | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1; combat clean |
| 14 | combat_egress | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1; combat clean |
| 15 | combat_drag_different_cell | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1; combat clean |
| 16 | combat_ends_outdoors | PASS | **PASS** | |
| 17 | combat_reentry_cross_building | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1; combat clean |
| 18 | attacker_dies_instantly | FAIL | **FAIL** `combat_pause_not_observed` | D6, reproduces exactly |
| 19 | unreachable_target_bounded_failure | PASS | **PASS** | |
| 20 | external_preemption | PASS | **PASS** | |
| 21 | prepare_for_relocation | PASS | **PASS** | |
| 22 | death_or_incapacity_recovery | PASS | **PASS** | |
| 23 | ten_sequential_cycles | PASS | **PASS** | 1372s, 10 cycles |
| 24 | two_bots_opposite_directions | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1 escalation `no_candidate` |
| 25 | bot_a_dwell_bot_b_traverse | FAIL `exit_not_outdoors` | **FAIL** `controller_path_failed` | D1 escalation `no_candidate` |

D1 changed the starport failure **mode** (`exit_not_outdoors` →
`controller_path_failed`, because a failed escalation now routes through
`onPathFailed()`) without changing the failure **outcome**.

## Acceptance criteria (proposal §8)

| # | Criterion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| 1 | `exit_not_outdoors` scenarios reach `exit_complete_outdoors` | 10 of 10 | **0 of 10** | **FAIL** |
| 2 | `teleportsDetected` / `zSanityViolations` stay 0 | 0 / 0 | 0 / 0 | **PASS** |
| 3 | `hollowEscalationsFailed` is 0 | 0 | **11** | **FAIL** |
| 4 | Currently-passing scenarios still pass | 10 | 10 | **PASS** |
| 5 | Gate-off snapshot unchanged | unchanged | unchanged | **PASS** |
| 6 | Clean startup, alive, no crash | no backtrace | pid 1692798 alive, 0 backtraces | **PASS** |
| 7 | D5a no regression | 6 pauses / 6 resumes | 6 / 6, all `attempt=1` | **PASS** |
| 8 | D5b no regression | 19 & 20 PASS | both PASS | **PASS** |

## Root cause — the approved design selects points inside the hollow

Every escalation produced an identical signature:

```
ST_EGRESS escalation=triggered reason=truncated_arrival_in_hollow
          arrival=(3575.89,-4813.35,5.085) requested=(3527,-4803,5)
          hollowMissDistance=0
ST_EGRESS escalation=result status=no_candidate candidates=0
```

11 triggers, 11 `no_candidate`, **0 `escalation=target` lines ever emitted** —
across 2 distinct arrival points and 3 distinct agents, so this is structural,
not agent- or position-specific. `hollowMissDistance=0` confirms the bot is
fully inside the building footprint, not merely inside the margin.

**Option B (travel point) — reached, and rejected.** The owning building's
template is `object/building/tatooine/starport_tatooine.iff`, so the
`"starport"` test passes and Option B *was* attempted. `planet_manager.lua:502`
defines *Mos Eisley Starport* at `(3599.894, -4780.449)` with
`interplanetaryTravelAllowed = 1` — about 40 m from the bot's arrival point and
well inside the 256 m search radius, so it was found and is interplanetary. Its
**arrival position is the landing pad**, which *is* the enclosed hollow. P.4.5a
uses this datum to send a miner *to* a starport, where it is correct; as an
escape target it names precisely the place we are trying to leave.

**Option A (exterior path-graph nodes) — same class of problem.** The exterior
floor mesh's global nodes are the building's own "just outside the door" points,
which sit within `AABB + hollowContainmentMarginMeters (15)`. Option A therefore
enumerates exactly the nodes its own validity filter excludes.

So both options are defeated by one fact: **every "just outside this building"
datum the game exposes lies inside the region the escalation must clear.** The
proposal's §3 assumption — that these datasets supply points outside the hollow —
is wrong, and no amount of implementation care would have rescued it.

Note the corrected hollow geometry (below) *tightened* this: the previous buggy
form gated hard on the north range with no margin, so nodes north of the building
would have slipped through by accident. Being more correct made Option A stricter.

## What did work

**The hollow-geometry correction shipped with zero regression.** All 10
previously-passing scenarios pass again, including `ten_sequential_cycles`
(1372 s, ten full enter/dwell/exit cycles) and `cantina_long_dwell` (710 s). This
was the risk flagged when the fix landed — it changes pre-existing shipped
behaviour by widening the hollow to the north — and the run clears it.

**D5a / D5b unregressed.** 6 pauses, 6 resumes, all `attempt=1`; all 7 attacker
despawns report `botStillInCombat=false`; scenarios 19 and 20 pass.

**D6 reproduces exactly** (`combat_pause_not_observed`), confirming run 3's
finding is deterministic rather than flaky.

**The bounded-attempt reset fix is confirmed working**: escalation retriggered
once per scenario across 11 scenarios. Without the `prepareForRelocation` reset
it would have fired once and then reported `attempt_cap` for the rest of the run,
masking the real defect behind a counter artefact.

## Observability gap for the next iteration

`candidates` counts only *accepted* nodes, so `candidates=0` cannot distinguish
"the mesh exposed no global nodes" from "every node was rejected", nor say which
filter (hollow / bounds / water) rejected them, nor report Option B's outcome at
all. The diagnosis above is therefore well-supported by template and travel-point
data but is **not** directly proven by telemetry.

Minimal instrumentation to add before the next attempt:
- `escalation=candidate source=… examined=N rejectedHollow=N rejectedBounds=N rejectedWater=N`
- an explicit Option B line: `escalation=travelpoint found=0|1 interplanetary=0|1 rejected=hollow|bounds|water|none`

## Recommended next step

The evidence points at a simpler mechanism than the one approved. The bot is
stuck because `findPath` truncates, but the scenario's real outdoor destination
(`3527,-4803`) is a legitimate reachable point ~50 m west. **Option C alone —
`directOverland` straight to the final destination, with no intermediate target
selection — is the natural candidate**, and it is exactly the P.4.2 precedent
this project already shipped and verified for un-navmeshed terrain. That removes
the target-selection step whose data sources are the actual problem.

This needs an amended proposal and owner approval before implementation; it is a
change to the approved design, not a bug fix within it.

## Evidence

| Path | Contents |
| --- | --- |
| `MMOCoreORB/bin/log/trip-verify-20260817-180013-d1-hollow-escalation-structuretraversal.log` | 86,672 B run delta — primary evidence |
| `MMOCoreORB/bin/log/trip-verify-20260817-180013-d1-hollow-escalation-dashboard.jsonl` | 3,758,254 B, `result.structureTraversal` @5 s, first 50 min of the ~64 min run |

Log census: `ST_PATH` 219, `ST_PHASE` 195, `SCENARIO_STEP` 63, `ST_RESUME_TICK` 60,
`SCENARIO_RESULT` 25, `ST_EGRESS` 22, `ST_FAIL` 12, `SCENARIO_ATTACKER_DESPAWN` 7,
`ST_COMBAT_PAUSE` 6, `ST_RESUME` 6, `SCENARIO_BOT_DESPAWN` 3,
`SCENARIO_FORCED_PATH_FAILURE` 1, `ST_RESOLVE` 1.

A second sampler window covering the run's tail was cancelled once the matrix
finished; the log delta and the final dashboard snapshot both cover all 25
scenarios, so no assertion depends on the missing tail samples.

## Cleanup

- `sim_player_manager.lua` restored byte-for-byte (md5 `f984023580673be964f71f2d94f4b12b`);
  all four gates back to `false`; `luac -p` clean.
- `structureTraversal.bots` is `[]` — no leaked harness bots or attackers.
- 38/38 dashboard roots; server left running; no crash.
- No database, persistent roster, or unrelated log touched. No receipt recorded.

## Defect queue after this run

| ID | Status |
| --- | --- |
| D1 | **Open — approach invalidated.** Implementation is correct, gated, and regression-free, but Options A and B cannot supply a valid target. Needs an amended proposal (Option C alone). |
| D2 | Open — med-center egress (2 failures) |
| D3 | FIXED, verified |
| D4 | Code FIXED; Theed anchor still wrong (1 failure) |
| D5a/D5b | FIXED, verified; unregressed here |
| D6 | Open — `attacker_dies_instantly`; deterministic; needs one pre-clear telemetry field |
| — | **Hollow geometry corrected** (pre-existing bug): margin now spent on east+north, height gated on model `.Y`. Verified regression-free. |
