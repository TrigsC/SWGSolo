# Architecture Documentation Rules

[ARCHI.md](ARCHI.md) documents the SWGSolo (Core3 fork) architecture. After
each task (new feature, refactor, bug fix), determine if ARCHI.md needs
updating.

## When to Update

Update after ANY change that alters:

- Project structure (new directories, moved files, new top-level manager)
- Technology stack (new dependency, new external tool wired into the build)
- The distributed-object/IDL system (§9) — new annotation usage, new
  codegen behavior, a new locking convention
- The Lua scripting layer (§10) — a new `bin/scripts/` subtree, a new
  convention for where config/behavior lives
- The web/REST layer (§11) — a new `APIProxy*Manager`, a new dashboard
  section, an auth change
- The AI Economy (§12) or AI PvP/Jedi (§13) subsystems — a new
  controller/manager, a change to the simulation-only/gating policy, a
  change to activation trust tiers or recovery/watchdog behavior
- Data flow between major components (update the Mermaid diagram)
- Build, docker, or dev-workflow changes (§6)

## How to Update by Change Type

### Major Feature / Refactor

Review: §5 (Core Architecture Principles), §8 (Game World/Server
Architecture), §9 (Distributed Object/IDL System), and whichever of §12/§13
is affected. Update the Mermaid data-flow diagram if component interactions
changed.

### Minor Feature / Enhancement

Update: the specific subsystem section (§9-§13) it touched, plus §7
(Configuration) if a new tunable was added.

### Bug Fix

Usually no update needed, unless the fix reveals or corrects an
architectural assumption (e.g. a locking-order bug, a navmesh/pathfinding
constraint like the P.4.1/P.4.2 overland-reachability work) — those are
architecturally significant and belong in §8/§9/§12 even as a "bug fix."

### Dependency Changes

Update: §3 (Technology Stack), and any affected architectural section (e.g.
a new IDL compiler version affects §9).

## Guidelines

- Be precise and factual — reflect the actual codebase, not an idealized
  version. This project's own standard (CLAUDE.md) is "verify against the
  live dashboard, not assumptions" — the same applies to this doc.
- Be concise — enough detail to orient a future session, not
  implementation-level detail (that belongs in the design docs under
  `docs/ai-*-design.md`, which ARCHI.md §12/§13 point to).
- Update the Mermaid diagram whenever data flow between components changes.
- Reference actual file paths, not descriptions ("`SimPlayerManager.cpp`",
  not "the manager file").
- If ARCHI.md exceeds ~20,000 tokens, run `TRIP-compact` before committing
  (checked automatically in `TRIP-3-release` Step 7).
