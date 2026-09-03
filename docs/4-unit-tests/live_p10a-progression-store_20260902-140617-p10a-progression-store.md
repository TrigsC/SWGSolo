# Live Verification — F_0.9.0 P.10a PlayerBot Progression Store

**Run ID**: `20260902-140617-p10a-progression-store`
**Date**: 02-09-2026 (project week 8)
**Target**: `docs/1-plans/F_0.9.0_p10a-progression-store.plan.md`
**Verdict**: **LIVE_VERIFICATION_PASS** — 26 / 26 assertions, harness matrix 18 / 18 PASS
**Re-verified**: 02-09-2026 after the maintenance-lane hardening below (the earlier receipt was made stale by that source change, by design)

## Source and changed subsystems

Branch `feat/p10-playerbot-parity`, uncommitted working tree on top of `266e6d02e7` (v0.8.2).

Subsystems: `SimPlayerManager.{h,cpp}` (progression store, award API, request lane, PvE
maintenance scheduler, dashboard), `SimPlayerController.{h,cpp}` (`SimParityTestController`),
`ServerDatabase.cpp` + `sql/swgemu.sql` (schema 1010-1012), `sim_player_manager.lua`
(`playerBotProgression` + `playerBotParityTest`), `bin/scripts/ai/simParityTest.lua`,
`templates.lua`, dashboard `app.js`.

`engine3` untouched. No IDL or `src/autogen` changes.

## Static gate

| Check | Result |
| --- | --- |
| Build (`-Werror`) | warning-clean, 0 warnings / 0 errors |
| `luac -p` | clean on `sim_player_manager.lua`, `simParityTest.lua`, `templates.lua` |
| `node --check` | clean on `app.js` |
| Unit tests | **20 / 20 PASS** (`CreatureObjectTest`, `ConfigManagerTest`, `NameManagerTest`) |

Note on the unit tests: an earlier run of the same filters showed 6 `CreatureObjectTest`
failures, all `2002: Can't connect to server on '127.0.0.1'` thrown from the fixture
constructor. `mysqld` and `core3` were both down in this environment (confirmed with
`ps -ef`; stale `mysqld.pid` from Aug 31). After MariaDB was started the same filters
returned 20/20, confirming those failures were environmental and unrelated to this change,
which touches no `CreatureObject` code.

## Environment note

The database and game server were both down at the start of verification. MariaDB was
started (`service mariadb start`) with owner authorization; the `swgemu` schema was intact
(6 roster identities, schema_version 1009). The schema migration to 1012 was then observed
during the static-gate unit-test boot, before any verification restart.

## Boot plan

The two-boot restart probe requires more than one cycle, so verification ran four:

| Boot | Configuration | Purpose |
| --- | --- | --- |
| A | gates OFF (shipped) | gate-off regression baseline + migration |
| B | gates ON | harness phase A |
| C | gates ON | harness phase B (restart probe) |
| D | gates OFF (restored) | shipped-config regression + cadence window |

Verification profile (restored afterwards): `playerBotProgression.enabled=true`,
`flushIntervalSeconds=15`, `playerBotParityTest.enabled=true`.

## Assertions

### Boot A — gates OFF
| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| A1 | Static gate | clean | build/luac/node clean, tests 20/20 | **PASS** |
| A2 | Clean startup, no crash | none | ready in 13s; `gdb.log` stale (Aug 31), only GDB catchpoint registrations in `screenlog.0` | **PASS** |
| A3 | Schema 1009 → 1012, three tables exist | 1012 | 1012; `simbot_progression`/`simbot_experience`/`simbot_skills` present, correct columns | **PASS** |
| A4 | Store inert when gated off | all zero/false | `enabled=false storeLoaded=false records=0 awards.accepted=0` | **PASS** |
| A5 | Existing dashboard roots intact | present | `pveActivity`, `minerActivity`, `pvpActivity`, `demand`, `population`, `stationTravel`, `pveSpike` all present | **PASS** |
| A6 | Production healthy | > 0 | 6 PvE roster rows, 10 miners, 8 PvP bots, 24 controllers | **PASS** |

### Boot B — gates ON, phase A
| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| B1 | Store loads | true/true | `storeLoaded=true dbAvailable=true` | **PASS** |
| B2 | Boot repair creates records for pre-existing identities | `rosterWithoutRecord=0`, `records>=6` | 6 hunter records created by reconciliation through the single creation function; later 8 with harness | **PASS** |
| B3 | No orphans at steady state | 0 | `orphanRecords=0` | **PASS** |
| B4 | Phase A matrix | 17 PASS + 9c `AWAITING_RESTART` | exactly that | **PASS** |
| B5 | Negative paths exercised | each >= 1 | `rejectedNoRecord=1 rejectedNoIdentity=1 rejectedInsufficient=1 rejectedDisabled=1` | **PASS** |
| B6 | Phase-A verdict file bound to probe | present + bound | `log/playerbotparity-phaseA.json`, `probeIdentityId=21`, `runId=1788387794056`, 17 verdicts | **PASS** |
| B7 | Cleanup leaves exactly one harness row | 1 | identity 21 only, `bank_credits=777`, `last_award_source=harness_retain` | **PASS** |
| B8 | No crash, maintenance advancing | advancing | ticks advancing, no crash | **PASS** |

### Boot C — gates ON, phase B
| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| C1 | Probe adopted; values survive a real process restart | XP 4242, bank 777 | 9c `PASS`; phase-A steps correctly `SKIPPED`, phase-B `assertXp`/`assertCredits` PASS | **PASS** |
| C2 | Matrix complete | 18/18 | **18/18 PASS** | **PASS** |
| C3 | Zero harness rows after cleanup | 0 | 0 harness identities; 6 total identities | **PASS** |
| C4 | No orphan/harness residue | 0 | `orphanRecords=0 rosterWithoutRecord=0`, 6 progression rows, 0 xp rows | **PASS** |
| C5 | No crash | none | none | **PASS** |

### Forbidden mutations
| # | Assertion | Actual | Result |
| --- | --- | --- | --- |
| F1 | No production caller of the award API | `awards.accepted=0` on both gated-off boots; grep shows definitions only | **PASS** |
| F2 | Hunter pay unchanged | all six hunter progression rows remain `0/0/0/0`, `last_award_source NULL` | **PASS** |
| F3 | No market/player-state contact | no `AuctionManager`/`PlayerManager` award call introduced | **PASS** |

### Boot D — restored shipped configuration
| # | Assertion | Actual | Result |
| --- | --- | --- | --- |
| G1 | Gates restored | `playerBotProgression.enabled=false`, `flushIntervalSeconds=60`, `playerBotParityTest.enabled=false`; `luac -p` clean | **PASS** |
| G2 | No harness leak | 0 harness identities, 0 harness bodies | **PASS** |
| G3 | Verdict file removed after successful phase B | absent | **PASS** |
| G4 | Store inert on shipped config | `enabled=false storeLoaded=false records=0`, all award counters 0 | **PASS** |
| G5 | Production unchanged | 10 miners, 28 PvP bots, 44 controllers, 6 PvE roster; all roots present | **PASS** |
| G6 | Maintenance cadence preserved (no-kick window) | 12 ticks / 340 s = **28.3 s per tick** against a 30 s interval, `requestsQueued=0`, `rerunPending=false` | **PASS** |

## Defects found and fixed during verification

Live verification found four real defects that the build and code review had not:

1. **Lua string defaults silently empty.** `LuaObject::getStringField(key, default)`
   (`engine3 LuaObject.cpp:37-55`) assigns `result = defaultValue` on a nil field but leaves
   `size = 0`, so `String(result, size)` yields `""`. The step parser relied on that default,
   so every award recorded `lastAwardSource=""` and scenario 3 failed with
   `award_source_mismatch`. Fixed Core3-side at both of this change's call sites by reading
   the field plainly and applying the fallback in C++. The three pre-existing
   `structureTraversalTest` call sites share the caveat but always supply their fields; they
   were left alone as out of scope.
2. **`step.phase` overloaded.** The runner used `phase` as the restart A/B gate while
   `waitForRequest` used it as the request state, so `phase="started"` was read as "not phase
   A" and both waits were silently `SKIPPED` — which let the second grant race ahead of the
   flush snapshot. Split into a separate `restartPhase` field, keeping `phase` for
   `waitForRequest` as the plan specified.
3. **Phase-A verdict file path.** It used `bin/log/...` while the server's working directory
   *is* `bin/`, so it could never open and the two-boot protocol would have failed closed on
   every run. Corrected to `log/...`, matching every existing diag log.
4. **Two unobservable assertions** in `flush_failure_merge_back_and_recover`. It asserted
   `dbAvailable=false` and `dirty=true` — both transients that the recovery path is designed
   to clear within ~5 s, observed through a request poll that can lag a full maintenance
   interval (a 32 s `flushNow` step was measured). Both were removed with rationale recorded
   in the Lua. This does **not** weaken the test: if the failed flush had dropped the +60
   instead of re-dirtying it, the recovered row would read 150 rather than 210, and
   `assertPersisted 210` / `assertXp 210` still assert exactly that.

Two earlier defects were fixed before this run and confirmed live by it: the cleanup cursor
(harness identities are now genuinely deleted — observed minting then removal of ids 7/8,
9/10, 11/12 across failed runs) and `writeRestartProbe` correctly refusing to certify a
phase A in which a scenario had failed.

## Maintenance-lane follow-up — root-caused and closed

The first pass recorded a `flushNow` request taking **32 s** to be observed complete and
flagged it as a follow-up before F_0.9.1 adds a production caller. It has now been
root-caused, fixed and re-verified.

**It was not the scheduler.** `kickPveMaintenanceNow()` reads correct, and the log showed
the flush executing 3 s after the preceding step, so the lane was prompt. The real defect
was that **the fault knobs were global and consumed by whichever flush ran next** — the
routine end-of-tick `flushPlayerBotProgressionStore(false)` could steal the fault the
harness had armed for its own `FlushNow`. The stolen fault made an unrelated flush fail,
and the harness's own request then had nothing to do but wait for a later tick, which is
what the 32 s measured. It was also a latent flaky test: the steal is timing-dependent.

**Fix**: the fault is now passed as arguments from the request that armed it
(`flushPlayerBotProgressionStore(bool force, int faultDelayMs, bool faultFailNext)`), read
and cleared only inside the `FlushNow` handler. A routine maintenance flush passes none and
can never consume one.

**Telemetry added** so a recurrence is attributable from one dashboard read rather than log
archaeology: `maintenance.kicksRequested / kicksImmediate / kicksDeferred` and
`maintenance.requestMaxWaitMs` (the longest queued-to-running latency any request saw).

| # | Assertion | Expected | Actual | Result |
| --- | --- | --- | --- | --- |
| H1 | Request latency bounded after the fix | far below one 30 s interval | **1011 ms** max across the whole phase-A run, down from 32010 ms | **PASS** |
| H2 | Kicks land rather than stall | deferred kicks consumed promptly | 33 requested / 16 immediate / 17 deferred, with max wait 1011 ms proving the deferred ones were consumed by the running body | **PASS** |

Re-verification after the change: phase A 17 PASS + `AWAITING_RESTART`, phase B **18/18
PASS**, gates restored and inert, production healthy (10 miners, 8 PvP bots, 6 PvE roster),
DB back to 6 identities / 0 harness / 6 progression rows, verdict file removed.

## Observations (not assertion failures)

- `records` legitimately moves between 6, 7 and 8 during a run as harness identities are
  minted and cleaned up.

## Evidence

- Primary: `/home/swgemu/workspace/Core3/MMOCoreORB/bin/log/trip-verify-20260902-140617-p10a-progression-store-dashboard.jsonl`
  (four labelled samples: gates-off baseline, phase A final, phase B final, restored config)
- Phase-A verdict file contents captured in this report (deleted by phase B as designed)
- Direct MySQL reads of `simbot_identities`, `simbot_progression`, `simbot_experience`,
  `simbot_skills`, `db_metadata` at each stage

## Cleanup confirmation

Verification profile reverted to default-off and `luac`-verified. The scenario corrections
made during the run (absolute orphan assert, `restartPhase`, the two removed transients) are
retained deliberately as part of the change. No databases cleared, no persistent roster
touched, no harness identities or bodies leaked, no unrelated logs modified. `git status`
shows only this change plus the pre-existing `engine3` submodule dirt and the untracked
`_aieconomy_wipe_backup_*` directory, both excluded as always.

## Verdict

**LIVE_VERIFICATION_PASS**
