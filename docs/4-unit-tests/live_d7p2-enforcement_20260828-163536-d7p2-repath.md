# Live Evidence — F_0.8.0 D7 Part 1 Phase 2, zero-clip enforcement

**Verdict: `PHASE_2_EVIDENCE_COLLECTED`. Not a TRIP-verify receipt, and the
owner requirement is NOT met.**

Enforcement is **safe** — 22 PASS / 4 FAIL, scenario-by-scenario identical to
the no-enforcement baseline, +0% wall time, zero anomalies. Enforcement is
**largely ineffective** — the clip rate of paths the bots actually walked moved
only **11.3% → 9.7%**. Two runs, two findings, both recorded below.

- Runs: `20260828-155527-d7p2-enforce` (first attempt) and
  `20260828-163536-d7p2-repath` (after the fix)
- Baseline: `20260828-101454-d2b-harness-fix` (enforce off, 22 PASS)
- Config: run-9 gate set **plus** `zeroClip.enforce = true`,
  `zeroClip.rejectionCap = 2`; no own-structure exemption (owner decision)

## Run 11 — the first attempt found a real defect in the implementation

**15 PASS / 11 FAIL. Seven regressions, all `controller_path_failed`.**

| regressed scenario |
|---|
| `mos_eisley_starport_deep_foyer4` |
| `hospital_to_cantina` |
| `starport_transit_terminal_to_collector` |
| `combat_reentry_cross_building` |
| `ten_sequential_cycles` |
| `two_bots_opposite_directions` |
| `bot_a_dwell_bot_b_traverse` |

Root cause, from the trace:

```
ST_PATH result=request  generation=3 building=1697358 cell=1697364
ST_FAIL reason=path_failed agent=281475039884620 generation=3
ST_PHASE generation=3 to=Idle reason=path_failed
ST_CLEARANCE result=would_block action=rejected hitAt=0.094
             blockingTemplate=object/building/naboo/filler_building_naboo_style_4.iff
```

**One refusal killed the whole traversal.** The design routed a refusal into
`onPathFailed()`, mirroring the `acceptFoundPath` rejection — but for a
structure traversal `onPathFailed()` is *terminal*, not a retry. The
`rejectionCap = 2` budget was therefore never reachable: the first refusal ended
the leg before a second could occur.

The two rejections are not alike, and treating them alike was the error. A stale
endpoint means the *request* was wrong. A clipping route means the destination
is still valid and still reachable, and only *this answer* is unusable.

**Fix**: `rejectClippingPath()` re-issues the same destination as a fresh path
request (`state = CALCULATING_PATH` + `SimPathFindTask`), falling through to the
old failure routing only when there is no agent or zone. Cell egress keeps
`failCellEgress()`, because its exit-set ladder advancing to the next candidate
*door* is a better answer than re-asking for the same one.

## Run 12 — safe, and that is all

**22 PASS / 4 FAIL. Zero regressions. Zero scenario-by-scenario differences vs
the baseline.** All seven run-11 regressions recovered. The four remaining
failures are the same tracked ones (D4, D6, two `controller_path_failed`).

| | baseline (OFF) | enforce (ON) |
|---|---|---|
| probed paths | 1226 | 1388 |
| refused, never walked | 0 | **227** |
| paths actually walked | 1226 | 1161 |
| of which obstructed | 139 | **113** |
| **clip rate of walked paths** | **11.3%** | **9.7%** |
| matrix wall time | 74.6 min | 74.7 min (**+0%**) |
| teleports / zSanity / resumeFailures | 0 / 0 / 0 | 0 / 0 / 0 |

The predicted budget pressure from extra repaths **did not materialise**:
+0% overall, worst single scenario +3.1 s on a 1374 s run.

## Why it barely works — the decisive measurement

Refusal-run lengths per bot, and the outcome of the path walked immediately
after each run:

```
consecutive refusals before the bot moved on:  {1: 1, 2: 113}
outcome of the path walked right after:        would_block 113,  skipped 1
```

**113 of 114 refusal sequences ran to the full cap and then walked the same
obstruction.** The pathfinder is deterministic: refuse → re-ask → identical
clipping route → refuse → re-ask → identical clipping route → budget spent →
walk it. Enforcement spends two extra path requests and arrives exactly where it
started.

The 1.6-point improvement is not enforcement succeeding on 26 paths; it is
mostly the reshuffling of which routes got probed at all.

**This is the failure mode the original D7 §6.1 anticipated.** Its answer was a
*blocked-leg ladder* — exit set → doorstep → navmesh → fail — where each rung
asks a **different question**. "Treat a blocked path as a failed path" was
chosen instead (owner, 2026-08-28), on my recommendation, and against a
deterministic pathfinder an identical re-request is the one response guaranteed
to change nothing. That recommendation was wrong.

## What is nonetheless established

- Enforcement is **safe to ship default-off**: no regression, no anomaly, no
  measurable cost, no crash across two full matrices.
- The refusal → repath → bounded → accept-and-walk lifecycle **works exactly as
  designed** and cannot strand a bot: 114 cap exhaustions, every one of which
  walked, zero frozen bots.
- The evidence trail survives enforcement — refusals never reach
  `state = MOVING`, and are admitted to the counters explicitly, so blocked
  routes cannot vanish from the record.
- `wouldBlock - blocked` is now the meaningful residual: **113 clipping walks**.

## Recommended next step

Make the retry **ask a different question**, i.e. build the §6.1 ladder rather
than re-request. Cheapest first rung with real prospects: force the retry onto
the navmesh (`useRecastPath`) when the direct answer is obstructed, and fall
back to the current identical re-ask only when that is unavailable. That is a
bounded change to `rejectClippingPath()` and reuses machinery already present.

Until then, `zeroClip.enforce` should stay **false** in committed defaults: it
costs 227 wasted path requests to remove 26 clips.

## Cleanup

`sim_player_manager.lua` restored, md5 `a63828df7e5a6366f05ff09a3f757ec7`,
`luac -p` clean. All gates default-off. Zero harness bots left. `core3` alive,
no crash markers.

## Evidence

- `bin/log/trip-verify-20260828-155527-d7p2-enforce.log` (1188 lines, run 11)
- `bin/log/trip-verify-20260828-163536-d7p2-repath.log` (2372 lines, run 12)
