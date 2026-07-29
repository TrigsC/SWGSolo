# Tutorial 0.6.0 — Fanning one event out to N families without N locks or N drifting ledgers

**Audience**: advanced; Core3/engine3 internals (`ReadLocker`/`Locker` on
`CreatureObject`, `VectorMap`, file-local `static` helpers, the demand/supply
ledger in `SimPlayerManager`).
**Grounded in**: `SimPlayerManager.cpp/.h` and
`bin/scripts/managers/sim_player_manager.lua` from the F_0.6.0 (P.8.8) diff.

---

## The problem this release actually solved

A hunter kill used to credit exactly one conceptual resource family (`meat`). A
kill is really *one* game event that yields *three* things a butcher would
take — hide, bone, meat. Modeling only meat meant only meat ever accumulated
supply, only meat ever generated demand pressure, and the moment meat crossed
its reserve target the **whole roster** went idle (the residual F_0.5.0
saturation problem). The fix is to let one kill fan out into three family
credits — and to do it without three separate object locks and without the
demand ledger drifting out of sync when three families are dispatched in one
matchmaker pass.

Two sub-problems fall out of "one event → N families", and each has a small,
reusable pattern in this diff.

## Pattern 1 — read all N sub-quantities under a *single* target lock

The tempting shape is a loop that locks the creature once per family. That is
three lock/unlock cycles on the same `CreatureObject` for one kill, and — worse —
three *windows* in which the target's state can change between reads, so your
three deposits can disagree with each other.

`recordPveHunterHarvest` instead takes the target lock **once**, reads all three
families' yields and maxes into a small fixed array, releases the lock, then does
the bookkeeping:

```cpp
struct PveHunterHarvestFamily {
    String family;
    int harvestAmount = 0;
    int maxAmount = 0;
    // ...
};
PveHunterHarvestFamily harvests[3];
for (int familyIndex = 0; familyIndex < 3; ++familyIndex)
    harvests[familyIndex].family = getPveCreatureFamily(familyIndex); // 0=hide,1=bone,2=meat
```

The lock scope wraps only the *reads* — a consistent snapshot of the creature —
after which every deposit works off immutable local values. This mirrors the
project's iron rule from F_0.5.0's tutorial: hold an object lock for the shortest
possible consistent read, never across the follow-on work. Here the follow-on
work is the per-family deposit into `pveSessionHarvestByFamily` and the owning
`PveHuntOrder`, taken under `pveMutex` — a *different* lock, never nested inside
the target lock.

The per-family threshold gate lives in this second phase:

```cpp
PveHunterHarvestFamily& harvest = harvests[familyIndex];
if (harvest.harvestAmount >= 3) { /* deposit this family */ }
else { /* count a per-family miss */ }
```

Families are independent: a creature with no bone yield contributes hide and meat
normally and books a bone *miss*, rather than the whole kill failing. That
independence is the entire point — one absent sub-quantity must not veto the
others.

## Pattern 2 — file-local `static` helpers to name the family axis once

Indexing families by raw `int` (0/1/2) scattered across four call sites is how
you get a transposed-array bug six months later. The diff introduces three
file-local `static` helpers at the top of `SimPlayerManager.cpp` so the mapping
exists in exactly one place:

```cpp
static String getPveCreatureFamily(int index);                 // 0->"hide", 1->"bone", 2->"meat"
static uint64 getPveHuntOrderExpectedFamilyUnits(const PveHuntOrder& order, const String& family);
static uint64 getPveHuntOrderHarvestedFamilyUnits(const PveHuntOrder& order, const String& family);
```

`static` (internal linkage) is deliberate: these are private to this translation
unit, add no symbols to the link surface, and need no header/IDL change. They are
the seam the coverage-debt note earmarks for a future `SimPveMissionMath.h`
extraction and unit tests — pure functions with no lock or object dependency.

`PveHuntOrder` itself only gained six plain `uint64` fields
(`expectedHideUnits`, `expectedBoneUnits`, `expectedMeatUnits`, and the
`harvested*Units` trio), each defaulting to `0`. Because a `PveHuntOrder` is an
in-memory struct, **not** an IDL distributed object, there is no `.idl` edit, no
autogen regen, no persistence migration — an order predating the change simply
reads its per-family expectations as 0 and self-heals. That "additive plain
struct field" move is the cheapest way to widen a hot in-memory record.

## Pattern 3 — one shared decrement so the ledger can't drift

The demand ledger tracks, per `(profile, family)`, a `signalUnits` value that
gates dispatch (`signalUnits > 0` means "this family is still short, keep
hunting it"). When the matchmaker assigns an order, it must decrement the signal
it just consumed — otherwise the *next* iteration in the same pass sees stale
headroom and over-dispatches.

With three families per order and two matchmaker code paths (market + legacy),
the danger is four subtly-different decrement loops. The diff collapses them to
one helper, called from both paths after each assignment:

```cpp
static void decrementPveHuntFamilySignals(
        Vector<DemandFamilyResult>& demandResults, const PveHuntOrder& order) {
    for (int familyIndex = 0; familyIndex < 3; ++familyIndex) {
        String family = getPveCreatureFamily(familyIndex);
        uint64 expectedUnits = getPveHuntOrderExpectedFamilyUnits(order, family);
        // subtract expectedUnits from *every* demandResult whose family matches
    }
}
```

Note it decrements across **all** matching demand results, not just the one the
order was primarily dispatched against — because a single kill genuinely reduces
shortage for every profile that recognizes that family. This is the intra-pass
invariant the live dashboard confirmed: `reservedInboundSupply == familyInbound`
for every family (no over- or under-reservation).

## Pattern 4 — extend an existing profile's *conceptual label*, don't fork a new profile

Hide and bone needed to become *recognized* demand families. The plan's owner
decision was to reuse existing crafting profiles rather than invent new ones, so
the recognition change is one predicate:

```cpp
// demandStateProfileUsesConceptualLabel(profileKey, label):
//   composite_armor_supply       recognizes  hide / hide_*
//   master_weaponsmith_staples   recognizes  bone / bone_*
```

`high_damage_weapon_components` was *split out* of the branch it previously
shared so it keeps its own labels — a reminder that when you widen a shared
predicate, you check what else was riding on the old condition. The matching Lua
side is pure config: `acquisitionLedger.creatureFamilies = { "meat","hide","bone" }`
plus per-family `familyReserveTargets` and `familyAllocationCeilingFraction`
(`1.0` each — the anti-domination cap must never strand a family below its true
`desiredReserve`, the lesson F_0.5.0 paid for). No compiled constant; the axis
stays tunable without a rebuild, per §5/§10 of ARCHI.md.

## The dashboard tie-off

Finally the new state is surfaced the way §11 prescribes — assemble a JSON object
from the `VectorMap` and hang it off `pveActivity`:

```cpp
JSONSerializationType harvestByFamilyJson = JSONSerializationType::object();
for (int i = 0; i < harvestByFamily.size(); ++i)
    harvestByFamilyJson[harvestByFamily.elementAt(i).getKey()] = harvestByFamily.get(i);
result["harvestByFamily"] = harvestByFamilyJson;
```

That single field is what made acceptance a five-minute read instead of a
guess: `harvestByFamily` summing exactly to `hunterHarvestUnitsTotal` with
`hunterHarvestMisses = 0` is direct proof the per-family deposit accounting
neither drops nor double-counts a unit.

## Takeaways

- **One event → N credits: snapshot once under the object lock, deposit N times
  under the ledger lock.** Never lock the object per sub-item; never nest the two
  locks.
- **Name a new axis in exactly one place** — file-local `static` helpers over
  scattered magic indices. They double as your future unit-test seam.
- **Widen an in-memory record with defaulted plain fields**, not an IDL change,
  when the record isn't a distributed object — additive, migration-free,
  self-healing.
- **When a quantity fans out, its ledger update must fan out too, through one
  shared function** — divergent decrement loops are how a demand ledger silently
  over-dispatches.
- **Prefer extending an existing config-driven predicate to forking a new
  subsystem** — but audit what else shared the branch you just widened.
