# Live Verification — D7 Part 2, 3D ordering + elevation band

**Verdict: `LIVE_VERIFICATION_PASS` for the Part 2 contract.**
**All 8 assertions pass. The 240-violation regression is cleared.**

Read the scope carefully: this verifies that **Part 2's exit set and
per-candidate budget work correctly and without regression**. It does **not**
claim any scenario was fixed — 10 PASS / 15 FAIL is unchanged, and that was the
contract (A5 is "no regression", not "new passes").

- **Run ID**: `20260823-183815-d7-exit-set-elevation`
- **Change**: 3D ordering (was `distanceTo2d`) + elevation band
  `exitCandidateMaxVerticalMeters` (new Lua tunable, default 10 m)
- **Code state**: Codex APPROVED, build clean

## Assertions

| # | Assertion | Expected | Actual | |
| --- | --- | --- | --- | --- |
| A1 | `exitSetsBuilt > 0` | > 0 | 5 | PASS |
| A2 | `candidates > 0` on every build | > 0 | **1,2,1,2,2 — no starved set** | PASS |
| A3 | `exitCandidatesTried > exitSetsBuilt` | > 5 | **7** | PASS |
| A4 | no bot stuck while an **untried** candidate remained | none | `budgetExhausted=2` — nothing remained untried | PASS |
| A5 | run-3's 10 passes still pass | 10 | **10, zero regressions** | PASS |
| A6 | `blocked == 0` | 0 | 0 | PASS |
| A7 | `zSanityViolations == 0` | 0 | **0 (was 240)** | **PASS** |
| A8 | no crash, process alive | yes | gdb.log unchanged (876), alive 1h10m | PASS |

## The fix, measured

Same building that produced the regression:

| | Previous run | This run |
| --- | --- | --- |
| `doorNodes` | 5 | 5 |
| `candidates` | 3 | 1 |
| `rejectedElevation` | — | **4** |
| chosen | `dz ≈ −20` (77 m case elsewhere) | **`dz = −0.257`, 3.09 m** |

Across all five builds `rejectedElevation` was 4,1,4,1,1 and every retained
candidate sits within the band (`dz` = −0.257, −0.257, +5.40, +5.40, +5.40,
+9.70, +9.70). No build was starved to zero — the risk I flagged before the run
did not materialise, and `rejectedElevation` is on the build line so it would
have shown as data rather than as a mysterious empty set.

Supporting counters: `egressPathFailures` 10 → **6**,
`pathfinderFallbackActivations` 10 → **6**, `movement_anomaly` gone.

## What this does NOT fix, and what it reveals

The two med-centre scenarios returned to `controller_path_failed` — the same
failure as the pre-D7 baseline. That is the informative part:

```
exitSet=candidate index=0 ... distFromBot=87.6601 dz=9.6956
exitSet=candidate index=1 ... distFromBot=97.8726 dz=5.40068
```

The exit set now hands the controller **sane, level, in-bounds doors** — and
pathing to them still fails, at 84–98 m across a large interior. With Part 2
working, the residual med-centre failure **is D2**: the pathfinder cannot route
from the cell to a valid exterior door. Part 2 was never going to fix that; it
chooses *which* door, not *how to get there*.

Failure breakdown is now:

```
11  exit_not_outdoors        <- mode 2, walled hollow (D1's domain, unsolved)
 2  controller_path_failed   <- D2, med-centre routing
 1  target_cell_unresolved   <- D4
 1  combat_pause_not_observed <- D6, harness-side
```

## Honest limits of this PASS

- **A4 is satisfied as written but not in spirit.** `budgetExhausted=2` proves
  no *untried* candidate remained. It does **not** prove where the bot physically
  ended up after exhaustion — the harness makes no post-exhaustion position
  assertion. The owner's underlying concern ("does not clear and get stuck in
  the cell") therefore remains partly unmeasured. Closing it needs one assertion:
  after budget exhaustion, is the bot outdoors or still parented to a cell?
- The proposal's §5 criteria (`crossesGeometry == 0`; starport scenarios passing
  without clipping) are **not** met and were not in scope here — that is Phase 2
  and mode 2.
- `exitCandidateMaxVerticalMeters = 10` is a chosen default, not a derived one.
  It cleared the regression on these buildings; it is a Lua tunable precisely
  because the right value is an empirical question.

## Cleanup

Config restored to all gates off, with the new tunable retained and documented
(md5 `7a054f01b6f7c148b3cc65492469d39e` — the previous baseline predated the
knob and restoring it blindly would have silently dropped it). Sampler stopped.
Evidence at `bin/log/trip-verify-20260823-183815-d7-exit-set-elevation-structuretraversal.log`. No databases,
rosters or unrelated logs touched.
