# Live verification: F_0.9.20 P.10q Scout harvesting

**Plan**: `docs/1-plans/F_0.9.20_p10q-scout-harvesting.plan.md`
**Run ID**: `20261117-183159-f0920-scout-harvesting`
**Evidence**: `MMOCoreORB/bin/log/trip-verify-20261117-183159-f0920-scout-harvesting-missiondiag.log` (harness run), `...-clientpass.log` (owner client pass), `...-dashboard.json` (post-run snapshot)
**Verdict**: PASS. 7/7 scoped harness scenarios on the final binary, plus the owner client pass on the same code line.

## Scope (owner rule: receipt runs stay limited to what the release changes)

- **Harness**: the 7 harvest and listing scenarios only, run with `onlyScenarios` and `continueOnFailure = true`:
  - `harvest_gate_off`, `harvest_requires_scout`, `harvest_kill_and_deposit`, `harvest_stack_merge`;
  - `harvest_sell_threshold`, which now withdraws its own listing;
  - `bazaar_resource_listing_reserve` (new);
  - `harvest_bag_room_discard` (new).
- **Owner client pass (2026-11-1x to 11-17)**: real roster Scout bots harvest grouped kills (dewback, rockmite, womp rat, kreetles), sell resource stacks at the 500-unit threshold, and discard loot from full bags.

## Changed subsystems

- **New files**:
  - `SimBotHarvest.h`: pure policy.
  - `CreatureHarvestYield.h`: stock yield and XP helper.
  - `SimBotHarvestTest.cpp`: gtests.
- **`SimPlayerManager`**:
  - harvest gate and claims, plus `harvestPlayerBotCorpse` / `depositPlayerBotResource`;
  - the supply and demand snapshot, resource selling, and the dashboard;
  - harness ops and counters;
  - the trainer's deferred-XP copy;
  - the resource listing reserve;
  - the `makePlayerBotBagRoom` / `destroyPlayerBotHeldItem` discard.
- **`CreatureManagerImplementation`**: the corpse hook. Group-owned corpses are offered to each roster bot in the owning group.
- **`SimHunterController`**: harvest on loot-step arrival, and the group tick now runs the loot step.
- **`sim_player_manager.lua`**: the `harvest` config, the loot discard thresholds, `resourceListingReserve`, the brawler plan with Scout 4x4x, and the harness scenarios.
- **Web dashboard** (`app.js`): a harvest panel.

## Fixes made during the client pass (Codex-reviewed, APPROVED)

1. **Scout XP stayed capped.** The trainer kept an inline copy of the deferred-XP list, so Novice Scout XP stopped at 2000. The copy now strips `scout` and `medical` from the raw gate flags.
2. **Grouped kills queued no claims.** The hook read the threat map, which held only the human tank. It now offers a harvest-only claim to each roster bot in the owning group, which is the stock `canHarvestMe` rule.
3. **Grouped bots never used their claims.** `runGroupTick` now runs the loot step after the defence tiers.
4. **Loot crowded resources off the bazaar.** `resourceListingReserve` now limits loot to `max − reserve` listings, and resources are listed first on each visit.
5. **A full bag wasted the corpse.** Stock marks the corpse harvested before the deposit, so a failed deposit loses the harvest. `makePlayerBotBagRoom` now discards the least valuable loot before each harvest or loot attempt.

## Build and unit tests

- Warning-clean incremental build. `luac -p` is clean for every Lua edit, including arming and disarming the harness.
- `SimBot*` gtests, run with the server stopped under `timeout -s KILL 120`: **240/240**.
  - The test binary then hung at teardown and the timeout killed it, as on earlier runs.
  - Afterwards, no process held `bin/databases` open. The 60 defunct `core3` zombies are parented to PID 1, hold no files and are pre-existing.

## Boot

- `server-cycle.sh stop` ran for the tests, then `start`: ready in 8 s.
- All 7 scenarios finished about 20 s after the harness started.
- `core3` stayed alive (pid 260991). `screenlog.0` has no crash signatures; its only SIGSEGV and SIGABRT lines are the GDB catchpoint setup.

## Temporary scenario settings (all restored; dashboard confirmed 25 / 10 / harness off)

- `playerBotParityTest.enabled = true`, `continueOnFailure = true`, and `onlyScenarios` set to the 7 names.
- `bazaar.maxListingsPerBot = 2` and `resourceListingReserve = 1` for the run (shipping values 25 / 10).

## Assertion table

| # | Check | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | `harvest_gate_off` | gate off: no container, no XP | `HARVEST_RESULT id=3672 result=gateOff` | PASS |
| 2 | `harvest_requires_scout` | no novice box: `NO_SKILL` | `id=3673 result=noSkill` | PASS |
| 3 | `harvest_kill_and_deposit` | container + item row + resource ledger + scout XP | `id=3674 harvested units=1 xp=46` | PASS |
| 4 | `harvest_stack_merge` | second harvest of the same spawn tops up one container | 2 harvests, 1 container (id 3675) | PASS |
| 5 | `harvest_sell_threshold` | stack ≥ threshold is listed; harness listing withdrawn | `BAZAAR_LIST id=3676 price=50 result=listed`; then expired and retrieved | PASS |
| 6 | `bazaar_resource_listing_reserve` | with max 2 / reserve 1: 1 loot listing, the second refused by the reserve | `id=3677 ... result=listed` plus `result=skip_resource_reserve`; `bazaar.skipped.resourceReserve` +1 | PASS |
| 7 | `harvest_bag_room_discard` | 2 loot items, cap 3: both discarded before the harvest, then the harvest succeeds | `BAG_ROOM id=3678 discarded=2 free=3`; `harvested`; `loot.junkDiscarded` +2 | PASS |
| 8 | Harness listings never left on the real bazaar | `bazaar.listed` = `bazaar.retrieved` | 2 / 2 | PASS |
| 9 | Loot-cap override touched no production bot | no `BAG_ROOM` for a roster id in the run window | only id 3678 (harness) | PASS |
| 10 | Client: grouped kill harvested | group-member claims → loot step → harvest | womp rat: 84 harvested. Kreetles: 274 and 2432 each harvested all 3, walking up to 60 m during `GROUP_FOLLOW` | PASS |
| 11 | Client: bag-room discard on real bots | full bags make room; ledger and rows consistent | `BAG_ROOM` 848/849/2430 (10/10/9). 849: ledger has 10 `discard` rows, loot rows 38 → 28; 848 then harvested both corpses (previously `bagFailed`) | PASS |
| 12 | Client: scout XP flows, then caps | XP until the plan's scout boxes, then 0 | 9 boxes (novice + harvest 1–4 + movement 1–4), 92000 scout XP; later harvests `xp=0` | PASS |
| 13 | Client: resource listings | stacks ≥ 500 listed near the human floor | e.g. 552 bone at 717 cr, 508 bone at 660 cr | PASS |
| 14 | Universal | clean start, process alive, no crash or new fatal | as shown under Boot | PASS |

## Observations (not blocking)

- **Leftover harness identities.** The pre-existing harvest scenarios (`harvest_gate_off` to `harvest_sell_threshold`) don't delete their identities, so 3672–3676 remain. The two new scenarios do. One older harness listing (identity 3669, 1 hide at 50 cr) is still on the bazaar until it's bought or expires.
- **Small resource stacks fill bags.** Stacks stay small because a stack only merges within one spawn, and they now take up most of a bag. The owner chose a follow-up: discard or sell stacks from expired spawns below the threshold, keeping high-quality ones.
- **No bazaar visits for about 29 h.** Plan-complete bots idle when demand is empty (need = 0, holdings above the 50k targets). This is the known dispatch gap; the owner's policy decision is pending.
- **Resources have no buyers.** That waits for crafting.
