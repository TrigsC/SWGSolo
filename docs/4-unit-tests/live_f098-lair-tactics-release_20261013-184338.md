# Live verification — F_0.9.8 lair tactics and mission graduation (release)

**Run ID:** `20261013-184338-f098-release`
**Plan:** `docs/1-plans/F_0.9.8_p10i-lair-tactics-and-mission-graduation.plan.md` (with 8b–8e)
**Source:** `8fbefd61fc` (C++ unchanged since `300c39cc5c`, which is what the binary was built from)
**Verdict:** LIVE_VERIFICATION_PASS

## How this run was taken

This is an observation of the owner's own live run, not a scripted harness. The server was
restarted by the owner at 18:43:38 UTC on 2026-10-13 on the release binary, with the six
progression PlayerBots (84, 274, 848, 849, 850, 851) and every F_0.9.8 gate on. The window
is 18:43:38 to 20:20:15 UTC (1.61 h). Counts are from `missiondiag.log` lines inside the
window, so they are deltas for this run.

Supporting evidence from the previous run (18.7 h, 23:42 UTC Oct 12 onward, same code
except stock placement) is quoted where an assertion needs a longer sample.

## Static gate

| Check | Result |
|---|---|
| Incremental build | clean, binary unchanged |
| `luac -p` on the committed and live `sim_player_manager.lua` | clean |
| Unit tests `SimBot*` (7 suites) | 100 passed |

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Startup | clean, ready | ready, REST listening 10 s after start | PASS |
| 2 | Fatal or backtrace attributable to the change | none | none; see the incident below | PASS |
| 3 | Terminal visits that produce offers | all | 23 `OFFERS_BEGIN`, 23 `OFFERS_OK`, 0 `POS_FAIL` (46 `POS_OK`) | PASS |
| 4 | Arrival search places the lair | no defer, within 256 tries | 24 `REVEAL_RELOCATE`, 0 defers, 105 tries in total, median 3, max 22 | PASS |
| 5 | Missions end in a destroyed lair | majority | 17 of 26 `MISSION_SUMMARY` (6 `travel_home`, 3 `hunter_died`) | PASS |
| 6 | Flee-leg cap holds | no encounter above 1 leg | max `fleeLegs` = 1 over 34 `TRAVEL_MODE` lines | PASS |
| 7 | No stalled bot | longest quiet stretch under 15 min | 7.4 min; all six active at the end | PASS |
| 8 | Lair retreat defense fires and bots survive it | `LAIR_RETREAT` present; deaths in RETREATING or HEALING below the 7 per 3h18m baseline | 15 `LAIR_RETREAT`; 1 death in RETREATING | PASS |
| 9 | Progression intact and advancing | tiers not lower than before | Nordihi 13, Castu 11, Paki 11, Oloopo 11, Hasina 11, Towe 10 | PASS |
| 10 | Restart after recovery | clean boot, saves succeed, bots resume | ready in 12 s; 3 delta saves clean; kills, offers and a destroyed lair logged within 17 min | PASS |

Deaths in the window: 4 (two to level-14 gualamas on the approach, one to a thug on the walk,
one to a scyk during a lair retreat). Walk deaths are a known open item, not a release gate.

Supporting, previous run (18.7 h): 70 lair retreats, 69 resumed, no stalled bot; no bot ran
more than one flee leg; 103 stand-and-fights.

## Incident during the release gate (not caused by the change)

At 20:20:19 UTC the release testing gate ran `core3 runUnitTests` while the server was up.
The server's next delta save, at 20:20:25, logged `BDB0060 PANIC: fatal region error
detected; run recovery` and the server deadlocked. This is the second time a unit-test run
against the live Berkeley DB environment has been followed by this panic (first:
2026-09-21). The observation window above ends before it.

Recovery, with the owner's go-ahead: stopped the exact `core3` pid, backed up `databases`
to `~/dbbackup-20261013-202618` (sizes and md5 verified), ran `db5.3_recover` (clean), and
started the server at 20:26:38. Assertion 10 covers the result.

Rule going forward: unit tests run only with the server stopped.

## Evidence

- `MMOCoreORB/bin/log/trip-verify-20261013-184338-f098-release-missiondiag.log` — the
  window's `missiondiag.log` lines.
- `MMOCoreORB/bin/log/trip-verify-20261013-184338-f098-release-dashboard-postrecovery.json`
  — dashboard after the recovery restart. The in-window dashboard capture failed because
  the server had already stalled.

## Cleanup

No temporary settings or entities were added. The live Lua keeps its live-only values
(progression master gates, novice roster, 2,000,000 reserves, diagnostic logging).
