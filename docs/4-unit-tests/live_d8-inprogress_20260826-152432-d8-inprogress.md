# Live Verification — D8 InProgress outcome for in-flight entry legs

**Verdict: `LIVE_VERIFICATION_FAIL`.** Matrix **10 PASS / 15 FAIL** (count
unchanged for the third run), but the fix worked and finally exposed the real
blocker, which two earlier bugs had been masking.

- **Run ID**: `20260826-152432-d8-inprogress` — evidence `bin/log/trip-verify-20260826-152432-d8-inprogress.log` (2469 lines)
- Restart clean, 0 players, ready in 11s. Counters: `zSanity/teleports/resumeFailures = 0`

## Progression across the three runs

| Signal | run 1 (gates on) | run 2 (+entry gate) | run 3 (+InProgress) |
|---|---|---|---|
| door entries | 11 | 11 | **13** |
| `escalation reason=attempt_cap` | 11 | 0 | 0 |
| `ST_FAIL path_failed` | 12 | 3 | 3 |
| dominant failure | `controller_path_failed` x13 | `exit_not_outdoors` x11 | `exit_budget_exceeded` x11 |
| exit step lifetime after entry | 1.0s | 1.06s | **53.1s (full budget)** |

Each fix removed a premature termination and let the scenario run longer. The
failure reason walking from `path_failed` -> `not_outdoors` -> `budget_exceeded`
is the signature of that.

## The real blocker: the bot freezes on the entry leg

```
t=494624  doorEgress action=entering door=3613.8,-4845.38,5.83582 cellIndex=15 cellOid=1106383
t=494733  ST_PHASE Egress -> ApproachDoor reason=entry_path_found
t=494733  ST_PATH result=accepted nodes=4 world=(3613.8,-4845.38,5.83582) cell=1106383
          local=(-44.1848,6.03827,0.835817)
          <-- 53 SECONDS OF COMPLETE SILENCE for this agent -->
t=547610  SCENARIO_RESULT status=FAIL reason=exit_budget_exceeded
```

Grepping the agent OID across that window returns **nothing**: no re-path
request, no phase change, no arrival, no stuck-watchdog line. A valid 4-node
path into cell 1106383 was accepted and then never executed.

For contrast, every ordinary `outdoor_enter` on the same bot works:
`ApproachDoor` -> path accepted -> ~9s -> `entered_structure` ->
`target_cell_arrived`.

## Leading hypothesis: leash suppression blocks the cell transition

The Leg B entry is the ONLY move issued while the `simPvpCombat` no-op-MOVE map
is installed AND targeting a cell.

- Leg A (outdoor walk to the door, 49.6m in 12.8s) works under suppression, so
  the controller can drive OUTDOOR movement with the MOVE socket stubbed.
- Leg B targets a CellObject. Crossing a portal changes the agent's parent, and
  that transition is driven by the agent's own movement machinery
  (`setNextStepPosition` / `activateAiBehavior`), which the no-op MOVE tree
  stubs out. The path is found; nothing executes it.
- `leash=restored` appears only at the scenario reset 53s later, confirming
  suppression was installed for the whole frozen window.

## Decisive next experiment (config only, no rebuild)

Re-run with `hollowDoorEgress.suppressLeash = false` and everything else
identical. If the bot then crosses into cell 15, suppression is the blocker.

This also re-tests the original leash hypothesis on its merits: the evidence
that motivated `suppressLeash` was a re-path back to mid-pad, and we have since
found and fixed TWO mechanisms (escalation re-check, NotHandled arrival tail)
that could produce exactly that symptom. Suppression may no longer be needed at
all.

## Unchanged, pre-existing

`theed_starport_hangar` (D4), `attacker_dies_instantly` (D6, harness-side),
`naboo_hospital_enter_exit` + `cantina_to_corellia_hospital`
(`controller_path_failed`).

## Cleanup

`sim_player_manager.lua` restored to default-off, `luac -p` clean. Running
server still has gates live until the next restart.
