# Changelog Table

| Version | Week | Commit Message |
| --- | --- | --- |
| `0.2.0` | 1 | feat: P.6.5b starport boarding realism - collector runs, tactical arrival, cell-aware pathing |
| `0.1.0` | 1 | chore: initialize TRIP workflow |

# Changelog Summary

- **v0.2.0 (P.6.5b Starport Boarding Realism - Week 1, 14-07-2026)**:
  - **Feature**: PvP squads depart from real starport ticket collectors (interior at Theed via new cell-aware pathing), tactical arrival one city out from hot destinations (convergence direct), formalized real-ship wait. All Lua-gated, simulation-only.
  - **Engine fix**: coordinate-space mixing in `AiAgentImplementation::findNextPosition`'s multi-node loop (latent for outdoor-only paths; found by Codex code review).
  - **Review**: Codex 2 rounds → APPROVED with observations (`docs/3-code-review/CR_w1_v0.2.0.md`); live verification pending owner restart per design doc §13.3.
  - **Files**: SimPlayerManager.{h,cpp}, SimPvPController.{h,cpp}, SimPlayerController.{h,cpp}, AiAgentImplementation.cpp, sim_player_manager.lua, dashboard app.js, design doc §13.

- **v0.1.0 (TRIP Initialization - Week 1, 13-07-2026)**:
  - **Setup**: Initialized TRIP workflow docs structure (`1-plans`, `2-changelog`, `3-code-review`, `4-unit-tests`, `5-tuto`, `6-memo`) alongside the existing design-doc corpus in `docs/`.
  - **Documentation**: Generated `docs/ARCHI.md` — classified SWGSolo as a Game (MMO server) project with a Web Backend aspect (REST dashboard) and a Lua-scripting layer; documented the IDL/ORB distributed-object system, Reference/Locker discipline, manager pattern, and the fork's custom AI Economy (SimPlayers), AI PvP, and Jedi archetype subsystems.
  - **Decisions**: TRIP's docs/git anchor is `workspace/Core3` (the real repo, not the outer `/srv/games/swgemu-core3` root where `.claude/skills/` lives but which has no real git history); mainline branch for TRIP-3 fast-forward merges is `miner-ai` (no `main`/`master` exists; `unstable` is origin's default but has no recent work); versioning added fresh via a new root `VERSION` file since the project previously had no SemVer marker (tracked by raw git log only).
  - **Files Added**: `docs/ARCHI.md`, `docs/ARCHI-rules.md`, `docs/2-changelog/changelog_table.md`, `docs/4-unit-tests/TESTING.md`, `VERSION`.
  - **Files Updated**: `.claude/skills/TRIP-1-plan/SKILL.md`, `TRIP-2-implement/SKILL.md`, `TRIP-3-release/SKILL.md`, `TRIP-review/checklist.md`, `TRIP-review/cr-template.md`, `TRIP-test/SKILL.md` — all placeholders replaced with SWGSolo-specific commands/guidance (docker-exec build/test invocations, IDL/locking/simulation-safety checklist sections, tutorial preferences).
