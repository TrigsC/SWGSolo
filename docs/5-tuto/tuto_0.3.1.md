# Tutorial 0.3.1 — Liveness Guards: Designing Escapes for States That Can't See Themselves

*Level: Advanced · Focus: architecture (watchdog design, progress metrics,
suppression windows) · Grounded in the P.6.5e diff.*

## The core principle

A deadlock rarely looks like a deadlock from inside. Each of the two bots
frozen at Theed's collector was in a perfectly *valid* state: in combat,
defender alive and adjacent, waiting for the combat system to resolve. The
combat system was waiting for a hit that LOS would never allow. No component
was wrong; the *composition* was stuck. You can't fix that class of bug by
patching a component — you fix it by adding a **liveness guard**: an
observer that measures *progress*, not state.

## Choosing the progress metric

The guard is only as good as its definition of progress. P.6.5e uses "any
decrease in the current HEALTH/ACTION/MIND of either combatant"
(`SimPvpBotController::onTick`). Why exactly that?

- **State-based checks were already tried and have holes.** The P.6.2a
  phantom guard checks defender *state* (dead? gone? >72m?) — a live enemy
  at 0m passes every check forever. State describes *what is*; progress
  describes *what's changing*.
- **Decreases only.** Passive regen and heals *increase* pools; counting
  them would let a fight where nobody lands a hit look "alive" indefinitely.
  A DOT ticking counts — damage genuinely is landing.
- **Both sides.** Losing a fight is progress too; break only when *neither*
  combatant's pools move.
- **Re-baseline on defender swap** — a fresh engagement must never inherit
  the previous fight's stale clock.

Transferable rule: pick the narrowest observable that *must* change if the
system is healthy, and treat "unchanged for N× its natural period" as stuck.
(45s ≈ many combat rounds; compare the miner stuck-watchdog, which uses
position deltas for the same reason.)

## The second-order trap: escape loops

The first plan draft cleared combat and stopped. Codex's review caught the
consequence: the enemy is still alive three meters away, so the next target
scan re-engages, freezes again, and the "escape" becomes a 45-second
metronome. **Every liveness escape needs to answer: what stops the system
from immediately re-entering the state it escaped?**

P.6.5e's answer is a *scoped suppression window*: remember exactly one OID
(`stalemateIgnoredOid`) for 20 seconds, and skip only it, only in the bot's
own `scanForTargets`. The engine's defender path is deliberately unfiltered
— if the ignored enemy actually lands a hit, that's real combat with real
progress, and the fight resumes legitimately. Suppress the *loop*, never
the *gameplay*.

## Lifecycle discipline (where the review earned its keep twice more)

- **Config that can be disabled must stay disablable.** The parse
  `raw <= 0 → 0 else clamp(15, 300)` exists because the codebase's habitual
  `clampMinerInt` treats out-of-range as "retain current" — a Lua `0` would
  have been silently ignored, making runtime disable impossible. Check your
  clamp helpers' zero semantics before reusing them on an off-switch.
- **Reset state at every teleport — but at the right granularity.** Leaders
  reset via `prepareForRelocation`; calling that on members would bump their
  work-loop generation and orphan the tick chain that drives `assertFollow`
  (a P.6.1-class regression). The fix is a scoped `resetStalemateState()`
  used by both paths — one helper, two call sites, no drift.

## Where to look in the diff

- `SimPvPController.cpp` — the progress tracker inside the existing
  phantom-guard block of `onTick` (note the shared read choreography), the
  ignore check in `scanForTargets`, `resetStalemateState()`.
- `SimPlayerManager.cpp` — zero-preserving parses; `pvpCollectorJitterOffset`
  (golden-angle per-squad offset — deterministic so route replans are
  stable, cell-local-only indoors because world deltas are wrong under
  building rotation); the member-boarding scoped reset.
