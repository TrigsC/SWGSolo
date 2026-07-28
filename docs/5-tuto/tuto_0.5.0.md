# Tutorial 0.5.0 — The epoch compare-and-commit: making a long, lock-free operation safe against a canceller

**Audience**: advanced; Core3/engine3 internals (task-pool threads, `Locker`/`Mutex`,
`VectorMap`, the single-writer discipline).
**Grounded in**: `SimHunterController.cpp`, `SimPlayerManager.cpp/.h` from the
F_0.5.0 (P.8.7) diff.

---

## The problem this release actually solved

A hunter walks to a mission terminal and asks the manager to fill its board:
`beginMissionAccept()` (on one task-pool thread) calls
`SimPlayerManager::generatePveBotMissionOffers(...)`, which does a *lot* of work —
scan demand, pick a lair, build offers, and finally **commit** them into the
shared `pveBotMissions` map.

Meanwhile the order can end from an *entirely different* thread: the order
completes, is abandoned, times out, or the hunter's body is drained by the
world-presence churn. Those run `clearPveHunterOrderLocked` /
`drainSimPresenceBodies`.

The race: generation starts while the order is alive, the order ends mid-scan,
and generation then commits offers for a hunter that no longer has an order —
resurrecting dead state. This is what produced the `mission_offers_unavailable`
churn (558 events/session) and phantom board entries.

The naïve "fix" is to hold `pveMutex` for the whole of
`generatePveBotMissionOffers`. You can't: it does file I/O (diagnostics) and
heavy scanning, and this project's iron rule is **never hold `pveMutex` across
I/O or long work** — that stalls every miner, PvP squad, and dashboard read that
shares the mutex, and risks the exact server hang the owner cares about avoiding.

## The pattern: a monotonic epoch, checked at commit

Instead of *preventing* the race with a long lock, we *detect* it at the moment
that matters — the commit — under a brief lock, and discard a stale result.

**1. A globally-monotonic sequence + a per-identity stamp** (`SimPlayerManager.h`):

```cpp
uint64 pveTerminalVisitEpochSeq = 0;               // never reset while running
VectorMap<uint64, uint64> pveTerminalVisitEpoch;   // identityId -> epoch
```

**2. Open a visit** — capture the epoch at board-open, under the lock, only if
the order is genuinely live (`SimPlayerManager::openTerminalVisit`):

```cpp
Locker pveLock(&pveMutex);
if (!pveHuntOrders.contains(identityId) || order.bodyOid != bodyOid)
    return 0;                          // no live order -> caller abandons
uint64 epoch = ++pveTerminalVisitEpochSeq;
if (epoch == 0) epoch = ++pveTerminalVisitEpochSeq;   // skip the 0 sentinel
pveTerminalVisitEpoch.put(identityId, epoch);
pveBotMissions.drop(identityId);
return epoch;
```

The hunter caches this in `missionOfferVisitEpoch` and passes it straight into
`generatePveBotMissionOffers(..., uint64 visitEpoch)`.

**3. Commit only if the epoch still matches** — the long, lock-free scan happens
with *no* lock held; then a *short* critical section validates and commits:

```cpp
Locker pveLock(&pveMutex);
if (!pveTerminalVisitEpoch.contains(identityId) ||
        pveTerminalVisitEpoch.get(identityId) != visitEpoch) {
    // someone ended the order (or started a newer visit) while we scanned.
    offers.removeAll();
    // MissionDiagLog OFFERS_FAIL reason=staleVisit
    return false;
}
// ...else commit the freshly-built offers into pveBotMissions...
```

**4. Invalidate the epoch anywhere the order can die** — the canceller side just
drops the stamp (inline, under the lock it already holds):

```cpp
// clearPveHunterOrderLocked (already under pveMutex):
pveBotMissions.drop(identityId);
pveTerminalVisitEpoch.drop(identityId);   // <- makes any in-flight commit stale
// same drop added to drainSimPresenceBodies and the config-reload removeAll
```

Once the stamp is dropped (or replaced by a newer visit), the in-flight
generation's `visitEpoch` can never match again, so its commit is discarded. No
long lock, no I/O under lock, and the canceller pays nothing but a `drop`.

## Why a *global* sequence, not a per-identity counter (the ABA trap)

If the epoch were, say, "attempt number" reset per visit, this sequence could
bite:

1. Visit A opens (epoch 1), generation starts.
2. Order ends, a *new* order for the same identity opens visit B — which, reset,
   is *also* epoch 1.
3. Generation A commits, sees epoch 1 == 1, and commits **A's** offers into
   **B's** visit.

That's the ABA problem: the value returned to a state it had before, so equality
no longer proves identity. The fix is that `pveTerminalVisitEpochSeq` is a single
process-global counter that **only ever increments** and is *not* reset on config
reload (only the `VectorMap` is cleared). Every visit anywhere gets a
never-repeated number, so `==` at commit truly means "the same visit I started."

## The two atomics that partner with it (C1/C2)

The epoch guards the manager's shared map. The controller's own two tick threads
(`onTick`, `runActiveTick`/`SimHunterActiveTickTask`) needed their own
discipline:

- **C1** — `std::atomic<uint64> activeTickGeneration`. `scheduleActiveTick` does
  `fetch_add(1, acq_rel)+1` and stamps the task; a task whose stamp is stale
  self-discards on entry. Plain `uint64` here was a real cross-thread data race
  (read-modify-write from multiple pools).
- **C2** — `std::atomic<bool> tickRunning`, a *non-blocking* single-flight guard:
  a `runActiveTick` that loses the `compare_exchange_strong` reschedules itself
  (`scheduleActiveTick(100)`) instead of blocking a task-pool thread. An
  RAII `TickGuard` clears it on every exit path.

Note the deliberate distinction: C1 is **latest-wins-before-entry** (stale ticks
are dropped *before* running), while C2 is **mutual exclusion of execution**
(two bodies never overlap). Conflating the two was the flaw in the first design
Codex rejected — the generation counter alone does *not* give you single-flight.

## The transferable lesson

When a long operation must run without a lock but a concurrent actor can
invalidate its result, don't reach for a bigger lock. **Stamp the operation with
a monotonic token at start, and re-validate the token under a short lock at the
single commit point; let cancellers invalidate by moving the token.** Keep the
token globally monotonic so equality can't be spoofed by reuse (ABA). It is the
same shape as a compare-and-swap or an optimistic-concurrency version column —
applied here to a `VectorMap` commit across engine3 task-pool threads.

**What we explicitly did *not* do**: build the full per-controller
lifecycle-event strand (every writer routed through one ordered queue with
per-op staleness tokens). Codex showed a *partial* strand gives false safety —
worse than none — so F_0.5.0 caps at the terminal-visit epoch (C1–C4) and records
the strand as deferred debt. Correct-and-bounded beats ambitious-and-partial when
the failure mode is a server hang.
