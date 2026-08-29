# Live Test — hollow door egress (mode 2, Legs A + B)

**Verdict: LEG A WORKS. Leg B is the remaining work.**
Matrix **11 PASS / 14 FAIL** — unchanged from baseline, **zero regressions**.

A bot stuck on a starport pad now crosses the hollow to a real door,
**clip-free**, for the first time. It does not yet go *through* the building, so
no scenario flips yet.

- **Run ID**: `20260825-134205-door-egress-walk`
- **Walks executed**: 11
- `zSanityViolations` / `teleportsDetected`: 0 / 0. No crash (gdb.log 876).

## Leg A — measured working

```
doorEgress action=walking target=3618.86,-4845.27,11.04 dist=53.8574 attempt=1
ST_PATH  accepted nodes=14 -> (3618.86,-4845.27,5.085)
ST_CLEARANCE result=clear conclusive=1 segments=13 hitAt=none blockingTemplate=none
doorEgress result=found nearestDist=6.66524 botPos=3618.22,-4842.34
```

- `nearestDist` **53.86 -> 6.67**: the bot genuinely traversed the pad.
- `result=clear conclusive=1` over 13 segments: **no geometry crossed** — the
  same probe that measured D1's Option C punching the hull at `hitAt=0.069`.
  The owner's zero-clip bar is met on this leg.

## The doors, and a filter that was hiding one

With the bot at `(3575.89, -4813.35, 5.085)`, the starport's three exterior
`CellPortal` nodes are:

| Door | World | dz | Dist | |
| --- | --- | --- | --- | --- |
| 1 | `(3618.86, -4845.27, 11.04)` | +5.96 | 53.9 | ground, south end |
| 2 | `(3617.01, -4748.90, 15.34)` | **+10.25** | 77.1 | ground, **north end** |
| 3 | `(3618.35, -4801.29, 82.62)` | +77.53 | 89.2 | roof |

Doors 1 and 2 sit **96 m apart on opposite ends** of the building's north-south
axis — the owner's spur model confirmed by measurement, and the open risk from
the earlier scoping ("do both doors face the pad?") resolved: they do not.

**`exitCandidateMaxVerticalMeters = 10` was rejecting door 2 by 0.25 m.** That
threshold was hand-picked as "roughly one to two storeys" on 2026-08-23 and never
derived; it had been silently discarding half the door set. Now **20**, derived
from the measured heights: keeps both real doors, still rejects the roof by 57 m.

Rejected candidates are now logged with positions
(`exitSet=rejected reason=elevation world=... dz=...`). Reporting only survivors
is how a hand-picked threshold silently decides the answer — the fourth instance
of that pattern in this feature.

## What is missing — Leg B

The scenario still fails `exit_not_outdoors`, correctly: **arriving at a door is
not leaving.** The bot stands 6.7 m from the entrance, still in the hollow, and
the step times out.

One concrete detail for that slice: the accepted path ends at the door's x/y but
at **z = 5.085 (pad level), not 11.04 (door level)** — the door is ~6 m up. Leg B
must use `moveToInterior(worldPos, localPos, targetCell)` with the target
**cell**, not a move to the door's world point, or the bot will stand under the
doorway rather than enter it.

Everything Leg B needs is built and verified: ingress has been 100% reliable
across every run, and Retry 6's multi-cell egress is live-verified on two planets.

## Cost watch

`ST_CLEARANCE elapsedUs` reached **79,582** on a 76-node path with 239
candidates — well above the 19 ms p95 measured on 2026-08-20. Worth resolving
before D7 enforcement is ever enabled.

## Cleanup

All gates default-off. `exitCandidateMaxVerticalMeters = 20` **retained** — it is
a measured value, not a run override. New baseline md5
`d8559a65b6ea4a08dc7ef1aed66c905d`.
