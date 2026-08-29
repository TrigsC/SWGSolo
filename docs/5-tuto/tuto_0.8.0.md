# Tutorial 0.8.0 — Asymmetric failure policy: when to fail open, when to fail closed

The core principle behind F_0.8.0 is not the traversal state machine. It is a
decision this release had to make **twice, in opposite directions, in the same
subsystem, within the same week** — and getting the direction wrong was, both
times, the actual defect.

The question: *when a check cannot reach a conclusion, what do you do?*

## The two sites

Both live in the structure-traversal stack. Both are "I ran a check and the
answer was inconclusive." They resolve oppositely.

### Site 1 — the harness exit assertion: fail CLOSED

`SimPlayerManager.cpp`, the `step.op == "exit"` branch of
`completeStructureTraversalTestStep`. The scenario matrix asserts a bot really
got out of a building. It needs the building's OID to test containment.

The original code walked *backwards through the scenario config* looking for the
most recent step that named a `buildingOid`:

```cpp
uint64 assertBuildingOid = step.buildingOid;

for (int back = cursor; back >= 0 && assertBuildingOid == 0; --back)
    assertBuildingOid = steps.get(back).buildingOid;
```

Two ways that lies. `naboo_hospital_enter_exit` pins only a `cellOid`, so the
scan yields `0` — and `isHarnessOutdoorsClearFor(0)` returned **true**.
`cantina_to_corellia_hospital` enters a cantina and *then* a hospital, so the
scan returns the **cantina** while the bot is exiting the **hospital**.

The fix is not just "resolve the building better." It is that the fallback
direction was wrong:

```cpp
uint64 assertBuildingOid = structureTraversalTestBotBuildingOid[botIndex];

if (assertBuildingOid == 0) {
    assertionPassed = false;   // fail CLOSED
} else {
    assertionPassed = controller->isHarnessOutdoorsClearFor(assertBuildingOid);
}
```

and inside the predicate, every unresolvable case — no OID, no zone, a dead OID,
a non-building object — returns `false` rather than `true`.

**Why closed:** this is a *test oracle*. Its only product is the number the
whole feature is judged on. An oracle that cannot see must not certify the
subject, because a false PASS is indistinguishable from success and silently
corrupts every decision downstream. This project had already been burned by
exactly that — a matrix pass that was really a bot walking through a wall.

### Site 2 — zero-clip enforcement: fail OPEN

`SimPlayerController::shouldRejectClippingPath`. The probe reports whether a
path intersects geometry, with outcomes `clear`, `would_block`, `skipped`,
`truncated`, `error`.

```cpp
if (manager == nullptr ||
        !manager->isStructureTraversalZeroClipEnforceEnabled() ||
        !result.wouldBlock())
    return false;
```

Only `WouldBlock` refuses. `Skipped`, `Truncated` and `Error` all fall through
to "walk it."

**Why open:** this is a *mover*, not an oracle. Roughly half of all probes on
this server are inconclusive **by construction** — the aggregation is
worst-evidence-first, so any cell-local segment downgrades the whole path to
`Skipped`. Failing closed here would refuse the majority of all movement and
freeze the server. The cost of a wrong "allow" is a cosmetic clip; the cost of a
wrong "deny" is an outage.

## The rule

> Fail closed when the output is a **claim**. Fail open when the output is an
> **action** whose denial is worse than its error.

An oracle's wrong answer propagates into every decision that trusts it. A
mover's wrong answer is one bad step, self-correcting on the next tick. That
asymmetry — not the subject matter — decides the direction.

## The same principle, third time: the rejection budget

`zeroClip.rejectionCap` is the same idea at a different scale. Enforcement
refuses an obstructed route, but only twice:

```cpp
if (zeroClipRejections >= rejectionCap) {
    capExhausted = true;
    return false;                 // walk it anyway
}

zeroClipRejections++;
```

A pathfinder that deterministically returns the same clipping route would
otherwise refuse forever. So the *bounded* form of "fail open" is: try to do the
right thing a fixed number of times, then take the survivable failure and
**record that you did** (`zeroClip.capExhausted`). Silence would have been the
real bug — an enforcement that quietly gives up looks identical to one that
works.

Run 11 proved the point by getting this wrong. The first implementation routed a
refusal into `onPathFailed()`, which for a structure traversal is **terminal**:

```
ST_PATH result=request   generation=3
ST_FAIL reason=path_failed generation=3
ST_PHASE to=Idle reason=path_failed
ST_CLEARANCE result=would_block action=rejected
```

One refusal killed the traversal, the cap was never reachable, and seven
scenarios regressed. The bound only protects you if the thing it bounds can
actually run more than once.

## Corollary — a check that cannot see must say so

The reason these decisions are even makeable is that the probe never collapses
"I found nothing" into "there is nothing." Five explicit outcomes, aggregated
worst-evidence-first: a path is `clear` only when **every** probed segment was
conclusively clear. That is what let the same measurement drive two opposite
policies — and what let the walkable-confirmation work discover that ~half the
"blocks" were bridge stairs rather than walls.

Collapse your inconclusive state into either success or failure and you lose the
ability to choose. Keep it, and the policy becomes a one-line decision at each
call site — which is exactly what it should be.
