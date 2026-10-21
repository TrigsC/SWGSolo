# Tutorial 0.9.17: Decide where the facts are

G2 lets a group leader send grouped bots to buff and rally ("get buffs and meet at the Mos Eisley
cantina"). The interesting part is not the parsing. It is deciding *which thread, at which moment,
with which facts* makes each decision. Nearly every review finding and live defect came from a
decision made where its facts were stale.

## 1. Issue in the chat task, plan on the bot

The chat task (`onPlayerBotGroupChatTask`) knows the text and the leader. It does not know the bot's
buffs or position without locking a body from a thread that holds none of the controller's state.
So it only resolves the place and calls `issuePlayerBotGroupDirective`, which stores a directive in
`PLANNING`.

The bot's own tick (`runGroupDirectiveTick`, `SimHunterController.cpp`) then plans:
`computeBuffNeeds`, its live position, and `planPlayerBotGroupDirective` with a compare-and-set
from `PLANNING`. A dead bot never plans, because its tick returns in the prologue, and
`preparePlayerBotGroupingDeath` resets any directive back to `PLANNING`. The clone re-plans from the
cloning facility with empty buffs, instead of reusing the corpse's city.

**Lesson:** issue intent where the request arrives; decide where the facts live.

## 2. Test the context, not the phase

The group buff reuses the solo provider flow (`moveToNextPveBuffProvider`,
`finishPveBuffProviderFlow`). The first draft branched on `phase == GROUP_BUFF`. Codex found that
the fee reservation and the "nothing needed" exit happen before the phase is set. Live verification
then found the opposite case: a release followed in the same second by `startOrder` moved the phase
to a solo phase, so the bot kept walking providers for a group that no longer existed.

The fix is a context flag, `groupBuffActive`, set as the first statement of `beginGroupBuff` and
cleared only by `finishGroupBuff` and `cancelGroupBuff`. Every branch tests the flag; `startOrder`
and every solo tick call `cancelGroupBuff`.

**Lesson:** a phase says what the controller is doing now; it does not say which flow owns the
shared state. Give the flow its own flag.

## 3. Version anything a callback can outlive

A shuttle leg can be in flight when the leader issues a new directive. Its finish callback must not
advance the new route. `PlayerBotGroupRoute` carries a `routeEpoch`. `startGroupTravelLeg` records
the epoch before starting either kind of leg, because a rejected start fires the finish callback
synchronously, inside the start call. The callback advances only by compare-and-set on
(generation, epoch). Invalidating a route rotates the epoch instead of dropping the record, so the
10-minute shuttle suppression survives.

**Lesson:** if a callback can outlive the request that created it, give the request an identity
the callback must present.

## 4. Fixed targets do not move

Follow logic has a HOLD band: a bot within `followMaxMeters` of a moving leader waits for the leader
to move. `driveGroupTravel` reused it for a rally slot. A fixed point never moves, so a bot that
stopped 6-10 m from its slot held there for 24 minutes, silently. The fix counts the follow radius
as arrival for directive targets, and the new `GROUP_DIRECTIVE_MOVE` event made the next problem
visible in one log line instead of an hour of inference.

**Lesson:** when you reuse a mode for a new kind of target, re-check every branch's assumption
about whether the target changes.

## 5. A test fixture is a live object too

Most verification time went to the harness, not G2. The stand-in leader is a stock AiAgent. After a
teleport it kept leashing home, a leashing agent is not attackable, it outran following bots, it
despawned on a long trip with no player near, and with its level-178 weapon it killed the bot it was
leading. Each looked like a different flake until a self-describing failure reason
(`standInAttackable=false`) named the shared cause.

**Lesson:** a fixture built from production objects inherits all their behavior. Neutralize it
explicitly (home, movement state, weapon, despawn), and make failures report the state that decided
them.
