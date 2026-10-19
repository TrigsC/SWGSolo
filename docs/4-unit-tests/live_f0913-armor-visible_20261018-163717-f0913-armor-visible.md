# Live verification — F_0.9.13 armor loadout with Amendment A (visible armor)

**Run:** `20261018-163717-f0913-armor-visible` (2026-10-18 16:37 to 17:05 host time)
**Plan:** `docs/1-plans/F_0.9.13_p10n-armor-loadout.plan.md` (Amendment A, Phase 5)
**Branch:** `feat/p10n-armor-loadout` (uncommitted working tree on top of `2ba7b1136b`, v0.9.13)
**Supersedes:** `live_f0913-armor-loadout_20261017-220959-f0913-armor-loadout.md` (55/55 on the
pre-amendment build)
**Profiles:** combat (armor mitigation), economy and persistence, REST and dashboard, in-client
appearance (owner)
**Verdict:** LIVE_VERIFICATION_PASS

## Why this run exists

After the 55/55 receipt, the owner looked in game. Server-side armor was correct (slot, wearables
list, protection map, mitigation, encumbrance), but nothing showed on a bot. The novice bodies used
stock `dressed_commoner_*` templates whose client `.cdf` bakes clothing, which masks server-sent
armor; weapons are not masked, which is why Castu's knuckler always showed. The owner chose
Amendment A:

- clothing-free vendor bodies, as P.7 did for the ranked Jedi;
- server-sent clothing that armor swaps off and back on;
- at the owner's request, a fixed look per bot and ten species.

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`) on every rebuild |
| `luac -p` | clean on all 8 changed Lua files, the verify copies and the restored live copy |
| `node --check app.js` | clean |
| gtests `SimBot*`, server **stopped** | 170 / 170 (13 new in this chunk: 10 armor policy, 3 appearance) |
| `LuaMobileTest` templates | the 20 new bodies and the outfit groups pass; 11 pre-existing failures on unrelated mobiles (`tusken_king`, `tusken_guard`, `light_jedi_padawan`, `smart_doctor_buffer`) |
| Codex code review | all code findings addressed; live and in-client verification was the only open gate |

## Restarts on the amended build

| Boot (host) | Result | Cause |
|---|---|---|
| 15:22 | 20 / 47: `loadout_bazaar_upgrade` | **Harness isolation defect:** a harness buyer bought the owner's real Vibro Knuckler listing (Novi, 400 credits). The halted run's cleanup deleted that harness identity and the knuckler with it. Fixed: harness buyers only consider harness-created listings. |
| 15:55 | 27 / 47: `armor_keeps_when_buffs_drop` | Harness body was a species that cannot wear padded armor (`skippedRace`, correct product behavior). Fixed: harness-only `setBodyTemplate` pins a human body in armor scenarios. |
| 16:20 | 43 / 47: `armor_clothing_swap` | A chest plate (`vest`) never collides with the outfit shirt (`shirt`), as for players. Fixed: the scenario uses boots (`shoe`). |
| 16:28 | 35 / 47: `armor_mitigation_live` | A chest alone covers one hit location; one fight produced no armored hit. Fixed: a full padded set. |
| 16:37 | **47 / 47** plus production | final build and scenarios |

Earlier in this chunk (pre-amendment), live verification also fixed the `armorLoadout`
parse-order defect and the `armor_usability_filters` scenario; see the superseded report.

## Assertions

Matrix figures are from boot 16:37 (final code). Production figures are from the same boot.

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build, Lua, JS | clean | clean | PASS |
| 2 | `SimBot*` gtests, server stopped | all pass | 170 / 170 | PASS |
| 3 | New templates in `LuaMobileTest` | resolve | resolve; no failure involves them | PASS |
| 4 | `store_boot_invariant` | PASS | PASS | PASS |
| 5–14 | SELL and BUY regressions (10) | PASS | PASS | PASS |
| 15–28 | Weapon loadout regressions (13 `loadout_*` plus `loadout_production_equip`) | PASS | PASS, with the owner's real listings on the bazaar | PASS |
| 29–47 | Armor scenarios (19 `armor_*`) | PASS | PASS | PASS |
| 48 | `armor_clothing_swap` | boots stow the shoes; removal restores them | as expected | PASS |
| 49 | `appearance_stable_across_respawn` | same body, clothing, features, scale and hair after two respawns | as expected | PASS |
| 50 | `armor_production_seed` (temporary) | a production visit buys the seeded chest | as expected | PASS |
| 51 | Species and fixed bodies on production bots | vendor bodies across species | 84 Sullustan male, 274 Trandoshan female, 848 Bothan female, 850 Twi'lek female, 851 Human male | PASS |
| 52 | Server clothing on the new bodies | shirt, pants, shoes, hair slotted | `chest1`, `pants1`, `shoes`, `hair` present | PASS |
| 53 | Species wear rule | no garment the species cannot wear | Trandoshan 274 has no `shoes` slotted | PASS |
| 54 | Armor worn on production bodies | slotted and flagged | 5 bots wear 9 pieces (`chest2`, `shoes`, helmet), the shirt still in `chest1` | PASS |
| 55 | Production mitigation | an armored roster body absorbs damage | bot 84 current body `mitigatedTotal` 1,283; global 447 hits, 17,261 absorbed | PASS |
| 56 | Production purchases | roster bots buy with their own credits | composite helmets, chest plates and boots and vibro knucklers bought by 84, 274, 848, 849, 850 (`bazaar_buy`) | PASS |
| 57 | Harness isolation | no harness purchase of a non-harness listing | weapon regressions passed with the owner's listings present | PASS |
| 58 | Store invariants | `orphanRecords` 0, `rosterWithoutRecord` 0 | 0, 0 | PASS |
| 59 | Universal: no crash, no new error signature | none | none; `gdb.log` unchanged at 1,190,538 bytes | PASS |
| 60 | Owner in-client check (P.7 rule) | clothed, armor visible, look kept after death | owner: "All good" | PASS |

60 / 60.

## Incident

Before the harness isolation fix, harness identity 1522 bought the owner's Vibro Knuckler (Novi,
400 credits). The halted run's cleanup deleted identity 1522, its vault and the knuckler. Novi was
paid 400 credits. The item could not be restored server-side; the owner was told and can recreate
it in game. The fix makes a recurrence impossible for harness buyers.

## Configuration

Temporary settings, removed after the run:

- the parity harness enabled, `startupDelaySeconds` 300, scenarios trimmed to the matrix above plus
  the temporary `armor_production_seed`;
- the reaper override (on, 60 s).

Owner-approved and kept live, unstaged: `armorLoadout = true` and
`loadout.armor.bazaarUpgrades = true` (`-- TRIP-VERIFY F_0.9.13 (live-only)`).

## Evidence

- `bin/log/trip-verify-20261018-163717-f0913-armor-visible-evidence.log`:
  - the five matrix watchers of the amended build;
  - the final armor dashboard and invariants;
  - the production bodies and their armor inspects;
  - every `LOADOUT_ARMOR`, armor `BAZAAR_BUY` and `BAZAAR_SALE` event of the final boot;
  - the production purchases;
  - the `gdb.log` size.
- `bin/log/trip-verify-20261018-163717-f0913-armor-visible-final-dashboard.json`
