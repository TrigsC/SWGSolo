# Live verification — F_0.9.13 armor loadout

**Run:** `20261017-220959-f0913-armor-loadout` (2026-10-17 22:09 to 2026-10-18 01:12 host time)
**Plan:** `docs/1-plans/F_0.9.13_p10n-armor-loadout.plan.md`
**Branch:** `feat/p10n-armor-loadout` (uncommitted working tree on top of `2ba7b1136b`, v0.9.13)
**Profiles:** combat (armor mitigation), economy and persistence, REST and dashboard
**Verdict:** LIVE_VERIFICATION_PASS

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) on every rebuild, including the final one |
| `luac -p sim_player_manager.lua` | clean (working copy, verify copies, restored live copy) |
| `node --check app.js` | clean |
| gtests `SimBot*`, server **stopped** | 167 / 167 (10 new `SimBotArmorLoadout`), run twice: before the first boot and after the parse fix |
| Codex code review | all code findings addressed; live verification was the only open gate |

The owner approved stopping the server for the gtests. `server-cycle.sh` gained a `stop` subcommand
that calls its existing guarded `stop_server`.

## Restarts

| Boot (host) | Reason | Result |
|---|---|---|
| 22:10 | first boot of the new build, 44-scenario matrix | 30 / 44: `armor_usability_filters` failed (scenario defect) |
| 22:20 | scenario fixed | **44 / 44** |
| 00:14 | production stage: armor gates armed, one seeded listing | product defect found: `armorLoadout = true` parsed as false |
| 00:49 | parse fix, full 45-scenario matrix plus production stage in one boot | **45 / 45** plus production |

## Defects found by live verification

**Product defect, fixed in the verified build:**

1. **`playerBotProgression.armorLoadout = true` parsed as false.** `applyPlayerBotProgressionConfig`
   read the field while the child `loadout` table was pushed; engine3
   `LuaObject::getBooleanField` reads the top-of-stack table (`lua_gettable(L, -2)`), so the
   lookup hit `loadout` and fell back to the default. The read now happens before `loadout` is
   opened. Every harness scenario arms the gate per identity, which is why only the production
   stage could see it: the dashboard showed `armor.configured = false` with the line present.

**Scenario defect (no production impact):**

- `armor_usability_filters` expected padded gloves to cover nothing. Stock
  `ArmorObjectImplementation` derives coverage from slots when the template sets none (gloves →
  arms), so the bot correctly wore the gloves and refused only the imperial chest. The scenario now
  asserts exactly that. The no-coverage reason stays covered by its gtest.

## Assertions

Matrix figures are from boot 00:49 (final code). Production figures are from the same boot.

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build, Lua, JS | clean | clean | PASS |
| 2 | In-binary gtests, server stopped | all pass | 167 / 167 | PASS |
| 3 | `store_boot_invariant` | PASS | PASS | PASS |
| 4–13 | SELL and BUY regressions (`bazaar_sale_settles`, `_name_collision`, `_instant_pickup`, `_lost_sale_recovered`, `bazaar_buy_human_listing`, `_over_ceiling`, `_not_resold`, `_bot_listing`, `_lost_race`, `_crash_refund`) | PASS | PASS | PASS |
| 14–27 | Weapon loadout regressions (13 `loadout_*` plus `loadout_production_equip`) | PASS | PASS | PASS |
| 28 | `armor_gate_off` | nothing worn | as expected | PASS |
| 29 | `armor_unbuffed_waits` | not worn, `deferredUnbuffed` +1 | as expected | PASS |
| 30 | `armor_buffed_equips` | worn and protected | as expected | PASS |
| 31 | `armor_keeps_when_buffs_drop` | still worn | as expected | PASS |
| 32 | `armor_respawn_rewears` | worn on the new body, unbuffed | as expected | PASS |
| 33 | `armor_rewear_encumbrance_deferred` | not re-worn unbuffed | as expected | PASS |
| 34 | `armor_best_per_slot` | best piece; 5 % better held by hysteresis | as expected | PASS |
| 35 | `armor_usability_filters` | gloves worn, imperial chest refused and unprotected | as expected | PASS |
| 36 | `armor_too_encumbering` | refused even when buffed | as expected | PASS |
| 37 | `armor_in_combat_deferred` | nothing equipped mid-fight | as expected | PASS |
| 38 | `armor_sell_protects` | worn piece protected, inferior duplicate listed | as expected | PASS |
| 39 | `armor_mitigation_live` | per-agent tally rises in a real fight | as expected | PASS |
| 40 | `armor_bazaar_upgrade` | better piece bought, `armorUpgrades` +1 | as expected | PASS |
| 41 | `armor_upgrade_over_budget` | `skippedOverBudget` +1 | as expected | PASS |
| 42 | `armor_upgrade_not_better` | `skippedNotBetter` +1 | as expected | PASS |
| 43 | `armor_upgrade_one_pending` | second buy refused while one pending | `armorUpgrades` +1 | PASS |
| 44 | `armor_replacement_failure_restores` | old piece back on after a failed transfer | as expected | PASS |
| 45 | `armor_sold_tombstone` | listed piece comes off, no longer protected | as expected | PASS |
| 46 | `armor_gate_disable_drains` | armor removed, source unprotected after drain | as expected | PASS |
| 47 | `armor_production_seed` (temporary) | seeded chest listed, a production visit buys | as expected | PASS |
| 48 | Gate parse after fix | `armor.configured` true | true; gate effective once the store loaded | PASS |
| 49 | Production purchase | a roster bot debits for armor with its own credits | bot 84 debited 1,500 for the seeded chest; bot 848 won a six-bot race for the leftover pre-fix seed, five `lost_race` refunds within 1 s, seller paid 1,500 | PASS |
| 50 | Buff rule | new armor waits for buffs | 848 and 84 both logged `deferred reason=unbuffed`, then equipped after buffing | PASS |
| 51 | Encumbrance | max secondaries fall by the piece's encumbrance | strength and quickness max 2,000 → 1,960 for both (chest: health 40, action 40) | PASS |
| 52 | Respawn re-wear | worn armor goes back on after death | bot 848 `rewear worn=1`, `rewears` 2 | PASS |
| 53 | Production mitigation | an armored roster body absorbs damage | bot 848 current body `mitigatedTotal` 54; copy condition 1000 → 993 | PASS |
| 54 | Store invariants after the run | `orphanRecords` 0, `rosterWithoutRecord` 0 | 0, 0 | PASS |
| 55 | Universal: no crash, no new error signature | none | none; `gdb.log` unchanged at 1,190,538 bytes | PASS |

55 / 55.

**Not observed live (covered by construction):** no player was connected, and no fight against an
armored-outfit NPC was sampled. The player path is a pure move of the stock lines into
`applyWornArmorReduction` / `applyWornArmorBlastReduction`, and `simBotWornArmor` is set only by
the sim-layer equip code and reset in `loadTemplateData`.

## Observations

- **Dashboard display quirk (follow-up):** `playerBotProgression.armor.bazaarUpgrades` and
  `gates.armorBazaarUpgrades` always read false, because they evaluate the gate for identity 0 and
  `playerBotBazaarBuyGateEnabled` returns false for identity 0 by design. Per-identity purchases
  work (assertion 49).
- **Leftover harness identities:** the 00:14 boot was stopped before its seed scenario could delete
  its seller, harness identity 1444 (11,480 harness-granted credits, outside the production
  population). Earlier boots also left positional harness identities (1321, 1322, 1389, 1390,
  1442, 1443, 1445, 1446). None affect the store invariants.

## Configuration

Temporary settings, removed after the run:

- the parity harness enabled, `startupDelaySeconds` 300 (120 in the production boot), scenarios
  trimmed to the matrix above plus the temporary `armor_production_seed`;
- the reaper override (on, 60 s).

Owner-approved and kept live, unstaged like the other live-only lines:

- `armorLoadout = true, -- TRIP-VERIFY F_0.9.13 (live-only)`;
- `loadout.armor.bazaarUpgrades = true, -- TRIP-VERIFY F_0.9.13 (live-only)`.

After the restore the dashboard showed the reaper back off, armor configured and enabled, two
production bots wearing two pieces, and both store invariants at 0.

## Evidence

- `bin/log/trip-verify-20261017-220959-f0913-armor-loadout-evidence.log`: the three matrix
  watchers, the production and per-bot mitigation watchers, the armor dashboard sections, the
  final inspect of bots 848 and 84, every `LOADOUT_ARMOR`, armor `BAZAAR_BUY` and `BAZAAR_SALE`
  event of the final boot, the ledger rows of both seeded auctions and the `gdb.log` size.
- `bin/log/trip-verify-20261017-220959-f0913-armor-loadout-final-dashboard.json`
