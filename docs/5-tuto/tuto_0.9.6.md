# Tutorial 0.9.6 — When the instrument is the bug

This release taught a novice PlayerBot to feed itself: leave the city, pick a
creature it can beat, kill it, rest, train, repeat. The loop itself is ordinary
state-machine work. What is worth keeping is that **four of the failures in this
chunk were in the things doing the measuring or the remembering, not in the
behaviour under test** — and every one of them survived a warning-clean build,
Codex review, and in three cases a previous live run.

---

## 1. A single-slot memory is a bug the moment the world offers two

The Engage branch remembers a target whose path just failed, so the scan does not
re-pick it immediately:

```cpp
// SimHunterController.cpp, as shipped before the fix
if (earlyGamePhase == EARLY_ENGAGE) {
    earlyGameSkipTargetOid = targetOid;                      // ONE slot
    earlyGameSkipTargetUntilMs = System::getMiliTime() + 60000;
    handleEarlyGameTargetUnavailable();
    return;
}
```

and the scan honours it:

```cpp
if (earlyGameSkipTargetOid != 0 &&
        candidate->getObjectID() == earlyGameSkipTargetOid &&
        scanNowMs < earlyGameSkipTargetUntilMs)
    continue;
```

This is correct for one unreachable creature and catastrophic for two. In
TRIP-verify `20260915-185828` the novice stood inside a Mos Eisley NavArea with
two kreetles just *off* the navmesh. Path to A fails → skip A → scan picks B →
path to B fails → skip B, **overwriting A's skip** → scan picks A again. The bot
alternated for 70 minutes: 440 Engage transitions, 0 kills, mover IDLE the whole
time, `exitResolveFailures=15313`.

The fix is unglamorous — two parallel vectors and an eviction rule:

```cpp
Vector<uint64> earlyGameSkipTargetOids;
Vector<uint64> earlyGameSkipTargetUntilMs;   // same index
```

```cpp
// evict the entry that would expire soonest when at capacity
while (earlyGameSkipTargetOids.size() > 0 &&
        earlyGameSkipTargetOids.size() >= config.unreachableSkipMaxTargets) {
    int soonest = 0;
    for (int i = 1; i < earlyGameSkipTargetUntilMs.size(); ++i)
        if (earlyGameSkipTargetUntilMs.get(i) < earlyGameSkipTargetUntilMs.get(soonest))
            soonest = i;
    earlyGameSkipTargetOids.remove(soonest);
    earlyGameSkipTargetUntilMs.remove(soonest);
}
```

Two engine details are load-bearing. A `Vector<Struct>` was deliberately not
used: engine3's `Vector<E>` instantiates virtual serialization over `E`, so a
plain local struct costs you a compile error at the vtable, not at the call site.
And both vectors are touched only from the controller's single-flight
`runActiveTick` lane, which is why no mutex appears here — the same
single-writer rule that governs `targetOid`.

**The generalisable form:** any "remember the thing that just failed" slot should
be sized to the number of such things the environment can present *simultaneously*,
not to the number your reproduction had.

---

## 2. A probe that prevents the event it is meant to force

The verification contract could not wait for a natural death, so it carried a
forced-death probe: disable fleeing, widen the level band, let the bot fight
something far too big.

```lua
fleeHamFraction = 0,      -- never retreat
levelBandAbove  = 25,     -- engage almost anything
minTtkSeconds   = 600,    -- ...and only things that take 10 minutes to kill
maxTtkSeconds   = 86400,
```

Read the third line against `calculateEarlyGameTtk`:

```cpp
return (allowInCombat || ttkSeconds >= config.minTtkSeconds) &&
       ttkSeconds <= config.maxTtkSeconds;
```

Nothing near a newbie spawner takes ten minutes to kill, so **every** candidate
was rejected. Across 300 samples the subject sat in `LeaveCity` (268) and
`SelectTarget` (32) with zero targets, zero kills, and a minimum HAM fraction of
0.99. The probe could not kill the bot because the probe had made the bot
unable to fight. With `minTtkSeconds = 0` the same 25-minute window produced two
deaths and two cloner respawns.

**The generalisable form:** a test scaffold that constrains the subject is itself
subject to review. "It did not happen" is only evidence when you can show the
path to it was open.

---

## 3. Measure the behaviour, not one incidental shape of it

The all-attackers assertion was written as:

> at least one `flee_other_attacker` line **with `defenders >= 2`**

The behaviour fired twice for the subject — and both lines read `defenders=1`,
because the creature judged unwinnable was the *only* entry in the defender list
and simply was not the current target:

```
SimPveEarlyGameCombat identity=274 phase=Engage ham=792/800,719/800,791/800
  defenders=1 [...dressed_criminal_thug_aqualish_female_02.iff:lvl10:18m:eligible]
  decision=flee_other_attacker
```

The clause encoded an assumption about *how* the situation arises (a second
attacker joins a fight already in progress) rather than what the code must do
(leave when any attacker is unbeatable). The assertion was amended — on the
record, with the original wording preserved and the raw counts published, because
an amendment made after seeing results is only honest if it is visible.

**The generalisable form:** assert the decision, report the circumstances. When a
measurement fails, first ask whether it measured the requirement or a
coincidence of the run that inspired it.

---

## 4. Offset-based evidence is fragile across hours

Every log delta in this workflow is `tail -c +<offset>` against a byte offset
captured at boot. Mid-verification, `core3.log` was truncated in place — 68 MB at
the Run D offset, past 99 MB at capture, then a few hundred KB with no rotated
sibling. The next delta came back **0 bytes**, and the death-cause assertion
promptly "failed" with three deaths and no lines.

It was rescued because the same line already existed in a second place. The
early-game telemetry row carries the last death verbatim:

```cpp
sample.lastDeath = ...;   // the full "SimPveEarlyGameDeath identity=... body=..." line
```

so the analyzer now merges both sources, and the dashboard's `deathZones` array
corroborated all three sites independently:

```python
for s in ok:
    ld = (combat(s) or {}).get("lastDeath") or ""
    if ld.startswith("SimPveEarlyGameDeath"):
        line_by_body.setdefault(int(re.search(r" body=(\d+) ", ld).group(1)), ld)
```

Note what did *not* change: the assertion text. Reading the identical line from a
second source is an evidence-source change; loosening what must be true would
have been an amendment.

**The generalisable form:** for a long run, capture deltas per phase rather than
once at the end, and prefer evidence that exists in two independent places —
structured telemetry plus a log line — for anything a receipt depends on.

---

## Why this keeps happening here

The AI-economy layer has no unit-test seam worth the name: its behaviour needs a
live zone, a navmesh, real creature spawns and real combat. The acceptance bar is
therefore a live run, which makes the harness, the contract and the analyzer
**part of the product surface**. Three of the four failures above were in that
surface. Budget review effort for it accordingly — and when a run fails, ask in
this order: did the bot misbehave, could the bot have behaved, and was I able to
see it if it did?
