# Live Verification — D7 Phase 2 after Codex review

**PASS on all three conditions the code review required.** 22 PASS / 4 FAIL,
zero regressions, zero scenario differences against both the no-enforcement
baseline AND the pre-review run. One honest caveat: the reference-counted
snapshot costs ~20% at p50, reported below rather than glossed.

- Run: `20260829-090817-d7p2-postreview`
- Compares against: `20260828-101454-d2b-harness-fix` (enforce off) and
  `20260828-223832-d7p2-walkable2` (pre-review, run 15)
- Config: run-9 gates + `zeroClip.enforce`, `zeroClip.walkableConfirm`

## Codex's three conditions

### 1. Movement verdicts unchanged — PASS

```
PASS = 22
regressions vs no-enforce baseline: NONE
scenario diffs vs baseline: 0
scenario diffs vs run 15  : 0
```

The three review fixes are behaviour-neutral for movement, as intended.

### 2. True `capExhausted` count — PASS

| | run 15 (over-counted) | run 16 (correct) |
|---|---|---|
| clearanceChecks | 987 | 1129 |
| wouldBlock | 96 | 108 |
| blocked | 64 | 72 |
| **capExhausted** | **33** | **36** |
| walkableReclassified | 110 | 109 |

The absolute figure went *up*, which is not a contradiction: run-to-run traffic
volume differs (987 vs 1129 probes) by more than the correction removes. The
meaningful comparison is the rate — 33/923 = 3.6% pre-review, 36/1057 = 3.4%
post-review. Stable. What the fix guarantees is that every counted exhaustion is
now a route that actually reached `state = MOVING`; before, a path rejected
afterwards by combat or endpoint validation still incremented it.

### 3. Probe cost — REGRESSION PRESENT, ~20% at p50

| run | n | p50 µs | p95 µs | max µs |
|---|---|---|---|---|
| run10 baseline, raw snapshot | 1226 | 8332 | 47373 | 91211 |
| run15 pre-review | 987 | 7849 | 47748 | 97573 |
| **run16 post-review** | 1129 | **10219** | 46152 | 97341 |

Attributed, not guessed. Cost rose across **every** outcome including `skipped`,
which does almost no narrow-phase work:

| outcome | run15 p50 | run16 p50 |
|---|---|---|
| skipped | 6769 | 8321 |
| clear | 10847 | 14432 |
| would_block | 7162 | 8554 |
| truncated | 9844 | 11217 |

Meanwhile the workload is identical — segments probed per path p50 3 / p95 62-63
/ mean 14.2 vs 14.6. Same work, ~20% more time, on every outcome. That is the
signature of the `SortedVector<ManagedReference<TreeEntry*> >` snapshot
acquiring and releasing a reference per candidate (up to `maxCandidates` = 256),
paid on every probe regardless of outcome.

**Assessment: accept it.** It is the price of not having a use-after-free, p95
and max are flat-to-better (the tail is narrow-phase dominated, not snapshot
dominated), and the whole probe runs on the pathfinding worker thread, off the
movement path, behind a gate that ships off.

## Clip rate holds

**3.4% of walked paths** (baseline 11.3%, run 15 3.5%). 109 reclassifications.
All anomaly counters zero: teleports 0, zSanity 0, resumeFailures 0. Zero bots
left alive.

## What is NOT certified

**D7 §5.1 — `crossesGeometry == 0` — is NOT met and is not being claimed.** The
residual 36 walked obstructions are genuine clips through starport shells and
debris. Reaching 0 needs a detour generator or navmesh coverage of those
objects, both out of scope. `zeroClip.enforce` and `zeroClip.walkableConfirm`
therefore ship **default-off**.

Deferred with evidence: Codex's maximum-horizontal-deviation discriminator,
which is better than the length ratio and closes the documented hole where a
short lateral wall hides inside a long segment. Changing the discriminator
invalidates the measurement above and needs its own matrix.

## Cleanup

`sim_player_manager.lua` restored to committed defaults — `enforce = false`,
`walkableConfirm = false`, `farSideEgress = false` — verified in the container,
`luac -p` clean. `core3` alive, no crash markers.

## Evidence

- `bin/log/trip-verify-20260829-090817-d7p2-postreview.log` (2112 lines)
