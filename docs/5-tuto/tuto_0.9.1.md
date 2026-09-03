# Tutorial 0.9.1 — Adding a writer to a hot path you don't own

**Release**: F_0.9.1 (P.10b kill XP)
**Level**: advanced
**Focus**: Core3 lock choreography, deferred-task mutation, parity with engine arithmetic

---

## The problem this release actually solved

You need a new subsystem to react to an engine event. The event fires deep inside stock
Core3 code, on a hot path, with objects already locked by callers you don't control. Every
instinct — take a lock, look something up, write a row — is wrong there.

`PlayerManagerImplementation::disseminateExperience` (`:1907`) is that path. It runs once
per creature death, and it runs with `destructedObject` **already locked**. You can prove
that from the code without reading a single caller: the P.7.4b FRS block inside it uses

```cpp
Locker crossLocker(attackerCreo, destructedObject);   // :1985
```

A cross-locker means "lock A while B is already held." If `destructedObject` weren't
locked on entry, that line would be a plain `Locker`. **Read the lock idiom to infer the
contract** — it is more reliable than tracing callers in a codebase this size.

## Step 1: find the data before you write the code

The temptation is to add tracking so your subsystem knows who damaged what. Don't, until
you've checked. `ThreatMap::addDamage` (`ThreatMap.cpp:69-105`):

```cpp
if (xp == "" && target->isCreatureObject()) {
    CreatureObject* tarCreo = target->asCreatureObject();
    if (tarCreo != nullptr) {
        WeaponObject* weapon = tarCreo->getWeapon();
        if (weapon != nullptr)
            xpToAward = weapon->getXpType();
    }
}
```

Note what is *absent*: any `isPlayerCreature()` fence. The threat map records **every**
attacker, AiAgents included, already keyed by that attacker's weapon XP type. The data
this release needed had been accumulating for years; nothing had ever read it.

The lesson generalises: before adding instrumentation, grep the structure that already
observes the event and check whether it is fenced or merely *used* in a fenced way. Here
the *consumers* were fenced (`awardExperience` returns 0 without a `PlayerObject` ghost,
`:2703-2707`; the dissemination loop gates on `attacker->isPlayerCreature()`, `:2216`) but
the *producer* never was.

## Step 2: collect synchronously, mutate asynchronously

The pattern, copied from the FRS block that already lives in this function:

```cpp
// In disseminateExperience — arithmetic only, no locks, no mutexes.
PlayerBotKillXpEvent event;
event.baseXp = baseXp;
event.totalDamage = totalDamage;
event.globalMultiplier = globalExpMultiplier;
// ... fill per-attacker OIDs, damage-by-type, group/GCW multipliers ...

if (event.attackers.size() > 0) {
    Core::getTaskManager()->executeTask([event] () {
        SimPlayerManager* awardManager = SimPlayerManager::instance();
        if (awardManager != nullptr)
            awardManager->awardPlayerBotKillExperience(event);
    }, "SimPlayerBotKillXpTask");
}
```

Three properties make this safe, and all three are deliberate:

1. **The lambda captures by value.** `event` is a POD of OIDs and numbers. No
   `ManagedReference` to the corpse or any attacker crosses the boundary — holding a
   reference to a dying object past destruction is precisely the lifetime hazard that
   orphaned nine miners in the P.4.4 vehicle work.
2. **Nothing is resolved on the death path.** Identity resolution needs `pveMutex`, which
   the PvE maintenance thread can hold across SQL. Resolving inline would let a database
   round-trip stall creature deaths. On the task, that latency is free.
3. **The mutexes are never nested.** The task takes `pveMutex`, releases it, then takes
   `progressionMutex`. Neither is ever held while locking an agent — the discipline
   documented at `SimPlayerManager.h:1841-1845` and the reason ARCHI calls lock
   choreography this project's #1 stability rule.

## Step 3: a pre-filter is not an authority

Wildlife kills wildlife constantly. Unfiltered, every one of those deaths would allocate a
POD and spawn a task. So the branch pre-filters on `AiAgent::getSimPlayerBot()`.

That is the flag that once leaked onto wild creatures through engine object reuse, fixed
in `d38877020c`. Using it here is fine — but only because it is used as a **cost filter**
and never as the answer:

- **stale `true`** on a wild creature ⇒ one extra task that resolves no identity and
  awards nothing;
- **stale `false`** on a real bot ⇒ that bot loses one kill's XP.

Neither is a correctness or safety defect. Authority stays with
`resolvePlayerBotIdentity`, on the task, under `pveMutex`. Live verification watched this
work: 29 wildlife deaths reached the task and were rejected as non-roster, while the
awards that mattered landed.

**The generalisable rule**: an unreliable signal is safe as an optimisation and dangerous
as a decision. Write down which one you're using it as.

## Step 4: parity means the rounding too

"Same formula" is not the same as "same result." `awardExperience` declares:

```cpp
int PlayerManagerImplementation::awardExperience(CreatureObject* player,
        const String& xpType, int amount, ...)          // :2703
```

`int amount`. The player's `float xpAmount` is **truncated at the call boundary**, then
truncated *again* inside after `globalExpMultiplier` is applied (`:2779`). Two stages.
Collapse them into one multiply-then-truncate and you diverge: at `xp = 1.9` with a global
multiplier of 2, a player earns `(int)((int)1.9 * 2) = 2`; a single-stage bot earns
`(int)(1.9 * 2) = 3`.

`combat_general` truncates **once**, because `combatXp` is already a `uint32` by the time
it is passed — there is no float-to-int boundary to cross, and `localMultiplier` and
`globalExpMultiplier` are multiplied inside the same cast. The accumulation truncates per
iteration too (`combatXp += xpAmount` where the left side is `uint32`, `:2272`).

None of this is stylistic. A reviewer caught the single-stage version in code review; it
would have shipped a bot that out-earned an equivalent player on a tuned server.

## Step 5: don't let a test oracle borrow from the code under test

Scenario 28's first implementation asserted the literals `85` and `8`. Correct at the
shipped defaults, wrong the moment anyone tunes `globalExpMultiplier` — and it would have
failed a *correct* award.

The fix needs the same multiplier the award used. Two options, and the choice matters:

- **Borrow it from the award path** (have the task record what it used). Circular: a wrong
  multiplier becomes invisible because the oracle inherits the same wrongness.
- **Read it independently.** One additive line in `PlayerManager.idl:592`:

```cpp
public float getExperienceMultiplier() {
    return globalExpMultiplier;
}
```

The plan had said "no IDL." Overriding that was the right call because `src/autogen/` is
gitignored — the commit carries one line, the stubs regenerate at build time, and the
oracle stays independent. The real cost was build fan-out: 17 targets became 89.

**When a plan constraint collides with test independence, independence wins — but record
the override where the next reader will find it** (the plan's IDL Impact section now
carries the rationale).

## Step 6: the bug the build could not see

Live verification found one defect after a warning-clean build and two approving reviews:

```
deleteIdentity   FAIL   identity_not_found
```

`deleteIdentity` is asynchronous. Its identity is re-resolved on **every** poll of the
step. The `DeleteIdentity` handler pruned the named reference when the request completed —
so the very poll that observed completion looked up a reference that had just been erased.

The positional form, `deleteIdentity{identityIndex=2}`, had worked for eighteen scenarios
because that handler never touched the positional vector — only the refs map. **A new
addressing mode inherited an async lifetime the old one never had.**

The fix was a deletion, not an addition: references are scenario-scoped bookkeeping,
already cleared wholesale by the cleanup pass. Nothing may prune them mid-flight.

The category is worth naming, because three of this release's defects share it. None were
errors in the code's logic; all three were errors about **what an observer would see at
runtime** — a counter inflated by a zero-value award, an assertion racing a second grant,
a lookup racing its own completion. Compilers and reviewers reason about correctness in
isolation. Only running the thing shows you the interleaving.

---

## Takeaways

1. Infer a locked-path contract from its lock idiom (`Locker(a, b)` ⇒ `b` is already held).
2. Check whether the data you need already exists before adding instrumentation.
3. On a hot, already-locked path: copy a POD, defer the mutation, capture by value.
4. Never nest subsystem mutexes, and never hold one while locking an agent.
5. State whether an unreliable signal is an optimisation or a decision. Only the first is safe.
6. Parity includes truncation order.
7. A test oracle must not derive its expectation from the implementation it checks.
8. New addressing modes inherit the lifetimes of the operations they're used with.
