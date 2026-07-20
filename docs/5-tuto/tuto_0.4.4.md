# Tutorial 0.4.4 — Split-Brain NPCs: When the Controller Moves but the Tree Fights

*Level: Advanced · Focus: Core3/engine3 internals (AiAgent behavior events, the
IDLE-override tree pattern, combat-permission masks) · Grounded in the P.8.3 diff.*

## The core principle

This fork's SimPlayers are a **split-brain** design: a C++ controller
(`SimHunterController`) owns *movement*, while the stock engine behavior tree
owns *combat*. That split is the source of both the power (a controller can plan
overland routes the stock wander tree never could) and the subtlest bugs in this
release. The lesson: when you take over half of an engine subsystem, you inherit
responsibility for the coordination points you just severed.

## Why the tree still runs at all

The miner tree (`bin/scripts/ai/simMiner.lua`) is fully no-op — it overrides
*both* the root and IDLE slots so `GeneratePatrol` can never wander a stationed
miner off its resource. But a hunter must *fight*, so `simHunter.lua` overrides
**only** the IDLE slot:

```lua
-- simHunter.lua: only IDLE is overridden; root falls back to rootDefault,
-- so the attack/target/equip/kill sockets stay live.
idleSimHunter = { {id="...", name="Wait", pid="none", args={duration=3600.0}} }
addAiTemplate("idleSimHunter", idleSimHunter)
```

`AiMap` resolves each socket by name (`idleSimHunter` exists; `rootSimHunter`
does not → falls back to `rootDefault`). So movement is the controller's, combat
is the tree's. `simPvp.lua` uses the exact same trick — and the working PvP bots
are the reference implementation this release kept measuring against.

## Coordination point #1: waking the tree

Here is the trap. The AI fires attacks only when its `AiBehaviorEvent` runs and
re-evaluates into the combat socket. That event is *scheduled*, and the IDLE
override schedules it `Wait(3600)` — an hour out. So an idle hunter that suddenly
acquires a target sits on a stale, hour-long schedule and **aims without
firing**, sometimes for minutes. Nothing is "wrong"; the event just hasn't run.

The fix is one line the controller was missing — and the working PvP controller
already had it (`SimPvPController.cpp:842`):

```cpp
// SimHunterController::engageTarget, after startCombat + setTargetObject:
agent->activateAiBehavior(true);   // reschedule the behavior event NOW
```

Transferable rule: **an event-driven subsystem only acts when its event fires.**
If you change *what* it should do (a new target) without rescheduling *when* it
next runs, you've queued an intention the system won't honor until its old timer
expires. Grep discipline pays off here — `grep activateAiBehavior` over the two
sibling controllers is exactly what surfaced the omission.

## Coordination point #2: combat permission is a mask, not a wish

Making creatures *retaliate* wasn't about the controller at all — it was a data
gate. `AiAgentImplementation::inflictDamage` only adds an attacker to the
defender list (the retaliation trigger) if `attacker->isAttackableBy(defender)`,
and that check hard-rejects any target whose `pvpStatusBitmask` lacks
`ObjectFlag::ATTACKABLE`. The hunter shipped with `PLAYER` only, so combat was
one-sided — the creature literally could not "see" the hunter as a valid enemy.
Adding `ATTACKABLE` fixed retaliation but opened a new door: now *players* could
attack the neutral bot too. The scoped fix lives in the same method
(`AiAgentImplementation.cpp:6579`):

```cpp
// Neutral sim bots read like neutral players: creatures may attack them,
// real players may not.
if (getSimPlayerBot() && getFaction() == 0 && creature->isPlayerCreature())
    return false;
```

Note *where* it lives: not in the hunter controller, but in the engine's shared
permission method — because "who may attack whom" is an engine-wide invariant,
and the controller has no say in it. Put the guard where the decision is actually
made, scoped tightly (`getSimPlayerBot() && faction 0`) so no other AiAgent's
behavior changes.

## The takeaway

When you split an engine subsystem across a custom controller and the stock
tree, three things become *your* job: (1) the socket-override pattern that
decides which half the tree keeps, (2) re-scheduling the tree's event whenever
you hand it new intent, and (3) the shared data gates (bitmasks, faction) that
the tree consults but the controller must set. Miss any one and the bot ends up
in a perfectly valid state that simply never acts — the recurring signature of
this whole release.
