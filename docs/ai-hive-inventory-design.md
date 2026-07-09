# P.5 AI Hive Inventory — design & status

Companion to `ai-miner-navigation-design.md`. Covers where the resources that AI
miners gather are *held*, and how future crafter NPCs will *consume* them.

**Next phase:** P.5.4 (crafted-goods output ledger, type-correct reservation,
demand-loop closure, multi-profile crafting) is designed in
`ai-crafter-output-ledger-design.md`.

## 0. Live status
- **P.5.1 (implemented + verified live):** exact resource-spawn-identity hive
  deposits into the persistent galaxy stockpile. `persistSpawnIdentifiedLots =
  true` in lua (C++ default off). Verified: 7 `exact_type` lots with real spawn
  ids/names/quantities, `persistenceReady=true`.
- **P.5.2 (implemented + verified live):** reserve/consume/release reservation
  API on the hive, deposits switched to increment-by-delta so producer yield and
  consumer draws compose, non-destructive reservation self-test. Verified:
  `hiveReservations.apiReady=true`, granted=released climbing together, consumed=0.
- **P.5.3 (implemented + verified live):** the first REAL hive consumer — a
  demand-driven crafter task that reserves AND **consumes** (draws stock down).
  Self-test retired (`hiveReservationSelfTestConfig.enabled=false`);
  `hiveCrafterConsumerConfig.enabled=true`. See §4b. Verified (server up ~1000s):
  `hiveCrafters.batchesCompleted=11 unitsConsumed=275`,
  `producedByProfile=[chef_buff_foods:275]`, `hiveReservations.consumed=11
  granted=11 released=0`. Exact-type demand→reservation coupling confirmed
  (chef_buff_foods → fruit_fruits_naboo → Ptohi lot, `fallback=false` until the
  small Ptohi lot drew below the batch size, then `fallback=true` as designed).
- **P.5.2 durability fix (implemented, compiled clean `-Werror`, pending
  restart+verify):** exact-lot quantity changes now survive restart. Core3's
  periodic save is dirty-gated (`_isUpdated()`, DOBObjectManager
  `runObjectsMarkedForUpdate`), but the hand-written IDL methods
  `addSpawnLotQuantity`/`consumeReservedQuantity` write fields directly without
  dirtying the object, so pre-fix only the creation-time value persisted and all
  deposit/consume growth was lost on restart (observed: Feveate 13729→~0). Fixed
  by calling `ObjectManager::updatePersistentObject(lot)` (= `_setUpdated(true)`)
  after the deposit and consume mutations in `AiEconomyManager.cpp`. See §5.

## 1. The decision: one galaxy hive, not per-NPC inventories
AI NPCs do **not** own individual inventories. All gathered resources land in a
single **galaxy-scoped hive stockpile** owned by `AiEconomyManager`
(`ownerScope = "galaxy"`). Miners are stateless **producers** that deposit;
crafters (future) are **consumers** that reserve and draw down.

Why (industry-standard warehouse/ledger pattern):
- **NPC identity is ephemeral.** Creature object IDs regenerate on respawn and
  server restart, and the miner population changes as NPCs are added. Binding
  stock to an NPC id is fragile. The hive keys each lot on a stable, monotonic
  `entryId` from `AiEconomyData.nextStockpileEntryId`, fully decoupled from any
  NPC.
- **Single source of truth.** One store that demand reads, crafters consume, and
  the dashboard renders. It is what makes "walk away for a week" possible — the
  economy survives on the hive, not on churning NPCs.
- **A log is not enough.** The acquisition *event ledger* (ring buffer) is the
  audit trail and stays. But crafting needs a **stateful balance with
  reservations** (how much is available *right now*, without two crafters
  double-spending the same units) — that is the hive lot, not a log.

## 2. Persistence layer (pre-existing, now populated)
`server/zone/managers/aieconomy/`:
- `AiEconomyData.idl` — root `ManagedObject`: `schemaVersion`,
  `nextStockpileEntryId` (stable id counter), timestamps, `Vector<AiEconomyStockpileLot>`.
- `AiEconomyStockpileLot.idl` — one hive lot:
  - `entryId` (stable), `resourceSpawnObjectId` (real in-game identity),
    `resourceSpawnName`, `resourceType`, `resourceClassChain`.
  - `sourcePlanet`/`sourceZone`, `acquisitionSource`, `resourceLifecycleState`,
    `ownerScope`, `identityConfidence`, `matchedDemandProfiles`, `qualityTier`.
  - `quantity` + `reservedQuantity` with `getAvailableQuantity()` (reservation
    primitive).
  - The 10 SWG resource stats: `oq cd dr hr fl ma pe sr ut cr`.
- `AiEconomyManager` — Singleton owner. Load-or-create on boot from isolated
  ObjectDatabases `aieconomy` / `aieconomylots`, rigorous `validateEconomyData`.

Two lot tiers share the store, distinguished by `identityConfidence`:
- **`conceptual_label`** — coarse one-lot-per-resource-type rollup
  (`updateConceptualMinerTotals`). Pre-existing; feeds demand. Still gated off
  (`persistConceptualMinerTotals = false`).
- **`exact_type`** (P.5.1) — crafting-grade, one lot per unique resource spawn,
  carrying the 10 stats (`updateStockpileSpawnLots`).

## 3. P.5.1 deposit path (implemented)
1. **Capture identity at target selection.** `MinerIntelligentTargetAssignment`
   gained `targetResourceSpawnObjectId`, `targetResourceClassChain`,
   `targetResourceActive`, and the 10 `targetResource{Oq..Cr}` fields. They are
   populated at assignment creation (`runMiner…` demand-weighted path,
   ~SimPlayerManager.cpp:16565) directly from the chosen
   `ResourceIntelligenceEntry` (which already carries objectID + classChain +
   stats). The struct round-trips by value through the assignment map, so cached
   reuse preserves identity. Paths that don't set a spawn id degrade gracefully
   to conceptual-only.
2. **Accumulate per spawn.** On sample yield, `recordSpawnIdentifiedMinerYield`
   adds the amount to a runtime `VectorMap<uint64 spawnId, MinerSpawnYieldAccumulator>`
   (guarded by `spawnYieldAccumulatorMutex`), caching identity + stats.
3. **Flush to hive.** The existing periodic persistence task (300s) calls
   `flushSpawnIdentifiedLotsToHive()` → `AiEconomyManager::updateStockpileSpawnLots`.
   Upsert keyed by `resourceSpawnObjectId`: existing lot →
   `addSpawnLotQuantity(delta)`, else create via `initializeSpawnLot` + `setResourceStats`.
4. **Increment-by-delta accounting (P.5.2).** Each flush adds only the NEW yield
   since the last flush: `delta = sessionQuantity − lastFlushedQuantity`;
   `lot.quantity += delta`; on success advance `lastFlushedQuantity`
   (`markSpawnYieldFlushed`). The persisted `quantity` is authoritative on-hand
   stock, so consumer draws (which decrement it) are never overwritten by the
   next deposit. On restart, accumulators reset to 0, so only the new run's yield
   is added on top of the persisted quantity — no double-count.
   (Superseded the P.5.1 set-absolute `startupBaseline + session` model, which
   would have erased consumption.)

New exact lots: `acquisitionSource="conceptual_miner"` (same producer),
`ownerScope="galaxy"`, `identityConfidence="exact_type"`,
`resourceLifecycleState="active"|"inactive"`. They do **not** match the
conceptual-rollup signature, so the two tiers never collide.

## 4. Reservation / consumption API (P.5.2 — implemented)
On `AiEconomyManager`, guarded by `persistenceMutationMutex` (same mutex as the
deposit flush, so deposits and reservations never interleave):
- `reserveFromStockpile(resourceType, minOq, qty, &token, &entryId, &reason)` —
  picks the highest-OQ eligible `exact_type` lot (tie-break deepest stack) with
  `availableQuantity >= qty` and `lifecycleState != "despawned"`; bumps
  `reservedQuantity`; returns a runtime token. Empty `resourceType` matches any
  exact lot. `availableQuantity = quantity − reservedQuantity`.
- `consumeReservation(token, &reason)` — `consumeReservedQuantity` on the lot
  (decrements both `quantity` and `reservedQuantity`); the actual craft draw.
- `releaseReservation(token, &reason)` — rolls back `reservedQuantity` on cancel.
- `getReservationStats(...)` — active count, reserved qty, granted/consumed/released
  counters (dashboard `hiveReservations`).

Reservations are **runtime-only** (`VectorMap<token, HiveReservation>`); the
lot's `reservedQuantity` is persisted but **reconciled to 0 on load**
(`reconcileReservationsOnLoad`) so a crash mid-reservation cannot leak reserved
stock. Still simulation-only: consumption decrements the **ledger** only; it
creates/destroys no real `ResourceContainer` and touches no player/market/credit
state.

**Self-test (P.5.2, non-destructive) — RETIRED in P.5.3:**
`HiveReservationSelfTestTask` (config `hiveReservationSelfTestConfig`, every 120s)
reserved a small qty from any eligible exact lot then **released** it (never
consumed), proving the reserve/release accounting without depleting gathered
stock. Now that the P.5.3 crafter drives real reservations, it is disabled
(`enabled=false`); code remains for future diagnostics.

## 4b. First crafter consumer (P.5.3 — implemented)
The first thing that actually **consumes** from the hive. A gated, conceptual
manager-side task (`HiveCrafterConsumerTask`, config `hiveCrafterConsumerConfig`,
default interval 90s) — no physical NPC yet (that is a later phase). Each tick:
1. `computeDemandStateResults(...)` — a helper **extracted from**
   `logDemandStateSimulations()` so the crafter selects targets from the same
   demand engine (single source of truth: `pressureScore`, `state`,
   `activeResource.type/oq`).
2. Pick the highest-`pressureScore` profile that has an active resource
   opportunity. `preferShortageProfiles`: first pass restricts to
   `state==critical|low`; if none qualify, second pass considers any profile with
   an opportunity.
3. `reserveFromStockpile(activeResource.type, minOq, craftBatchQuantity, ...)`. If
   `noEligibleLot` and `allowAnyLotFallback`, retry with empty type + minOq 0 so
   the consume path is still exercised against real stock (`fallbackUsed=true`).
4. On reserve success → `consumeReservation(token)` — the **real draw-down**
   (decrements the exact lot's `quantity`). On consume failure the reservation is
   **released** (never leaked).
5. Runtime counters (guarded by `hiveCrafterConsumerMutex`): `batchesCompleted`,
   `unitsConsumed`, per-profile `craftedUnitsProduced` (first-cut recipe ratio:
   1 crafted unit per unit consumed), `lastProfile`, `lastReserveReason`,
   `fallbackUsed`.

Config: `enabled`, `intervalSeconds`, `craftBatchQuantity` (25), `minOq` (0),
`preferShortageProfiles`, `allowAnyLotFallback` — all in
`bin/scripts/managers/sim_player_manager.lua`; C++ member defaults are off.
Dashboard: new `result["hiveCrafters"]` section; the `crafters` `futureRoles`
entry flips `not_implemented` → `simulation`; `hiveReservations.consumed` now
climbs (primary proof of real consumption).

**Accounting nuance (unchanged, follow-up):** demand pressure is computed from
`supplyMode="conceptual_totals"` (conceptual lots 1–4) while consumption draws the
`exact_type` lots (5+), so consuming does **not** yet relieve demand pressure.
P.5.3 proves the consume path + demand→reservation coupling; reconciling exact-lot
depletion into demand supply is a later phase.

## 5. Safety
- Persisting the hive writes **only** to the private `aieconomy` /
  `aieconomylots` databases. No player inventory, market/bazaar, real
  `ResourceSpawn`/`ResourceContainer`, or credits are mutated. Fully reversible
  (drop those two DBs). Categorically different from the still-disabled real
  resource acquisition (`enableRealResourceAcquisition=false`).
- Locking reuses the proven `persistenceMutationMutex` + per-managed-object
  `Locker` choreography; the deposit accumulator has its own manager-side mutex.
  No new cross-object lock ordering, no NPC-side locks.
- Toggle at runtime via `aiEconomyPersistenceConfig.persistSpawnIdentifiedLots`.
- **Durability fix (P.5.2 hardening):** the deposit (`addSpawnLotQuantity`) and
  consume (`consumeReservedQuantity`) paths in `AiEconomyManager.cpp` now call
  `ObjectManager::instance()->updatePersistentObject(lot)` after the lot `Locker`
  closes so the mutated on-hand quantity is flagged for Core3's dirty-gated
  periodic save. It only sets `_setUpdated(true)` — lock-free, no new lock
  ordering, writes stay in `aieconomylots`. Reserve/release are NOT persisted
  (reservedQuantity is `reconcileReservationsOnLoad`-zeroed on boot).

## 6. Verify after restart (dashboard)
`stockpileInspection` section (P.5.1):
- `spawnIdentifiedPersistEnabled` true; `persistenceReady` true.
- `lots[]` rows with `identityConfidence: "exact_type"`, non-zero
  `resourceSpawnObjectId`, real `sourceResourceName`, `conceptualMinerLot: false`,
  and now `resourceLifecycleState: "active"` (P.5.2 fixed the `inShift` flag;
  was wrongly "inactive").
- Log line `AiEconomyPersistenceSpawnLots updated=true spawns=… addedQuantity=…`.
- **Durability check (post P.5.2 fix):** note the exact-lot quantities, restart,
  and confirm they **resume** (persisted + new yield), NOT reset to ~0. Pre-fix
  this failed (e.g. Feveate 13729→~0). Also consume from a lot, restart twice,
  and confirm the draw-down persists.

`hiveReservations` section (P.5.2):
- `selfTestEnabled` now **false** (retired); `HiveReservationSelfTest` log lines
  should **stop**.
- `consumed` now **> 0 and climbing** (was pinned at 0) — the definitive
  "real consume" signal driven by the P.5.3 crafter; `activeReservations` ~0
  between ticks (reserve+consume in the same tick, none leaked).

`hiveCrafters` section (P.5.3):
- `enabled=true`; `batchesCompleted` and `unitsConsumed` climbing each interval;
  `producedByProfile[]` populating; `lastProfile` matching a high-pressure demand
  profile (e.g. `chef_buff_foods`, `high_damage_weapon_components`).
- Log line `HiveCrafterConsumer profile=… reserved=true entryId=… consumed=true
  units=…`.
- A targeted `exact_type` lot's `quantity` **decreases** over time;
  `reservedQuantity` returns to 0 between ticks.
- Cross-restart: consumed lot quantities **stay reduced** (persisted draw-down
  composes with new miner deposits via the P.5.2 increment-by-delta model — no
  double-count, no resurrection of consumed stock).

## 7. Open follow-ups
- Surface the 10 stats + `qualityTier` per lot on the dashboard (extend
  `AiEconomyStockpileInspectionLot` + the copy loop; currently identity is shown
  but stats are not).
- Consider marking a lot `resourceLifecycleState="despawned"` once its spawn
  leaves the resource map (stockpiled units remain usable for crafting).
- Decide whether to keep the conceptual rollup once exact lots are the primary
  demand-supply source (derive the coarse view from exact lots).
- ~~P.5.3: wire the first crafter NPC to `reserveFromStockpile` →
  `consumeReservation`, and turn the self-test off.~~ **DONE (see §4b).**
- ~~Family/class-chain reservation matching (chef ↔ any fruit).~~ → **P.5.4a**
  in `ai-crafter-output-ledger-design.md`.
- ~~Reconcile exact-lot depletion into demand supply so consumption relieves
  pressure.~~ → **P.5.4c** (`supplyMode="exact_lots"`), same doc.
- ~~Recipe ratios (N inputs → 1 output).~~ → **P.5.4b** (single-input recipes;
  multi-resource recipes stay deferred), same doc.
- ~~Persistent crafted-goods ledger for downstream consumers (buffers, PvE).~~
  → **P.5.4b** (`finished_good` lot tier), same doc.
- Physical crafter-NPC embodiment at crafting stations — **deferred past P.5.4**
  (owner decision 2026-07-02: economy substance first, embodiment later).
