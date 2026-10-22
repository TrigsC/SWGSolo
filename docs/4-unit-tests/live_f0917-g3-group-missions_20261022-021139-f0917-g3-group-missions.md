# Live verification: F_0.9.17 P.10p G3 group missions

**Plan**: `docs/1-plans/F_0.9.17_p10p-g3-group-missions.plan.md`
**Primary run ID**: `20261022-021139-f0917-g3-group-missions` (final boot; six boots in all, listed below)
**Evidence**: `MMOCoreORB/bin/log/trip-verify-20261022-021139-f0917-g3-group-missions-evidence.log`
**Verdict**: PASS on owner-accepted accumulated evidence: the scoped harness runs, three checks accepted on
direct evidence (owner decision, 2026-10-22), and the owner's in-client approval of the final build.

## Scope (owner rule: receipt runs stay limited to what the release changes)

- **Scenarios**: 16, run with `onlyScenarios`, `continueOnFailure = true`, and `maxRunMinutes = 180`.
  - 11 G3 scenarios.
  - 5 G1/G2 regression scenarios on code G3 touched: `group_follow`, `group_death_rejoin`,
    `group_gate_off`, `group_rally_cantina`, `group_directive_during_travel`.
- **Dropped by the owner** (known G1-era flakes that G3 code does not run in): `group_defend_xp_loot`,
  `group_no_faction_assist`.
- **No full matrix, no automatic re-run.** Each re-run was scoped to the scenarios that failed and was
  agreed with the owner.

## Changed subsystems

- `SimBotGroupMission.h`, `SimBotGroupRally.h` (parse, stages), `SimBotParityRunPolicy.h`.
- `SimPlayerManager` (plan, grab, split, group waypoint hook, harness scope controls).
- `SimHunterController` (mission stages, holder/non-holder paths, leader assist).
- Stock code:
  - `MissionObjectiveImplementation::awardReward` (gated bot shares);
  - `UpdateNearestMissionForGroupTask` (bot missions in the one group waypoint);
  - `MissionManager::getMaxActiveMissions` (IDL native) with `Core3.MissionManager.MaxActiveMissions`.

## Build and unit tests

- Warning-clean incremental build on every iteration. `luac -p` clean.
- `SimBot*` gtests: **212/212** before every harness boot and **213/213** after the owner-pass fixes (new
  owner-phrase test), with the server stopped, under `timeout -s KILL 120`. No `databases/` fd holders.

## Boots

| Boot | Run ID | Scope | Outcome | Action |
|---|---|---|---|---|
| 1 | 20261021-212149 | 18 | no G3 evidence | Harness bugs: bot re-selected per step; selection required the city tier. Fixed. |
| 2 | 20261021-220234 | 16 | 5/5 regression PASS | GRAB skipped off-planet bots; the leader's direction was rejected by the town arc. Fixed (product). |
| 3 | 20261021-231415 | 16 | 10 PASS | Stand-in winner publish cut off by the global gate; leader assist had no main-defender fallback; counter-key typo (product). Three scenario expectations fixed. |
| 4 | 20261022-001137 | 6 | 3 PASS | Borrowed-ref lookup for ledger ops; `awaitBotOutOfCombat` added (harness). |
| 5 | 20261022-002440 | 3 | assist evidence | Wild city creatures interrupted both assist checks. |
| 6 | 20261022-021139 | 1 | split proven | Ledger read raced the flush; `flushNow` added before the ledger check. |

Codex reviewed and APPROVED every post-approval code delta before the boot that ran it.

## Assertion table

| # | Scenario / check | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | `group_follow` | PASS | PASS (boots 2, 3) | PASS |
| 2 | `group_death_rejoin` | PASS | PASS (boots 2, 3) | PASS |
| 3 | `group_gate_off` | PASS | PASS (boots 2, 3) | PASS |
| 4 | `group_rally_cantina` | PASS | PASS (boots 2, 3) | PASS |
| 5 | `group_directive_during_travel` | PASS | PASS (boots 2, 3) | PASS |
| 6 | `group_mission_grab_and_run` | grab travel, entry, group sets out | PASS (boot 4) | PASS |
| 7 | `group_mission_creature_filter` | kreetle entry | PASS (boot 3) | PASS |
| 8 | `group_mission_none_found` | no entry | PASS (boots 2, 3) | PASS |
| 9 | `group_mission_unknown_creature` | reply, no grab | PASS (boots 2, 3) | PASS |
| 10 | `group_mission_skip` | grab 2, travel, skip, travel again | PASS (boot 4) | PASS |
| 11 | `group_mission_holder_death` | grab resumes from the clone | PASS (boot 3) | PASS |
| 12 | `group_mission_follow_cancels` | directive cleared, plan kept | PASS (boot 4) | PASS |
| 13 | `group_mission_gate_off` | no plan | PASS (boot 3) | PASS |
| 14 | `group_mission_split_bot_shares` | fight, split, bot ledger rows | See note 1 | PASS (accepted) |
| 15 | `group_mission_assist_leader_target` | bot engages the leader's attacker | See note 2 | PASS (accepted) |
| 16 | `group_assist_leader_no_faction` | no attack on a GCW-faction attacker | See note 3 | PASS (accepted) |
| U1 | Clean startup on every boot | ready | `fresh who.json` in 7-13 s | PASS |
| U2 | No crash or backtrace | `gdb.log` unchanged | unchanged since 2026-10-17 | PASS |

**Note 1 (split, boot 6):**
- Every harness step passed through the lair fight: grab, entry, both bots at `MISSION_TRAVEL`, the
  stand-in moved, `splitBotShares` +2 after 381 s, and `splitHarnessLeaderShares` +1.
- Diag: `LAIR_DESTROYED_CREDIT`, then `GROUP_MISSION_SPLIT reward=1163 share=387 members=3`
  (1163 / 3 = 387, the stock divisor).
- `simbot_ledger` holds exactly one `credits_bank +387 group_mission` row each for identities 84 and
  274, with `ref_id` = the lair. The one-shot `assertLedger` step read the database 31 s before the
  flush wrote them; the scenario now flushes first.

**Note 2 (assist target, boot 5):**
- `GROUP_ASSIST_LEADER result=engaged target=<harness attacker>`: the bot engaged the leader's
  attacker, from 41 m.
- Within 5 s a wild jawa attacked the bot, and self-defence correctly took priority, so the
  harness-attacker damage check failed. This is the same city-spawner interference as the dropped G1
  flakes.

**Note 3 (no faction, boot 5):**
- `assertNoBotDamage` passed: the bot never attacked the Imperial attacker.
- The `leaderAssistsSkipped` counter did not move, because the bot was fighting a wild jawa
  (`TRAVEL_ENGAGE`), so the verdict never ran.

## Found outside G3 (reported to the owner, not fixed here)

- **A bot parked at the Theed cantina (left by a G2 rally) cannot travel off-planet.** Every
  `GROUP_TRAVEL theed -> mos_eisley` leg fails about 2 s after start and retries in bursts. This
  matches the known cross-building pathing defect at Theed (ARCHI §12, v0.8.2). It is reachable in
  production through G2 alone.
- **The Mos Eisley harness spawn point has wild creatures** (jawas, kreetles from city spawners). This
  is the root of the G1 `group_defend_xp_loot` / `group_no_faction_assist` flakes and the assist
  interference above.

## Owner in-client pass (2026-10-22): the live evidence for the final build

The owner tested G3 in the real client over several rounds on Tatooine and Dantooine, with roster bots
(Rocla, Catsu and others). Each round's findings were fixed, reviewed by Codex, built warning-clean, and
re-tested on the next restart. The owner approved G3 on the final build ("ok I am good with G3").

| Owner finding | Cause | Fix | Re-tested |
|---|---|---|---|
| "playerbot" as the sender in group chat | G1/G2 travel and hold lines posted as "PlayerBot", G3 lines as "Mission" | Every line posts as a named bot (`postPlayerBotGroupReply`, `postPlayerBotGroupMissionLine`) | OK |
| "Grab 2 Bol lairs …" got no reply | Only mission/missions/mish were nouns | `lair`/`lairs` added | OK |
| SE and W missions placed north | Compass vs math bearing convention; then an unsigned `System::random` sum wrapping at 2^32 (every negative math bearing landed at 31-121 deg) | `math = 90 - compass`; the signed jitter computed before the sum. The grab event logs the parsed bearing | OK |
| "south east" as two words | The first direction word won | north/south + east/west combine (gtest) | OK |
| The holder walked straight up and attacked the lair | G3 had reused the lair attack call outside the F_0.9.8 phases | Removed: G3 adds no tactics (G4 owns them). Bots hold short (G2), defend and assist; nobody attacks the lair on its own | OK |
| Stopped about 60 m short, did not continue | Hold released only when the leader was nearer the lair than the holder; reveal re-tried every 500 ms | Release when the leader reaches the holder at the hold point; F_0.9.8 reveal rules (DEFER 2 s, bounded NO_SPAWN, abandon frees the offer) | OK |
| Only the holder knows the relocated spawn | Bots followed the leader after the reveal | The holder leads to the revealed lair (stops at engage range); the others follow the holder; coordinates posted in chat | OK ("Catsu did take us to it") |
| About 3 minutes before a buff/meet reply | The directive was not planned while the bot's tick was booked minutes ahead (G1 route hold) | Wake the bot on issue | OK |
| Bots return from cloning unbuffed | G2 rule: never rebuff on its own | Owner decision: the bot asks ("Say '<name> buff' …"); the named buff order goes to that bot only | OK ("the clone buff worked") |
| "West" gave NW and SW (2000+ m apart) | ±45 deg cone | `bearingToleranceDeg` 22 (one compass sector) | Owner approved |

Still owner-confirmed only, not exercised by the harness: the player-mission paths (the leader's own
missions, bot shares on them, the player's share), the stock group waypoint, and a raised
`MaxActiveMissions`.

## Cleanup

- Harness settings restored to the defaults: `enabled = false`, `continueOnFailure = false`,
  `onlyScenarios = {}`, `maxRunMinutes = 0`. No `TRIP-VERIFY G3 temp` lines remain.
- `travelDiag.logging` was enabled for about 75 s during boot 2 and is restored to `false`.
- Harness groups were released by the scenarios' teardown steps. No database was cleared.
