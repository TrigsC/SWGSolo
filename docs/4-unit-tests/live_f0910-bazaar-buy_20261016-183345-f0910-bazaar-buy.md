# Live verification — F_0.9.10 bazaar BUY

**Run:** `20261016-183345-f0910-bazaar-buy` (2026-10-16 18:33 UTC to 22:03 UTC)
**Plan:** `docs/1-plans/F_0.9.10_p10k-bazaar-buy.plan.md`
**Branch:** `feat/p10k-bazaar-buy` (uncommitted working tree on top of `de2c9d7e0c`, v0.9.11)
**Profiles:** economy and persistence; REST and dashboard
**Verdict:** LIVE_VERIFICATION_PASS

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) on every rebuild, including the final one |
| `luac -p sim_player_manager.lua` | clean |
| `node --check app.js` | clean |
| gtests `SimBot*`, server **stopped** | 147 / 147 (11 suites) |
| Standalone pure gtests (`SimBotBazaar*`, `SimBotItemReconcile*`) | 40 / 40 |
| Codex code review | APPROVED in round 3; the verification-time delta was reviewed again and APPROVED |

The in-binary test process printed its result and then hung during shutdown on a missing `Core3.TreFiles`
config value; the 600 s timeout ended it (`rc=124` after `PASSED 147 tests`). The server was stopped
for the whole test run.

## Restarts

All through the guarded lifecycle script. Each restart after the first carried a fix or a harness
correction listed below.

| Boot | Reason | Matrix result |
|---|---|---|
| 18:45 | first boot of the new build | `store_boot_invariant` failed: 31 records against a roster of 30 |
| 18:52 | temporary reaper override (on, 60 s), as F_0.9.9 used | `bazaar_buy_human_listing`: no candidate |
| 19:01 | scan diagnostics added | no `BAZAAR_BUY` event for the harness bot |
| 19:09 | product fix 1 (bag) | `assertSellerBankDelta`: `identity_not_found` |
| 19:16 | harness op fix | `paidToSeeder` 0 |
| 19:24 | product fix 3 (seeder), fairness template | `paidToBots` 0 |
| 19:32 | product fix 2 (bot seller) | fairness expectation |
| 19:39 | fairness assertion corrected | `lost_race` refusal reason |
| 19:46 | product fix 4 (`GONE` ordering) | **25 / 26** (only the production visit; see below) |
| 20:56 | production-only rerun, `awaitBazaarVisit expect="bought"` | boot-time production purchases (assertion 32) |

## Defects found by live verification

**Product defects, all fixed in the verified build:**

1. **A bot that had never looted could never buy.** It has no vault bag until its first item, and
   `buyPlayerBotItemsAtTerminal` returned on `getItemBag == nullptr`. It now uses
   `getOrCreateItemBag`, as the loot path does.
2. **Bot sellers were never recognised as bots.** The bag snapshot is keyed identity → bag, and the
   code looked the owner OID up as a key. It now uses `playerBotIdentityForBag`. Only the
   `sellerKind` label and the `paidToBots`/`paidToSeeder` counters were wrong; payment and settlement
   were correct.
3. **Seeder versus human was decided by name** (`"marketseeder"`). It is now "not in the player-name
   map".
4. **A listing already bought by someone else was refused as `SELLER_UNRESOLVED`, not `GONE`.**
   `buySimBotItem` resolved the new owner as a seller before checking status. An unlocked `FORSALE`
   pre-check now comes first; the locked recheck is unchanged. The refund was correct either way;
   only the reason was wrong.

**Harness defects (no production impact):**

- `assertSellerBankDelta` was in the identity-required op list.
- The fairness scenario's second template had a different client object type (262153), and its first
  assertion expected no purchase. The second need reaches a listing that the first need just cached,
  free, in the same pass, which is the intended behaviour.
- `bazaar_buy_production_visit` waited for a *listing* visit; it now takes `expect="bought"`.

**Pre-existing environment state:** progression rows 1098 and 1099 had no roster identity, left by
the F_0.9.9 restart-probe runs. The temporary reaper override cleared them, as in F_0.9.9.

## Assertions

Matrix figures are from boot 19:46 (final product code). Production figures are from boots 18:45 and
20:56.

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build, Lua, JS | clean | clean | PASS |
| 2 | In-binary gtests, server stopped | all pass | 147 / 147 | PASS |
| 3 | `store_boot_invariant` | PASS | PASS | PASS |
| 4–7 | SELL regressions: `bazaar_sale_settles`, `bazaar_name_collision`, `bazaar_instant_pickup`, `bazaar_lost_sale_recovered` | PASS | PASS | PASS |
| 8 | `bazaar_buy_gate_off` | no debit, no purchase | as expected | PASS |
| 9 | `bazaar_buy_human_listing` | bot −100; seller +100; item `held`, `source=bazaar_buy`, in bag; `paidToSeeder` 100 | as expected | PASS |
| 10 | `bazaar_buy_over_ceiling` (1,000 > 500) | `overCeiling` +1, no debit | as expected | PASS |
| 11 | `bazaar_buy_no_value_fallback` (loot value 0, `maxPrice` 0) | `noCeiling` +1, no debit | as expected | PASS |
| 12 | `bazaar_buy_reserve` (bank 5,050, reserve 5,000, price 100) | `unaffordable` +1, no debit | as expected | PASS |
| 13 | `bazaar_buy_cheapest` (400 and 100) | buys at 100 | −100 | PASS |
| 14 | `bazaar_buy_need_satisfied` | one purchase, then `noNeed` | as expected | PASS |
| 15 | `bazaar_buy_not_resold` | `bazaar.skipped.needed` +1, nothing listed | as expected | PASS |
| 16 | `bazaar_buy_bot_listing` (loot value 100 → ask and ceiling 300) | buyer −300, seller listing `sold`, `paidToBots` 300 | as expected | PASS |
| 17 | `bazaar_buy_two_bots_one_listing` | two debits; one commits, one refunded `lostRace` | as expected | PASS |
| 18 | `bazaar_buy_need_fairness` (load cap 1, two needs) | the later need is bought | −100 | PASS |
| 19 | `bazaar_buy_need_once_per_pass` (two listings, `quantity` 1) | one debit, at 100 | as expected | PASS |
| 20 | `bazaar_buy_own_listing` | no debit; own listing still listed | as expected | PASS |
| 21 | `bazaar_buy_lost_race` | refunded, `lostRace` +1 | as expected | PASS |
| 22 | `bazaar_buy_seller_unresolved` (owner OID unresolvable) | refunded, `sellerUnresolved` +1 | as expected | PASS |
| 23 | `bazaar_buy_double_buy_guard` | a buyer of the retired listing is not charged | bank unchanged | PASS |
| 24 | `bazaar_buy_crash_refund` (debit applied, never executed, store reload) | refunded, `reconcile.refunded` +1 | as expected | PASS |
| 25 | `bazaar_buy_crash_commit` (object delivered, no MySQL commit, reload) | committed, `held` `bazaar_buy` row, no second debit, `reconcile.committed` +1 | as expected | PASS |
| 26 | `bazaar_buy_gate_off_refund` | refunded, not executed | as expected | PASS |
| 27 | `bazaar_buy_bot_to_bot_recovery` (sale POD suppressed, reload) | seller `listed` → `sold`, `reconcile.settled` +1 | as expected | PASS |
| 28 | Production, boot 18:45: real roster bots buy each other's listings | visits `outcome=bought`; seller paid; losers refunded | bot 274 bought the knife (348 → bot 84) and the staff (708 → bot 850); refunds to 848, 849, 851, 84 (708) and 850 (348); ledger 826–843 | PASS |
| 29 | Production, boot 20:56 | as 28 | bot 851 bought bot 848's DLT20a (483); bot 84 bought bot 274's (615); refunds to 849 (483) and 850 (615); ledger 1166–1177 | PASS |
| 30 | Money conservation on production trades | every debit matches one seller credit or one refund | true for all 11 debits (4 commits, 7 refunds) | PASS |
| 31 | Store invariants after the run | `orphanRecords` 0, `rosterWithoutRecord` 0 | 0, 0 | PASS |
| 32 | Universal: no crash, no new error signature attributable to the change | none | none (see Error lines) | PASS |

32 / 32.

**`bazaar_buy_production_visit` as a harness scenario failed both times, on timing.**

- **What happened:** production bots visit as soon as the server boots. The scenario takes its
  counter baseline when the harness starts, 300 s later, and resets cooldowns. By then every bot's
  need was met, so the next visits had nothing to buy.
- **How the assertion was measured:** assertions 28–29 measure the production behaviour directly
  from `missiondiag.log` and `simbot_ledger`.
- **Debt:** the scenario needs a listing staged after its baseline.

## Error lines in the run window

| Count | Line | Disposition |
|---|---|---|
| 2 | `doInstantBuy … Seller not found [<helper name>]` | `bazaar_buy_lost_race`: a harness helper creature buys the Market Seeder listing through stock `doInstantBuy`. Stock code resolves the seller by name, and the system seller is not in the player-name map. This is pre-existing stock behaviour for seeded listings; the bot path resolves the seller by OID. |
| 4 | `Auction Item failed validation: missing owner` | stock maintenance deleting SOLD harness listings whose buyer was a despawned helper creature (as in F_0.9.9) |
| — | `cannot open …lua` lines | unrelated boot noise, present before the run |

No crash, no backtrace, no BDB signature; `gdb.log` dates from 2026-09-21.

## Configuration

Temporary settings, all removed after the run (the Lua file equals its pre-run copy plus the
committed scenario corrections):

- the harness enabled, with `startupDelaySeconds` 300 and its scenario filter;
- the reaper override (on, 60 s);
- `bazaarBuy = true`;
- the live brawler `buyList` (survival knife, metal staff, DLT20a rifle).

`bazaarBuy` is `false` again on the live server; arming it live is the owner's call.

## Evidence

- `bin/log/trip-verify-20261016-183345-f0910-bazaar-buy-evidence.log`: all `BAZAAR_*` events, the
  production ledger rows, the error summary and the gtest summary.
- `bin/log/trip-verify-20261016-183345-f0910-bazaar-buy-gtest.txt`
- `bin/log/trip-verify-20261016-183345-f0910-bazaar-buy-final-dashboard.json`
