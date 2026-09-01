# Tutorial 0.8.2 — When the fix makes the bug *louder*: layered defects and the anchor/destination distinction

## The shape of this release

Two defects produced one symptom. Fixing the first did not half-fix the symptom —
it changed the failure mode from *silent* to *loud*, which is the useful thing to
recognise early.

```
symptom:  PvE hunters take synthetic doctor buffs instead of visiting a doctor

layer 1 (F_0.7.3)  the doctor is never FOUND      -> bot never leaves the cantina
layer 2 (F_0.7.5)  the doctor cannot be REACHED   -> bot tries 23x, fails 22x
```

After F_0.7.3 the dashboard looked *worse*: a bot burning 536 path failures instead
of quietly taking a fallback buff. That is progress, and reading it as a regression
would have been the wrong call.

## Lesson 1: an anchor is not a destination

`SimPlayerManager::resolvePvpCityLocations` publishes per-city exterior points —
`hangout` (cantina) and `medCenter` (hospital). It is tempting to assume the bot
*walks to* `medCenter`. It does not.

The move target comes from the provider NPC itself, in `moveToNextPveBuffProvider`'s
`approach()` lambda:

```cpp
enterStructure(provider.worldPos, provider.localPos, provider.cell.get());
```

All three fields are snapshotted off the creature in `findProvider`. `medCenter` is
only the *centre of the search* that located that creature, plus a distance tiebreak.

There *is* a `moveTo(medCenter)` in `SimHunterController.cpp`, which is exactly what
makes this trap convincing — but it sits behind an early return:

```cpp
if (SimPlayerManager::instance()->isPveRealBuffsEnabled())
    return;          // real-buffs path exits here
...
moveTo(medCenter);   // legacy synthetic path only
```

**Why it matters**: the natural fix for "the doctor isn't found" is "move the anchor."
That cannot work, because the anchor is not an input to the failing call. What *did*
work was making the anchor load-bearing in a new way — as a **waypoint** — which is
F_0.7.5.

### The silent-fallback trap

```cpp
medCenter = resolved.medCenterResolved ? resolved.medCenter : home;
```

When no hospital is found, `medCenter` becomes the **shuttle pad**. The scan then
re-runs centred on the same wrong point. A miss degrades into a *plausible* value
rather than an absent one, so nothing upstream can tell the difference. The fix adds
a dual anchor (pad **and** hangout) at a wider radius, and surfaces
`medCenterResolved` on the dashboard so the degradation is observable.

Watch for this pattern generally: `x = resolved ? real : fallback` erases the
distinction between "resolved to the fallback" and "resolved for real".

## Lesson 2: isolate by varying ONE input

The second defect had three plausible explanations that all looked identical from the
production logs: a bad target cell, accumulated egress state, or a bad start point.

The harness (`structureTraversalTest.scenarios`) settled it by holding the target
fixed and varying **only the spawn**:

| scenario | spawn | result | eliminates |
| --- | --- | --- | --- |
| A | the exact failing point | FAIL | *stateful* — reproduces from a **fresh spawn**, no egress involved |
| B | the shuttle pad | PASS | *target* — same cell enters fine |
| C | the med-centre anchor | PASS | *target* — enters in 35 s vs 109 s from the pad |
| D | bad point → `moveTo` anchor → enter | **PASS** | proves the fix before writing it |
| F | 15 m from the bad point | FAIL | *localized hole* — nudging the exit point won't help |

D is the important one: it validated the design *before* any production code changed.
And D's first step passing carried the decisive detail — the bot can **move** overland
from that point; only *cell-entry pathfinding* has no route. That distinction is what
makes the fix work, because leg 1 uses the overland walker (the `directOverland` trust
tier from P.4.2), which never needed a navmesh path.

## Lesson 3: gate subordination must be enforced, not documented

The first implementation shipped this comment:

> staging only engages when structure traversal is also on

…and a condition that never checked it. Codex caught it. With traversal off,
`enterStructure` degrades to `moveToInterior`, so the staging leg would still have
inserted itself into a configuration the fix was never measured against:

```cpp
if (crossBuilding && isStructureTraversalFeatureEnabled() &&
        SimPlayerManager::instance()->isPveBuffCrossBuildingStagingEnabled() &&
        resolvePveProviderStagingPoint(stage, stagingPoint)) {
```

**Rule of thumb**: if a comment states a precondition, the code must test it. A
documented invariant with no enforcement is just a wish — and this codebase already
learned the harder version of that lesson in F_0.8.1, where a non-chaining override
silently opted PvP leaders out of base-class defenses until `acceptFoundPath` was
made a non-virtual template method.

## Lesson 4: verify the config you actually ship

The natural instinct is to verify under the rich diagnostic profile — logging on,
probes on, harness running. But the TRIP receipt binds a **source fingerprint**, and
the Lua config *is* source. Verifying with logging on and then reverting it produces
a receipt for a tree that was never exercised.

So the release config was set **first**, then verified, and A2 asserted the shipped
gates by reading them back off the live dashboard rather than trusting the file.

The honest cost, recorded in the report: with logging shipped off, the run could not
localize 21 `zSanityViolations` to outdoor cells. Stating that beats quietly leaning
on a prior run's evidence.

## Takeaways

1. A "search anchor" and a "movement destination" are different roles — check which one you're holding before proposing to move it.
2. `x = ok ? real : fallback` hides resolution failures; surface the boolean.
3. Isolate by varying one input against a fixed target; a passing *composite* scenario can validate a fix before you write it.
4. Enforce documented preconditions in code.
5. Verify the configuration you intend to ship, and state what that configuration prevents you from seeing.
