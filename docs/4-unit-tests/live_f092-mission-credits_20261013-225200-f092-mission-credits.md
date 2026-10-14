# Live verification — F_0.9.2 mission credits, holdings journal and doctor-buff fee

**Run:** `20261013-225200-f092-mission-credits` (2026-10-13 22:52 UTC to 2026-10-14 02:14 UTC)
**Plan:** `docs/1-plans/F_0.9.2_p10c-mission-credits.plan.md`
**Branch:** `feat/p10c-mission-credits` (uncommitted working tree on top of `94d27eaaef`)
**Verdict:** LIVE_VERIFICATION_PASS

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) |
| `luac -p sim_player_manager.lua` | clean |
| `node --check app.js` | clean |
| gtests `SimBot*`, run with the server **stopped** | 104 passed, 8 suites (4 new in `SimBotMissionCreditsTest`) |
| Codex code review | APPROVED, 2 rounds |

## Restarts

Three, all through the guarded lifecycle script, all clean (normal GDB shutdown,
no force-kill). 22:52 stop → unit tests → 23:02 start (schema upgraded to 1014);
23:20 restart with the assertion made to report its counts; 23:36 stop → scenario
fix → start. Two players were connected at the first stop, none at the others.

## Parity harness (third boot, this chunk's scenarios only)

17 of 17 PASS: `store_boot_invariant`, the five `mission_credit_*`, the six
`buff_payment_*`, and the five `ledger_*` scenarios.

`ledger_requeued_after_failed_insert` failed on the first two boots with
`found=1 got=1 want=0`. The injected journal-INSERT failure fired
(`SimPlayerLedgerInsertFailed` in `core3.log`), but the maintenance lane retried
and wrote the row before the queued SQL assertion ran, so the intermediate
"zero rows, one pending" state could not be observed. The scenario now asserts
that the fault fired (`flushFailures` +1) and that the row exists exactly once
with nothing pending. This was a test-oracle defect; no production code changed.

## Stage 1 — income (`awardMissionCredits = true`, fee off), 23:38 to 01:11 UTC

Six novice brawlers on Tatooine.

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | `MISSION_CREDIT result=AWARDED` count equals `LAIR_DESTROYED_CREDIT` and `MISSION_SUMMARY endReason=lair_destroyed` | equal | 19 / 19 / 19 | PASS |
| 2 | Every amount within `375 × level … 375 × level + 15 × level` | all | 19 of 19 (levels 10 to 19, 3,803 to 7,374) | PASS |
| 3 | No award for missions that ended another way | 0 | 0 (3 `travel_home`, 2 `hunter_died` unpaid) | PASS |
| 4 | Per bot: dashboard bank = sum of logged awards | 6 of 6 | 6 of 6 | PASS |
| 5 | Per bot: MySQL `bank_credits` = `SUM(simbot_ledger.delta)` = newest `balance_after` | 6 of 6 | 6 of 6 | PASS |
| 6 | Per bot: journal row count = award count | 6 of 6 | 6 of 6 (19 rows) | PASS |
| 7 | `ledger.dropped` and `ledger.pending` | 0 / 0 | 0 / 0 | PASS |

## Stage 2 — sink (`buffPayment.enabled = true`, fee 500), 01:13 to 02:13 UTC

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 8 | `charged` delta × 500 = `creditsSpent` delta | equal | 7 × 500 = 3,500 | PASS |
| 9 | One `doctor_buff` journal row per charge | 7 | 7 (bots 84 ×2, 849 ×2, 848, 850, 851) | PASS |
| 10 | Per bot: bank = awards − charges = journal sum = newest `balance_after` | 6 of 6 | 6 of 6 | PASS |
| 11 | `chargeFailed` | 0 | 0 | PASS |
| 12 | `reservedNow` at rest | 0 | 0 | PASS |
| 13 | Awards continue with the fee on | > 0 | 8 awards, 8 lairs destroyed | PASS |

## Universal

| # | Assertion | Actual | Result |
|---|---|---|---|
| 14 | Clean startup on each boot, schema 1014 applied once | yes | PASS |
| 15 | Process alive for both stages | yes | PASS |
| 16 | `BDB0` / `PANIC` / backtrace / `SIGSEGV` in `core3.log` | 0 | PASS |

16 of 16.

## Not exercised live

- **The unaffordable path.** Every bot had at least 13,000 credits when the fee
  went on, so `skippedUnaffordable` never moved in stage 2. It is covered only
  by harness scenario `buff_payment_unaffordable`.
- **A player's terminal mission reward.** The stock generator now calls the
  method the two reward lines were moved into; no player took a destroy mission
  during the run.
- **The retention prune against old rows.** The prune statement ran at each boot
  without error, but no row is older than 30 days, so it deleted nothing.
- **Reconcile after a real crash.** Covered by harness scenario
  `ledger_reconciles_mismatch` only.

## Observations

- Deaths (`HUNTER_DEATH`): 8 in the 93-minute income stage, 4 in the 60-minute
  fee stage. All but two were on the walk (`TRAVEL_TO_MISSION`,
  `TRAVEL_TO_LAIR`, `TRAVEL_TO_TERMINAL`), the open item carried from v0.9.8. The
  fee did not raise the rate.
- Castu (274) was not charged in stage 2: it took no doctor buff in that hour.
- `ledger.balanceMismatch = 1` is the harness scenario that creates a mismatch on
  purpose (identity 901).

## Left in the database by the harness

Identities 884 and 885 (`harness`, from the two boots that stopped at the failing
scenario) and orphan record 901 with two journal rows (the last scenario had no
reaper step; it has one now). Not deleted.

## Cleanup

Harness disabled and the scenario filter removed from the working Lua.
`buffPayment` returned to `enabled = false`, `doctorFeeCredits = 0`.
`awardMissionCredits = true` is left armed **live-only**, alongside the other
live-only progression gates; the committed default is `false`.

## Evidence

In `MMOCoreORB/bin/log/`: `trip-verify-<run>-missiondiag.log` (both stages),
`-missiondiag-stage1.log`, `-dashboard-baseline.json`, `-dashboard-stage1.json`,
`-dashboard-stage2.json`, `-offsets.txt`.
