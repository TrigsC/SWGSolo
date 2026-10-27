# Live verification: F_0.9.18 P.10p G4 group tactics

**Plan**: `docs/1-plans/F_0.9.18_p10p-g4-group-tactics.plan.md`
**Primary run ID**: `20261023-035349-f0918-g4-group-tactics` (seven boots in all, listed below)
**Evidence**: `MMOCoreORB/bin/log/trip-verify-20261023-035349-f0918-g4-group-tactics-evidence.log`
**Verdict**: PASS on owner-accepted evidence: the scoped harness (9/10 scenarios, 1 accepted as environment),
the post-fix regression and pull boots, and the owner's in-client approval of the final build ("I think this is
in a pretty good place and if it needs more revisions it will be bug fixes", 2026-10-27).

## Scope (owner rule: receipt runs stay limited to what the release changes)

- **Scenarios**: 10, run with `onlyScenarios`, `continueOnFailure = true`, and `maxRunMinutes = 120`.
  - 6 G4 scenarios: `group_tactics_eat`, `group_tactics_pull`, `group_tactics_pull_nothing`,
    `group_tactics_spin_poll`, `group_tactics_spin_toggle`, `group_tactics_gate_off`.
  - 4 regression scenarios on code G4 touched: `group_mission_grab_and_run`, `group_mission_follow_cancels`,
    `group_follow`, `group_gate_off`.
- Each re-run after boot 2 was scoped to the scenario that failed and agreed with the owner before the restart.

## Changed subsystems

- `SimBotGroupTactics.h` (new), `SimBotGroupRally.h` (tactic tier), `SimBotGrouping.h` (filler helpers moved).
- `SimPlayerManager` (gate, group tactics state, per-bot orders, dispatch, lair observer helpers, food `force`
  and result, spin ability fact, counters, dashboard, harness ops).
- `SimHunterController` (order handling, pull sub-mode, spin mode, back off, group area guard, movement leases).
- Stock seams: `SimBotAreaAttackGuard.h` flags, `AiAgentImplementation.cpp` (`forbidArea` skip,
  `findPreferredAreaAbilityForTarget`, shared HAM/reserve helper), `AiAgent.idl` (one native method),
  `ai/bt/leaf/SimpleActions.h` (spin preference before the special roll).

## Build and unit tests

- Warning-clean incremental build on every iteration; `luac -p` clean after every Lua edit.
- `SimBot*` gtests with the server stopped, under `timeout -s KILL 120`: 217/217, then **218/218** after the
  per-phase pull budget test. No `databases/` fd holders after either run.
- Codex reviewed and APPROVED every post-approval delta before the boot that ran it.

## Boots

| Boot | Scope | Outcome | Action |
|---|---|---|---|
| 1 | 10 | 6 PASS, 4 FAIL | `assertGroupTactics` read identity 0 (it ran before the identity block); an empty `identityRef` resolved to a different bot than the one borrowed (latent G3-era harness bug); the eat scenario seeded a production vault (correctly refused by the harness); product: a creature attacking before a pull target was chosen aborted the pull (now adopted). |
| 2 | 10 | 8 PASS, 2 FAIL | Product: the returning puller was pinned on its creature by the combat tree and self-defence (now: the bot drops its own chase, keeps the creature's aggro; a rifle/carbine/pistol keeps shooting on the run, owner request). `pull_nothing`: wild creatures within 64 m of the stand-in (environment). |
| 3 | pull | FAIL | Product: the pull budget counted from the order; each phase now has its own budget, APPROACH can no longer hang. Return telemetry added. |
| 4 | pull | FAIL | Telemetry: bot `MOVING` in place during RETURN; RETURN now replaces the path at once. |
| 5 | pull | FAIL | Still no movement; owner approved a diagnostic boot. |
| 6 | pull + cell-nav trace | FAIL (diagnostic) | Trace: the APPROACH path was never issued; the mover kept the G1 follow destination. Earlier "tags" were creatures catching the bot as it walked by. Fix: the first APPROACH tick replaces the follow path; the tag range is the weapon's own reach (was the hunter's 48 m default). |
| 7 | pull | **PASS** | Walked to the kreetle, tagged at 3 m, back beside the leader in 3 s (`pull_returned`). |

## Assertion table

| # | Scenario / check | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | `group_follow` | PASS | PASS (boots 1, 2) | PASS |
| 2 | `group_gate_off` | PASS | PASS (boots 1, 2) | PASS |
| 3 | `group_mission_grab_and_run` | PASS | PASS (boots 1, 2) | PASS |
| 4 | `group_mission_follow_cancels` | PASS | PASS (boots 1, 2) | PASS |
| 5 | `group_tactics_eat` | order dispatched, one eat outcome per "take foods" | PASS (boot 2) | PASS |
| 6 | `group_tactics_pull` | order, target, tag, return to the leader | PASS (boot 7) | PASS |
| 7 | `group_tactics_pull_nothing` | "Nothing to pull." | wild creatures in range at the stand-in spot | ENVIRONMENT (owner accepted; `choosePullTarget` "nothing" reason gtested) |
| 8 | `group_tactics_spin_poll` | one reply per bot (+2) | PASS (boots 1, 2) | PASS |
| 9 | `group_tactics_spin_toggle` | spin on/off per group, cleared when every bot leaves | PASS (boot 2) | PASS |
| 10 | `group_tactics_gate_off` | rejected, no state | PASS (boots 1, 2) | PASS |
| U1 | Clean startup on every boot | ready | fresh `who.json` in 7-13 s, all 7 boots | PASS |
| U2 | No crash or backtrace | `gdb.log` unchanged | unchanged since 2026-10-17 | PASS |
| U3 | Dashboard root | `playerBotProgression.grouping.tactics` present, `enabled: false` by default | present | PASS |

## Evidence excerpts

- Spin poll reply: `GROUP_SPIN id=848 spin_poll_reply ability=unarmedspinattack2 ham=100 armor=2`.
- Boot 6 trace (pulling bot): `ARRIVAL_TICK state=MOVING ... destination=world=(3463.65,-4884.38,...)`, the
  follow slot beside the stand-in, for the whole "approach".
- Boot 7: `pull_target ... name=a kreetle candidates=21`, `TRAVEL_ENGAGE ... distM=3`,
  `pull_return dist=22.6676 ... lease=true`, `pull_returned`.

## Post-harness boots

| Boot | Scope | Outcome | Action |
|---|---|---|---|
| 8 | 5 (grab_and_run, follow_cancels, follow, spin_toggle, pull) | 4 PASS, pull FAIL | Regressions confirm the follow fix did not break G3. Pull: `onTick` re-engaged the pulled creature; it now skips self-defence during the pull return and back-off. |
| 9 | pull | **PASS** | Tagged at 3 m, walked back 24 m with no re-engage, `pull_returned` in 3 s. |

## Owner in-client pass (2026-10-23 to 2026-10-27, Dantooine, bots Paki, Oloopo, Nordihi, Towe)

Each round was read from `missiondiag.log` (`GROUP_PULL`, `GROUP_SPIN` with a rate-limited `spin_check` reason
line, `GROUP_MISSION`, `GROUP_DIRECTIVE`), fixed, built warning-clean, approved by Codex and restarted with the
owner's agreement. gtests 218/218 with the server stopped after each pure-header change.

| Owner finding | Cause (from the logs) | Fix |
|---|---|---|
| Following dropped about a minute after each "follow" | The nearest-mission refresh re-published `MISSION_WAIT` (G3) | Publishes only while the plan runs |
| Spin bots never engaged (`spinEngages` 0) | Spin ran only in the mission-fight branch; then a 4 m stand point the lair footprint blocks | Stage-free `runGroupSpin`; engage within stand + footprint margin or after 15 s |
| Bots sat out wave 1 | Trigger needed the leader in combat | Owner rule: lair aggro or "attack the lair" |
| Second mission never started | Entry completed only through the holder's controller, whose context "follow" had cleared | Manager completes the entry on lair destruction; run resumes |
| "get buffs" left bots in the wild | Non-holders carried the mission lair (`controllerBusy`); refresh overwrote the buff directive | Holder-only lair fields; G2 trips are not overwritten |
| Group left lair 1 mid-fight for mission 2 | Refresh switched the current entry to a nearer advertised point | Current revealed lair is kept until destroyed |
| Pull "Can't reach it." / killed the target in place | Pack mates attacked first; combat tree pinned the returning bot | Attackers during the approach mean the pull worked; chase dropped each tick |
| "attack the lair" then nothing | `onTick` shed the spinning bot's lair combat | Spin flag (atomic, raised before combat); re-engage when out of combat |
| A far bot (1 km) never came | Spin claimed it from 1 km and blocked its travel | Spin claims bots within 64 m only |
| "attack the lair" carried over to every lair | The go order was group-wide | Manager binds it to one lair; off switches added |
| Holder walked onto the lair | Arrival radius does not cut an in-flight path aimed at the lair | Lead to a point 40 m short (`leadStopMeters`) |

Owner-confirmed working: spin poll, spin on, "attack the lair", spin through waves, back off, take foods (both
bots answered "No food on me", correct for empty vaults), the mission chain over three and four missions,
buffs mid-run. The final lead-stop fix is built and approved; it loads at the next owner restart.

Still out of scope: healing bots (G5).

## Cleanup

- Harness settings restored to the defaults: `enabled = false`, `continueOnFailure = false`,
  `onlyScenarios = {}`, `maxRunMinutes = 0`. No `TRIP-VERIFY G4 temp` lines remain.
- `cellNavDiag.logging` was on for boots 6-7 only and is restored to `false`; the diagnostic `traceGroupBot`
  step was removed from the scenario.
- Harness groups and the spawned pull targets were released by the scenarios' teardown steps. No database was
  cleared.
