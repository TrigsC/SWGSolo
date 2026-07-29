# Code Review: F_0.6.0 — Multi-Family Hunter Demand (hide/bone/meat spread)

**Review Date**: 2026-07-29
**Version**: 0.6.0
**Files Reviewed**:

- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `docs/ARCHI.md`
- `docs/ai-pve-playerbot-design.md`
- `docs/4-unit-tests/COVERAGE-DEBT.md`

**Excluded (pre-existing owner changes, not part of this feature):**
`bin/scripts/mobile/dungeon/death_watch_bunker/death_watch_wraith.lua` and the
`utils/engine3` submodule.

**Plan**: `docs/1-plans/F_0.6.0_multi-family-hunter-demand.plan.md`

---

## Executive Summary

Hunters now credit three creature families (hide, bone, meat) per kill instead
of meat alone, so demand pressure spreads across families and meat saturation
can no longer idle the whole roster. This is a demand-spread slice only —
in-session consumption/drain remains a deferred consumer phase. **APPROVED**

---

## Changes Overview

Each kill credits all three families under a single target-lock in
`recordPveHunterHarvest` (per-family deposit gated at `harvestAmount >= 3`);
hide/bone demand is recognized by extending existing profiles in
`demandStateProfileUsesConceptualLabel` (`composite_armor_supply`→`hide*`,
`master_weaponsmith_staples`→`bone*`); each hunt reserves the expected units of
all three families on accept (`computeReservedInboundByProfileFamily` iterating
enabled profiles × 3 families), and both matchmaker paths run a shared
intra-pass multi-family signal decrement (`decrementPveHuntFamilySignals`) so a
single pass never over-dispatches a family. A `harvestByFamily` object is added
to the `pveActivity` dashboard JSON. New per-family expected/harvested fields on
`PveHuntOrder` are plain in-memory struct fields (no IDL/autogen change). All
behavior is simulation-only and, by explicit plan authorization, always-on (no
new gate). Build was warning-clean (`-Werror`); Lua parse-clean.

---

## Findings

Round 1 returned REQUEST_CHANGES with four findings. Of these, one was
in-scope for F_0.6.0 (documentation) and was addressed; the remaining three were
out of scope by policy (two pre-existing owner-owned working-tree files, one
inherent owner-controlled live gate). Round 2 returned **APPROVED** with zero
findings.

### Critical Issues

None.

### Major Issues

1. **Shared `death_watch_wraith` template damage reduced (~36%)** — `death_watch_wraith.lua:10`
   **Disposition: out of scope / excluded.** This is a pre-existing, owner-owned
   uncommitted working-tree change (tracked as a separate open owner item), only
   visible to the reviewer because it diffs the whole tree. It is not part of
   F_0.6.0, is not reverted here, and is excluded from the F_0.6.0 commit by
   selective staging.

### Minor Issues

1. **Documentation unfinished** — `docs/ARCHI.md:342`, design doc missing an F_0.6.0 section
   **Disposition: addressed.** ARCHI.md §12 hunter-dispatch paragraph was
   rewritten for the three-family model; `docs/ai-pve-playerbot-design.md` gained
   a **P.8.8** section (credit path, demand recognition, reserve-all-three, the
   deliberate demand-spread-only boundary) plus a phase-map entry. Round 2
   confirmed docs current.

2. **`engine3` submodule modified but parent gitlink unchanged** — `utils/engine3` (TaskManager.h)
   **Disposition: out of scope / excluded.** Pre-existing owner-owned submodule
   change, excluded from the F_0.6.0 commit; not reverted here.

3. **Live acceptance gate pending** — plan live-verify checkpoint
   **Disposition: addressed (verified).** Restarting the live server is
   owner-controlled; the owner had already restarted. Verified live 2026-07-29
   (~9h uptime, 0 errors): `demandFamilies` recognizes hide
   (`composite_armor_supply` signal=37007, supply=2100) and bone
   (`master_weaponsmith_staples` signal=39127, supply=840); `harvestByFamily`
   climbing hide=2100/bone=840/meat=2100 (= `hunterHarvestUnitsTotal` 5040,
   misses=0); `reservedInboundSupply == familyInbound` per family (no
   over/under-dispatch); roster stayed busy though meat is saturated
   (supply=55268 ≫ reserveTarget 2500) because hide/bone carry the pressure.

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; all-family crediting, demand mapping, expected-yield capture, inbound reservation, both matchmaker decrements, and dashboard output conform to the plan.
- [x] 2. Code Quality — Passed; shared helpers avoid duplicating family accounting.
- [x] 3. Architectural Compliance — Passed; structural logic in C++, tunables in Lua, manager/dashboard patterns followed.
- [x] 4. Distributed Object / IDL Discipline — Passed; new fields are in-memory struct fields, no IDL/autogen changes; target/accumulator/PvE locks remain non-nested.
- [x] 5. Lua/C++ Boundary — Passed; family reserves and ceilings configured in `sim_player_manager.lua`.
- [x] 6. AI-Economy / Simulation Safety — Passed; simulation-only; always-on is explicitly plan-authorized (no new gate).
- [x] 7. Error Handling — Passed; missing/sub-threshold families increment misses independently without blocking other deposits.
- [x] 8. Security — Not applicable; no endpoint, authentication, or sensitive configuration changes.
- [x] 9. Performance — Passed; bounded to three families and a small profile set, outside population-scale tick loops.

---

## Verdict

**APPROVED**

Round 2 found no Critical, Major, Minor, or Suggestion issues. The only in-scope
Round-1 finding (documentation) was addressed; the two owner-owned working-tree
files are excluded from the commit rather than reverted, and the live gate has
since been satisfied. Deferred (documented debt, unchanged by this release): an
in-session family **consumer** (crafter/chef bots eating stockpiled
hide/bone/meat) that would drain `familySupply` in-session and close the loop —
until then each family re-saturates at its reserve target, the same accepted
debt meat already carried. Pure-helper unit-test seam remains recorded at
`docs/4-unit-tests/COVERAGE-DEBT.md`.
