# Live Verification — D8 entry-leg gate on hollow escalation

**Verdict: `LIVE_VERIFICATION_FAIL`.** Matrix **10 PASS / 15 FAIL** (unchanged
count), but the gate WORKED and the blocker moved two layers deeper.

- **Run ID**: `20260826-100531-d8-entryleg-gate` — evidence `bin/log/trip-verify-20260826-100531-d8-entryleg-gate.log` (2095 lines)
- Binary built 17:05:09, process started 17:05:35 (fresh binary confirmed)
- Restart clean, 0 players disconnected, ready in 12s
- Counters: `hollowEscalationsFailed 11 -> 0`, `zSanity/teleports/resumeFailures = 0`

## The gate did what it was supposed to

| Signal | before gate | after gate |
|---|---|---|
| `escalation=result reason=attempt_cap` | 11 | **0** |
| `ST_FAIL reason=path_failed` | 12 | **3** |
| door entries (`result=entered`) | 11 | 11 |
| `repair=multicell` invocations | (never fired) | **24** |

The escalation no longer preempts the entry leg it just scheduled.

## Why the score did not move — the harness stops measuring too early

Failure profile returned to the BASELINE shape: 11 `exit_not_outdoors`
(was 13 `controller_path_failed`).

Passing cantina exit:
```
Egress -> (78.5s) -> ST_PHASE to=Idle reason=exit_complete_outdoors -> pass=1
```
Failing starport exit:
```
t=435548  doorEgress action=walking  dist=49.6334
t=448308  doorEgress action=entering cellIndex=15 cellOid=1106383
t=448418  ST_PHASE Egress -> ApproachDoor reason=entry_path_found
t=449474  ST_HARNESS exitAssert pass=0 inHollowOfScenarioBuilding=1
t=449474  op=exit status=FAIL durationMs=68546   <- budget is 120000
```

**The exit step did not time out** (68.5s of a 120s budget). It was ended by an
arrival notification 1.06s after Leg B was issued.

Root cause: `SimTraversalTestController::onArrived()` notifies the harness
unconditionally:

```cpp
void SimTraversalTestController::onArrived() {
    if (agent != nullptr)
        SimPlayerManager::instance()->notifyStructureTraversalTestArrived(
            agent->getObjectID());
}
```

It cannot distinguish "the traversal finished" from "an intermediate leg
finished". Leg A's arrival AT THE DOOR is reported as the traversal's arrival,
so `completeStructureTraversalTestStep(..., "PASS", "arrived")` fires and the
exit assertion runs while the bot is still 1.95m outside, mid-Leg-B.

This is **harness/observability**, not production movement. It is the third
instance of one pattern in this feature — an intermediate leg boundary treated
as terminal — after `external_move_preemption` and the escalation re-check.

## Recommended fix (NOT applied)

Gate the notification on the traversal actually being finished, e.g.
`if (!isTraversalActive())` in `SimTraversalTestController::onArrived()`.
Ordering is favourable: `completeStructureTraversalIfArrived()` clears the
traversal on genuine completion BEFORE `onArrived()` runs (observed: cantina
goes `to=Idle reason=exit_complete_outdoors` 0.5s before its assert), while the
door-egress path returns `Started` with the traversal still active. Worst case
on a mistake is a step timing out against its budget, not a false PASS.

**Important:** this fix does not by itself prove the starport egress works. It
only lets the scenario run long enough to find out whether the interior route
(cell 15 -> west exit) completes. `repair=multicell` fired 24 times this run,
which is the first time that code has been exercised here, but that is
suggestive, not proof.

## Unchanged, pre-existing

- `theed_starport_hangar` — `target_cell_unresolved` (D4)
- `attacker_dies_instantly` — `combat_pause_not_observed` (D6, harness-side)
- `naboo_hospital_enter_exit`, `cantina_to_corellia_hospital` —
  `controller_path_failed` (2 of the 3 remaining path failures)

## Cleanup

`sim_player_manager.lua` restored to default-off, `luac -p` clean. The RUNNING
server still has the gates live in memory; the restored file applies on the next
restart, which is the owner's call.
