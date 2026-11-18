# Live verification: F_0.9.21 P.10r Artisan surveying and sampling

**Plan**: `docs/1-plans/F_0.9.21_p10r-artisan-surveying.plan.md`
**Run ID**: `20261117-225454-f0921-artisan-surveying`
**Evidence**: `MMOCoreORB/bin/log/trip-verify-20261117-225454-f0921-artisan-surveying-final-missiondiag.log` (final boot), `...-missiondiag.log` (earlier boots), `...-artisans.log`, `...-dashboard.json`
**Verdict**: PASS (13/13 assertions). 13/13 scoped harness scenarios ran in one boot on the final binary, and 4 real artisans ran on the miner controller with every live gate the owner chose: sampling on, 4 artisans, legacy retirement on, stale stacks on.

## Scope

- **Harness** (`onlyScenarios`, `continueOnFailure`):
  - the nine new scenarios: `sample_gate_off`, `sample_requires_artisan`, `sample_requires_tool`,
    `sample_deposit_and_xp`, `sample_stack_merge`, `survey_tool_grant_idempotent`, `stale_stack_discard`,
    `stale_stack_listed`, `stale_stack_kept_high_quality`;
  - four F_0.9.20 regressions: `harvest_kill_and_deposit`, `harvest_sell_threshold`,
    `bazaar_resource_listing_reserve`, `harvest_bag_room_discard`.
- **Real controller pass:** 4 roster artisans (3679 Vedethoi, 3680 Nohoco, 3681 Camo, 3682 Ravi) and the 10 legacy
  SimMiners on Corellia, Naboo and Tatooine.

## Build and unit tests

- The build is warning-clean after every change, and `luac -p` is clean after every Lua edit.
- `SimBot*` gtests ran with the server stopped under `timeout -s KILL 120`: **247/247**, 7 of them new
  (`SimBotSampling*`).
  - The first run had 1 failure, caused by bad test data: density 0.30 with skill 0 fails stock's density floor
    before the rate check. The test now uses 0.40.
  - Teardown hung and was killed, as usual. No process held `bin/databases` before or after.

## Defects found live and fixed during this pass (each Codex-reviewed, APPROVED)

1. **A restart stranded survey tools.**
   - Tools granted at 23:02 were lost by a restart at 23:05, before the 5-minute delta save. The boot item reconcile
     wrote them off, but the one-time grant receipts survived, so the 4 artisans had no tools and nobody to buy from.
   - Owner decision: a tool written off by the reconcile is re-granted. The reconcile now revokes the receipt
     (a `receipt` −1 row). At load, a receipt counts as grants − max(revocations, reconciled losses of granted
     objects).
   - Result: exactly one re-grant per class (8 grant rows and 8 receipts per artisan). The next two boots granted
     nothing.
2. **Real samples were all `noSpawn`.**
   - `SimMinerController::targetResource` holds the resource type (`steel_duralloy`), while the spawn name is
     `intelligentResourceName` (`Citeian`).
   - The intelligent finish now passes the spawn name, and `samplePlayerBotResource` falls back to
     `getCurrentSpawn(type, zone)`.
3. **Idle legacy miners never retired.**
   - A `WAITING` miner never re-enters `startSimLoop`, so a retirement request on it never completed.
   - The request now books a generation-gated work-loop check, which acts only on an `IDLE`/`WAITING` miner with no
     assignment. A busy miner retires at its next `startSimLoop` boundary instead, ignoring the state left by the
     action that just completed.
4. **Missing sample effect (owner, in game).** Artisans knelt but played no effect. Stock `sendSample` broadcasts the
   active survey tool's `sampleAnimation` as a `PlayClientEffectLoc`. `broadcastPlayerBotSampleEffect` now does the same
   with the artisan's held tool of the spawn's class at each sample start.
5. **Artisans were hard to find on the dashboard (owner).** The WILDS roster now tags artisans, shows a
   `Sampling <resource>` phase and lists bodies with a live presence first (dashboard-only change).
6. **Test-only fixes:**
   - the sample scenarios now train Surveying 1–4, because a novice rounds below stock's 2-unit minimum;
   - `stale_stack_listed` now grants credits for the listing fee and asserts the row is `retrieved` after expiry and
     the sweep.

## Assertion table (final binary unless noted)

| # | Check | Expected | Actual | Result |
|---|---|---|---|---|
| 1 | Harness, final boot | 13/13 in one boot | 13/13, no fails | PASS |
| 2 | Stock sample math shared with players | identical player path; helper reproduces stock bands | gtests (density floor, rate fail, too-few units, gamble/rich bands) pass; `sendSampleResults` keeps stock messages and RNG order | PASS |
| 3 | Real artisan bodies on the miner controller | artisans attach as `SimMinerController`, not attackable, own capacity separate from hunters | 4 `SimPveBodyAttached` artisans each boot; the 10-brawler hunter pool unchanged | PASS |
| 4 | Real artisan sampling | `sampled` with units and XP through the miner controller | Vedethoi: `sampled` 3 units / 6 XP per sample; one merged `sample` container; `resource_harvesting_inorganic` 0 → 45 XP; 34 mineral units | PASS |
| 5 | Survey tools held and class-limited | 4 classes per artisan, sampling only matching spawns | 16 tools held (4 × mineral/chemical/gas/water); `sample_requires_tool` returns `noTool` | PASS |
| 6 | Lost tool re-granted exactly once | after a reconcile write-off, exactly one new grant per class; none at later boots | 4 + 4 grant rows per artisan; unchanged across two further boots | PASS (manual integration check; coverage debt logged) |
| 7 | Legacy retirement to the formula target | `legacyLive` → max(0, 10 − sampled artisans), never below | request then `SimLegacyMinerRetired` 1 s later; 10 → 9 at target 9 on two boots; held | PASS |
| 8 | Stale stacks | discard below keep-quality, list when sellable, keep high-quality | `stale_stack_discard`, `stale_stack_listed`, `stale_stack_kept_high_quality` pass; `BAG_ROOM ... stale=1` | PASS |
| 9 | F_0.9.20 regressions | harvest, sell threshold, reserve and bag room unchanged | 4/4 pass | PASS |
| 10 | Harness never on real listings | harness listings withdrawn | every harness listing expired and retrieved (`stale_stack_listed` asserts `retrieved`) | PASS |
| 11 | Universal | clean start, process alive, no crash or new fatal | core3 up 12+ min on the final boot; `screenlog.0` has only the GDB catchpoint lines | PASS |
| 12 | Artisans train from sampling XP | the first survey box trains from real XP, and its mod rises | Vedethoi, Nohoco and Camo trained `crafting_artisan_survey_01` (XP 1234 / 936 / 845); Vedethoi's `surveying` went 20 → 35; next box Surveying II at 3000 | PASS |
| 13 | In-game sample visuals (owner) | the artisan kneels and shows its survey tool's stock sample effect at each sample start | owner confirmed in game on Vedethoi (Tatooine 3847, -4862) after the effect fix | PASS |

## Temporary settings (restored, `luac` clean)

- Harness: `enabled`, `continueOnFailure`, `onlyScenarios`.
- Bazaar: `maxListingsPerBot` 2 and `resourceListingReserve` 1 for the run; the shipped values are 25 and 10.
- **Kept live-only, at the owner's choice:**
  - `samplingEnabled = true`;
  - `harvest.staleStacks.enabled = true`;
  - `retireLegacyMiners = true`;
  - artisan distribution `count = 4`.

## Observations (not blocking)

- **Only 1 of 4 artisans sampled in the window.** The miner planner hands out a few assignments at a time, and most
  were held by legacy miners. The others will pick up assignments as legacy miners retire and the planner rotates.
- **The artisan bazaar trip was not exercised live.** At about 3 units a sample, a stack needs hours to reach the
  sell threshold or go stale. The pricing, reserve and stale rules it calls are covered by the harness; the trip
  itself is logged in `COVERAGE-DEBT.md`.
- **Coverage-debt entries added:** receipt revocation, the spawn-name bridge, legacy retirement, and the artisan
  bazaar trip. All are reachable only by real miner-controller bodies.
