# Measurement — is the starport pad navmeshed?

**Answer: YES. Unambiguously, at both ends of every leg.**

```
16 of 16 traversal path requests:  navAtBot=1 navAtTarget=1
```

Across three buildings, both ingress and egress legs, no exceptions.

- **Run ID**: `20260825-084524-navmesh-probe`
- **Config**: hybrid movement OFF (measured harmful on 2026-08-25), traversal +
  harness on, so the bot behaves exactly as in the 11-PASS baseline.

## Evidence

```
building=1106368 cell=1106372 navAtBot=1 navAtTarget=1 botPos=(3527,-4803,5)      -> ingress
building=1106368 cell=0       navAtBot=1 navAtTarget=1 botPos=(3616.35,-4800.94)  -> egress from the pad
building=1106368 cell=0       navAtBot=1 navAtTarget=1 botPos=(3613.79,-4845.36)  -> egress retry
building=1082874 cell=0       navAtBot=1 navAtTarget=1                            -> cantina
building=1697358 cell=0       navAtBot=1 navAtTarget=1                            -> Naboo med centre
```

The starport egress case is the one that matters: the bot standing **on the pad**
at `(3616.35, -4800.94)` targeting `(3527, -4803, 5)` outside — and **both
points are inside navmesh coverage**.

## What this settles, and what it does not

**Settles:** the routing data to get off the pad exists. Mode 2 is not a missing
navmesh. The bots are walking straight overland lines through a curved walled
space while a usable mesh sits underneath them.

**Does not settle:** whether both points are in the *same* NavArea.
`findNavAreaAt` returns presence, not identity. Recast cannot route between two
disjoint NavAreas, so "both navmeshed" is necessary but not sufficient — if the
pad has its own area and the street another, a recast path between them fails
exactly as observed. **That is the next measurement, and it is one field:** log
the NavArea object id at each end rather than a boolean.

This is the same lesson as the last two probes: a boolean answered a coarser
question than the one that decides the design.

## Why this does not mean "turn hybrid back on"

Hybrid was measured harmful two days ago for an unrelated reason — it carries
three hand-added exclusions and structure-traversal egress needs a fourth. The
navmesh being present makes the *destination* right and says nothing about that
mechanism. Per-leg movement mode remains the design answer.

## Method note

The previous probe lived inside the hollow scan, which only fires when a bot is
hollow-stuck; with hybrid on, no bot ever got there and it produced zero
samples. Moving it onto the path-request path — which fires on every leg
regardless of configuration — produced 16 samples in one short run.

## Cleanup

All gates restored to default-off. Evidence at
`bin/log/trip-verify-20260825-084524-navmesh-probe-structuretraversal.log`.
