# Tutorial 0.9.12 — Who waits decides the order: buying across two stores

F_0.9.9 taught the bot to *sell*. Its rule was "paid is a receipt, not a state". F_0.9.10 teaches
the bot to *buy* and reuses that rule. It also flips one decision, because a different party is
waiting. This tutorial follows that decision and the three ideas built on it.

## 1. Order the writes by who is waiting

A purchase changes two stores that never commit together:

- **MySQL** holds the bot's bank balance and its ledger.
- **The object store (BerkeleyDB, saved every 5 minutes)** holds the item, the listing and the
  seller's credits.

In SELL, a human buyer was waiting. The owner refused to make a player wait for the bot's
bookkeeping, so the object store moved first (the buyer has the item at once) and the bot carried
the residual crash window. In BUY, nobody human is waiting on the buyer, so the order flips: **the
debit becomes durable first, and only then does the object store move.**

`buyPlayerBotItemsAtTerminal` (`SimPlayerManager.cpp:12553`) only debits: one `bazaar_buy` ledger
row with `ref_id` = auction and `detail` = `obj=<item OID>`. The maintenance lane executes later, and
only once that row is at or below the identity's applied mark (`:13188`,
`isPlayerBotLedgerAppliedLocked`). The bot is already back on its job; the purchase completes
seconds later in `executePlayerBotBazaarPurchases` (`:13141`).

The payoff is a short crash table:

| Crash after | Reboot sees | Outcome |
|---|---|---|
| the debit only | the listing still for sale | refund |
| execution, before the 5-minute save | the listing back, the item with its seller, the seller unpaid | refund; consistent, because nothing happened |
| execution and the save | the item in the bag | commit |

The state "the bot holds the item without having paid" cannot exist.

**Lesson:** when two stores cannot commit together, write first to the store whose loss is cheap
to repair, and choose it by asking who would be harmed by a wait.

## 2. Evidence, not state, again, and evidence must survive time

Boot recovery decides each open purchase in `decidePurchaseReconcile` (`SimBotBazaar.h:338`). The
first draft asked one question: *is the item in the bag?* Codex found the flaw. A bot that bought an
item and later legitimately sold it would be refunded at the next boot.

The shipped decision uses evidence that cannot be undone:

1. **The auction OID is still listed.** Auction OIDs are never reused, so this proves the object
   store reverted the buy: refund.
2. **A `bazaar_buy` item receipt exists.** It is written at commit, so the purchase committed,
   whatever happened to the item afterwards.
3. **The object is in the bag.** Delivered, but the MySQL commit was lost: commit now.
4. **Otherwise:** refund.

Evidence also has to outlive housekeeping. The ledger prune (`:11297`) used to delete rows by age,
and could keep a debit while deleting its item receipt or its refund. Such a debit reads as an open
purchase, which means a second refund or a wrong one. The prune now never removes a receipt while
its debit exists. It does this through a derived table, which MariaDB materialises, rather than a
correlated subquery on the table being deleted from.

The item reconcile had a related trap. It adopted any unknown object in a bag under a generic
source, before the purchase reconcile ran, so the purchase never got its receipt. It now leaves such
an object to the purchase reconcile (`:16690`).

**Lesson:** a receipt only proves something if no other process can remove it or write it under a
different name.

## 3. One row per object: a trade is a handoff

`simbot_items` is keyed by object id. In a bot-to-bot trade, the seller's `listed` row and the
buyer's new `held` row are the same row. The F_0.9.9 sale settlement deletes "the seller's row" on
sale, so the buyer's item vanished.

`commitPlayerBotBazaarPurchaseLocked` (`:12972`) therefore converts the row in place:

- the identity becomes the buyer, and the state becomes `held`;
- the held counts move from seller to buyer.

The settlement drops a row only while the seller still owns it (`settlementDropsItemRow`,
`SimBotBazaar.h:127`). Either order of the two operations ends with one buyer row and one receipt on
each side.

## 4. What live verification taught

The harness passed scenario after scenario while four product bugs waited for real data:

- **A bot that had never looted could never buy.** It had no vault bag, and the buy path asked for
  an existing one. Any acquisition path must create the bag, as loot does
  (`getOrCreateItemBag`).
- **Bot sellers were never recognised.** `getItemBagMapSnapshot` is keyed identity -> bag, and the
  code looked the owner OID up as a key (`:12911` now uses `playerBotIdentityForBag`).
- **"Seeder" was decided by a display name.** It is now "not in the player-name map".
- **A listing someone else bought reported the wrong refusal.** `buySimBotItem`
  (`AuctionManagerImplementation.cpp:264`) resolved the new owner as a seller before checking the
  status, so the refusal said "seller unresolved" instead of "gone".

Real bots then bought each other's knife, staff and rifles on two boots, and the owner sold a Vibro
Knuckler to a bot from the game client. The owner's bank rose by exactly 400, and the bot's debit
and item receipt landed in the same second.

**Lesson:** counters and labels are part of the product. Three of the four bugs left payments
correct while making the dashboard lie, and the dashboard is how this project sees the economy.
