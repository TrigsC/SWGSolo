# Code Review: F_0.9.9 Bazaar SELL (P.10j)

**Review Date**: 2026-10-15  
**Version**: 0.9.11  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/sql/swgemu.sql`
- `MMOCoreORB/src/server/db/ServerDatabase.cpp`
- `MMOCoreORB/src/server/zone/managers/aieconomy/AiEconomyManager.cpp`
- `MMOCoreORB/src/server/zone/managers/auction/AuctionManager.idl`
- `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/auction/MarketSeederBridge.cpp`
- `MMOCoreORB/src/server/zone/objects/auction/MarketSeederBridge.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotItemReconcile.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotItemReconcileTest.cpp`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`

**Plan**: `workspace/Core3/docs/1-plans/F_0.9.9_p10j-bazaar-sell.plan.md`

---

## Executive Summary

This change lets roster PlayerBots list vault loot on the real bazaar, transfer purchased items immediately to human buyers, settle proceeds durably, recover lost sale notifications, retrieve and relist expired items, and expose the lifecycle through diagnostics and verification tooling. All correctness findings were addressed or explicitly overridden by owner decisions; bounded retention and crash-window risks remain accepted observations.

**APPROVED with observations**

---

## Changes Overview

The implementation adds gated PlayerBot bazaar visits, stock `AuctionItem` publication, human-market-floor pricing, durable listing and ledger records, restart reconciliation, instant buyer pickup, expedited settlement flushes, expiry retrieval, and retention cleanup. It extends the auction manager, progression store, item reconciliation, Lua configuration, dashboard, schema, tests, and live harness. The final delta replaces the original receipt-gated pickup hold with owner-approved instant pickup and buyer-owned sale recovery.

---

## Findings

### Critical Issues

#### Sale ownership could be released before seller receipts were durable

- **Location**: `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:1240`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11734`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:29510`, `docs/1-plans/F_0.9.9_p10j-bazaar-sell.plan.md:198`
- **Description**: The original implementation could expose a sold item before seller receipts and applied marks were durable. It was first fixed with a retrieval hold, but live verification showed that buyers could wait approximately 60 seconds.
- **Disposition**: **Accepted with override.** The owner explicitly selected instant pickup. Ownership moves immediately; enqueueing raises an atomic flush request and kicks maintenance so settlement is force-flushed within seconds. The owner accepted the residual bot-only credit-loss window described at `F_0.9.9_p10j-bazaar-sell.plan.md:218`; players are neither blocked nor charged twice.

#### Item reconciliation could destroy a bag that still owned a market listing

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15264`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15275`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15369`
- **Description**: Reconciliation before market readiness could mistake listed objects for missing inventory and discard an apparently empty, unmapped owner bag.
- **Disposition**: **Addressed.** Reconciliation waits for the market, records every auction-owner bag, and excludes those bags from deletion.

#### Successful ledger inserts could be replayed after a later batch failure

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15893`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:15907`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16068`
- **Description**: A failure after a successful ledger insert could requeue and duplicate already-written credit rows.
- **Disposition**: **Addressed.** The loop tracks the successful prefix, requeues only rows whose inserts did not complete, and resolves applied high-water marks before updating balances.

### Major Issues

#### Live-only Lua settings differed from release-safe defaults

- **Location**: `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2529`
- **Description**: The working copy carries the owner’s enabled live-only gate while the release must default off.
- **Disposition**: **Accepted with override.** Project release staging preserves the committed default-off value and includes only the intended bazaar configuration and scenarios.

#### Vault-root adoption violated established lock and transfer choreography

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12778`
- **Description**: The original adoption path used a lock and transfer order inconsistent with the vault forge path.
- **Disposition**: **Addressed.** The code now locks object → bag → vault, removes the object from the bag, and transfers it to the vault root.

#### Schema migration 1016 was not restartable

- **Location**: `MMOCoreORB/src/server/db/ServerDatabase.cpp:264`, `MMOCoreORB/src/server/db/ServerDatabase.cpp:268`
- **Description**: Re-entering a partially completed migration could fail on columns already added.
- **Disposition**: **Addressed.** Added columns use idempotent `ADD COLUMN IF NOT EXISTS` migrations.

#### Bazaar tests were ignored and absent from the normal diff

- **Location**: `MMOCoreORB/.gitignore:25`, `MMOCoreORB/src/tests/SimBotBazaarTest.cpp:77`
- **Description**: The new bazaar test file is covered by the repository ignore rule and requires explicit release inclusion.
- **Disposition**: **Accepted with override.** The established release procedure force-adds ignored test sources. The last stopped-server run passed all 140 SimBot tests.

#### Rowless listing adoption could duplicate acquisition history

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12438`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12541`
- **Description**: Adoption could append another item-acquisition row when the original durable acquisition already existed.
- **Disposition**: **Addressed.** Reconciliation queries acquisition history before locking progression state, remains pending on query failure, and journals acquisition only when none exists.

#### Bot listings bypassed stock sellability and factory-crate behavior

- **Location**: `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:42`, `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:55`, `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:91`, `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:113`
- **Description**: The initial bot path did not share the player path’s unsellable-arrangement validation or factory-crate metadata.
- **Disposition**: **Addressed.** Shared helpers are used by both listing paths.

#### Load-time balance reconciliation did not schedule its applied-mark update

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11542`
- **Description**: Balance-mismatch ledger rows were added without dirtying their progression identities.
- **Disposition**: **Addressed.** Every affected identity is marked dirty so the scalar balance and applied-mark update runs.

#### Nullable listing timestamps prevented progression-store startup

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11372`
- **Description**: Open listings have NULL closure timestamps, which caused `getUnsignedLong` to throw and abort the store load.
- **Disposition**: **Addressed.** All timestamp columns are selected through `COALESCE(...,0)`.

#### Expired items could never be relisted

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11776`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12043`
- **Description**: Candidate filters rejected objects with retrieved or orphaned listing entries.
- **Disposition**: **Addressed.** Only pending and live listings block a candidate.

#### Relisting was initially reported to reuse the prior auction ID

- **Location**: `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:103`, `MMOCoreORB/src/server/zone/objects/auction/AuctionItem.idl:65`, `MMOCoreORB/src/server/zone/managers/object/ObjectManager.cpp:658`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11953`
- **Description**: The review initially concluded that relisting reused the archived row’s primary key.
- **Disposition**: **Overridden/withdrawn.** The constructor argument initializes `auctionedItemObjectID`, while persistence assigns every new `AuctionItem` a fresh database OID. Live MySQL evidence confirmed distinct auction IDs for each relist.

#### Recovered taxed sales could tax an already-gross price again

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17079`
- **Description**: Object-evidence recovery initially treated a settled SOLD row’s `price` as a pre-tax ask, potentially multiplying city tax twice and over-crediting the bot.
- **Disposition**: **Addressed.** SOLD rows recover gross from `netPaid + tax` at `SimPlayerManager.cpp:17082`; only non-settled/LISTED evidence rebuilds gross from the pre-tax price at `SimPlayerManager.cpp:17084`.

### Minor Issues

#### Closed listing, index, and receipt state lacked retention

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11117`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11146`
- **Description**: Closed rows and their in-memory indexes and receipt sums could grow indefinitely.
- **Disposition**: **Addressed.** The daily retention pass prunes persistent and in-memory closed entries while preserving live listings and unreleased legacy sales.

#### Rowless held-sale reconciliation was unreachable

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotBazaar.h:211`, `MMOCoreORB/src/tests/SimBotBazaarTest.cpp:77`
- **Description**: The generic settlement branch preceded `RECONCILE_ADOPT_SOLD`.
- **Disposition**: **Addressed.** Rowless held sales now select adoption before generic settlement.

#### `saleUnresolved` counted retries instead of distinct auctions

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12273`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12927`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17100`
- **Description**: Settlement, sweep, and release could increment the same diagnostic repeatedly.
- **Disposition**: **Addressed.** All paths use a mutex-protected once-per-auction helper.

#### Unresolved-auction deduplication state has no retention path

- **Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h:2562`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:17102`
- **Description**: Auction IDs inserted into `progressionBazaarUnresolvedAuctions` remain for the process lifetime.
- **Disposition**: **Accepted with override.** Entries occur only for held sales whose seller identity is unavailable, each entry is small, and normal identity deletion releases listings first. Pruning with listing retention remains accepted follow-up debt.

#### Documentation retained the removed pickup-hold guarantee

- **Location**: `docs/1-plans/ROADMAP_p10-playerbot-parity.md:1012`, `docs/1-plans/F_0.9.9_p10j-bazaar-sell.plan.md:430`, `docs/1-plans/F_0.9.9_p10j-bazaar-sell.plan.md:776`
- **Description**: The roadmap and plan initially claimed that dropped sale notifications remained recoverable because the `AuctionItem` stayed bag-owned, and the POD definition omitted its new release marker.
- **Disposition**: **Addressed.** The documents now describe instant pickup, buyer-owned recovery, the accepted crash window, and the `released` field.

### Suggestions

#### Final instant-pickup verification remains scheduled

- **Location**: `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2361`, `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua:2378`
- **Description**: The new `bazaar_instant_pickup` and `bazaar_lost_sale_recovered` scenarios have not yet been rerun in the stopped-server/live window.
- **Disposition**: **Accepted workflow.** The build is warning-clean, the affected pure-header suite is unchanged from its 140/140 run, and the requester explicitly assigned the delta’s execution to the next verification gate.

> **Release resolution (2026-10-16).** The instant-pickup re-verification ran and passed
> (`docs/4-unit-tests/live_f099-instant-pickup_20261015-213013-f099-instant-pickup.md`,
> 26/26).
> - Gtests passed 140/140 with the server stopped.
> - `bazaar_instant_pickup`, `bazaar_lost_sale_recovered` and the two-boot restart probe
>   all passed.
> - The owner then bought two bot listings in the game client and collected them
>   immediately. Each buyer debit matched its seller credit to the credit (171 and 351),
>   settled in the same second.

---

## Checklist

- [x] 1. Functional Requirements — Passed
- [x] 2. Code Quality — Passed
- [x] 3. Architectural Compliance — Passed
- [x] 4. Distributed Object / IDL Discipline — Passed
- [x] 5. Lua/C++ Boundary — Passed
- [x] 6. AI-Economy / Simulation Safety — Passed, with the owner-approved instant-pickup risk; live re-verification passed 26/26 (see the release resolution)
- [x] 7. Error Handling — Passed
- [x] 8. Security — Passed
- [ ] 9. Performance — Passed with accepted bounded debt for process-lifetime unresolved-auction deduplication state

---

## Verdict

**APPROVED with observations**

All correctness, durability, recovery, and documentation findings are resolved or explicitly overridden. Promotion must preserve the default-off partial-staging boundary and force-add ignored test sources; the instant-pickup live rerun and unresolved-auction retention cleanup remain documented follow-up work.

