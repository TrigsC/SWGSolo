# Tutorial 0.9.11 — Receipts, not states: selling on a shared market

F_0.9.9 puts roster PlayerBots on the real bazaar. Each bot sale moves value between two
parties: a player's credits leave in the object store (BerkeleyDB), and the bot's credits
arrive in MySQL. Those two stores save at different times, and MyISAM has no
transactions. This tutorial follows the three ideas that made such sales safe, and the
one product trade-off the owner made on top of them.

## 1. The seller must be something that never changes

Stock Core3 records a listing's seller as `ownerID` (an object id) and `ownerName`, and
several paths resolve the seller **by name**: `doInstantBuy` pays
`getPlayer(ownerName)`, and expiry mails go to `ownerName`. A bot breaks both:

- **Its body is rebuilt with a new OID on every death,** so a body-owned listing would be
  orphaned at the bot's first death.
- **Its name comes from the same `NameManager` that names players.** Resolving by name can
  pay or mail a *real character* who happens to share it.

The fix is to make the bot's **vault bag** the seller. The bag survives deaths and
restarts, and its OID can never equal a player's. `isSimBotListing`
(`AuctionManagerImplementation.cpp:69`) recognises it cheaply. A real owner is answered
from the player-name map with no object load; only an unknown owner is looked up and
checked with `AiEconomyManager::isItemBag`. Every guarded stock branch keys on that
predicate, never on a name. A buyer named like the bot still becomes the owner, because
the bot branch of `updateAuctionOwner` skips its name short-circuit.

**Lesson:** on a shared surface, find the identity the engine trusts (here an OID) and give
the simulated party one that cannot collide with a real one.

## 2. "Paid" is a receipt, not a state

A sale writes four MySQL rows with no transaction: the ledger row, the balance, the item
row and the listing row. Any subset can be on disk after a crash. If the listing row's
`state = sold` decided whether the bot was paid, both failures are possible: a crash
before the balance landed means it is never paid, and a crash before the state landed
means it is paid twice.

So the decision is made from **receipts**: ledger rows keyed by the auction id. That only
moves the problem unless "this row's credits are in the balance" is itself atomic. The
F_0.9.2 boot check keeps the *balance* authoritative and appends a compensating row, so a
ledger row can exist for credits that never reached the balance. The applied mark closes
that gap:

- **The mark is written with the balance.** `simbot_progression.bank_ledger_applied_id`
  is written in the **same single-row UPDATE as the balance**. MyISAM makes a single-row
  UPDATE atomic, so a credit row is applied if and only if its id is at or below the mark.
- **The insert loop never re-queues a written row.** It does not read row ids back. A
  successful INSERT advances `insertedRows` (`SimPlayerManager.cpp:15893`), so only rows
  whose INSERT never completed are queued again. The balance UPDATE resolves the mark
  itself with `playerBotLedgerMaxId` (`:16068`). If that lookup fails, it throws before
  the UPDATE.
- **Void rows stay void.** A row left above the mark by a crash is voided at load,
  `source = CONCAT('void_', source)` (`:11267`), before any flush could move the mark
  past it.
- **Each repair writes only what is missing.** `decideReceiptWrites`
  (`SimBotBazaar.h:97`) receives both receipt sums, credit and item, so settlement and
  every repair path write only the receipt that is absent.

**Lesson:** when you cannot have a transaction, make the *proof* of each effect atomic with
the effect, then let every path ask the proof rather than a status column.

## 3. Evidence that a buyer cannot destroy

Recovery needs evidence that a sale happened when its in-memory settlement message (POD)
is lost. Two sources were tried before one stuck:

- **The item in a player's inventory.** The buyer can sell, trade or destroy it.
  Rejected.
- **The `AuctionItem`, kept bag-owned until the bot's receipts were applied.** This is
  durable evidence, but the price was a hold of up to one 60-second flush interval
  before the buyer could collect.

The owner rejected the hold: *"A player is not going to want to wait a minute for an
item."* The shipped design keeps the receipts from section 2 and changes what is
recovered from:

- `doInstantBuy` gives the item to the buyer immediately, as it would for any seller.
- `enqueuePlayerBotBazaarSale` (`:11715`) raises `progressionBazaarFlushRequested` and
  kicks the maintenance lane. The lane settles and force-flushes (`:29517`), so the
  receipts are durable within seconds instead of within the 60-second flush interval.
- A lost POD is recovered at boot by `findBuyerOwnedBotSaleLocked` (`:17049`), from the
  buyer-owned `SOLD` record, or from the item under a player creature once retrieved.
  Settlement checks the receipts, so a recovered sale can only complete a payment, never
  repeat one. A row that was already settled is rebuilt from `netPaid + tax`, so city tax
  is never applied twice (the one Codex Major in this delta).

That leaves a crash window measured in seconds, in which the bot, never the player, can
lose a sale's credits. The owner accepted it explicitly, and the plan records it.

**Lesson:** durability guarantees have a user-experience price. Make the price visible,
let the owner choose, and shrink the window that remains.

## 4. What live verification taught

The harness passed scenario after scenario while two production bugs waited for real
data:

- **The store load failed once real listings existed.** `closed_at` is NULL on an open
  listing, and `getUnsignedLong` throws on NULL, so every boot failed to load the store.
  The first boot had no listing rows, so it passed. Rule: `COALESCE` every nullable
  numeric column you read.
- **Expired items were never relisted.** A retrieved listing keeps its entry in the live
  map, and two candidate filters skipped any item that had an entry. Rule: a "has an
  entry" check on a map that keeps closed entries must check the state.

Both were caught by real bots and real restarts, not by the harness alone. The owner's own
purchase in the game client then confirmed the whole loop: the buyer's debit matched the
bot's credit to the credit, settled in the same second.
