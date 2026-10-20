# Tutorial 0.9.16: A new mode must pass every old gate

F_0.9.15 gave roster bots a new way to exist: inside a player's group. The pure policy, the offer
state machine and the lock choreography were planned through six review rounds and mostly worked.
Nearly every live defect had one shape. Existing code asked "is this bot doing one of the things I
know about?", and the answer for a grouped bot was no. A new mode is not finished when its own code
works; it is finished when every gate the old modes pass also lets it through.

## 1. The gate you never wrote: `shouldResumeHybridTravel`

The follow tick computed a slot beside the leader and called `moveTo(slot)`. The bot never moved.
The dashboard said `GROUP_FOLLOW`, the mover said `IDLE`, and nothing failed. A cell-nav trace
routed to the bot (the `traceGroupBot` harness op) showed the truth: 58 path nodes found, then
`PATH_REJECT reason=hybrid_resume_cancelled`.

The hunter's override (`SimHunterController.h:553`) admits a hybrid path only while there is a
reason to travel: an active order (and not cleaning up) or the early game. The guard exists because
a path request can still be in flight when an order ends, and must not revive movement toward a
finished target. Grouping deliberately suspends both reasons, so it suspended movement too. The fix
adds `isGroupPhase()` to the list.

**Lesson:** when you add a mode that moves a controller, grep for every predicate that decides
whether movement is legitimate (`shouldResumeHybridTravel`, `isMovementHeld`,
`allowsMovementInCombat`, the phase TTL table). A guard that fails closed fails silently.

## 2. Don't re-path just because the mover is idle

The second follow bug looked like slowness: a bot crawled 16 m in 20 s. The tick re-issued
`moveTo` whenever the mover was not `MOVING`, and the mover passes through `IDLE` between path
nodes. `moveTo` advances the work-loop generation and discards the in-flight path; the hunter's own
pursuit code (`moveToTarget`, "Finding 4") already warned about this.

The rule now (`SimHunterController.cpp:920-928`) re-paths only for a new slot, a slot moved more
than `repathMoveMeters`, or a real stall: the bot is far from its slot, `IDLE` or `WAITING`, with no
path for 8 s. Two later bugs came from the same branch:

- the owner's client showed bots parked in `WAITING` for 13 minutes (the stall rule only accepted
  `IDLE`);
- a bot arriving from a cross-planet leg waited forever, because "stalled" needs a previous path
  and the trip had reset the follow clock (`!haveTarget`).

**Lesson:** a "should I move?" rule has three inputs (target moved, mover state, time since the
last request). Enumerate the mover's states, including the ones another subsystem leaves it in.

## 3. One meaning per phase, or a bit to tell them apart

The owner chose "a grinding novice is free between fights". The policy honoured it for the
early-game phase. But the early game runs under the hunter's `HUNTING` phase, which the availability
switch classifies as busy, so no grinding novice ever answered LFB. The snapshot
(`SimBotGrouping.h:215`) gained an `earlyGameActive` bit. It is set only through
`setEarlyGameActiveFlag` (`SimHunterController.cpp:284`), which updates the field and the bit
together. The classifier checks that bit before the phase switch (`SimBotGrouping.h:207`).

The snapshot itself is the pattern worth keeping. The manager never reads controller fields or agent
state. Each writer CASes only its own bits (`updateGroupingAvailabilityBits`, `:254`), so a combat
publisher and a phase change never overwrite each other.

**Lesson:** when two modes share one enum value, the value no longer carries the decision. Add the
missing bit at the writer, not a guess at the reader.

## 4. Two client paths to one feature

Every harness scenario passed. On the owner's first real-client pass, `status` and `leave` did
nothing. The hook sat in `ChatManagerImplementation::handleGroupChat`, which serves `/g`. The
client's Group tab posts a room message to the group's own chat room through
`handleChatRoomMessage` (`ChatManagerImplementation.cpp:766`), the same path as LFB. The harness
called the manager entry point directly, so it could not tell the difference.

**Lesson:** harness ops that call your manager prove your manager, not the route into it. For
anything a player types, a real client is part of the verification contract.

## 5. Lock order when you kill something yourself

The harness XP step dealt the killing blow with only the victim locked. `notifyDestruction` then
cross-locks the destructor; its retry loop (unlock victim, try destructor, relock) spun forever
against queued writers. The REST workers piled up behind the bot's lock, and the server hung two
hours into a verification boot. A GDB dump of all 123 threads named the step.

Stock `KillCommand` holds the killer locked and cross-locks the target. The harness now does the
same (`SimPlayerManager.cpp:25076`).

**Lesson:** "match the nearest existing caller's lock choreography" applies to test code too, and
the nearest caller for "kill this creature" is `KillCommand`.

## 6. Small things that broke loudly

- `replaceGroupingToken` (`SimPlayerManager.cpp:11065`) took the tail with `subString(index +
  token.length())`. For `"I'm free, invite {name}"` that index equals the length, which is the
  engine3 boundary that throws. One reply line in four silently killed its task.
- After `/kick` the bot no longer points at its group, so the reconcile read "no group" as "leader
  left". It now reads the recorded group (`:62664`), and `judgeRelease` (`SimBotGrouping.h:853`)
  judges membership first.

Both were found only by running the code: once by a harness exception log, once by a release
reason that did not match the scenario.
