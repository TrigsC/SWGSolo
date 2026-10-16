# Live verification — F_0.9.9 bazaar SELL

**Run:** `20261015-180804-f099-bazaar-sell` (2026-10-15 18:08 UTC to 21:05 UTC)
**Plan:** `docs/1-plans/F_0.9.9_p10j-bazaar-sell.plan.md`
**Branch:** `feat/p10j-bazaar-sell` (uncommitted working tree on top of `da8c7d9aa0`)
**Verdict:** LIVE_VERIFICATION_PASS (final build, harness pass 8)

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`), final build |
| `luac -p sim_player_manager.lua` | clean |
| `node --check app.js` | clean |
| gtests `SimBot*`, run with the server **stopped** | 140 passed, 11 suites (22 new in `SimBotBazaarTest`, extended `SimBotItemReconcileTest`) |
| Codex code review | APPROVED after 4 rounds; the verification-time delta is reviewed separately |

Databases backed up before the first boot of schema 1016:
`databases_backup_f099_20261015-180829` (byte-identical, checked with `diff -rq`) and
`bin/sqlbackup_f099_pre1016.sql` (the `simbot_*` tables and `db_metadata`).

## Defects found by live verification

Verification ran eight harness passes. Three were stopped by product defects, all fixed in
the final build; the rest by harness defects.

1. **The progression store failed to load once real listings existed** (pass 2). The
   listing query read `closed_at` and `released_at` with `getUnsignedLong`, and both are
   NULL on an open listing, so every load threw `IllegalArgumentException` and retried
   every 30 s. Pass 1 loaded only because no listing row existed yet. While it was
   down (about 30 minutes, 19:00 to 19:31), no bot progression ran. Fix: `COALESCE(...,0)` on the
   three time columns.
2. **An expired item could never be relisted** (pass 4). The retrieved listing keeps its
   entry in the live map, and both the terminal's candidate filter and
   `shouldPlayerBotVisitBazaar` skipped any item with an entry. Fix: only a `pending` or
   `listed` entry blocks.
3. **`saleUnresolved` counted attempts, not sales** (pass 5): one sale showed 3. Fix: counted
   once per auction.
4. **A pure reconcile branch was unreachable** (unit tests, before the first boot): a
   row-less held sale returned `SETTLE` before `ADOPT_SOLD`. Production adopted earlier
   anyway, so behaviour was unaffected; the decision table is now correct.
5. **Harness defects** (no production impact):
   - the "human" seller was not placed in the world;
   - seeded human listings leaked between scenarios and became the next one's floor;
   - `bazaar_human_above_cap` was impossible, because stock caps a human ask at 20,000;
   - the name-collision buyer did not carry the bot's name;
   - `expireListing` steps named no identity;
   - the restart probe minted a named identity that the end-of-run cleanup deletes;
   - `production_visit` asserted an exact visit count while several roster bots visit.

## Assertions (final build)

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build | warning-clean | clean | PASS |
| 2 | Lua syntax | clean | clean | PASS |
| 3 | Dashboard JS syntax | clean | clean | PASS |
| 4 | In-binary unit tests (server stopped) | all pass | 140 / 140 | PASS |
| 5 | Schema | 1016; `simbot_listings`, `ask_price`, applied marks | 1016, present | PASS |
| 6 | `store_boot_invariant` | PASS | PASS | PASS |
| 7 | `bazaar_gate_off` | no listing, no fee | PASS | PASS |
| 8 | `bazaar_list_from_vault` (owner = bag, item row `listed`, fee journaled) | PASS | PASS | PASS |
| 9 | `bazaar_skip_generic_junk` | `skipped.junk` +1 | PASS | PASS |
| 10 | `bazaar_never_undercut` (human 400 above base 300) | listed at 400 | 400 | PASS |
| 11 | `bazaar_human_above_cap` (human ask at the 20,000 cap) | listed at 20,000 | 20,000 | PASS |
| 12 | `bazaar_sale_settles` (net = gross − tax; both receipts) | PASS | PASS | PASS |
| 13 | `bazaar_name_collision` (buyer named like the bot) | store credited, buyer becomes owner | PASS | PASS |
| 14 | `bazaar_expiry_relist` (relist at persisted ask after a markup change; human floor raises it) | 300, then 400 | 300, 400 | PASS |
| 15 | `bazaar_sale_during_publish` | settles once, stays `sold` | PASS | PASS |
| 16 | `bazaar_settlement_mode` (master gate off) | settles, no award | PASS | PASS |
| 17 | `bazaar_settle_replay` | paid once | PASS | PASS |
| 18 | `bazaar_retrieval_hold` (held before the receipts are applied, released after) | owner bag, then buyer | as expected | PASS |
| 19 | `bazaar_unresolvable_seller` | released, `saleUnresolved` +1 | PASS | PASS |
| 20 | `bazaar_flush_fault_mid_batch` (fault after the first ledger insert) | paid once | PASS | PASS |
| 21 | `bazaar_void_row_not_resurrected` | PASS | PASS | PASS |
| 22 | `bazaar_drain_on_disable` | `drained` +1 | PASS | PASS |
| 23 | `bazaar_listing_cap` and unaffordable fee | PASS | PASS | PASS |
| 24 | `bazaar_delete_with_listing` | no orphan `AuctionItem` | PASS | PASS |
| 25 | `bazaar_reconcile_states` | PASS | PASS | PASS |
| 26 | `bazaar_restart_probe` (two boots, delta save at 20:57:35 between) | listing still owned by the bag after restart | PASS | PASS |
| 27 | `bazaar_production_visit` (real roster bot) | visit → list → back on its job | PASS (bot 274) | PASS |
| 28 | Production trace: real bots walk to an outdoor terminal and list | `BAZAAR_VISIT begin` → `BAZAAR_LIST` → `end outcome=listed` | 16 visits begun, 14 `listed`, 0 timeout, 0 pathFailed, 0 noOutdoorTerminal (two were cut by restarts) | PASS |
| 29 | Policy on real items: junk skipped, no-value skipped, price = loot value × 3 | as configured | bot 84: shirt 579, DH17 carbine 489, survival knife 348, two `skip_junk`; bot 850: pullover 570, one `skip_no_value` | PASS |
| 30 | Same price on relist (real bot) | persisted ask reused | bot 84 relisted the shirt at 579 on every visit | PASS |
| 31 | Bot listings survive stock boot load and maintenance | no bag-owned listing deleted | 5 production listings (made 18:28) live through 6 restarts; every boot-time "invalid owner" deletion was a sold harness listing (`status 2`) owned by its despawned NPC buyer | PASS |
| 32 | Store invariants after the run | no orphan records; vault consistent | `orphanRecords` 0, `rosterWithoutRecord` 0, vault `held` 17 (up from 16 by production loot), `orphaned` 5 (unchanged, F_0.9.3 artifacts) | PASS |
| 33 | Universal: no crash, backtrace or BDB signature; process up | none | none (`screenlog.0` holds only catchpoint setup; `gdb.log` dates from 2026-09-21) | PASS |

33 / 33.

## Error lines in the run window

| Count | Line | Disposition |
|---|---|---|
| 26 | `Deleting SOLD item` | harness sales owned by despawned NPC buyers, stock cleanup |
| 26 | `Auction with invalid owner` | the same sold harness listings (all `status 2`) |
| 11 | `SimPlayerProgressionStoreLoadFailed` | defect 1, pass 2 only, fixed |
| 3 | `SimPlayerLedgerInsertFailed` | injected by `bazaar_flush_fault_mid_batch` |
| 1 | `listOnBazaar: No bazaar terminal found near (0, 0, 0)` | harness defect, pass 1 only, fixed |
| 1 | `unknown appearance type SPRT`, 1 `handleNetStatusRequest` | unrelated (template data; a client connection) |

## Not exercised

- **A human buying a bot listing in the game client.** Sales were driven by harness buyers
  through stock `doInstantBuy`; the client-side purchase path is stock and unchanged.
- **A non-zero city sales tax.** Every terminal used is in Mos Eisley, which charges none, so
  `tax=0` on every sale.

## Configuration

Temporary settings were removed after the run:
- the harness disabled, and its scenario filter, `startupDelaySeconds` 300 and the reaper
  override (on, 60 s) removed.

Left armed live, the owner's call:
- `playerBotProgression.bazaarSell = true` as a marked live-only line (`false` in the commit).
  Turning it off withdraws every bot listing to the vault through `drainOnDisable`.

## Evidence

- `bin/log/trip-verify-20261015-180804-f099-bazaar-sell-evidence.log`: all `BAZAAR_*`
  events, the error summary, schema and market lines, the unit-test summary.
- `bin/log/trip-verify-20261015-180804-f099-bazaar-sell-gtest.txt`
- `bin/log/trip-verify-20261015-180804-f099-bazaar-sell-phaseA-dashboard.json`: phase A,
  21/22 with the probe `AWAITING_RESTART`.
- `bin/log/trip-verify-20261015-180804-f099-bazaar-sell-final-dashboard.json`
