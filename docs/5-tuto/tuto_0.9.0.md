# Tutorial 0.9.0 — Where durable state lives when the thing it describes is disposable

## The shape of this release

Every previous SimPlayer feature could keep its state on the bot. Movement
phase, traversal generation, combat target — all of it lives on the
`AiAgent` or its controller, and all of it is *supposed* to die with the body.

P.10 is the first feature where that is false. A PlayerBot's XP has to outlive
the body that earned it, and in this engine the body is disposable by design:

```
hunter dies -> CreatureManager::notifyDestruction -> corpse
            -> SimHunterController CLONE_HOME
            -> SimPlayerManager::spawnPveIdentityBody  (a NEW object, new OID)
```

So the first question is not "how do we award XP" but "where can XP possibly
live". Answering that badly is how you get a leak that survives restarts.

## Lesson 1: the engine's reward paths are ghost-shaped, and a bot has no ghost

It is worth being concrete about why you cannot simply call the existing API.

`PlayerManager::awardExperience` opens with:

```cpp
PlayerObject* playerObject = player->getPlayerObject();
if (playerObject == nullptr)
    return 0;                       // a PlayerBot lands here, every time
```

XP itself lives in `PlayerObject.experienceList` (`PlayerObject.idl:128`) — on
the **ghost**, the object a real client session owns. A PlayerBot body is an
`AiAgent` with no ghost at all.

The same shape repeats everywhere:

- `PlayerManagerImplementation::disseminateExperience` only visits attackers
  where `attacker->isPlayerCreature()` is true.
- Loot is created in `CreatureManagerImplementation::notifyDestruction` only
  when the highest-damage group leader `isPlayerCreature()`.
- `SkillManager::canLearnSkill` returns false the moment `getPlayerObject()`
  is null.

The useful read is that these are not arbitrary gates. They exist because the
thing being written *is* the ghost. There is no version of "just call
`awardExperience` on the bot" that works, and hunting for one wastes a day.

What is genuinely reusable is the *data*: `ThreatMapEntry::addDamage` already
keys every attacker's damage by `weapon->getXpType()` (`ThreatMap.cpp:18-22`),
including a bot's. The measurement is right; only the destination is missing.

## Lesson 2: closing a leak class by construction, not by a check

The obvious design is a field on `AiAgent` and a flag saying "this one is a
PlayerBot". This project has already paid for that answer once.

`simPlayerBot` was a sticky boolean. It was set on bot bodies and cleared in
exactly one place, so when the engine recycled a destroyed bot's object for a
wild creature, the flag came with it. Players saw white, unattackable gorgs.
The fix was a reset in `AiAgentImplementation::loadTemplateData` (~:358-367) —
a guard at the one chokepoint that reinitialises templates.

That fix works, but notice its shape: *the leak is still possible, and a check
prevents it*. If some future path constructs an agent without going through
`loadTemplateData`, the leak returns.

F_0.9.0 takes the other route. Progression is keyed by **roster identity id**,
and the award API refuses to act without an existing record:

```cpp
// SimPlayerManager::grantPlayerBotExperience
Locker progressionLock(&progressionMutex);
if (!progressionRecords.contains(identityId)) {
    progressionAwardsRejectedNoRecord.increment();
    return false;                    // never creates one
}
```

Records are created by exactly one function,
`ensurePlayerBotProgressionRecord`, which refuses any id absent from the loaded
roster, and the body→identity resolver consults only `pveBodyIdentityIds`:

```cpp
uint64 SimPlayerManager::resolvePlayerBotIdentity(uint64 bodyOid) {
    Locker pveLock(&pveMutex);
    return pveBodyIdentityIds.contains(bodyOid) ? pveBodyIdentityIds.get(bodyOid) : 0;
}
```

It never consults `getSimPlayerBot()`. That is the whole trick. A leaked flag
on a wild creature is now *irrelevant*: the creature has no roster mapping, so
it resolves to identity 0, so it has no record, so every award is rejected and
counted. There is no guard to forget, because there is no path to guard.

The harness asserts exactly this, and it is worth reading as an executable
statement of the invariant (`awardToNonRosterPlayerBotBody`): spawn an ordinary
artisan, `setSimPlayerBot(true)` on it — deliberately reproducing the leak —
resolve it, try to award, and require **both** that the resolve returned 0 and
that the award was refused.

**The general rule**: when you find yourself adding a check to prevent state
reaching the wrong object, ask whether you can instead make the wrong object
structurally unable to hold it.

## Lesson 3: an atomic swap is about what happens *during* the I/O

The flush looks unremarkable until you ask what happens to an award that lands
while SQL is in flight.

The roster's existing flush (`flushPveIdentityRoster`) copies the dirty set,
writes, then clears the whole set. If an award arrives during the write, its id
is cleared along with everything else and the change is silently lost until
something else happens to re-dirty it. For lifetime kill counters that is
tolerable. For credits it is not.

So the progression flush swaps instead of clearing:

```cpp
{
    Locker progressionLock(&progressionMutex);
    for (...) batch.put(identityId, progressionRecords.get(identityId));
    progressionDirtyIds.removeAll();     // the swap boundary
}
// ... SQL happens out here, holding no lock ...
```

An award arriving after that block finds an empty dirty map, re-dirties its own
id, and is picked up by the next flush. The batch holds a *copy*, so the write
in flight is consistent with the moment it started. On failure, the record being
written plus every id still queued are merged back — re-dirtied, not
overwritten, so a concurrent award is preserved.

The harness scenario that proves this (`award_during_flush_persists`) is worth
copying as a pattern: arm a deliberate delay inside the flush, grant 100, start
the flush asynchronously, wait until it has *started*, grant 50 while it sleeps,
then assert the SQL row reads **100** and the id is dirty again. Then flush and
assert 150. A test that only checked the final value would pass even if the swap
were broken.

## Lesson 4: two engine specifics that cost a verification cycle each

**`LuaObject::getStringField(key, default)` does not give you the default.**

```cpp
// engine3 LuaObject.cpp:37-55
if (lua_isnil(L, -1))
    result = defaultValue;      // pointer set...
else {
    result = lua_tostring(L, -1);
    size = lua_rawlen(L, -1);   // ...but size ONLY set here
}
String val;
if (result != nullptr)
    val = String(result, size); // size is still 0 -> empty string
```

An omitted Lua field yields `""`, not your default. Every award in the first
live run recorded an empty source because of this. Read the field plainly and
apply the fallback in C++:

```cpp
step.source = table.getStringField("source").trim();
if (step.source.isEmpty())
    step.source = "harness";
```

Three pre-existing `structureTraversalTest` call sites have the same latent
hazard and only work because their Lua always supplies the field.

**The server's working directory is `bin/`.** Every diag log in the tree writes
`log/x.log`, not `bin/log/x.log` — compare `CellNavDiagLog.h:35` and
`StructureTraversalDiagLog.h:57`. The phase-A verdict file used the repo-relative
form, so `ofstream::is_open()` failed silently and the two-boot protocol would
have failed closed on every run. Paths in documentation are repo-relative; paths
in code are cwd-relative, and those are different strings.

## Lesson 5: state a test arms must travel with the request

The subtlest bug in this release was not in the store. The harness armed a
flush fault by setting manager-level knobs, and `flushPlayerBotProgressionStore`
read and cleared them at the top — so **whichever flush ran next** consumed them.
The routine end-of-tick maintenance flush could steal a fault armed for the
harness's own `FlushNow`, fail an unrelated flush, and leave the real request
waiting for the next tick. That is what a 32-second request latency turned out to
be: not a scheduler defect, but a stolen fault.

The fix is ownership. The fault is now a parameter of the operation that was
supposed to receive it:

```cpp
void flushPlayerBotProgressionStore(bool force, int faultDelayMs = 0,
    bool faultFailNext = false);
```

The knobs are read and cleared only inside the `FlushNow` handler; a routine
flush passes nothing and cannot consume one. Max request wait went from
32010 ms to 1011 ms.

The generalisation: shared mutable state that a test arms for one specific
operation is a race waiting to happen, and it will present as a *timing* bug
somewhere far away from the thing that is actually wrong.

## Lesson 6: what live verification is actually for

This release passed a warning-clean `-Werror` build and a full Codex review
with no open findings, and then live verification found four real defects — two
of which (the empty Lua default, the wrong log path) would have made the feature
quietly not work.

None of the four were visible to static review, because all four are about what
the *running system* does with configuration and paths. That is the argument for
this project's harness-first bar: not that reviewers are careless, but that a
class of defect only exists at runtime.

The corollary is that a receipt is bound to a source fingerprint for a reason.
Fixing the scheduler follow-up after the first PASS correctly made that receipt
stale, and the whole cycle — build, review, phase A, restart, phase B, restore —
had to run again. That is the system working, not overhead.
