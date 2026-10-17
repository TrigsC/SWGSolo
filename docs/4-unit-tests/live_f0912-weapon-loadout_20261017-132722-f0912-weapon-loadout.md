# Live verification — F_0.9.12 weapon loadout

**Run:** `20261017-132722-f0912-weapon-loadout` (2026-10-17, 13:27 to 14:19 host time)
**Plan:** `docs/1-plans/F_0.9.12_p10m-weapon-loadout.plan.md`
**Branch:** `feat/p10m-weapon-loadout` (uncommitted working tree on top of `51bfcf52c7`, v0.9.12)
**Profiles:** economy and persistence; REST and dashboard; combat (kill XP type)
**Verdict:** LIVE_VERIFICATION_PASS

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) on every rebuild, including the final one |
| `luac -p sim_player_manager.lua` | clean (working copy and restored live copy) |
| `node --check app.js` | clean |
| gtests `SimBot*`, server **stopped** | 157 / 157 (10 new `SimBotLoadout`) |
| Codex code review | all code findings addressed; live verification was the only open item |

The in-binary test process printed `PASSED 157 tests` and then hung during shutdown, as known; the
600 s timeout ended it (`rc=124`).

## Server state before the run

The live server had crashed at 14:00 (SIGSEGV in the stock `TerrainCache::evict`, reached from an
ordinary AI `findNextPosition` on `TW-tatooine0`; no fork code in the stack) and sat halted in GDB.
With the owner's go-ahead, the halted GDB session was quit, the unit tests ran with the server down,
and the guarded lifecycle script restarted it for verification.

## Restarts

| Boot (host) | Reason | Matrix result |
|---|---|---|
| 13:27 | first boot of the new build | 18 / 25: `loadout_sell_lists_inferior_duplicate` failed |
| 13:38 | product fix 1 (buy-list keep prefers loadout-protected items) | 20 / 25: `loadout_bazaar_upgrade` counter read 0 |
| 13:47 | harness fix (upgrade and loadout counters registered) | 21 / 25: `loadout_upgrade_over_budget` listing failed |
| 13:55 | harness fix (upgrade sellers funded for the listing fee) | 24 / 25; production stage found product defect 2 |
| 14:06 | product fixes 2 and 3 (reconcile every activity; retain combat-deferred retries) | **25 / 25** plus production |

## Defects found by live verification

**Product defects, fixed in the verified build:**

1. **Two keep rules held an extra duplicate.** The owner's live brawler `buyList` keeps one Vibro
   Knuckler, chosen by oldest `acquiredAt`. With two knucklers acquired in the same second, the
   buy list could claim the inferior one while the loadout protected the best one, so the bot kept
   both forever. The buy-list keep now counts loadout-protected items first, then oldest, then
   object id (`listPlayerBotItemsAtTerminal` and `shouldPlayerBotVisitBazaar`).
2. **Hunting bots never re-equipped.** The slow-tick reconcile inherited the legacy early-game
   filter (grind, or plan complete). Bot 274 is graduated, plan incomplete, `activity=hunt`, so it
   kept its spawn weapon until death. With the loadout gate on, every activity is reconciled; the
   gate-off loop is unchanged.
3. **A combat-deferred full-sweep equip was not retried** (found by Codex on fix 2): a queued
   mismatch now marks the identity loadout-dirty, so the next ordinary tick retries it.

**Harness defects (no production impact):**

- the upgrade and loadout counters were not registered in the harness counter lookup and baseline;
- three upgrade scenarios listed a weapon from a seller with no credits for the listing fee.

## Assertions

Matrix figures are from boot 14:06 (final code). Production figures are from the same boot.

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build, Lua, JS | clean | clean | PASS |
| 2 | In-binary gtests, server stopped | all pass | 157 / 157 | PASS |
| 3 | `store_boot_invariant` | PASS | PASS | PASS |
| 4–7 | SELL regressions: `bazaar_sale_settles`, `bazaar_name_collision`, `bazaar_instant_pickup`, `bazaar_lost_sale_recovered` | PASS | PASS | PASS |
| 8–13 | BUY regressions: `bazaar_buy_human_listing`, `_over_ceiling`, `_not_resold`, `_bot_listing`, `_lost_race`, `_crash_refund` | PASS | PASS | PASS |
| 14 | `loadout_gate_off` | stock weapon kept, nothing protected | as expected | PASS |
| 15 | `loadout_equips_certified` | vault weapon equipped with the item's stats | as expected | PASS |
| 16 | `loadout_skips_uncertified` | starter kept | as expected | PASS |
| 17 | `loadout_follows_training_class` | a better knife does not replace unarmed | as expected | PASS |
| 18 | `loadout_template_complete_any_class` | any certified class | as expected | PASS |
| 19 | `loadout_hysteresis` | a 5 % better weapon does not swap | as expected | PASS |
| 20 | `loadout_respawn_reapplies` | same vault weapon after respawn | as expected | PASS |
| 21 | `loadout_sell_protects_equipped_and_spare` | protected items not listed | as expected | PASS |
| 22 | `loadout_sell_lists_inferior_duplicate` | inferior duplicate listed, best kept | as expected | PASS |
| 23 | `loadout_bazaar_upgrade` | 140-damage listing bought, then equipped; `bazaar.buy.upgrades` +1 | as expected | PASS |
| 24 | `loadout_upgrade_over_budget` (ceiling 2,500, price 3,000) | `skippedOverBudget` +1, no debit | as expected | PASS |
| 25 | `loadout_upgrade_not_better` (101 vs 100) | `skippedNotBetter` +1 | as expected | PASS |
| 26 | `loadout_upgrade_one_pending` | second buy refused while one is pending | `upgrades` +1 | PASS |
| 27 | `loadout_production_equip` (read-only) | a real roster bot equips a vault weapon | bot 274 | PASS |
| 28 | Production preview before arming (inspect, gate off) | certs and choice visible | `cert_knife_dagger`, `cert_vibroknuckler`; Vibro Knuckler, 205 DPS; knife, staff, pistol uncertified | PASS |
| 29 | Production equip after arming | body weapon = vault item | bot 274 equipped `vibroknuckler.iff` 130–280, `combat_meleespecialize_unarmed`, 41 s after arming; item row stays `held` | PASS |
| 30 | Production kill XP type | kill credited to the box being funded | `combat_meleespecialize_unarmed` 1,946,576 → 1,949,156 at 14:18:47 | PASS |
| 31 | Other roster bots | starter kept (no certified vault weapon for their class) | ids 84, 848–851: `source=starter` | PASS |
| 32 | SELL never lists the equipped weapon | no listing event for the knuckler | none | PASS |
| 33 | Store invariants after the run | `orphanRecords` 0, `rosterWithoutRecord` 0 | 0, 0 | PASS |
| 34 | Universal: no crash, no new error signature | none | none; `gdb.log` unchanged since the 14:00 pre-run crash | PASS |

34 / 34.

## Configuration

Temporary settings, all removed after the run (the live Lua was restored from its pre-run copy plus
the committed seller-credit scenario fix):

- the parity harness enabled, `startupDelaySeconds` 300, scenarios trimmed to the 25 above;
- the reaper override (on, 60 s);
- `weaponLoadout = true`, armed only for the production stage (from the harness's last scenario).

Pre-existing live-only lines (`bazaarSell`, `bazaarBuy`, the brawler `buyList`, progression gates)
were untouched. `weaponLoadout` is `false` again on the live server; arming it live is the owner's
call. Bot 274 keeps its knuckler copy until its next respawn, when the gate-off spawn path returns it
to the starter weapon; the vault item is unaffected.

## Evidence

- `bin/log/trip-verify-20261017-132722-f0912-weapon-loadout-evidence.log`: the five harness runs,
  all `LOADOUT` and `BAZAAR_BUY` events of the final boot, bot 274's preview and final inspect, the
  XP delta and the gtest summary.
- `bin/log/trip-verify-20261017-132722-f0912-weapon-loadout-final-dashboard.json`
