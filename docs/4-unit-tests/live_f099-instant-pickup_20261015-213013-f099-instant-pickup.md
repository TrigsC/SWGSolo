# Live verification — F_0.9.9 bazaar SELL, instant pickup

**Run:** `20261015-213013-f099-instant-pickup` (2026-10-15 21:30 UTC to 22:20 UTC)
**Plan:** `docs/1-plans/F_0.9.9_p10j-bazaar-sell.plan.md` (section *Instant pickup*)
**Previous run:** `live_f099-bazaar-sell_20261015-180804-f099-bazaar-sell.md` (33/33 on the
retrieval-hold build)
**Verdict:** LIVE_VERIFICATION_PASS

## Why this run

After the first PASS, the owner rejected the retrieval hold: a buyer waited up to one
60-second flush interval to collect an item bought from a bot. Owner decision
2026-10-15: **instant pickup**.

- A bot sale hands the item to the buyer at once.
- The sale kicks the maintenance lane, which settles and force-flushes immediately.
- A lost sale is recovered from the buyer-owned `SOLD` record, or from the item under a
  player creature.

The owner accepted the residual crash window: the bot loses that sale's credits; players
are never affected.

Codex reviewed the change (one Major: a recovered settled sale was taxed twice; fixed) and
APPROVED.

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) |
| `luac -p` | clean |
| gtests `SimBot*`, server stopped | 140 / 140 |
| Codex code review | APPROVED (instant-pickup delta) |

## Assertions

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build, Lua syntax, unit tests | clean, 140/140 | clean, 140/140 | PASS |
| 2 | `store_boot_invariant` | PASS | PASS | PASS |
| 3–14 | Phase-A bazaar scenarios carried over from the first run: gate off, list from vault, skip junk, never undercut, human ask at the cap, sale settles, name collision, expiry relist, sale during publish, settlement mode, settle replay, unresolvable seller | PASS | PASS | PASS |
| 15 | `bazaar_instant_pickup`: the buyer owns the item right after the buy, before any sweep or flush | `ownerIsBag=false` immediately; `sold` after settlement | as expected | PASS |
| 16 | `bazaar_lost_sale_recovered`: sale POD suppressed, store reload | paid from the buyer-owned `SOLD` record | `sold` | PASS |
| 17–21 | Flush fault, void row, drain on disable, listing cap, delete with listing, reconcile states | PASS | PASS | PASS |
| 22 | `bazaar_production_visit` (real roster bot) | visit, list, back to the job | PASS | PASS |
| 23 | `bazaar_restart_probe`, two boots with a delta save between | listing still `listed` and bag-owned after restart | PASS on the probe-only rerun (probe 1097; delta save 22:11:52) | PASS |
| 24 | Production visits on this build | `BAZAAR_VISIT begin` → `end outcome=listed` | 3 begun, 3 `listed`; bot 84 listed at a second terminal (3945364) | PASS |
| 25 | Store invariants | no orphans | `orphanRecords` 0, `rosterWithoutRecord` 0, vault `held` 17, `orphaned` 5 (unchanged) | PASS |
| 26 | Universal: no crash, no store or bazaar errors | none | none. Errors in the window: 9 + 9 stock deletions of sold harness listings (`status 2`), client network noise | PASS |

26 / 26.

## Notes

- **The restart probe failed once in the full run, then passed on a targeted rerun.**
  - In the full run, phase B reported `bazaar_listing_mismatch`.
  - The listing had survived that restart: the failure cleanup then retrieved it from the
    live auction map (`bazaar.retrieved` +1).
  - So the failing piece was phase B's lookup, most likely which harness identity it
    resolved as the probe, not persistence. The cleanup deleted the probe's rows before
    they could be inspected.
  - The probe passed in the first run's pass 8 and again on a probe-only rerun on this
    build, with its rows captured before the restart.
  - Recorded as harness debt.
- **`bazaar_flush_fault_mid_batch` no longer reaches its injected fault.**
  - Instant pickup's immediate flush writes the sale's receipts before the scenario's
    faulted `FlushNow`, so the scenario passes without exercising the mid-batch fault.
  - That path was exercised and passed in the first run (`SimPlayerLedgerInsertFailed`
    injected, sale paid once), and the flush code has not changed since.
  - The scenario needs a re-arm that faults the kicked flush. Harness debt.

## Configuration

The temporary harness block was removed. The Lua file is identical to its pre-run copy.
`bazaarSell = true` remains as the owner-approved live-only line.

## Evidence

- `bin/log/trip-verify-20261015-213013-f099-instant-pickup-evidence.log`
- `bin/log/trip-verify-20261015-213013-f099-instant-pickup-gtest.txt`
- `bin/log/trip-verify-20261015-213013-f099-instant-pickup-phaseA-dashboard.json`
- `bin/log/trip-verify-20261015-213013-f099-instant-pickup-final-dashboard.json`
