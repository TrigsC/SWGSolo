# Code Review: F_0.9.10 Bazaar BUY (P.10k)

**Review Date**: 2026-10-16  
**Version**: 0.9.12  
**Files Reviewed**:

- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/bin/web/aieconomy-dashboard/app.js`
- `MMOCoreORB/src/server/zone/managers/auction/AuctionManager.idl`
- `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerBazaar.h` (new)
- `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotBazaar.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimHunterController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/tests/SimBotBazaarTest.cpp`
- `docs/1-plans/ROADMAP_p10-playerbot-parity.md`

**Plan**: `docs/1-plans/F_0.9.10_p10k-bazaar-buy.plan.md`

---

## Executive Summary

This change adds default-off PlayerBot bazaar purchasing with bounded candidate scanning, durable debit and delivery receipts, seller payment, crash reconciliation, dashboard visibility, and live diagnostics. All critical, major, and minor findings raised during the review iterations were addressed, and the supplied build, automated-test, and live-verification evidence meets the approval gate.

APPROVED

---

## Changes Overview

PlayerBots can now buy needed items from human, bot, or seeded bazaar listings while respecting configured price ceilings, reserve credits, quantity needs, and visit limits. Purchases use a debit-first maintenance flow with durable ledger evidence, direct vault delivery, idempotent refund or commit recovery, and bot-to-bot ownership handoff. The change also hardens stock instant-buy races, exposes purchase state through the dashboard, adds gated scan diagnostics, and expands standalone, in-binary, and live harness coverage.

---

## Findings

### Critical Issues

#### Purchase recovery trusted a nondurable held-row projection

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimBotBazaar.h:333`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11346`

The original recovery path could treat a MySQL item row as proof that the object-store half of a purchase committed, risking lost credits after object-store rollback.

**Disposition: Addressed.** Recovery now prioritizes the still-listed auction as rollback evidence, a durable `bazaar_buy` item receipt as commit evidence, and actual bag containment only when no receipt exists (`SimBotBazaar.h:333-347`, `SimPlayerManager.cpp:11346-11375`).

#### Crash reconciliation could adopt an item without durable purchase evidence

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12984`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:16690`

Generic item reconciliation could adopt a rowless delivered purchase before purchase reconciliation, while purchase commit could suppress the receipt when a row already existed. A later sale could consequently appear undelivered and be refunded.

**Disposition: Addressed.** Generic reconciliation defers matching `RESOLVE_PENDING` purchases without receipts, and purchase commit writes the receipt whenever `itemReceiptSum <= 0`, including for pre-existing rows (`SimPlayerManager.cpp:12984-13034`, `SimPlayerManager.cpp:16690-16703`).

#### Ledger pruning could retain a debit while deleting its proof or refund

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:11279`

Retention could preserve an unresolved purchase debit while pruning its item receipt or refund, causing a committed or refunded purchase to be reconstructed as open.

**Disposition: Addressed.** A materialized `buydebit` derived join preserves `bazaar_buy_refund` rows and `bazaar_buy` item receipts for as long as their debit exists (`SimPlayerManager.cpp:11279-11313`).

### Major Issues

#### Purchase debits did not mark progression dirty

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12888`

The in-memory bank debit and ledger append did not mark the progression record dirty, allowing the persisted balance and applied ledger state to diverge.

**Disposition: Addressed.** The debit path calls `markProgressionDirtyLocked` immediately after appending the debit (`SimPlayerManager.cpp:12888-12897`).

#### Stock instant-buy recheck rejected valid `OFFERED` listings

**Location**: `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:1399`

The new locked race guard initially accepted only `FORSALE`, which would break the existing `OFFERED` instant-buy path.

**Disposition: Addressed.** The locked guard accepts both `FORSALE` and `OFFERED`, while rejecting retired listings before charging the player (`AuctionManagerImplementation.cpp:1399-1409`).

#### Cached mismatches consumed the per-need object-load budget

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12708`

Counting every scanned listing against `candidateScanLimit` allowed cached mismatches to prevent a need from reaching later valid listings indefinitely.

**Disposition: Addressed.** Per-need and global budgets now count actual cache-miss object loads; cache hits remain free (`SimPlayerManager.cpp:12708-12750`).

#### Prior purchases and premature need decrement could starve valid listings

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12616`, `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12918`

A refunded or otherwise previously attempted auction could continue winning selection, and decrementing need before a successful debit could prevent later candidates from being considered.

**Disposition: Addressed.** Auctions already represented by a purchase for the identity are excluded during the raw scan, and remaining need is decremented only after the debit and purchase record succeed (`SimPlayerManager.cpp:12616-12625`, `SimPlayerManager.cpp:12669-12676`, `SimPlayerManager.cpp:12918-12920`).

#### Never-looted bots could not buy because they lacked a vault bag

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12578`

Live verification showed that `getItemBag` returned null for bots that had never acquired loot, preventing their first bazaar purchase.

**Disposition: Addressed.** The purchase path now uses the established `getOrCreateItemBag` acquisition flow (`SimPlayerManager.cpp:12578-12583`).

### Minor Issues

#### Global object-load limit was not checked immediately before every load

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12728`

The outer-loop-only check allowed multiple needs in the same round to exceed `maxObjectLoadsPerVisit`.

**Disposition: Addressed.** Every cache miss rechecks the global limit immediately before loading the object (`SimPlayerManager.cpp:12728-12736`).

#### Seller-kind attribution used the wrong bag-map key

**Location**: `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp:12907`

The bag snapshot is keyed by identity, so testing `contains(ownerID)` misclassified bot sellers. Name matching also made seeder attribution dependent on a particular display name.

**Disposition: Addressed.** Bot sellers are resolved by searching bag-map values, humans through the player-name map, and remaining system sellers as seeders (`SimPlayerManager.cpp:10928-10935`, `SimPlayerManager.cpp:12907-12915`).

#### Already-bought listings could report `SELLER_UNRESOLVED` instead of `GONE`

**Location**: `MMOCoreORB/src/server/zone/managers/auction/AuctionManagerImplementation.cpp:274`

`buySimBotItem` resolved the listing’s new owner before checking its retired status, producing the wrong refusal classification after another buyer won.

**Disposition: Addressed.** An unlocked `FORSALE` precheck now classifies retired listings as `GONE`, while the existing locked status, expiry, object, and price checks remain authoritative (`AuctionManagerImplementation.cpp:274-289`, `AuctionManagerImplementation.cpp:315-326`).

### Suggestions

None.

---

## Checklist

- [x] 1. Functional Requirements — Passed; purchase, payment, refusal, recovery, and no-bag acquisition flows match the plan.
- [x] 2. Code Quality — Passed; shared helpers, bounded state, descriptive naming, and comments cover the complex recovery rules.
- [x] 3. Architectural Compliance — Passed; selection remains in terminal visits and execution/reconciliation remains in the maintenance lane.
- [x] 4. Distributed Object / IDL Discipline — Passed; the native interface is declared in IDL, no generated source was edited, and managed references and established lock ordering are used.
- [x] 5. Lua/C++ Boundary — Passed; gates, limits, reserve credits, and training-plan needs remain Lua-configured.
- [x] 6. AI-Economy / Simulation Safety — Passed; the mutating phase is plan-approved, default-off, and live-verified.
- [x] 7. Error Handling — Passed; purchase refusals are classified, counted, logged, and durably refunded.
- [x] 8. Security — Passed; no authentication surface was weakened and native purchase inputs and seller types are validated before mutation.
- [x] 9. Performance — Passed; candidate work is bounded by per-need and per-visit load limits, with a capped template cache.

---

## Verdict

**APPROVED**

All review findings are closed. Reported verification is lint-clean, warning-clean under `-Werror`, 147/147 for the in-binary SimBot suite, and 40/40 for the standalone suites. The live matrix completed 25/26 scenarios; the remaining production-visit assertion was invalidated because real bots had already visited and bought before the harness baseline, with successful purchases confirmed by ledger rows and `BAZAAR_VISIT outcome=bought`. Marked live-profile working-copy edits and the dirty `engine3` submodule were treated as declared out-of-commit scope.

**Release resolution (2026-10-16).** The owner then ran the one check the live harness could not: a human-seller sale in the game client. A Vibro Knuckler listed by the owner's character for 400 on the Mos Eisley bazaar was bought by roster bot 274; the owner's bank rose by exactly 400 (no city tax in Mos Eisley), the bot's applied debit (ledger 1209) and item receipt (1210) landed in the same second, and the knuckler is `held` in the bot's vault with `source=bazaar_buy`. The harness production-visit scenario's baseline race is recorded as harness debt.
