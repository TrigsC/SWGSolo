# Changelog Table

| Version | Week | Commit Message |
| --- | --- | --- |
| `0.4.5` | 2 | fix: P.8.4 PvE hunter combat targeting — player-safe attack guard + defender-driven re-engagement |
| `0.4.4` | 2 | F_0.4.4 P.8.3: PvE hunter combat & movement realism (real combat, navmesh-aware travel, self-defense + combat template, #/wilds dashboard) |
| `0.3.1` | 1 | fix: combat stalemate break + collector wait jitter (Theed doorway statue deadlock) |
| `0.3.0` | 1 | feat: P.6.5d city-loop realism - real shuttleports, cantina hangouts, break-off cohesion |
| `0.2.1` | 1 | hotfix: MOVEOUT route callout swallowed by global announce gap; add min departure notice |
| `0.2.0` | 1 | feat: P.6.5b starport boarding realism - collector runs, tactical arrival, cell-aware pathing |
| `0.1.0` | 1 | chore: initialize TRIP workflow |

# Changelog Summary

- **v0.4.5 (P.8.4 PvE Hunter Combat-Targeting Fixes - Week 2, 21-07-2026)**:
  - **Fix**: two owner-observed hunter combat bugs, both rooted in the controller driving combat off its own `targetOid` pin rather than `CombatManager`'s real defender state. (1) A faction-0 hunter's area attack could damage and then lock onto a real player — fixed by mirroring the sim-bot guard on the player side of `CreatureObject::isAttackableBy` (one chokepoint that also covers AoE splash via `getAreaTargets`). (2) The hunter would not re-fire on a closer creature that started attacking it — now selects the nearest live/attackable/non-player defender (bounded hysteresis) and re-engages via `startCombat` + `activateAiBehavior`.
  - **Hardening (from code review)**: single-writer mission-target ownership (`runActiveTick` sole writer; `onTick` defers in HUNTING, interceptor-only on travel legs) to remove an `onTick`/`runActiveTick` cross-thread race; defender-list snapshot under the hunter lock (OOB guard); shared bilateral defender cleanup; observer-gated pending-destruction guard so a just-killed target still credits quota without stalling combat.
  - **Scope**: gated to `getSimPlayerBot()` hunters (miners/PvP/NPCs unchanged); no IDL/schema/config changes; simulation-only.
  - **Review**: Codex 4 rounds → APPROVED (`docs/3-code-review/CR_w2_v0.4.5.md`), with an owner-verified live addendum (in-game: no player AoE damage, correct re-engagement).
  - **Files**: CreatureObjectImplementation.cpp, SimHunterController.{h,cpp}, ARCHI.md, ai-pve-playerbot-design.md, COVERAGE-DEBT.md, F_0.4.5 plan.
  - **Note**: ff-merged into `feat/pve-acquisition-demand-ledger` only, NOT `miner-ai`. Pre-existing `death_watch_wraith.lua`/`rifle_t21.lua` and `engine3` working-tree changes excluded.

- **v0.4.4 (P.8.3 PvE Hunter Combat & Movement Realism - Week 2, 20-07-2026)**:
  - **Feature**: makes the live PvE hunter loop read like real player activity (simulation-only loot) — real mutual combat via `CombatManager::startCombat` with the AI-aligned rifle; hunter-opt-in navmesh/overland hybrid movement (navmesh in cities, overland in the wild, validated egress); de-garbled per-phase announcements; HAM-filling non-stacking buffs; new read-only `#/wilds` dashboard route with live hunter positions.
  - **Live-verification hardening (owner-verified, not Codex-re-reviewed)**: `ATTACKABLE` flag for two-way combat + player-attack scoping (neutral bot is not player-attackable); `onTick` self-defense against non-target attackers (en-route interceptors + lair pre-acquisition aggro); `activateAiBehavior(true)` on engage to fix multi-minute fire-cadence stalls; buffs +100 → +2500; body template `artisan` → neutral `death_watch_wraith` + `rifle_t21` (wraith tuned down from DWB boss stats).
  - **Also carries**: the previously-unreleased P.8.1c/d + P.8.2 branch checkpoint (`1df1850b83`: acquisition/demand ledger, creature-resource turf split, mission-terminal hunting).
  - **Review**: Codex 4 rounds → APPROVED (`docs/3-code-review/CR_w2_v0.4.4.md`), with an owner-verified live-hardening addendum. Live-verified in-game across restarts.
  - **Files**: SimHunterController.{h,cpp}, SimPlayerController.{h,cpp}, SimPlayerManager.{h,cpp}, AiAgentImplementation.cpp, sim_player_manager.lua, death_watch_wraith.lua, dashboard app.js/index.html/styles.css, design doc + COVERAGE-DEBT.
  - **Note**: NOT merged to `miner-ai` (owner landing further bug fixes on the branch first).

- **v0.3.1 (P.6.5e Stalemate Break + Collector Jitter - Week 1, 15-07-2026)**:
  - **Fix**: owner-observed frozen statue pairs at Theed's interior collector — 45s combat-progress stalemate break (HAM-tracked, leaders+members, 20s single-enemy re-engage grace) + deterministic ~3m collector wait jitter. Zero-preserving config parse; member-teleport scoped reset.
  - **Review**: Codex 2 rounds → APPROVED (`docs/3-code-review/CR_w1_v0.3.1.md`); live verification per design doc §15.2 pending owner restart.
  - **Files**: SimPvPController.{h,cpp}, SimPlayerManager.{h,cpp}, sim_player_manager.lua, app.js, design doc §15.

- **v0.3.0 (P.6.5d City-Loop Realism + Squad Cohesion - Week 1, 14-07-2026)**:
  - **Feature**: three live-data locations per city (starport / shuttleport / cantina hangout) — intra-planet legs use real shuttleports both ways, squads hang out at validated cantina exteriors (Theed manual); break-off-after-deaths cohesion with promotion-safe latch, RETREAT callout, and 600s avoid-city stamp.
  - **Hardening (review)**: resolver never caches during boot or pre-navmesh (shuttle-pad mesh probe); dashboard peek-only; retryable maintenance warmup.
  - **Review**: Codex 3 rounds → APPROVED (`docs/3-code-review/CR_w1_v0.3.0.md`); live verification pending owner restart per design doc §14.2.
  - **Files**: SimPlayerManager.{h,cpp}, SimPvPController.{h,cpp}, sim_player_manager.lua, dashboard app.js, design doc §14.

- **v0.2.1 (Hotfix - Week 1, 14-07-2026)**:
  - **Issue**: owner-reported live — squads spoke "packing it up" then jumped with no route callout; players had no time to follow (first destination info was the arrival line in group chat).
  - **Fix**: route planning at departure intent is now silent (`pendingRouteAnnounce` stamped on the squad in `planPvpRoute`); the MOVEOUT callout is spoken at the pad in `onPvpSquadReadyToTravel` (restores the owner-verified 2026-07-09 timing); new `minDepartureNoticeSeconds` (lua 30, C++ default 0) holds boarding until the callout has had time to land even when a ship is already in (board-anyway cap unaffected). Stale stamps cleared on convergence replans and board-time fallback plans.
  - **Root Cause**: P.6.5b moved planning+MOVEOUT to loiter-end, same call stack as the DEPARTURE shout — `announcePvpEvent`'s global 4s anti-spam gap (which MOVEOUT does not bypass) silently dropped the route line on every normal departure.

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
