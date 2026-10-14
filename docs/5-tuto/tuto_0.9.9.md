# Tutorial 0.9.9 — Two stores, no transaction: write the journal first and let the balance win

F_0.9.2 gave PlayerBots their first holding, a bank balance, and with it a journal of every
change. The interesting part is not the arithmetic. It is how to keep two records of the same
fact honest when the database cannot update them atomically.

## 1. The constraint

`simbot_progression` and `simbot_ledger` are MyISAM tables (as every `simbot_*` table is). There
is no transaction that spans an `INSERT` into one and an `UPDATE` of the other. The server can
stop between them. So the question is not *whether* the two can disagree, but **which way they
disagree and who wins**.

## 2. Pick the failure you can detect

The flush writes the journal first:

```cpp
// SimPlayerManager.cpp:12750
// MyISAM has no transaction spanning the journal and the balance. Insert
// every journal row first. If one insert fails, only rows not yet inserted
// are requeued; rows already in MySQL must never be duplicated on retry.
```

If the server dies after the journal rows and before the balance `UPDATE`, the journal says
"+3,897, balance after 3,897" while the balance column still says 0. That state is
**detectable**: the newest row's `balance_after` does not equal the balance.

The opposite order would leave a balance that moved with no row explaining it, and nothing in
the data would show that a row was missing.

## 3. Detect it at load, repair the journal, never the balance

```cpp
// SimPlayerManager.cpp:10758 (inside loadPlayerBotProgressionStore)
entry.delta = balance - balanceAfter;
entry.balanceAfter = balance;
entry.source = "reconcile";
```

The loader appends a `reconcile` row so the deltas sum to the balance again. It does not rewrite
the balance from the journal. Holdings rule 3 says one authority per kind, and for credits the
authority is the balance column: it is what every award and spend reads under `progressionMutex`.

The `reconcile` row is permanent (exempt from the retention prune), so the gap is visible
forever rather than being masked by the next ordinary row. The Codex plan review caught that masking
case: with only a "newest row versus balance" check, `+100 / after 100` against balance 0,
followed by a normal `+50 / after 50`, would pass the next check while the deltas summed to 150
against a balance of 50.

## 4. Retry without duplicating

```cpp
// SimPlayerManager.cpp:12774
for (int pending = ledgerBatch.size() - 1; pending >= i; --pending)
    progressionLedgerPending.add(0, ledgerBatch.get(pending));
```

When insert `i` fails, only entries `i` and later go back to the front of the queue. Entries
already inserted are not requeued, because re-inserting them would double-count. The balance
batch is marked dirty again so the `UPDATE` retries too. This is the same swap-out, merge-back
shape the F_0.9.0 store flush already used for balances, extended to a second list.

The live harness taught one more thing here. A scenario injected a failed insert and then asked
"zero rows, one pending?". It failed twice with `got=1 want=0`: the maintenance lane had already
retried and written the row before the queued SQL assertion ran. The fix was to the test. The
oracle became "the fault fired, and the row exists exactly once", which is the property that
matters.

## 5. The same idea for spending: reserve, then commit or release

A doctor-buff trip takes minutes. If the fee were checked at the start and charged at the end,
a hot-reloaded fee or another spend in between could produce a free buff or a different price.
So the start **reserves** a snapshot:

```cpp
// SimHunterController.cpp:1665
PlayerBotBuffFee feeState = SimPlayerManager::instance()->
    reservePlayerBotDoctorBuffFee(identityId);
```

`commitPlayerBotDoctorBuffFee` (`SimPlayerManager.cpp:11427`) spends exactly the snapshotted
amount where doctor buffs land, and `spendPlayerBotCredits` treats reserved credits as
unavailable (`:11380`), so nothing else can spend them meanwhile. Every exit that delivers no
doctor buff (flow end, new order, death, teardown) releases the reservation.

The commit also rechecks the gate, outside `progressionMutex`:

```cpp
// SimPlayerManager.cpp:11437
// Recheck outside progressionMutex: the gate takes the parity mutex and
// must never be nested beneath the progression lock.
```

That makes "turn the gate off" take effect for trips already in flight, which is what the
rollback promise in the plan says.

## 6. Why this matters beyond credits

Loot (F_0.9.3) and the bazaar (F_0.9.9 onward in the roadmap's numbering) move real objects
between a corpse, the vault and `AuctionsMap`, again across stores with no shared transaction
(BerkeleyDB objects, MySQL rows). The rules are the same:

1. Order the writes so the failure you cannot prevent is one you can detect.
2. Name one authority per kind and repair the other record toward it, visibly.
3. Requeue only what was not written.
4. Anything that spans time holds a reservation that is committed once or released.
