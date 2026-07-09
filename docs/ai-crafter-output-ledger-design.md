# P.5.4 Crafted-Goods Output Ledger & Demand-Loop Closure — design

Companion to `ai-hive-inventory-design.md` (P.5.1–P.5.3, the raw-resource hive)
and `ai-miner-navigation-design.md`.

**Status: BUILD 1 (P.5.4a + P.5.4b) IMPLEMENTED, compiled clean `-Werror`
2026-07-02, PENDING RESTART+VERIFY (§9 build-1 criteria). P.5.4c/d not started.**

Implementation deltas from the design below (build 1):
- `reserveFromStockpileMatching` returns one `AiEconomyMatchedReservation`
  struct (token, entryID, resourceType, spawn name, matchedQuery + index, and
  the lot's 10 stats) instead of separate out-params; the crafter maps the
  matched index to the tier label via a parallel `tiers` vector.
- One new IDL method on `AiEconomyStockpileLot`: `addFinishedGoodQuantity(
  amount, qualityScore, tier)` — quantity add + running-average quality (kept
  in `oq`) + last-batch tier, since the lot class had no per-field setters.
- `validateEconomyData` allowlists extended: acquisitionSource `hive_crafter`,
  lifecycle `crafted`, identityConfidence `finished_good`.
- `AiEconomyStockpileInspectionLot` now carries the 10 stats + a
  `finishedGoodLot` flag (closes the long-standing "surface stats" follow-up);
  dashboard lot rows include `oq..cr`.
- Two independent lua gates as shipped: `useFamilyMatching` (P.5.4a) and
  `produceFinishedGoods` (P.5.4b), both C++-default **false**, both `true` in
  lua alongside `allowAnyLotFallback=false`. Legacy `craftBatchQuantity` still
  applies when produce is off; `recipes.<profile>.inputUnitsPerCraft` applies
  when on.
- Dashboard `finishedGoods` section ships in build 1 (lots + effective
  recipes); `hiveCrafters` gains `familyMatching`, `produceFinishedGoods`,
  `allowAnyLotFallback`, `unitsProduced`, `lastMatchedTier`,
  `lastMatchedQuery`, `lastGoodKey`.
- Note: with fallback off and no P.5.4d rotation yet, a top-pressure profile
  lacking type-correct stock makes the crafter SKIP that tick
  (`reserved=false reason=noMatchingLot`) — correct economics (miners chasing
  the same demand replenish it); rotation arrives in build 3.

Owner decision (2026-07-02): build the crafted-goods ledger and close the demand
loop FIRST; embody the crafter as a physical NPC LATER. Embodiment is cosmetic +
spawn-lifecycle risk (the P.4.4 vehicle lesson); the ledger is economy substance
with zero object-lifecycle exposure.

## 0. What P.5.4 delivers

Four sub-phases, each config-gated and independently verifiable:

| Sub-phase | Delivers | Fixes gap |
|---|---|---|
| **P.5.4a** | Type-correct reservation (family/class-chain matching) | Chef drawing metal (`fallbackUsed=true` persistently) |
| **P.5.4b** | Persistent finished-goods ledger (consume raw → produce a finished-good lot) | Consumption vanishes into a counter; nothing downstream can draw goods |
| **P.5.4c** | Demand supply reads exact hive lots (`supplyMode="exact_lots"`) | Shortages never move; economy can't settle |
| **P.5.4d** | Multi-profile servicing (staleness-aged pressure selection + output-stock governor) | Only `chef_buff_foods` ever crafts |

P.5.4a and P.5.4b **land together**: an output ledger without type-correct
inputs records nonsense (buff food made from Feveate steel).

## 1. Live evidence of the gaps (dashboard snapshot, 2026-07-02)

- `hiveCrafters`: `batchesCompleted=71 unitsConsumed=1775`,
  `producedByProfile=[chef_buff_foods:1775]` — **one profile only**, and
  `fallbackUsed=true` (chef's fruit lots are shallow, so nearly every batch
  draws whatever high-OQ lot is deepest — currently metal).
- `demand.supplyMode="conceptual_totals"`; every profile's
  `persistentStockpileSupply` comes from the four **frozen** `conceptual_label`
  lots (entryIds 1–4, 119,527 units, unchanged across every restart) and
  `aiConceptualSupply=0` resets each boot. The **~415k units of live
  `exact_type` stock (18 lots)** — the stock actually deposited and consumed —
  is invisible to demand. Pressure literally cannot respond to anything the
  miners or crafter do.
- `hiveReservations.consumed=71` proves the draw-down works; it just has no
  economic effect and no output.

## 2. Architecture: one warehouse, three lot tiers

The hive already distinguishes lot tiers by `identityConfidence`
(`conceptual_label` vs `exact_type`). P.5.4b adds a third tier —
**`finished_good`** — reusing `AiEconomyStockpileLot` unchanged:

```
miners ──deposit──▶ exact_type lots ──reserve/consume──▶ crafter recipes
                        ▲                                     │
                        │ supply signal (P.5.4c)              ▼ produce
demand pressure ◀───────┴──────────────  finished_good lots (P.5.4b)
      │                                        │
      ▼                                        ▼ (future P.5.5+)
miner targeting                    buffers / PvE / market listing
```

**Why reuse the lot class instead of a new `AiEconomyFinishedGoodLot` IDL:**
- **Zero schema risk.** No new field on `AiEconomyData`, no idlc regen, no
  load-path change, no migration of the live `aieconomy`/`aieconomylots` DBs.
  A new IDL class would need a new `Vector<>` member on the root object —
  Core3's name-based serialization tolerates added members, but it's risk we
  don't need to take for fields the lot class already has.
- **The reservation API comes for free.** Downstream consumers (buffer NPCs,
  PvE, market listing) will reserve/consume finished goods exactly like the
  crafter reserves raw — same tokens, same mutex, same durability fix.
- **Dashboard/persistence already render lots.** `stockpileInspection` and the
  dirty-flag durability path (`updatePersistentObject`) apply unchanged.
- Existing filters keep tiers isolated: `reserveFromStockpile` only matches
  `identityConfidence == "exact_type"`, so finished goods can never be
  double-drawn as raw inputs, and the conceptual-rollup upsert can't collide.

If finished goods later need richer typed fields (crafted serials, experimental
stats), promote to a dedicated IDL then — the entryId keying migrates cleanly.

### Field mapping for `finished_good` lots

| Lot field | Finished-good meaning |
|---|---|
| `entryId` | from the same `nextStockpileEntryId` counter (stable, monotonic) |
| `identityConfidence` | `"finished_good"` |
| `acquisitionSource` | `"hive_crafter"` |
| `resourceType` | **goodKey** (recipe output id, e.g. `buff_food_standard`) |
| `conceptualLabel` | human-readable good name (e.g. `Buff Food (standard)`) |
| `resourceClassChain` | good category chain, e.g. `crafted.food` / `crafted.weapon` — lets future consumers family-match goods the same way |
| `matchedDemandProfiles` | producing `profileKey` (e.g. `chef_buff_foods`) |
| `qualityTier` | input match tier: `exact` / `premium` / `bulk` |
| `oq` | weighted quality score of the input lot (profile stat weights, 0–1000) |
| `quantity` / `reservedQuantity` | on-hand / reserved goods (reservation-ready) |
| `resourceSpawnObjectId` | 0 (not spawn-backed) |
| `sourcePlanet`/`sourceZone` | empty (galaxy scope) |

**Upsert key:** `(identityConfidence="finished_good", resourceType=goodKey)` —
one lot per good, quantities accumulate. Quality `oq` updates as a
quantity-weighted running average on deposit (cheap, no per-craft lot spam).

## 3. P.5.4a — type-correct reservation

### The matcher
`reserveFromStockpile` currently requires exact `resourceType` string equality.
Add class-chain matching in `AiEconomyManager` mirroring the demand engine's
`resourceTypeMatches` (SimPlayerManager.cpp:967) semantics:

```cpp
// lot matches query if type == query, type begins with query,
// or classChain contains query
static bool lotMatchesResourceQuery(AiEconomyStockpileLot* lot, const String& query);
```

New overload (keep the existing method signature untouched for compatibility):

```cpp
bool reserveFromStockpileMatching(
    const Vector<String>& orderedQueries,  // try tier by tier
    int minOq, uint64 quantity,
    uint64& outToken, uint64& outEntryID,
    String& outMatchedQuery, int& outMatchedTierIndex,
    String& failureReason);
```

Selection within a tier keeps the proven rule: highest OQ, tie-break deepest
stack, `exact_type` only, lifecycle != `despawned`, available >= quantity.
First tier that yields a lot wins; later tiers aren't tried. Same
`persistenceMutationMutex` + lot `Locker` choreography as today.

### The candidate list
The crafter builds `orderedQueries` from the **selected profile definition**
(not just `activeResource.type`, which only names the best currently-ACTIVE
spawn — hive stock is often from older spawns):

1. `activeResource.type` (demand's ideal, when present)
2. each of `profile.exactTypes`
3. each of `profile.premiumFamilies`
4. each of `profile.bulkFamilies`

Tier index → `matchedTier` = `exact`(1–2)/`premium`(3)/`bulk`(4), recorded on
the output lot as `qualityTier`.

**`allowAnyLotFallback` flips to `false` in lua.** The any-lot fallback was
P.5.3 scaffolding to exercise the consume path; with family matching a chef
that can't find any food correctly **skips** (and P.5.4d rotates to another
profile). This is the type-correctness guarantee.

## 4. P.5.4b — finished-goods production

### Recipes (first cut: single-input, per-profile)
Static table in SimPlayerManager.cpp beside `createDemandProfileDefinitions()`
(same pattern), lua-overridable per profile:

```lua
hiveCrafterConsumerConfig = {
    ...
    recipes = {
        chef_buff_foods = { goodKey = "buff_food", goodName = "Buff Food",
            goodClassChain = "crafted.food", inputUnitsPerCraft = 25,
            outputUnitsPerCraft = 1, finishedGoodTargetUnits = 200 },
        composite_armor_supply = { goodKey = "composite_armor_segment", ... },
        master_weaponsmith_staples = { goodKey = "weapon_staple_stock", ... },
        high_damage_weapon_components = { goodKey = "high_damage_weapon", ... },
        chef_high_value_consumables = { goodKey = "high_value_consumable", ... },
        production_infrastructure = { goodKey = "factory_component", ... },
    },
}
```

`inputUnitsPerCraft` replaces the flat `craftBatchQuantity` per profile
(global stays as the default). Multi-input recipes (armor = metal + hide) are
an explicit **follow-up** — the reservation API already supports it (N
reservations, consume all-or-release-all), but P.5.4 keeps accounting simple.

### The craft tick (extends `runHiveCrafterConsumerTask`)
1. Select profile (P.5.4d rules, §6).
2. Build candidate list → `reserveFromStockpileMatching(...)`.
3. `consumeReservation(token)` — unchanged, release-on-failure unchanged.
4. **NEW — produce:** compute input quality score (profile
   `preferredStats`/`statWeights` against the consumed lot's 10 stats, reusing
   the `finishScore` weighting), then
   `AiEconomyManager::depositFinishedGood(goodKey, goodName, classChain,
   profileKey, tier, qualityScore, outputUnits, failureReason)` — upsert the
   `finished_good` lot: `addSpawnLotQuantity(outputUnits, "crafted", true)` +
   quality running-average + `updatePersistentObject(lot)` (the P.5.2
   durability idiom). Consume and produce run under the same
   `persistenceMutationMutex` acquisition — a crash between them can at worst
   lose one batch's output (acceptable; never double-produces).
5. Counters gain per-profile `lastServicedTimestamp`, `lastMatchedTier`,
   `lastMatchedQuery`, `skipReason`.

Config gate: `hiveCrafterProduceFinishedGoods` (C++ default **false**, lua
true on rollout). Off = exact P.5.3 behavior.

## 5. P.5.4c — close the demand loop (`supplyMode="exact_lots"`)

### Supply from the real ledger
New read-only snapshot on `AiEconomyManager` (same shape as
`snapshotPersistentConceptualMinerSupplyForDemand`):

```cpp
struct AiEconomyExactLotSupplyRow {
    String resourceType; String resourceClassChain;
    uint64 availableQuantity; int oq; String lifecycleState;
};
bool snapshotExactLotSupplyForDemand(Vector<AiEconomyExactLotSupplyRow>& rows, String& status);
```

In `computeDemandStateResults`, when
`demandStateSimulationConfig.supplyMode == "exact_lots"`:
- Per profile, match each row with the **same eligibility matcher** demand
  already uses: build a minimal `ResourceIntelligenceEntry` (type + classChain
  are all `resourceTypeMatches` reads) and run
  `getExactDemandTypeMatch`/`getBestDemandFamilyMatch`. Matched rows' available
  quantities sum into `result.persistentStockpileSupply`;
  `persistentStockpileConfidence = "exact_lot"`.
- **Drop the double-counts:** in this mode `aiConceptualSupply` is forced 0 and
  the conceptual-lot baseline is not added — the runtime session totals and the
  frozen lots 1–4 record the *same historical yield* the exact lots now carry
  authoritatively. `totalKnownSupply = market + exact-lot supply`.
- `supplyMode="conceptual_totals"` remains untouched — **instant lua rollback**.

### Why this settles the economy
- Miner deposits now **raise** the supply a profile sees; crafter consumption
  **lowers** it → `reserveRatio`, `shortageUnits`, `state`, `pressureScore` all
  move with real activity for the first time.
- Rising pressure → miners re-target that profile's resources (existing
  demand-weighted targeting) AND the crafter prioritizes it (existing
  pressure selection) → supply recovers → pressure falls → both back off.
  Negative feedback on both sides of the ledger.
- The **output governor** (§6) caps the crafter when finished stock meets its
  target, so crafting can't strip raw stock indefinitely. Equilibrium =
  raw reserves near `desiredReserve`, finished goods near
  `finishedGoodTargetUnits`, miners drifting to whatever is short.
- A profile whose raw supply hits `desiredReserve` reads `state="target"` /
  `surplus` — dampened pressure — exactly the saturation signal that
  previously never fired truthfully.

One lot can legitimately count toward several profiles (steel matches armor,
weapons, and infrastructure) — same units can't be *consumed* twice
(reservations are per-lot), and per-profile supply meaning "what this
profession could draw" is the correct semantic, matching how the conceptual
estimator already behaves.

## 6. P.5.4d — multi-profile servicing

Replace "always the single highest pressure" with **staleness-aged pressure**
(keeps the owner's proportional/demand-weighted philosophy from P.4.5b, adds a
fairness floor so all six professions craft):

```
effectiveScore(profile) = pressureScore
                        + minutesSinceLastServiced * stalenessBonusPerMinute
```

Each tick, service the highest `effectiveScore` profile that passes ALL gates:
- has an active opportunity (unchanged),
- shortage-first pass preserved (`preferShortageProfiles`),
- **output governor:** finished-good lot `quantity < finishedGoodTargetUnits`
  (else `skipReason="finishedGoodTargetMet"`),
- **type-correct stock exists** (the P.5.4a reservation itself is the check —
  a `noEligibleLot` marks the profile serviced-with-skip so the rotation moves
  on rather than hammering it).

Config: `stalenessBonusPerMinute` (default 20 — a profile unserviced for ~30
min outbids today's ~600-point pressure spread), `maxProfilesPerTick`
(default 1, conservative). Deterministic, explainable from the dashboard, no
RNG.

## 7. Dashboard & web widget

**JSON (`getAiEconomyDashboardSnapshot`):**
- `hiveCrafters` gains `perProfile[]`: `profile, craftedUnits, unitsConsumed,
  lastServicedAgeSeconds, lastMatchedTier, lastMatchedQuery, lastSkipReason,
  finishedGoodStock, finishedGoodTarget`.
- New `finishedGoods` section: per `finished_good` lot — `goodKey, goodName,
  producingProfile, quantity, reservedQuantity, qualityScore, qualityTier,
  lastCraftedAgeSeconds`; totals + `produceEnabled`.
- `demand` gains `supplyMode` surfaced per profile (already global) +
  `persistentStockpileConfidence` per row (`exact_lot` proves 4c live) and
  `supplyDelta` (supply change since boot — the "it moves now" signal).
- `stockpileInspection` lot rows: add the 10 stats + `qualityTier` (existing
  §7 follow-up; needed anyway so quality provenance is auditable).

**Web widget (`bin/web/aieconomy-dashboard/app.js` + `index.html`):** new
**“Economy Loop”** card, one row per profession, human-readable:

> **Chef (buff foods)** — raw food **29.4k / 100k** `LOW` → crafting **Buff
> Food** from `fruit_fruits_naboo` (premium, OQ 560) → **1,775 crafted**,
> goods **142 / 200** — *shortage easing*

i.e. per profile: raw supply vs desiredReserve with the demand state chip,
last input (matched tier + OQ), finished-good stock vs target, and a plain
trend word derived from `supplyDelta`/pressure movement. This is the single
at-a-glance "is the loop closed and settling" view the raw JSON can't give.

## 8. Safety & rollback

- **Still simulation-only.** Every write stays inside the private
  `aieconomy`/`aieconomylots` ObjectDBs. No `ResourceContainer`, player
  inventory, market/bazaar, credits, or real crafting-system objects. No NPC,
  zone, or object-lifecycle involvement at all (embodiment deliberately
  deferred). Fully reversible: drop the two DBs.
- **Config-gated, C++ defaults off:** `hiveCrafterProduceFinishedGoods=false`,
  `supplyMode` defaults to `conceptual_totals`, `stalenessBonusPerMinute=0`
  (=P.5.3 behavior), recipes default to the current 25:1 flat batch. Lua turns
  each on; each is revertible independently at runtime (config refresh).
- **Locking:** no new mutexes, no new ordering. All ledger mutation stays under
  `persistenceMutationMutex` + per-lot `Locker` (the proven P.5.1–P.5.3
  choreography); crafter counters stay under `hiveCrafterConsumerMutex`; the
  demand snapshot is the same copy-out-under-data-Locker read pattern as
  `snapshotPersistentConceptualMinerSupplyForDemand`.
- **Durability:** finished-good deposits use the same
  `updatePersistentObject(lot)` dirty-flag idiom the P.5.2 fix proved out.
- **No schema change:** `AiEconomyData` and `AiEconomyStockpileLot` untouched
  except (optionally) nothing — the third tier is data, not schema.
  `CURRENT_SCHEMA_VERSION` stays 1. `validateEconomyData` gains awareness of
  `finished_good` lots (count them separately, don't reject).
- **Known-good rollback line:** disable produce + set supplyMode back →
  byte-for-byte P.5.3 behavior; finished-good lots persist inertly (harmless).

## 9. Implementation & verification order

Three build/restart cycles, each verified on the live dashboard before the next:

1. **Build 1 — P.5.4a + P.5.4b (type-correct + ledger).**
   Verify: `hiveCrafters.fallbackUsed` stays `false`; log
   `HiveCrafterConsumer ... matchedTier=premium matchedQuery=fruit` food-only
   for chef; `finishedGoods` lots appear, quantities climb at
   `outputUnitsPerCraft` per batch, survive restart; raw exact lots keep
   decrementing; `noEligibleLot` skips (not metal draws) when food runs dry.
2. **Build 2 — P.5.4c (exact-lot supply).**
   Verify: per-profile `persistentStockpileSupply` ≈ matching exact-lot
   availability (~415k total today, not 119,527 frozen); values **move within
   one session** (up on deposit flush every 300s, down on each craft);
   `state`/`pressureScore` respond; miner targeting shifts toward the profile
   the crafter is draining. Watch 2+ hours: no pressure oscillation/thrash
   (surplusDampening should suffice; else tune thresholds).
3. **Build 3 — P.5.4d + dashboard widget.**
   Verify: `producedByProfile` populates **all six** profiles within ~1 hour;
   staleness rotation visible in `perProfile.lastServicedAgeSeconds`; profiles
   at `finishedGoodTargetUnits` show `skipReason=finishedGoodTargetMet` and
   crafting pauses; Economy Loop card renders sane human-readable rows.
   Long-run (overnight): raw reserves and finished stocks approach their
   targets and hold — **the settle test.**

## 10. Deferred (explicitly out of P.5.4 scope)
- Physical crafter-NPC embodiment at crafting stations (cosmetic; after the
  loop is proven).
- Multi-input recipes (metal + hide armor) and real schematic fidelity.
- Finished-good **consumers**: buffer NPCs drawing buff food, PvE NPCs drawing
  weapons/armor — the P.5.5 candidates; the reservation API on
  `finished_good` lots is already the interface they'll use.
- Market listing of raw/finished goods for players (a REAL-economy mutation —
  requires explicit owner approval of an economy phase).
- Retiring the conceptual rollup + frozen lots 1–4 once `exact_lots` is the
  proven supply mode (then delete `conceptual_totals` path).
