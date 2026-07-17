# Coverage Debt Ledger

One line per uncovered risky path: `path | why hard | escape plan`. Delete a
line in the same change that gives its path meaningful coverage.

- `SimPlayerManager::planPvpRoute` tactical-arrival block + `isPvpCityHotLocked` | interwoven with `pvpSquadMutex` state and live Zone/PlanetManager lookups; no SimPvP gtest scaffolding exists | live-dashboard verification per `docs/ai-pvp-mimetic-travel-design.md` §13.3 (tacticalArrivalsTotal > 0, convergence rows stay direct)
- `SimPlayerController` cell-aware path pipeline (in-cell `PatrolPoint`/arrival math) | requires a zone + building cells + pathfinding fixture; DeadlockTestBase does not cover movement | live visual check at Theed Spaceport per §13.3; miners regression-checked via minerActivity baseline
- `SimPlayerManager::checkPvpBreakOff` rolling window + latch/promotion interplay | interwoven with pvpSquadMutex squad state, controller map, and live death callbacks | staged-gank live verification per design doc §14.2 (incl. leader-as-threshold-death case)
- `SimPvpBotController::onTick` stalemate progress tracker + re-engage suppression | per-tick combat-state machine entangled with live agent references and manager config; no SimPvP gtest scaffolding | live verification per `docs/ai-pvp-mimetic-travel-design.md` §15.2 (stalemateBreaksTotal climbing at idleMs≈45s, hardStuck clearCombat rate collapse)
- `SimHunterController` hunt state machine (disengage-move-reengage combat, retreat/heal, clone-wound, kill-observer harvest) | per-tick combat/movement machine over live agents + spawned creatures + ResourceManager + pveMutex; no SimPve gtest scaffolding | live verification per design doc §Phase 2 (hunter travels→engages→KILLS→typed harvest; retreat + death/clone observed)
