# Tutorial 0.9.5 — Three lessons from making a default-off capability testable

This release added skill training to PlayerBots. The interesting engineering
wasn't the training loop — it was three architectural mistakes the work exposed,
each of which survived a warning-clean build, unit tests, and multiple rounds of
code review, and each of which cost a live-verification cycle to find.

---

## 1. A capability gate that checks itself before the test override cannot be tested

Two gate helpers, written days apart, with one difference:

```cpp
// SimPlayerManager.cpp — the award gate (correct)
bool SimPlayerManager::playerBotProgressionAwardGateEnabled() {
    if (!progressionEnabled)                       // MASTER gate only
        return false;
    Locker parityLock(&playerBotParityTestMutex);
    if (playerBotParityTestGateOverrideActive)
        return playerBotParityTestGateOverride;    // override decides
    return true;
}

// the training gate, as originally written (broken)
bool SimPlayerManager::playerBotTrainingGateEnabled() {
    if (!progressionEnabled || !progressionTrainingEnabled)
        return false;                              // CAPABILITY checked first
    Locker parityLock(&playerBotParityTestMutex);
    if (playerBotParityTestTrainingGateOverrideActive)
        return playerBotParityTestTrainingGateOverride;  // unreachable
    return true;
}
```

Every P.10 capability ships `false` — that is the whole safety posture. So
folding `progressionTrainingEnabled` into the pre-override check made the
harness's `setTrainingGate` op **inert**, and every `trainPlayerBotSkill` call
was refused with `refusals.gate`. The capability could not be exercised at all.

Nothing static catches this. The code compiles, reads sensibly, and arguably
looks *more* careful than the correct version. It is only wrong relative to how
the harness must drive it, which means it is only visible when the code **runs
under its shipped configuration**.

**The rule** (now in ARCHI §12): master gate first, harness override second,
configured capability last.

```cpp
    if (!progressionEnabled)
        return false;
    Locker parityLock(&playerBotParityTestMutex);
    if (playerBotParityTestTrainingGateOverrideActive)
        return playerBotParityTestTrainingGateOverride;
    return progressionTrainingEnabled;             // the configured value
```

A corollary bit us separately: `setNoviceGate` wrote `pveNoviceEnabled`
directly, but `applyPveConfig` re-asserts it from Lua on **every** PvP
maintenance tick via `refreshPvpConfig()`. A test override that a config refresh
can revert is not an override. It now sets a latch that suppresses the
re-assert, saved on first latch and restored on all three cleanup paths
(`SimPlayerManager.cpp:9733`, `:11731`, `:11872`).

---

## 2. Adding a destructive operation changes what a manager must know

The old `governPvePopulation` only ever **added** bodies. Because of that it
never had to answer a question: *which bodies do I own?* Anything already in
`pveIdentityBodyOids` was simply skipped.

Making `pveMaxHunters` a true population cap required a drain — and the moment
the governor could destroy, ownership became load-bearing. The first version
derived it from eligibility:

```cpp
    if (isPveHuntEligible(identity))
        governorManaged.put(identity.id, true);
```

That destroyed the parity harness's own bodies, because harness identities are
not hunt-eligible. Fixing *that* exposed two more places counting the wrong set
(`assertBodyMaxBound` and the spawn budget both counted harness bodies against
the production cap — they had only agreed before because the governor was
destroying those bodies). And fixing *that* exposed a fourth: the novice
distribution pass never applied the eligibility check the general-fill pass did.

Four findings, one shape: **the same membership predicate written in more than
one place, then drifting.** The fix was structural rather than a fourth patch:

```cpp
    // Membership decided ONCE; both passes below decide ORDER, never membership.
    VectorMap<uint64, bool> eligible;
    for (int i = 0; i < roster.size(); ++i) { /* ...one predicate... */ }

    if (noviceEnabled) { /* distribution: filters to eligible */ }
    for (...)          { /* general fill: filters to eligible */ }
```

and ownership was decoupled from eligibility entirely
(`isGovernorManagedIdentity`, `SimPlayerManager.cpp:13842`), because eligibility
reads runtime-refreshable config: removing a profession from
`pveHuntingProfessions` would otherwise strand its live bodies — invisible to the
drain *and* uncounted against the cap.

**The generalisation**: when a component gains the ability to remove things it
previously only created, audit every place that answers "is this mine?" — there
is usually more than one, and they have never had to agree before.

---

## 3. You can often delete a distributed-transaction problem instead of solving it

Training touches XP, skill points and a skill row. MyISAM has no transactions,
so the obvious design is a write-ahead journal with phase replay — which is
exactly what F_0.9.6's bazaar will need.

It was unnecessary here. `simbot_experience` was redefined to mean **lifetime
earned** — which is already what F_0.9.1 wrote, since `grantPlayerBotExperience`
only ever adds — and the derived quantities fall out of it:

```cpp
available = earned − Σ getXpCost() over trained boxes of that xpType
spent     = Σ getSkillPointsRequired() over trained boxes
```

Training then mutates exactly **one** durable row: an `INSERT` into
`simbot_skills`. Atomic under MyISAM, self-healing at boot, no journal.

One subtlety worth internalising: the ceiling must apply to *spendable* XP, not
earned. `grantPlayerBotExperience` computes `room = cap − available`, so a bot
that caps out, spends on a box, and earns again keeps progressing. Capping
`earned` instead would stall the ladder permanently at the first cap — a bug
that would only appear ~20 boxes into a 23-box ladder.

**The generalisation**: before reaching for a journal, check whether one of your
stored values can be redefined as monotonic and the rest derived. Multi-row
consistency problems often dissolve into single-row writes.

---

## What this cost

Seventeen verification runs. Two product defects (§1, §2) were reachable only by
running the code under its shipped configuration; nine failures were in the test
harness itself; one stall was never explained. The two defects that mattered were
both found by scenarios that had nothing to do with the feature under test — the
training gate by a synthetic skill-record scenario, and the governor by a
**pre-existing F_0.9.1 scenario** that merely assumed a body would still exist.

That is the argument for re-running old scenarios unchanged rather than adapting
them to new behaviour: they encode assumptions the new code never agreed to.
