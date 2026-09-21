# Live diagnosis — F_0.9.8 Phase 0 (P.10i lair tactics, prerequisite phase)

- **Run id**: `20260921-f098-phase0-diagnosis`
- **Plan**: `docs/1-plans/F_0.9.8_p10i-lair-tactics-and-mission-graduation.plan.md`
- **Branch**: `feat/p10i-lair-tactics-mission-graduation`
- **Scope**: Phase 0 only. Phases 1–5 unimplemented; Phase 3 blocked on this run.
- **Verdict**: **`DIAGNOSIS COMPLETE — PHASE 3 PREMISE INVALIDATED`**
- **UPDATE 2026-09-21 (later same day)**: the diagnosed pathing defect was fixed
  (commit `48ccf3125b`) and the mission loop now completes end to end. See
  "Outcome after the fix" at the end of this report — it reverses the assumption
  that lair tactics are needed for missions to complete.

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

**Root cause — CONFIRMED 2026-09-21, and it is not what I first proposed.**

I initially favoured the F_0.7.5 cell-entry class. **That is disproven.**
`GET /v1/object/7275419/` reports `TreeEntry.parent = 0` and
`zone = dantooine`: the terminal is an **outdoor** object with no parent cell,
so nothing about building entry applies.

The actual mechanism is the hybrid mover, and it is the P.4.1/P.4.2 problem
class in a new place:

1. `SimHunterController::usesNavmeshHybridMovement()` returns **true**
   (`SimHunterController.h:324`) — navmesh inside cities, overland in the wild.
2. Mode is chosen by **where the bot stands**, not by whether the destination is
   reachable: `onMeshMode = agent->isInNavMesh()`
   (`SimPlayerController.cpp:1297`). A navmesh exists for this POI
   (`bin/navmeshes/dantooine_city_dantooine_mining_outpost.obj`), so the bot is
   on-mesh and navmesh pathing is selected.
3. The terminal 37 m away is not reachable on that mesh — a small POI whose
   coverage does not span the approach.
4. On failure the mover retries **the same navmesh query**
   `pveNavmeshRepathTries` times and then gives up
   (`SimPlayerController.cpp:2342-2357`). **There is no overland fallback once
   on-mesh.**

So a bot fails to cross 37 m of open ground it could simply walk, because the
mesh it is standing on cannot route there and nothing demotes it to overland.

This is precisely what P.4.2 solved for **miners** with the `directOverland`
trust tier, after the same discovery that `findPath` degrades off-mesh
(ARCHI §8). Hunters never received an equivalent on the on-mesh failure path.

**Shape of the fix** (not implemented): when hybrid movement exhausts its
navmesh retry budget and the destination is outdoors, demote that leg to
overland instead of calling `onPathFailed()` — the miner behaviour, scoped to
hunters and gated. `terminal.terminalOid` resolution should also record the
parent so an interior terminal is routed through `enterStructure` per the
F_0.8.1 rule, which today it is not (`location.position =
terminal->getWorldPosition()` with no parent captured).

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


## Outcome after the fix (2026-09-21, later)

The overland-fallback fix (`48ccf3125b`) was applied and the same scenario re-run
with mission diagnostics on.

| Metric | Before | After |
| --- | --- | --- |
| `missionsCompletedTotal` | 0 | **5** |
| `missionsAbandonedTotal` | every attempt | **0** |
| `abandonReasons` | `mission_offer_path_failed` 87 of 107 | `{}` |
| Lairs spawned / destroyed | 0 / 0 | 6 / 6 |
| `TERM_PATH_FAILED` / `MISSION_PATH_FAILED` | 9 / (unlogged) | **0 / 0** |

**Blocker 1 is fixed, and it was one defect, not two.** `mission_offer_path_failed`
on the `TRAVEL_TO_MISSION` leg had the same cause as the terminal leg and was
fixed by the same change. Instrumentation added specifically to catch it
(`MISSION_PATH_FAILED`) recorded **zero** occurrences — it proved the absence of
the bug rather than finding a second one.

**Blocker 2 no longer reproduces.** `REVEAL_RELOCATE` succeeded 5 times with no
`REVEAL_RELOCATE_DEFER` loop. The relocation ring's missing exhaustion path is
still a real latent defect (an area where all 32 probes fail will still spin to
the 900 s timeout) but it is not currently blocking.

### The original Phase 0 question, finally answerable — and the answer is a surprise

Per-offer summaries:

```
membersSpawned=10 membersKilled=3  timeInHuntingMs=35713  endReason=lair_destroyed
membersSpawned=9  membersKilled=2  timeInHuntingMs=56050  endReason=lair_destroyed
membersSpawned=10 membersKilled=1  timeInHuntingMs=28381  endReason=lair_destroyed
membersSpawned=12 membersKilled=4  timeInHuntingMs=46814  endReason=lair_destroyed
membersSpawned=12 membersKilled=4  timeInHuntingMs=100019 endReason=hunter_died
membersSpawned=11 membersKilled=1  timeInHuntingMs=27706  endReason=lair_destroyed
```

**Bots destroy the lair while killing only 1–4 of 9–12 spawned members**, in
27–56 seconds. They do not need to clear the field, and the "engage ratio" that
Phase 0 set out to explain is not the limiting factor on completion at all.

The `LAIR_DAMAGE` attribution explains how: **47 `combat` events vs 33
`spawn_self`**. Roughly 40 % of lair damage is the lair damaging itself as waves
spawn (`spawnLairMobile`'s trailing `inflictDamage`). A bot that keeps pressure
on the lair wins partly because the lair destroys itself. Separating those two
sources — the attribution Codex pushed for during review — is what makes this
visible; an aggregate would have shown only "damage happened".

### Consequence for F_0.9.8

The re-scoping stands, but for a stronger reason than when it was written.
Missions complete at **5 of 6 with no tactics layer at all**. Lair tactics are
therefore an optimisation for survivability (1 of 6 ended `hunter_died`), not a
prerequisite for mission completion. Phase 3 should be re-justified on
death-rate and throughput, not on completion, before it is written.


## Sustained-run baseline (2026-09-21, gates enabled)

With `overlandFallbackOnNavmeshFailure = true` and the four hunter-family
reserves at 900000, the loop ran unattended. Clean 30-minute window:

| Metric | Measured |
| --- | --- |
| Missions (completed / abandoned) | 6 (4 / 2) — 67 % completion |
| Deaths per mission | **0.67** |
| Deaths per kill | 0.33 |
| Lairs spawned / destroyed / credited | 5 / 4 / 4 |
| Harvest | 14 980 units (~30 k/hour) |
| Crashes, fatals, DB errors | none |

The economy loop is closed and stable: hunters produce into the same three
families whose demand dispatches them, with no crash, no DB fault and no path
failure.

**The binding constraint is now survivability, not pathing or tactics.** A bot
dies on two missions in three. This is the measured baseline Phase 3 must beat,
with completion (67 %) and harvest (~30 k/hour) as guardrails so a "safe" tactic
that stops killing lairs registers as the regression it would be.

**Correction to an earlier reading in this report.** The cumulative counters
(11 lairs destroyed vs 6 missions completed) suggested lairs were dying
uncredited. The clean window shows 4 destroyed and 4 credited — exact
correspondence. That gap was startup noise, not a credit-loss defect.
