# Live verification: F_0.9.15 P.10p G1, Looking For Bot and grouping

**Run:** `20261019-180238-f0915-lfb-grouping` (2026-10-19 18:02 to 2026-10-20 18:33 host time)
**Plan:** `docs/1-plans/F_0.9.15_p10p-lfb-grouping.plan.md`
**Branch:** `feat/p10p-lfb-grouping` (uncommitted working tree on top of `fed41bce50`, v0.9.15)
**Profiles:** travel and cell navigation (follow, cross-planet routing), PvE combat (group defense,
faction rule, group XP), REST and dashboard, plus an owner real-client pass
**Verdict:** LIVE_VERIFICATION_PASS

## Changed subsystems

- `SimBotGrouping.h` (new pure policy) and `SimBotGroupingTest.cpp` (13 gtests);
- `SimPlayerManager`:
  - config;
  - LFB room;
  - request, reply and offer pipeline;
  - invite verdict;
  - join, rejoin and leave;
  - reconcile;
  - death and respawn hooks;
  - dispatch and governor exclusions;
  - group routes;
  - dashboard and inspect;
  - harness ops;
- `SimHunterController`: group phases, availability snapshot, suspend and resume, follow, travel,
  defend;
- `SimPlayerController`: travel availability hook;
- stock hooks:
  - `ChatManagerImplementation` (LFB room, bot group room, `/g`);
  - `GroupManager::inviteToGroup` and the `joinGroup` harness stand-in;
  - the `InviteCommand` bot branch;
- `sim_player_manager.lua` (`groupingEnabled`, `grouping`, 19 `group_*` scenarios), `app.js`.

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) on every rebuild |
| `luac -p` | clean on the working copy, every verify copy and the restored live copy |
| `node --check app.js` | clean |
| `SimBot*` gtests, server **stopped** (`timeout -s KILL 120`) | 189/189 at the first window; **190/190** on the final build (13 grouping tests) |
| Database holders before every start | none |
| Codex code review | APPROVED after 4 rounds, then APPROVED for the verification-time deltas (3 more rounds) |

## Restarts and what each found

The harness halts on its first failure. Iteration boots ran subsets; the receipt rests on the final
full boot.

| Stage | Result | Cause and fix |
|---|---|---|
| Boot 1 (77) | 58 regression scenarios PASS; `group_invite_accept` FAIL | **Product:** reply lines ending in `{name}` threw `ArrayIndexOutOfBoundsException` (engine3 `subString(begin)` at `begin == length`), so replies never posted. Harness: per-player cooldown leaked between scenarios. |
| Group subsets | various | Harness: requests reached unrelated production bots (now restricted to overridden identities); "no reply" assertions did not hold; a decline assertion was too strict; scenarios invited before the offer existed. |
| `group_follow` | bot never moved | **Product:** `shouldResumeHybridTravel` rejected every follow path for a grouped bot (`PATH_REJECT reason=hybrid_resume_cancelled`, found with a cell-nav trace routed to the bot). Also follow re-path churn, and a stall rule that could never fire. |
| `group_kick_disband` | `leader_left` instead of `kicked` | **Product:** after a kick the bot has no group pointer; the reconcile now reads the recorded group and judges membership first. |
| `group_defend_xp_loot` | no assist | Harness: the armed stand-in killed the weak attacker within seconds (attackers now get deep pools). |
| Boot with defend | **server hang** | Harness: the XP kill step locked only the victim, and `notifyDestruction` spun cross-locking the destructor (bot). Confirmed from a full GDB dump of 123 threads (`trip-verify-…-deadlock-bt.txt`). Fixed to `KillCommand` order. |
| `group_no_faction_assist` | combat not started | Harness: a GCW NPC cannot fight a neutral stand-in; the stand-in plays the opposing faction for that step. |
| Full boot (77) | **77/77** | |
| Owner real-client pass | 3 product defects | Group-tab chat ignored (routed via `handleChatRoomMessage`); followers parked in `WAITING`; grinding novices never free. Also distance-aware `status`, faster follow, `follow` acknowledgement, and dead bots in group commands (code review). |
| Final build, full boot | 1st `armor_mitigation_live` (pre-existing flake); 2nd `group_status_leave` | Scenario: status asked before arrival. **Product:** a bot arriving from a cross-planet leg stayed `WAITING` (first follow path now allowed). |
| **Final build, full boot** | **77/77 ALL_PASS** | |

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build | warning-clean | warning-clean | PASS |
| 2 | Lua and JS syntax | clean | clean | PASS |
| 3 | `SimBot*` gtests, server stopped | all pass | 190/190 | PASS |
| 4 | P.10 regression matrix (store, bazaar, loadout, armor, food) | 58 PASS | 58 PASS | PASS |
| 5 | `group_parse_and_reply` | a reply within the window | PASS | PASS |
| 6 | `group_busy_silent` | no reply over 20 s | PASS | PASS |
| 7 | `group_filters` (TKM/composite/DWB without match) | no reply over 20 s | PASS | PASS |
| 8 | `group_invite_accept` | offer → invite → grouped | PASS | PASS |
| 9 | `group_no_offer_decline` | `no_offer`, inviter cleared | PASS | PASS |
| 10 | `group_wrong_player` | `wrong_player` | PASS | PASS |
| 11 | `group_offer_expiry` | offer expires, not grouped | PASS | PASS |
| 12 | `group_follow` | within slot tolerance | PASS (130 m walk) | PASS |
| 13 | `group_cross_planet` | bot reaches leader's planet | PASS (Naboo → Tatooine via collectors) | PASS |
| 14 | `group_status_leave` | status reply; leave releases | PASS | PASS |
| 15 | `group_kick_disband` | `kicked`, then `disbanded` | PASS | PASS |
| 16 | `group_leader_offline` | `leader_offline` after grace | PASS | PASS |
| 17 | `group_death_rejoin` | real death, rejoin with new body | PASS | PASS |
| 18 | `group_defend_xp_loot` | bot damages attacker; kill XP event carries 1.2; no bot loot claim | PASS | PASS |
| 19 | `group_gate_off` | `gate_off` release | PASS | PASS |
| 20 | `group_dispatch_excluded` | grouped bot gets no order, no reply | PASS | PASS |
| 21 | `group_stale_join_abort` | join aborts, not grouped | PASS (`joinAborted.offer_stale`) | PASS |
| 22 | `group_release_cancels_travel` | kick mid-route cancels travel | PASS | PASS |
| 23 | `group_no_faction_assist` | Imperial vs Rebel leader: no engage, `faction_fight` skip; neutral creature: engage | PASS (`faction_fight` 40, assists 7) | PASS |
| 24 | Store invariants | `orphanRecords` 0, `rosterWithoutRecord` 0 | 0, 0 | PASS |
| 25 | Universal: clean starts, no crash, no new error signature | none | `gdb.log` unchanged at 1,190,538 bytes; 0 exceptions on the final boot | PASS |
| 26 | Production stage | LFB room exists; live and free sample | room created each boot; 10 live; 30-minute free sample in evidence | PASS |
| 27 | Owner real-client pass | join, reply, invite, decline, Group-tab status/leave, follow, release, distance status | all confirmed after the fixes ("Everything is looking solid") | PASS |

27 / 27.

## Notes and follow-ups

- **Hang root cause is test-only.** The harness XP step's lock order deadlocked; no production path
  deals damage that way. The general lesson (a kill must hold the killer and cross-lock the victim,
  as `KillCommand` does) belongs in ARCHI.
- **Availability reality.** Mission bots are busy most of the time. Free responders come from bots
  back in town and from grinding novices, which is why the owner raised the cap to 10 (4 novices
  minted).
- **The LFB room is recreated each boot**, so a client's remembered tab is stale; players `/join`
  again.
- **Same-planet travel is overland** (plan section 7 amendment); intra-planet shuttles move to G2.
- **A fight shorter than one follow tick (0.75 s) gets no assist.**
- **`armor_mitigation_live` flaked once again** (pre-existing; passed on the unchanged rerun).

## Configuration

Temporary, removed after the run:

- the harness enabled, `startupDelaySeconds` 300, the scenario list trimmed to the 77 above;
- the reaper override;
- the cell-nav trace for one diagnosis boot.

Owner-approved and kept live, unstaged:

- `groupingEnabled = true`;
- `pveConfig.maxHunters` and `identityRoster.maxHunters` = 10;
- novice distribution count 10 (`-- TRIP-VERIFY F_0.9.15 (live-only)`).

## Evidence

- `bin/log/trip-verify-20261019-180238-f0915-lfb-grouping-evidence.log`:
  - the contract;
  - gtest results;
  - final counters;
  - the free-bot sample;
  - the owner pass notes.
- `bin/log/trip-verify-20261019-180238-f0915-lfb-grouping-final-dashboard.json`.
- `bin/log/trip-verify-20261019-180238-f0915-lfb-grouping-deadlock-bt.txt` (the harness deadlock
  thread dump).
- `bin/log/missiondiag.log`: the `LFB_*`, `GROUP_*`, `GROUP_FOLLOW` and `GROUP_ASSIST_SCAN` events.
