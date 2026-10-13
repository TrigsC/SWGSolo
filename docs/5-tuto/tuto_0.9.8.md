# Tutorial 0.9.8 — A transition that books no tick is a dead controller

This release fixed the same bug four times, in four places, weeks apart. Each fix looked
local. The principle behind all four is worth more than any one of them: **in a
self-scheduling controller, liveness is a property of every exit path, and the timeout that
is supposed to rescue you runs on the same clock you just stopped.**

## 1. How the hunter controller stays alive

`SimHunterController` has no periodic timer. It runs when something schedules it:

```cpp
// SimHunterController.cpp:281
void SimHunterController::scheduleActiveTick(int delayMs) {
	...
	uint64 gen = activeTickGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
	...
	Reference<SimHunterActiveTickTask*> task =
		new SimHunterActiveTickTask(this, gen);
	task->schedule(delayMs);
}
```

Each call bumps `activeTickGeneration` and tags the new task with it. A task whose
generation is stale when it fires does nothing, so only the latest booking runs
(single-flight). `runActiveTick` does the phase's work and is expected to book the next
tick before it returns. The other way in is a movement callback: `onArrived` and
`onPathFailed` also call `scheduleActiveTick`.

Two facts follow, and the bug lives between them:

1. A phase can legitimately return without booking a tick, if it has just issued a move
   and an arrival callback will wake it.
2. Phase timeouts (`phase_timeout_HUNTING` and the rest) are checked at the top of
   `runActiveTick`.

If the move never produces a callback, fact 1 leaves no tick booked, and fact 2 means the
timeout never fires either. The bot stands still until the server restarts. Nothing logs
an error, because nothing is running.

## 2. Four instances

| Commit | Where | Why no callback came |
|---|---|---|
| `faf170c374` | `engageMissionLair` | only one of its branches rearmed the tick |
| `cd67ef6951` | every `BUFF_UP` exit | provider paths issued a move and relied on arrival |
| `86e6ff1621` | start of each overland leg | `setPhase(...); moveTo(...)`, so travel combat, which lives in the tick, never ran during the walk |
| `4b37e62ed8` | `HEALING` resume | `moveToTarget()` / `scanForTarget()` book nothing on an empty field |

The last one is the cleanest example. A bot finished resting at a lair whose creatures had
all died or wandered off:

```cpp
// before
setPhase(HUNTING);
if (targetOid != 0) moveToTarget(); else scanForTarget();
```

`scanForTarget` found nothing and returned. No move, so no arrival. No tick. Castu and Paki
stood in the wild for five hours, thirty minutes into a run.

## 3. The guard idiom

Do not try to enumerate which callee books a tick on which branch. Ask the one question
that matters, after the fact:

```cpp
// SimHunterController.cpp, HEALING resume
const uint64 tickGenerationBefore =
	activeTickGeneration.load(std::memory_order_acquire);
setPhase(HUNTING);
if (targetOid != 0) moveToTarget(); else scanForTarget();
if (activeTickGeneration.load(std::memory_order_acquire) ==
		tickGenerationBefore)
	scheduleActiveTick(1000);
```

`scheduleActiveTick` always bumps the generation, so "unchanged" means exactly "nobody
booked a wake-up". The guard cannot shorten a cadence a callee chose, because a callee that
chose one has already bumped it.

The `BUFF_UP` version (`SimHunterController.cpp:4170`) carries a comment explaining why the
guard is **not** conditioned on still being in the same phase: the callee may have moved to
another phase and issued a move there, and a phase-scoped guard would hand the silence to
that phase and disable its timeout instead. Test the generation, not the phase.

For the overland legs the fix is blunter: `armTravelCombatTick()` books a 1 s tick at each
of the six leg entries, because travel combat needs the tick during the walk, not only at
its end.

## 4. Why review kept missing it

Each site reads correctly in isolation. `setPhase(X); moveTo(p); return;` is the normal
shape of half the controller. Whether it is safe depends on a fact that is not on the
page: does this move, from this state, always end in a callback? Three things in this
codebase break that assumption:

- the base mover holds a bot that is in combat, so a move issued while being hit never
  starts and never arrives;
- a scan that finds nothing issues no move at all;
- a callee can change phase and return early.

The evidence that finally exposed each instance was the same: a per-bot **last-event age**
in `missiondiag.log`. A dead controller is silent, so the signal is an absence. Every
status check in this release line ended up including "longest quiet stretch per bot" for
that reason.

## 5. A second, smaller lesson: copy the stock algorithm before improving on it

Bot missions failed to place a lair on about 80% of terminal visits. The bot path was
stricter than the stock game: it required the advertised point itself to be spawnable at
the terminal. Stock checks only bounds, water and city there, and finds the real point on
arrival. The fix was to do what stock does, with the search as a pure helper:

```cpp
// SimBotMissionPlacement.h:53
inline float revealSearchHalfWidth(int tryIndex) {
	...
	float halfWidth = REVEAL_BASE_HALF_WIDTH;          // 128 m
	for (int block = tryIndex / 32; block > 0 &&
			halfWidth < REVEAL_MAX_HALF_WIDTH; --block)
		halfWidth *= 2.f;                              // doubles every 32 tries
	...
}
```

One deliberate difference: stock spawns at its last point even when the search fails. The
bot path defers instead, bounded by `lairRevealDeferMaxCycles`. In the release run the
search never needed more than 22 of its 256 tries.

## Takeaways

- In a controller that schedules itself, audit **exits**, not entries. Every return from a
  transition must leave a tick booked or a callback guaranteed.
- A generation counter makes that checkable in three lines without knowing the callees.
- Put the watchdog on a different clock from the thing it watches, or accept that it
  cannot catch this failure.
- Measure silence. Per-entity last-event age finds dead controllers; error logs do not.
