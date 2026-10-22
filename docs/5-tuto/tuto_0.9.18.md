# Tutorial 0.9.18: Boundaries carry bugs

G3 lets a group leader run destroy missions with PlayerBots ("grab 2 bol missions to the W"). Almost
every defect that the owner's client test found sat on a boundary between two conventions, two
threads, or two owners of the same decision. This tutorial walks through five of them, using the code
as it shipped.

## 1. Two angle conventions meet in one function

The chat parser (`SimBotGroupRally.h`, `parseMissionRequest`) produces a **compass** bearing: 0 is
north, angles grow clockwise, and W is 270. The placement code (`SimPlayerManager.cpp`,
`choosePveBotMissionPosition`) works in **math** angles: 0 is east, angles grow counter-clockwise,
and the position is `cos(angle)`, `sin(angle)` from the city centre. The first build passed the
compass value straight into placement, so "SE" (135) was placed at 135 degrees math, which is
north-west.

The fix converts once, at the boundary: `math = 90 - compass`. The lesson generalises: when two
modules disagree on a unit or a frame, convert at the single point where data crosses between them,
and name the variables after the convention they hold (`wantedMathBearing`).

## 2. Unsigned meets negative

After the conversion, "west" still went north-east. West is `90 - 270 = -180`. The jitter was
`System::random(91) - 45`, and `System::random` returns an **unsigned** int. In
`-180 + random - 45` every operand is promoted to unsigned, the sum wraps at 2^32, and `% 360` then
works on a huge positive number. Because 2^32 mod 360 is 256, every negative bearing landed between
31 and 121 degrees. South (180 compass, so -90 math) also went through this path, but the harness
scenario that tested "to the s" happened to pass anyway, which is why the bug survived a live run.

The fix computes the jitter as a signed int first:
`static_cast<int>(System::random(n)) - tolerance`.

Diagnosis took one log line. The grab event now records the parsed bearing:
`grab=1 ... hasBearing=true bearing=270`. With that, the parser was cleared at a glance and the bug
was pinned between the request and the placement call.

## 3. One decision, one decider

Players already have a "Nearest mission for group" waypoint, kept by the stock
`UpdateNearestMissionForGroupTask`. G3 could have computed "nearest" a second time for the bots. It
does not. The stock task gathers player missions **and** bot missions
(`getGroupBotMissionTargets`), picks the winner by its own rule, and publishes it
(`publishGroupNearestMission`). The bots travel to the published winner, so the waypoint and the bots
can never disagree.

The stock task holds the group lock, so its gate check must be lock-free (an atomic). The harness
enables missions per identity, and checking that needs the parity mutex. The function is therefore
split in two:

- the stock entry point, `publishGroupNearestMission`, checks the atomic only;
- the manager's own refresh has already checked the per-group gate with no object lock held, so it
  calls the inner `applyGroupNearestMission` directly.

Merging those two paths once silently discarded every harness winner.

## 4. A wake-up is part of an order

The leader said "get buffs and meet at the Mining Outpost", and the bots planned it 169-201 seconds
later. Planning itself is instant. The bots, though, had their next tick booked minutes ahead by a G1
route hold (`scheduleActiveTick(holdMs)`), and nothing woke them. `issuePlayerBotGroupDirective` now
calls `notifyGroupingStateChanged()` after releasing `groupingMutex`.

This is the same rule as F_0.9.8 ("a transition that books no tick is a dead controller"), seen from
the other side: whoever changes what a controller should do must also make it look.

## 5. Reuse a flow, not a fragment

The first G3 build reused one call from the solo mission flow, `engageMissionLair`, inside the group
tick. The F_0.9.8 tactics live in the phase machine around that call: the reveal rules, the travel
combat, the wave holds. So the holder walked straight in and attacked.

The owner's rule settled it: G3 adds no tactics, because G4 owns them. The shipped code takes the
fragment out entirely. The group holds short (the G2 rule), the holder applies the F_0.9.8 **reveal**
rules in full (DEFER retries every 2 s, a bounded NO_SPAWN count, abandon frees the offer) and leads
to the real spawn, and the other bots only defend and assist.

**Lesson:** reuse a whole flow or none of it. A fragment of a flow carries its effects without its
guards.

## Process note: scoped receipts

G2's 19-hour verification is what produced the harness controls (`onlyScenarios`,
`continueOnFailure`, `maxRunMinutes`, `failClass`). G3's receipt ran 16 scenarios, re-ran only what
failed, and labelled world interference as environment. Even so, half of the failures were wrong
scenario expectations or harness bugs. Read a failure as a question about the test before reading it
as a verdict on the code.
