# Live diagnosis — F_0.9.8 Phase 0 (P.10i lair tactics, prerequisite phase)

- **Run id**: `20260921-f098-phase0-diagnosis`
- **Plan**: `docs/1-plans/F_0.9.8_p10i-lair-tactics-and-mission-graduation.plan.md`
- **Branch**: `feat/p10i-lair-tactics-mission-graduation`
- **Scope**: Phase 0 only. Phases 1–5 unimplemented; Phase 3 blocked on this run.
- **Verdict**: **`DIAGNOSIS COMPLETE — PHASE 3 PREMISE INVALIDATED`**

## What this run was for

Phase 0 exists to answer one question before Phase 3 writes a tactic state
machine: **why were only 10 of 48 reached lairs ever explicitly engaged?**
The plan deliberately refused to build a remedy for a guessed cause.

## Result: the question could not arise

**No mission reached a lair.** The lair-phase instrumentation produced zero
records because no lair was ever revealed.

| Event | Count |
| --- | --- |
| `OFFERS_ACCEPT` | 1 |
| `TERM_RESOLVED` | 10 |
| `TERM_PATH_FAILED` | 9 |
| `MISSION_CLEANUP` | 9 (**all** `mission_terminal_unavailable`) |
| `REVEAL_SPAWNCHECK` / `REVEAL_RELOCATE_DEFER` | 33 / 33 |
| `LAIR_REACHED`, lair revealed | **0 / 0** |
| `LAIR_FIELD`, `LAIR_DAMAGE`, `LAIR_MEMBER_KILL` | **0** |

111 new diagnostic lines, 6 hunter bodies, ~4 minutes of traffic.

## Two reproducible upstream blockers

### 1. Mission terminal unreachable at 37 m (5 of 6 bots)

```
TERM_RESOLVED   id=1 planet=dantooine city=mining_outpost type=general
                terminalOid=7275419 dist=37.4593 moveIssued=true
TERM_PATH_FAILED id=1 terminal=(-608.795,2481.63,2.9935) pos=(-640.502,2501.58,3.1466)
TERM_FALLBACK   id=1 phase=TRAVEL_TO_TERMINAL boardEnabled=true
→ MISSION_CLEANUP reason=mission_terminal_unavailable
```

The terminal **resolves**, the move **is issued**, and the path fails — from a
byte-identical position, every ~30 s, 9 times in 4 minutes. `TERM_FALLBACK`
fires and does not rescue it.

This is the historical dominant failure: `mission_offer_path_failed` +
`mission_terminal_unavailable` were **87 of 107** abandonments in the 2026-07-28
log. It is now reproducible on demand.

Two candidate causes, **neither confirmed**:
- The F_0.7.5 class (ARCHI §12) — deterministic cell-entry path failure from a
  fixed outdoor point. F_0.7.5 fixed this for **buff providers** via
  `realBuffs.crossBuildingStaging`; the mission-terminal approach never got it.
- Sparse navmesh at a small POI (ARCHI §8), needing a `directOverland`-style
  fallback on the terminal leg. The near-equal heights (terminal z=2.99 vs bot
  z=3.15) fit this better than a building-cell target.

Distinguishing them is the first task of any fix.

### 2. Lair reveal defers forever (the one bot that got through)

```
REVEAL_SPAWNCHECK id=2 advertised=(-4472.56,3640.5,7.43472) size=35
                  p45=false p0=true inBounds=true height=7.43472
REVEAL_RELOCATE_DEFER id=2 probes=32 offset=0
```

Repeated **33 times on a 2-second cadence**, unchanged. The advertised point is
spawn-permitted at margin 0 but not at margin 45, and **all 32 relocation
probes** (rings 16/32/48/64 m × 8 bearings) fail too.

The root cause is already documented in-code at `SimPlayerManager.cpp:19899`
("29/29 p45=false"): a point validated at offer generation is no longer
permitted when the hunter arrives. The relocation ring was the mitigation. What
this run adds is that **the mitigation has no exhaustion path** — when every
probe fails it returns `DEFER` and the controller retries forever, bounded only
by the 900 s `TRAVEL_TO_LAIR` phase timeout. That is ~450 wasted cycles per
mission before it gives up.

## Consequence for the plan

**Phase 3's premise is invalidated, not merely unproven.** A lair-tactics state
machine cannot improve mission completion while missions do not reach lairs.
The plan's own hold point exists for exactly this outcome and did its job: the
tactic layer was not built on a guess.

The 10-of-48 engage ratio remains unexplained, and **should not be
investigated further until the mission loop delivers bots to lairs** — there is
no way to observe it otherwise.

## Recommendation

Re-scope before writing Phase 3. The valuable work is a **mission-loop repair
chunk** covering the two blockers above, which stand between the roster and
every downstream P.10 chunk that depends on mission traffic (credits, loot,
bazaar). Phases 1, 2, 4 and 5 of F_0.9.8 remain independently implementable.

## Note on demand saturation

Mission traffic required raising four demand profiles 500k → 900k, because
supply (meat 587k / hide 531k / bone 505k) sat above the shipped 500k ceiling,
so pressure was 0 and the matchmaker issued **no orders at all** — the deadlock
described in roadmap §5c.1. This run confirms that deadlock is real and current:
without an in-session consumer, the hunter economy cannot start itself.

## Telemetry defect confirmed

The dashboard reported `abandonReasons = {path_failed_TRAVEL_HOME: 3}` for the
same three missions `missiondiag.log` recorded as `mission_terminal_unavailable`
— the reason-overwrite noted in roadmap §5c.4. **Trust `missiondiag.log`.**

## Config and cleanup

| | |
| --- | --- |
| Shipped config | `48f999be98715c150fc8ad806fbce8ea` |
| Verification config | `a21c462d677b0ff32a3379cdb95a1b18` (`missionDiag.logging=true`, 4 profiles 500k→900k) |
| Restored | `48f999be…` verified, 0 `VERIFY-20260921` markers, `luac -p` clean |

Server restarted on the shipped config, dashboard HTTP 200. No database
cleared; no unrelated log touched.

## Evidence

- `bin/log/missiondiag.log` (delta from offset 2642880)
- `bin/log/trip-diag-20260921-f098-phase0-diagnosis.interim-upstream.txt`
- `bin/log/trip-diag-20260921-hang-objectmanager-deadlock.*` (unrelated: the BDB
  panic and deadlock recovered earlier the same day)

## Unrelated incident recovered during this session

The Berkeley DB environment panicked with `DB_RUNRECOVERY (-30973)`, which
deadlocked the server (`ObjectBrokerAgent::startBackup` stops the task manager
and does not restart it on the exception path) and then aborted a restart in
`ServerCore::initialize`. Repaired with `db5.3_recover` after a verified
byte-for-byte backup (`~/dbbackup-panic-20260921-152205`). Five consecutive
delta saves completed cleanly afterwards. **`core3 runUnitTests` opens this same
environment and is the prime suspect**; it should not be run against a live
server.
