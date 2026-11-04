# Tutorial 0.9.20: A step that says "handled" owns the tick

G5 lets players heal roster PlayerBots, and lets Medic bots heal their group with real stims. The scoped harness
passed. The owner's client pass then found three defects in the bot's heal step. All three came from the two
answers that step gives its caller: *who* it decided about, and *whether it consumed the tick*. This tutorial walks
through them using the code as it shipped. It ends with the stock seam that made healing possible at all.

## 1. The step's return value is a contract

`SimHunterController::runGroupTick` is a chain of tiers. Each tier either handles the tick and returns, or falls
through to the next (`SimHunterController.cpp:3147-3155`):

```cpp
if (record.healOrderGeneration != 0 || groupHealPatientOid != 0) {
    if (runGroupHealTick(hunter, nowMs, record, group, ...))
        return;
}
if (runGroupSpin(hunter, nowMs, record, ...))
    return;
if (runGroupHealTick(hunter, nowMs, record, group, ...))
    return;
// Self-defence wins: never switch off a creature attacking this bot.
if (selfAttacker == nullptr)
    maybeAssistLeaderTarget(hunter, nowMs, record, group);
// ... member defence, directives, follow ...
```

`true` therefore means more than "I did something". It means "skip assist, group defence, directives and
follow". The automatic no-patient branch originally returned `true` after its "Can't reach" line. While a hurt
member stayed more than 32 m away, the medic re-ran that branch every tick and never reached `follow`.

The live log shows exactly that. Risu (2429) emitted `TRAVEL_ENGAGE` at 1793737760309. He then logged nothing,
not even the per-tick `GROUP_ASSIST_SCAN`, for 183 s, while the leader walked 318 m. The silence is the clue:
every later tier logs, so a tier earlier in the chain was returning.

The fix (`SimHunterController.cpp:2699`) keeps the message and gives the tick back:

```cpp
// Say so, but never hold the tick: a medic waiting on a patient
// it cannot reach stops following and defending the group.
postHealLine(line, noPatientOid, noPatientReason, true, -1);
}
return false;
```

Following the leader usually brings the medic back within reach, so not holding the tick also solves the
original problem.

**Rule:** in a tiered tick, return `true` only while the step is actually driving the bot (walking to a patient,
applying a stim, finishing an order). "I have nothing to do" is `false`, even when you have something to say.

## 2. Explain a miss only for someone who needs you

The first client-pass bug was spam: "Can't reach Dielopu Llico" about a puller with only a scratch. The chooser
recorded range and line-of-sight misses for any damaged member. The fix puts the need test before the reasons
(`SimBotHealing.h:286`, `chooseHealPatient`):

```cpp
// Range, refusal and sight only explain a miss for a member who needs a
// heal: a scratched puller out at 40 m is not "unreachable".
const float threshold = candidate.isSelf ? policy.selfHealThreshold :
    policy.groupHealThreshold;
if (!candidate.orderedTarget && !needsHeal(candidate.healthFraction,
        candidate.actionFraction, true, threshold))
    continue;
```

The result also names its subject. `PatientChoice::reasonIndex` points at the member the reason is about, so the
controller's chat line is about the right person. Before, the controller fell back to "the first damaged member".
The ordering of the final reasons matters too: `faction` wins only when no member who needs a heal was healable
(`!sawHealableNeed`). A refused stranger therefore never hides a reachable friend.

## 3. A named order is exclusive

"Paki, heal Towe" with Towe unhurt and someone else bleeding should answer "Towe isn't hurt.", not heal the other
member. The chooser now restricts itself to the ordered candidate when one is present:

```cpp
if (hasOrderedTarget && !candidate.orderedTarget)
    continue;
```

The case where Towe is not a candidate at all (left the group, other planet, dead) is a different fact.
`runGroupHealTick` checks for it before calling the chooser and ends the order as `patient_left`. Folding it into
`no_damage` would have reported "not hurt" about someone who isn't there. Both cases are pinned by pure tests
(`SimBotHealingTest.cpp`: `NamedOrdersNeverHealSomeoneElse`, `ExplainsMissesOnlyForPatientsWhoNeedAHeal`).
Putting this logic in a header with no engine types is what makes those tests a few lines long.

## 4. The stock seam: a ghostless player

Stock `isHealableBy` (`CreatureObjectImplementation.cpp:3691`) needs a `PlayerObject` ghost on both sides. A
roster-bot body has the PLAYER pvp bit but no ghost. G5 adds two narrow facts instead of a parallel faction table:

```cpp
const bool simBotHealer = healerAgent != nullptr &&
    healerAgent->getSimPlayerBot();
const bool simBotPlayerTarget = targetAgent != nullptr &&
    targetAgent->getSimPlayerBot() &&
    (getPvpStatusBitmask() & ObjectFlag::PLAYER) != 0;
...
bool thisIsPlayer = isPlayerCreature() || simBotPlayerTarget;
bool factionChecks = healFactionChecks(healerCreo, thisIsPlayer);
if (thisIsPlayer && !simBotPlayerTarget) { /* ghost-only checks */ }
```

The stock faction table therefore decides for bots exactly as it does for players. The client pass showed it
under the classic rules (`useCovertOvertSystem` off): a neutral Risu refused an Imperial combatant ("Can't help"),
and the combatant could still heal the neutral Risu. The harness pins the same table in `heal_faction_table`.

**Rule:** to extend a stock rule to a new kind of object, describe that object to the stock code in its own terms
(here, "a player with no ghost"). Don't copy the rule. A copy drifts the first time stock changes.
