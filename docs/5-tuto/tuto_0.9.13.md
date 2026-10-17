# Tutorial 0.9.13 — Derived state: what a bot wields is a function, not a record

F_0.9.12 lets a roster bot fight with the weapons in its vault. The obvious design stores "this item
is equipped" on the item row and moves the object onto the body. This release does neither, and the
reasons are the whole lesson.

## 1. Never put a durable object on a mortal body

A PlayerBot body is destroyed on every death and every drain. An object in its slot is destroyed or
orphaned with it, the same failure class as the P.4.4 vehicle bug, where an object tied to an AiAgent
outlived its owner's container.

So the real item never moves. `equipPlayerBotWeaponLocked` (`SimPlayerManager.cpp:9063`) creates a
fresh weapon from the item's template, runs the shared `AiAgent::setWeaponStats()`, and then
overwrites the stats with the item's own through `applySimBotLoadoutWeaponStats` (`:9045`).

The order matters. `setWeaponStats` is shared by every NPC and scales damage by mob level, so the
roster-only correction must come *after* it, in sim-layer code, never inside the shared AI
implementation. F_0.9.6 set that rule for template stats; this release extends it to item stats.

The vault row stays `held`. The body holds a copy, and losing a copy costs nothing.

## 2. Derive, don't store

There is no `equipped` state. `selectPlayerBotLoadout` (`:8864`) recomputes the choice from three
inputs at every spawn and reconcile:

- the vault contents (bag snapshot, then each item under its own lock);
- the certifications of the trained boxes (`collectPlayerBotCertificationsLocked`, `:8670`);
- the training state (the next box's XP type, or "any" once the plan is complete).

The pure decision lives in `SimBotLoadout::chooseWeapon` (`SimBotLoadout.h:140`), with no locks and
gtests beside it.

A stored flag would need a crash table, as F_0.9.9 and F_0.9.10 did. A derived value cannot
disagree with its inputs, so the crash table is empty. The one piece of memory kept, the recorded
`currentLoadoutSourceOid`, is only there for hysteresis (`:168`) and to decide whether a re-equip is
needed. Losing it at boot costs one extra re-equip.

**Lesson:** before persisting a state, ask whether it can be recomputed from state that is already
durable. If it can, persisting it only adds a way to be wrong.

## 3. Certification is parity work the engine never did

`CombatManager` caps an uncertified *player* at 5–10 damage, but it tests `isPlayerCreature()`
first, and `WeaponObject::isCertifiedFor` needs a ghost. An AiAgent is never checked. Parity
therefore has to live in the choice: a bot only ever picks what its boxes certify (`cert_*` entries
of `Skill::getAbilities()`, the same list F_0.9.7 reads for attacks).

The class rule matters as much as the stats. A brawler funding unarmed boxes who picks up a better
knife would start earning one-hand XP and stall its ladder. Selection follows the XP type of the
next box until `templateComplete`. Not `graduated`: that flag is set at a line depth, long before
the plan ends.

## 4. What live verification taught

The harness passed 24 of 25 scenarios. The production stage then found what no scenario could:

- **Inherited filters encode old assumptions.** The reconcile loop skipped anything not grinding,
  because the early-game loadout only mattered to grinders. Bot 274 hunts. It would have kept its
  spawn weapon until it died. With the gate on, the loop now covers every activity (`:16757`).
- **Two correct keep rules can be wrong together.** The buy list kept the *oldest* knuckler and the
  loadout kept the *best*. When both were acquired in the same second, the bot kept both, forever.
  The buy-list keep now counts protected items first (`:12594`).
- **A retry needs a durable reason.** A full-sweep mismatch deferred by combat was forgotten until
  the next sweep, 30 minutes later. Queuing a mismatch now marks the identity dirty (`:16815`), so
  the next ordinary tick retries.

After those fixes, bot 274 equipped its knuckler (130–280, the item's exact stats) 41 seconds after
the gate was armed. Its next kill paid 2,580 unarmed XP: the copy fights, and it pays into the box
the ladder is funding.
