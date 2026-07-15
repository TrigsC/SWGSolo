# Tutorial 0.3.0 — Caches That Lie: Resolution Lifecycle in a Booting MMO Server

*Level: Advanced · Focus: architecture (cache lifecycle, boot ordering,
readiness sentinels) · Grounded in the P.6.5d diff.*

## The core principle

A cache is a claim about the world: "this answer was true when I computed
it, and it will stay true." In a server that boots in stages, the second
half of that claim is the dangerous part — an answer computed **too early**
can be *correct at the time* and still poison the session. P.6.5d's
city-location cache went through exactly this failure analysis, in three
review rounds, and the progression is worth internalizing because it
applies to every resolve-and-cache pattern in Core3.

## The three-stage trap

**Stage 1 — the obvious race.** `getAiEconomyDashboardSnapshot()` eagerly
resolved all ten cities so the dashboard rows would be complete
(SimPlayerManager.cpp, removed hunk near :9178). But the dashboard SPA
auto-polls, and Core3 boots for ~40-50 seconds. A poll arriving before
zones load meant `zoneServer->getZone()` returned null, the resolver
cached the configured fallback, and — because the cache had no notion of
*why* an entry was a fallback — every squad used legacy coordinates until
the next restart. The lesson: **read paths must never be resolve paths.**
A consumer that exists for observability (the dashboard) must be a
peek-only consumer, or its timing becomes a correctness input.

**Stage 2 — the readiness guard that wasn't enough.** The first fix
checked `zoneServer->isServerLoading()` before caching. Codex's review
countered with a fact worth memorizing: navmesh build jobs explicitly
*wait for loading to finish before starting* (`NavMeshManager.cpp:91`).
So "server online" is not "world queryable" — there's a second readiness
frontier behind the first, and our cantina validation depends on it
(`getInRangeNavMeshes` returns nothing until meshes exist). Cold starts
and `run clean` rebuilds would still have pinned fallback hangouts.

**Stage 3 — the sentinel probe.** You can't ask "are all navmeshes ready?"
cheaply — but you don't need to. Every configured city has one spot that
is *guaranteed* on-mesh once that city's meshes exist: its shuttle pad.
So the resolver probes `getInRangeNavMeshes(shuttlePad)` first; zero areas
means "this city's world isn't queryable yet" → return the fallback
**without caching**. The uncached miss converts a permanent error into a
retry. A known-good reference point turned an unanswerable global question
("is the world ready?") into a cheap local one ("is THIS city ready?").

## The completing piece: who retries, and when is it done?

Uncached misses need a retrier. P.6.5d puts one in the PvP maintenance
task (30s cadence): a warmup loop that runs while
`!pvpCityLocationsWarmedUp`, and — the subtle part — only sets that flag
when **every** configured city has a *published cache entry*, not merely
when the loop has run once while online:

```
pvpCityLocationsWarmedUp = allCached;   // not `true`
```

The first version set `true` unconditionally after one online pass, which
would have stranded any city whose meshes finished late. Completion
criteria for a warmup must be observed state ("all present"), never "I
tried." Config changes that clear the cache also re-arm the flag — an
invalidation without a re-arm quietly demotes your warmup to
resolve-on-first-use.

## Transferable rules for this codebase

1. Dashboard/REST assembly reads caches; it never populates them.
2. A resolver that can fail for *environmental* reasons (zone not loaded,
   meshes not built) must distinguish those from *semantic* failures (a
   config typo, no cantina within 400m). Environmental → return uncached,
   retry. Semantic → cache the fallback; it's the real answer.
3. `isServerLoading()` gates object existence, not world queryability —
   pathfinding/navmesh consumers need their own sentinel (P.6.5d uses the
   city shuttle pad).
4. Warmup completion = observed completeness, and every cache invalidation
   site must re-arm it.

## Where to look in the diff

- `SimPlayerManager.cpp` — `resolvePvpCityLocations` (loading guard +
  shuttle-pad mesh probe, both returning uncached), the peek-only comment
  where the dashboard eager loop used to be, the consolidated warmup in
  `runPvpMaintenanceTask`, and the re-arm inside `applyPvpConfig`'s
  invalidation block.
- Compare with `resolvePvpBoardingPoint` (P.6.5b): same scan-outside-mutex/
  publish-under-mutex shape, but it never needed the readiness guards —
  its callers (route planning) only run long after boot. The lifecycle
  requirements come from the *earliest possible caller*, not the typical
  one.
