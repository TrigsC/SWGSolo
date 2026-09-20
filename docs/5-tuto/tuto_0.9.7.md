# Tutorial 0.9.7 — Two names for one command, and other things only a live probe finds

This release's core principle is uncomfortable: **the code that derives a bot's
abilities was correct at both ends and still produced almost nothing.** Every
defect below passed a warning-clean build, seven unit tests and multiple rounds
of code review. A live probe found them in the first ten minutes.

## 1. The bug that a reviewer cannot see

Derivation reads a trained skill's granted commands and resolves each through the
object controller:

```cpp
// SimPlayerManager.cpp, buildBodyProgressionSnapshot
const Vector<String>* abilities = skill->getAbilities();   // from skills.iff
...
const CombatQueueCommand* combatCommand = cast<const CombatQueueCommand*>(
    objectController->getQueueCommand(command));
```

Both halves are right. `getAbilities()` returns exactly the `commands` column
`skills.iff` carries, and `getQueueCommand` is how every other caller resolves a
command. The problem lives between them:

- `skills.iff` spells commands in **camelCase**: `unarmedHit1`, `melee2hHeadHit1`.
- The command registry is keyed by the **lower-case** name Lua registers
  (`bin/scripts/commands/commands.lua` → `unarmedHit1.lua` defines
  `name = "unarmedhit1"`).
- `ObjectController::getQueueCommand` hashes the exact string it is given.

So every hit, lunge, stun, blind and spin resolved to `nullptr` and was filed as
`excluded:unknown_command`. What survived were the handful of commands whose
skills.iff spelling is already lower case: `intimidate1`, `taunt`, `warcry1`,
`aim`. A brawler master derived **3** abilities instead of 23 — and the feature
still "worked", because Intimidate is in that surviving set.

The fix is one call:

```cpp
// skills.iff is camelCase; the registry and creature attack lists are lower case,
// and getQueueCommand hashes the exact string.
const String command = abilities->get(j).toLowerCase();
```

The lesson is about **where** it was caught. The plan's live-derivation probe
existed to answer a different question ("which boxes grant which commands?"), and
the inspection endpoint was built to be **gate-independent** — it reports what a
bot's boxes *would* grant even while the capability is off:

```
== 466 brawler  src template  reason capability_gate_off
  derived: ['taunt', 'warcry2', 'intimidate2']
  excluded: ['unarmedHit1:unknown_command', 'melee1hHit1:unknown_command', ...]
```

That `excluded` list, with its reasons, is the whole diagnosis. If inspection had
only reported *installed* abilities, or had refused to derive while gated off, the
probe would have shown an empty list and told us nothing.

## 2. Parity means paying the same costs, not just doing the same things

`CombatManager::applySpecialAttackCost` has a branch worth knowing about:

```cpp
if (attacker->isAiAgent()) {
    ...
    // Preserve stock NPC behavior for HAM costs; this change scopes only
    // the previously skipped Jedi weapon Force cost.
    return true;
}
```

Every NPC special in this codebase is **free** in Health/Action/Mind. A player's
is not. Letting bots keep specials for free would have been exactly the sort of
advantage the P.10 line exists to avoid, so the player arithmetic moved into a
shared function and skill-derived agents alone pay it.

Then the reserve — the rule that a special's own spend must not push a pool below
a threshold — shipped like this:

```cpp
bool reserveRefused =
    ham(HEALTH) - health < reserve * maxHam(HEALTH) ||
    ham(ACTION) - action < reserve * maxHam(ACTION) ||
    ham(MIND)   - mind   < reserve * maxHam(MIND);
```

Read it with the unarmed default weapon in mind: its base attack costs are
`0/0/0`, so `health`, `action` and `mind` are all zero. The expression then says
"refuse this special if the bot is *already* below 40 % in any pool" — which has
nothing to do with what the special costs. Live, a wounded novice refused **186
specials in a row** and dealt no damage until its budget expired. The fix is to
test only the pools with a non-zero cost:

```cpp
(health > 0.f && ham(HEALTH) - health < reserve * maxHam(HEALTH)) || ...
```

A condition that is trivially true for a free ability is a bug that only shows up
when something is free *and* something else is low.

## 3. Two components with different definitions of "valid"

The stock attack tree (`bin/scripts/ai/default.lua`) is a sequence:

```
SelectAttack → CheckAttackInRange → CheckAttackIsValid → EnqueueAttack
```

`CheckAttackIsValid` calls `validateStateAttack`, which refuses a special whose
state cannot apply — including `POSTUREDOWN` while the target's posture-down
recovery is running. The scorer in `selectSpecialAttack` has its own state
heuristics, and they do not match: it treats a lunge as a knockdown and consults
`checkKnockdownRecovery`, while the lunge's actual effect is POSTUREDOWN.

Neither is wrong on its own. Together they livelock: the scorer keeps choosing
`unarmedlunge1` (score ~30, versus 10 for a plain hit), the tree rejects it and
aborts the tick, and the next tick makes the identical choice. A live scenario
burned its entire 600-second budget without landing one attack.

The fix is not to rewrite the scorer's heuristics — that would change every wild
NPC's behaviour — but to make skill-derived selection agree with the gate that
will judge it:

```cpp
// The attack tree runs CheckAttackIsValid after SelectAttack and aborts the tick
// when the chosen special's state cannot apply.
if (command != nullptr && targetCreo != nullptr &&
        !validateStateAttack(targetCreo, key.hashCode()))
    continue;
```

**When you add a chooser upstream of an existing validator, the chooser must ask
the validator's question, not a similar one.**

## 4. Policy that the scorer must not get to overrule

The owner's requirement was specific: a novice learns Intimidate at Novice
Brawler, it costs nothing, so the bot should use it. Two designs failed live
before the third worked.

- **First**: on a missed special roll, if any free ability with a missing state
  exists, call `selectSpecialAttack(-1)`. But with a zero-cost weapon *every*
  special is free, and handing the decision back to the scorer got a damaging
  lunge — not Intimidate. Worse, the lunge's posture effect is essentially never
  held by the target, so the "state missing" test stayed true and forced a special
  on every single swing: 56 forced specials, zero auto-attacks.
- **Second**: restrict candidates to non-damaging commands
  (`getPoolsToDamage() == CombatManager::NONE`) and select the qualifying command
  **by name** with `selectNamedSpecialAttack`, plus a per-target back-off so a
  resisted Intimidate does not re-fire every tick. Correct, but still checked only
  on a missed roll — so the harness scenario, which sets the special chance to
  100 %, never reached the check and never used Intimidate at all.
- **Shipped**: check it **before** the roll.

```cpp
// A free control ability the target lacks is used first, before the special roll,
// then not forced again on that target for the retry window.
if (agent->getSkillAbilityPreferFreeState()) {
    String freeState = agent->findFreeStateAbilityForTarget();
    if (!freeState.isEmpty() && agent->selectNamedSpecialAttack(freeState)) {
        agent->noteFreeStateAbilityForced();
        ...
        return SUCCESS;
    }
}
bool wantSpecial = System::random(99) < agent->getSkillAbilitySpecialChance();
```

The final run: 23 Intimidates, 1 569 subsequent attacks against an intimidated
target, and 139 auto-attacks against 127 specials — the bot is not spamming.

## 5. The instrument that lies by omission

`assertAbilityUse` read the per-body ring of recent ability uses. The ring holds
`usageHistorySize` entries (12). A 22-second fight enqueues far more than 12
attacks, so the single early Intimidate had scrolled out by the time the assertion
ran — and the scenario failed while the behaviour was perfect.

The ring is still the only place a command, its source and the target's state stay
*correlated*, so it stays. What was added is lifetime evidence that survives
eviction:

```cpp
VectorMap<String, uint64> commandCounts;
// OR of every recorded targetStatesBefore; survives ring eviction so an
// early Intimidate in a long fight is still provable.
uint64 targetStatesSeen = 0;
```

The assertion now tries the ring first and falls back to those aggregates for a
command-only or state-only check — while a step naming *both* still requires one
matching ring entry, because only the ring can prove they co-occurred. Know what
your evidence store forgets, and assert against what it keeps.

## 6. What this cost, and why it was worth it

Eight defects, all found by live verification rather than review, across a run
that also produced an unexplained SIGSEGV in unrelated harness code (documented as
open in the release notes after it refused to reproduce in ~400 kills). None of
them were visible in the diff.

The transferable part is the ordering: **build the read-only, gate-independent
view first, then probe with it before enabling anything.** Every defect above was
diagnosed from the inspection endpoint or the counters it feeds — not from reading
the code that caused them.
