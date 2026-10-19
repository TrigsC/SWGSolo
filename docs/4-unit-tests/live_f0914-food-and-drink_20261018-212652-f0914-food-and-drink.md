# Live verification: F_0.9.14 food and drink for PlayerBots

**Run:** `20261018-212652-f0914-food-and-drink` (2026-10-18 21:26 to 2026-10-19 07:30 host time)
**Plan:** `docs/1-plans/F_0.9.14_p10o-food-and-drink.plan.md`
**Branch:** `feat/p10o-food-and-drink` (uncommitted working tree on top of `11034ab098`, v0.9.14)
**Profiles:** combat (food seam), economy and persistence (doses, item removal, bazaar restock),
REST and dashboard
**Verdict:** LIVE_VERIFICATION_PASS

## Changed subsystems

- `SimBotConsumables.h` (new pure policy) and its gtests;
- `Consumable::applyConsumptionBuff`, which the player use path now calls;
- the `AiAgent` flag `simBotConsumableParity`, and `AiAgent::getSkillModFallback` split out of
  `getSkillMod` (stock behavior unchanged);
- the flagged food seam in `CombatManager`;
- in `SimPlayerManager`: config and gates, the stomach, eating, last-dose removal, restock, SELL
  protection, dashboard, inspect and harness ops;
- the three eat call sites in `SimHunterController`;
- `app.js`.

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) on every rebuild (9 builds) |
| `luac -p` | clean on the working copy, every verify copy and the restored live copy |
| `node --check app.js` | clean |
| gtests `SimBot*`, server **stopped** | 177 / 177 on each of 4 runs (7 new) |
| Codex code review | APPROVED after 2 rounds, then APPROVED for each of the 4 verification-time deltas |

## Restarts

| Boot (host) | Result | Cause and fix |
|---|---|---|
| 21:29 | 48 / 59, `food_last_dose_removes_item` | Scenario defect: it asserted the MySQL ledger before a flush. Added `flushNow`. |
| 21:37 | 50 / 59, `food_combat_seam` | **Product defect:** with a crispic buff, the bot's accuracy fell back to `getChanceHit()*100`, because the food hid `AiAgent::getSkillMod`'s fallback. First fix restored only the template statistic. |
| 21:50 | 50 / 59, same, `accuracy=-170 defense=54 base=212/3` | The stock fallback for any accuracy mod is `100 + 2 x level` (212 for this bot). The fallback became its own unchanged method, `getSkillModFallback`, which the seam uses. Defense (+54) was right. |
| 22:02 | 53 / 59, `food_sell_protected` | Scenario defect: the seller had no listing-fee credits, and the scenario asserted a counter with no harness baseline. Both restock sellers had the same credit gap; all fixed. |
| 22:12 | 58 / 59, `food_production_seed` timed out | **Product defect:** a consumable need took every listing on the planet as a raw candidate and spent its 6-load budget on the cheapest non-food items. Candidates are now filtered by the template's client game object type, as buy-list entries are. |
| 23:28 and 23:36 | 35 / 59, `armor_mitigation_live` | Pre-existing flaky F_0.9.13 scenario: one fight must land an armored hit. Production armor absorbed hits normally in the same boots (7 hits, 243 absorbed). Passed on the next unchanged boot. |
| 23:47 | **59 / 59** | |
| 06:31 | **59 / 59** | Final build, with the owner's amended eat trigger (below). |

**Owner decision during verification:** bot 850 bought food, but all 21 of its fights in one boot
were travel ambushes, so under "eat only before bot-started fights" it never ate. The owner chose
to also eat when a bot turns to fight an attacker on a travel leg
(`SimHunterController::defendAgainstInterceptor`).

**Restart window maintenance (owner-approved):** before the first verify boot, 17 zombie `core3`
unit-test processes, some days old, still held 108 open file descriptors each in `bin/databases/`.
With the server stopped they were killed by exact PID, the database was backed up to
`~/dbbackup-20261019-f0914` (sizes and md5 verified) and `db5.3_recover` completed cleanly. Later
test runs used `timeout -s KILL 120`, and no process held the database before any start.

## Assertions

Harness figures are from the final boot (06:31). Production figures are from boots 8 and 9.

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build | warning-clean | warning-clean | PASS |
| 2 | Lua and JS syntax | clean | clean | PASS |
| 3 | `SimBot*` gtests, server stopped | all pass | 177 / 177 | PASS |
| 4 | `store_boot_invariant` | PASS | PASS | PASS |
| 5–14 | SELL and BUY regressions (10) | PASS | PASS | PASS |
| 15–28 | Weapon loadout regressions (14, incl. `loadout_production_equip`) | PASS | PASS | PASS |
| 29–49 | Armor and appearance regressions (21) | PASS | PASS | PASS |
| 50 | `food_eat_before_engage` | three items eaten, food 36, drink 38, buffs on | as expected | PASS |
| 51 | `food_no_reeat_while_buffed` | second call eats nothing | 1 dose left each | PASS |
| 52 | `food_stomach_full_waits` | both skipped `stomach_full` | as expected | PASS |
| 53 | `food_digest_frees_room` | 90 aged 20 s digests to 89, eats to 99 | as expected | PASS |
| 54 | `food_last_dose_removes_item` | item destroyed, row gone, ledger `item -1 consume` | as expected | PASS |
| 55 | `food_combat_seam` | accuracy +10, defense +54, both 0 after the gate closes | as expected | PASS |
| 56 | `food_gate_off_noop` | no eat with gate off; flag and buffs follow the gate on a live body | as expected | PASS |
| 57 | `food_respawn_reeats` | respawn clears the deadline, stomach kept (10 to 20) | as expected | PASS |
| 58 | `food_sell_protected` | only the filler lists; crispic stays | 1 listed, 2 doses kept | PASS |
| 59 | `food_restock_below_min` | 4 doses, buys an 8-dose stack debit-first | committed, 12 doses | PASS |
| 60 | `food_no_restock_at_min` | 10 doses, no purchase | 0 debited | PASS |
| 61 | `food_restock_price_per_dose` | cheaper per dose wins, over-price skipped | 1,000 paid, 1 skip | PASS |
| 62 | `food_production_seed` | a production visit buys | PASS on boots 8 and 9 | PASS |
| 63 | Production purchase with own credits | roster bot buys seeded food | bot 850: brandy 800 and crispic 400 committed (boot 8); 849, 850, 851 contended on boot 9, losers refunded | PASS |
| 64 | Production eating before a real fight | `CONSUME` immediately before a bot-started engage | 12 meals by bot 850, e.g. `LAIR_ENGAGE` then `CONSUME` then `LAIR_ATTACK phase=engage started=true inCombat=false` | PASS |
| 65 | Production stomach and stack accounting | fill matches item filling; last dose removes the stack | food 10 (crispic), drink 38 (brandy), digesting between meals; crispic stack `lastDose=1` | PASS |
| 66 | Store invariants | `orphanRecords` 0, `rosterWithoutRecord` 0 | 0, 0 | PASS |
| 67 | Universal: no crash, no new error signature | none | none; `gdb.log` unchanged at 1,190,538 bytes | PASS |
| 68 | No process holds the database before a start | 0 | 0 on every start | PASS |

68 / 68.

## Notes and follow-ups

- Seeded stacks were uncrafted template items. Template crispic lasts 45 s, which is why bot 850
  re-ate within a lair. Crafted crispic lasts 9 to 18 minutes.
- Pre-existing: harness `deleteIdentity` leaves `simbot_progression` rows in MySQL. They reappear
  as orphans at the next boot (44 at this run's baseline) and the reaper override clears them.
- Pre-existing: `armor_mitigation_live` depends on one fight landing an armored hit; it failed on
  two of nine boots.

## Configuration

Temporary, removed after the run:

- the parity harness enabled, `startupDelaySeconds` 300, scenarios trimmed to the matrix above plus
  the temporary `food_production_seed`;
- the reaper override (on, 60 s).

Owner-approved and kept live, unstaged: `consumablesEnabled = true` and
`consumables.bazaarRestock = true` (`-- TRIP-VERIFY F_0.9.14 (live-only)`).

## Evidence

- `bin/log/trip-verify-20261018-212652-f0914-food-and-drink-evidence.log`:
  - the nine matrix watchers;
  - production purchases and every production `CONSUME` event;
  - bot 850's meals in context, and inspects of the buyers;
  - final consumables, armor and invariant figures;
  - gtest results and the `gdb.log` size.
- `bin/log/trip-verify-20261018-212652-f0914-food-and-drink-final-dashboard.json`
