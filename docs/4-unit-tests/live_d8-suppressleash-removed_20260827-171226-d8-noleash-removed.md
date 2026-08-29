# Live Verification — suppressLeash removal (regression check)

**No behavioural regression.** Matrix **11 PASS / 15 FAIL of 26**, identical to
run 7 scenario-by-scenario. The run also caught a reconstruction defect in
telemetry-only code, now fixed.

- **Run ID**: `20260827-171226-d8-noleash-removed` — evidence `bin/log/trip-verify-20260827-171226-d8-noleash-removed.log`
- Restart clean, 0 players, ready in 14s. 26 scenarios armed.
- Dashboard confirms removal end-to-end: `hollowDoorEgress` keys are now
  `['observe','useCellPortals','walk']` — no `suppressLeash`.
- `leash=` log lines: **0**.

## Regression comparison vs run 7

| | run 7 | run 8 |
|---|---|---|
| result | 11 PASS / 15 FAIL | 11 PASS / 15 FAIL |
| failure profile | 13 `controller_path_failed`, 1 `target_cell_unresolved`, 1 `combat_pause_not_observed` | **identical** |
| transit scenario | PASS 84074 ms | PASS 84077 ms |
| anomaly counters | zSanity/teleports/resumeFailures 0 | **identical** |

## What the removal bought

- All five `getStructureTraversalBaseAiMapHash` declarations deleted, including
  both released-header overrides. `SimPvPController.h` / `SimHunterController.h`
  now differ from released only by `isCombatDriverActive()`, which is
  load-bearing (9 uses; gates `beginHollowEscalation` and the arrival branch).
- **D8's entire contribution to D9 is gone**: the AI-map swap, the
  generation-owned restore, the three-member suppression state, and every
  cross-thread install/restore path no longer exist. D9 now reduces to the two
  pre-existing resume-monitor sites plus the pause site, none introduced by D8.

## Defect found and fixed by this run

Removing `suppressLeash` accidentally deleted two unrelated functions that sat
between the leash helpers and `clearStructureTraversalState`:
`advanceTraversalGeneration` and `setStructureTraversalPhase`. The link failed,
which caught it. Neither git (the feature is uncommitted) nor persisted tool
output had a copy, so both were **reconstructed from evidence, not recovered**.

`advanceTraversalGeneration` was reconstructed verbatim from an earlier read.

`setStructureTraversalPhase` was reconstructed from its logging tail plus
inference — and the inference was **WRONG**. I omitted a redundant-phase
early-return, reasoning that callers guard (`pauseStructureTraversal` does) and
that run 7 showed no `from=X to=X` lines. That absence was in fact PROOF the
guard existed. This run produced **128 no-op transitions** the original never
emitted:

```
36  from=ApproachDoor to=ApproachDoor
91  from=Egress to=Egress
 1  from=InteriorRoute to=InteriorRoute
```

Behaviour was unaffected (assigning a phase to itself is a no-op, and the matrix
result was byte-identical), but the trace was polluted. The guard is restored and
the build is clean. **Not yet re-verified live** — the fix is diagnostic-only, so
the next matrix run should show zero `from=X to=X` lines.

## Standing risk

`setStructureTraversalPhase` is the one function in the tree that is
reconstructed rather than authored or verified. It should get explicit attention
in the Codex review.

## Cleanup

Gates restored to default-off after the run.
