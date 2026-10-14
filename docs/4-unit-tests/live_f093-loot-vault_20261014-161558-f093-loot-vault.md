# Live verification — F_0.9.3 loot from kills and the hive item vault

**Run:** `20261014-161558-f093-loot-vault` (2026-10-14 16:15 UTC to 23:20 UTC)
**Plan:** `docs/1-plans/F_0.9.3_p10d-loot-and-vault.plan.md`
**Branch:** `feat/p10d-loot-vault` (uncommitted working tree on top of `37180e7fb9`)
**Verdict:** LIVE_VERIFICATION_PASS (final build)

## Static gate

| Check | Result |
|---|---|
| Build | warning-clean (`-Werror`), final build |
| `luac -p sim_player_manager.lua` | clean |
| `node --check app.js` | clean |
| gtests `SimBot*`, run with the server **stopped** | 118 passed, 10 suites (14 new in `SimBotLootStepTest`, `SimBotItemReconcileTest`). Run on the build of 16:25; the pure headers they cover did not change afterwards |
| Codex code review | converged; one agreed debt item (below) |

Database backed up before the first boot of the new schema:
`databases_backup_f093_20261014-162614` (byte-identical files).

## Defects found by live verification

Live verification found four production defects and one design fact. All are
fixed in the final build except the design fact, which is documented.

1. **Vault never created** (first boot). `creature_inventory.iff` is a `GENERIC`
   container (type 4), not `VOLUME` (type 2) as the plan assumed. The vault failed
   closed exactly as designed: the gate stayed shut and the stock path was
   unchanged (`AiEconomyItemVaultUnavailable reason=createFailed`, the only ERROR
   line of the whole run). Fix: accept either item container type, identify bags
   by template, and destroy a vault object that fails validation.
2. **Real hunters never looted.** In 90 minutes the loot step never started for a
   production bot. Graduated bots kill in `ENGAGING_LAIR`, and a destroyed lair
   starts the next travel leg immediately; the step refused to start while
   walking. Fix: on a travel leg the step may start while walking, and resumes
   the leg through `resumeTravelLeg` when it ends.
3. **Stale-callback races in the loot walk** (found by Codex while reviewing fix
   2). A corpse arrival or path failure could reach the interrupted phase's
   arrival or failure logic. Fixed with a serialized handshake: the arrival and
   path-result tasks carry their captured generation (thread-local) into the
   callback, and `lootArrivalMutex` orders the callback check against the
   finish.
4. **Harness oracle defects** (no production impact): the harness despawned the
   corpse it wanted looted; the credit oracle ignored loot cash; the no-record
   scenario waited for a kill award that could not come; the store invariant was
   broken by records left over from interrupted runs.
5. **Design fact — the item loss window is the 5-minute object save.** A restart
   less than one delta save after a loot loses the fresh object, as it would for
   a player. Observed on an intermediate boot: the item's MySQL row had flushed
   but the object had not been saved, and the boot reconciliation moved the row to
   `orphaned` with a −1 journal row for the bot (`heldMissing=1`), exactly as the
   plan's reconciliation table specifies. The restart probe now waits for a delta
   save before restarting.

## Assertions (final build)

| # | Assertion | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Build | warning-clean | clean | PASS |
| 2 | Lua syntax | clean | clean | PASS |
| 3 | Dashboard JS syntax | clean | clean | PASS |
| 4 | In-binary unit tests (server stopped) | all pass | 118 / 118 | PASS |
| 5 | Schema | 1015, `simbot_items` present | 1015, present | PASS |
| 6 | Vault | available, created once, never re-created | `available=true`, `created=0` on later boots, `loadFailed=0` | PASS |
| 7 | `store_boot_invariant` | PASS | PASS | PASS |
| 8 | `loot_items_to_vault` (component + weapon, identical stat snapshots) | PASS | PASS | PASS |
| 9 | `loot_cash_to_store` (cash balance = corpse cash) | PASS | PASS | PASS |
| 10 | `loot_gate_off_legacy` (despawn due within 10 s) | PASS | PASS | PASS |
| 11 | `loot_corpse_owner_is_bot` (inventory owner = bot body) | PASS | PASS | PASS |
| 12 | `loot_vault_cap` (cap 1: one item stays, `skippedVaultFull` +1) | PASS | PASS | PASS |
| 13 | `loot_death_before_loot` | PASS | PASS | PASS |
| 14 | `loot_transfer_fault` | PASS | PASS | PASS |
| 15 | `loot_no_record` (owner check refuses, `notCreated.noRecord` +1) | PASS | PASS | PASS |
| 16 | `vault_reconcile_states` | PASS | PASS | PASS |
| 17 | `reaper_tears_down_items` (object, bag and row gone) | PASS | PASS | PASS |
| 18 | `loot_production_walk` (real roster hunter) | PASS | PASS | PASS |
| 19 | `loot_two_boot_snapshot` | PASS | PASS | PASS |
| 20 | `retained_record_survives_restart` (two boots, delta save between): item in its bag, row `held`, identical stats | PASS | PASS | PASS |
| 21 | Production walk trace: claim → step begins **while walking** on `TRAVEL_TO_MISSION` → `looted` → step ends with `resumeLeg=true` → hunter moving again | in order | as expected (bot 84, corpse 281475055493039) | PASS |
| 22 | Production walk holdings: cash balance and one `credits_cash` journal row (`source=loot`, `ref`=corpse) | cash = 65 | `cash_credits=65`, ledger row 254 | PASS |
| 23 | Universal: no crash, backtrace or BDB signature; process up | none | none; only ERROR line is defect 1 (earlier build) | PASS |

23 / 23.

## Not exercised

- **A production bot looting an item.** The jawa's loot roll came up empty, so
  the real walk moved cash only. Item movement into a bag is covered by the
  harness on the same code path (`lootPlayerBotCorpse`).
- **Ordinary production yield.** In 3.5 hours of normal play no production bot
  produced a lootable corpse: every lair species the bots worked (rock beetle,
  dune bantha, dune lizard, gualama) has an empty loot table, and roster bots
  only fight back. This is correct behaviour and expected parity, not a defect.

## Open observations

- **Two empty bags remain mapped.** One is bot 84's: the loot step creates the
  bag before it knows the corpse holds only cash. The other could not be
  attributed from the dashboard; `reapDestroyFailed=0`, and the harness deletes
  identities through the same delete request that tears bags down.
- **Harness leftovers:** five `orphaned` item rows and three loose vault objects
  (deliberate test artifacts from forged reconcile cases and the crash-window
  item), and one orphan progression row awaiting the reaper (disabled live).
  The 22 older harness identities predate this chunk.

## Agreed debt (Codex code review)

A path result that passes its generation check can still be applied after the
movement it belongs to was replaced. This window is a pre-existing property of
`SimPlayerController`'s mover for every movement replacement (travel-combat flee
and resume, combat approach, traversal legs, PvP); closing it means changing path
acceptance in the shared base for miners and PvP too. Recorded for ARCHI.

## Configuration

Temporary settings were reverted after the run: harness disabled, scenario
filter removed, `startupDelaySeconds` 60, reaper off with 3600 s. Left armed live
by design: `playerBotProgression.lootEnabled = true` (ships `false` in the commit).

## Evidence

- `bin/log/trip-verify-20261014-161558-f093-loot-vault-final.log` (final-build
  loot events, vault and reconcile lines, error lines, unit-test summary)
- `bin/log/trip-verify-20261014-161558-f093-loot-vault-final-dashboard.json`
- `bin/log/trip-verify-20261014-161558-f093-loot-vault-production.jsonl` (90-minute
  production observation on the intermediate build: 0 lootable corpses)
