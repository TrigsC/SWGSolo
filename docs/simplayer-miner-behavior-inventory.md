# SimPlayer Miner Behavior Inventory

P.3.4.3-pre audit. This document is an inventory only. It does not prescribe or implement behavior changes.

## Executive Summary

Two SimPlayer miner systems currently exist in the same controller.

The intended current path is the intelligent coverage path:

coverage slot -> assignment -> path validation -> limited activation -> movement -> arrival -> stationed -> repeated sampling -> simulated acquisition transaction -> remain stationed until coverage or safety logic clears/reassigns.

The legacy path is the original conceptual miner loop:

pick broad conceptual resource -> play survey animation -> move to random nearby navmesh/fallback location -> crouch/sample animation -> record conceptual yield -> repeat.

The legacy path is still active. Miner spawn config creates `artisan` miners with `behavior = "gather_resources"` and a `minerConfig` containing broad resources `iron`, `gas`, `water`, and `copper`. `SimMinerController::startSimLoop()` starts this conceptual loop whenever there is no pending intelligent assignment.

The intended path is also active. `minerIntelligentTargetingConfig.enabled=true`, `mode="soak"` is normalized to limited-mode behavior, limited activation is enabled, stationed lifecycle is enabled, repeated stationed sampling is enabled, and simulated acquisition transactions are enabled while real acquisition remains disabled.

The systems can coexist, but there are possible conflicts:

- A queued intelligent assignment does not immediately interrupt a legacy survey/move/sample already in progress. It waits until `startSimLoop()` runs.
- Legacy conceptual sample completion does not clear intelligent assignments, but it can delay activation.
- Stale controller tasks are possible because behavior/retry/arrival tasks are scheduled through weak controller references and are not centrally cancelled by lifecycle generation. A delayed `SimRetryTask` calling `startSimLoop()` is the main possible way old behavior could restart after a miner has become stationed.
- Stationed repeated sampling is the intended owner of recurring sample ticks, but it reuses the same `SimBehaviorTask::FINISH_SAMPLE` dispatch and the same crouch/sample animation as the legacy loop.

No audited Sim miner acquisition path creates `ResourceContainer`, calls `extractResource()`, mutates inventory, mutates vendors/market/crafting/credits, or enables real acquisition. There are unrelated persistence/destruction paths in SimPlayer/PvP lifecycle and optional conceptual total persistence, but the P.3.4 simulated acquisition path is runtime-only.

## Current Intended Miner Flow

| Step | File / Function | Notes |
| --- | --- | --- |
| Coverage slot / pressure source | `SimPlayerManager.cpp`, `buildDemandWeightedPressureResultsForMiners()` and `logMinerIntelligentTargetingDecisions()` | Demand-weighted profiles choose pressure and exact eligible resources. D.6.6 is canonical for intelligent targeting. |
| Assignment creation | `SimPlayerManager.cpp`, `logMinerIntelligentTargetingDecisions()` | Creates `MinerIntelligentTargetAssignment` with exact `targetResourceName`, `targetResourceType`, `targetZoneName`, density target coordinates, profile, generation id, target hash. |
| Density target | `SimPlayerManager.cpp`, `findMinerDensityTarget()` from `logMinerIntelligentTargetingDecisions()` | Finds an accepted density coordinate. `navAreaDensitySelectionConfig` can shadow or actively replace target selection, but current config has active NavArea selection disabled. |
| Path validation | `SimPlayerManager.cpp`, `logMinerPathValidationSimulations()`, `recordMinerPathValidationSnapshot()` | Records validation snapshot and trust status. Activation requires `verifiedPath` by current config. |
| Limited activation | `SimPlayerManager.cpp`, `logMinerIntelligentTargetingDecisions()` | In limited/soak mode, validates caps, cooldown, same-planet, controller state, and path trust before calling the controller. |
| Controller queue | `SimPlayerController.cpp`, `SimMinerController::requestIntelligentTargetAssignment()` | Stores exact target resource identity and marks `intelligentAssignmentPending=true`. If the same assignment is already active/stationed, reports `alreadyActive`. |
| Movement activation | `SimPlayerController.cpp`, `SimMinerController::beginIntelligentTargetAssignment()` | Runs at `startSimLoop()` time, sets `intelligentAssignmentActive=true`, sets `targetResource` to exact resource type/name, records lifecycle `activationStarted`, then calls `moveTo()`. |
| Path movement | `SimPlayerController.cpp`, `SimPlayerController::moveTo()`, `onPathFound()`, `checkArrival()` | Uses `PathFinderManager::findPath`, patrol points, watchdog movement, and arrival threshold. Movement speed/trust rules are not changed by P.3.4. |
| Arrival | `SimPlayerController.cpp`, `SimMinerController::onArrived()` | If `intelligentAssignmentActive`, records lifecycle `sampleStarted`, logs arrival, and calls `performIntelligentSample()`. |
| Sample animation / result delay | `SimPlayerController.cpp`, `SimMinerController::performIntelligentSample()` | Crouches, plays `sample`, schedules `FINISH_SAMPLE` after game-derived 3000 ms. |
| Stationed retention | `SimPlayerManager.cpp`, `transitionMinerIntelligentAssignmentToStationed()` | Keeps assignment as `stationed` if enabled and safety/reserve checks pass. Updates `stationSampleCount`, `stationYieldQuantity`, `lastStationSampleAtMs`, reachability coverage memory, and schedules repeat delay when enabled. |
| Repeated stationed sampling | `SimPlayerController.cpp`, `SimMinerController::startStationedSample()` and `performIntelligentSample()` | `START_STATIONED_SAMPLE` toggles local state back to active sampling, records `sampleStarted` with detail `stationedRepeat`, and performs another exact-resource sample tick. |
| Simulated acquisition transaction | `SimPlayerController.cpp`, `finishIntelligentSample()` -> `SimPlayerManager.cpp`, `recordSimulatedAcquisitionTransactionFromController()` | Records exact spawned resource identity into runtime-only ledger when readiness gates pass. Ledger retention caps rows only. |
| Conceptual/resource-aware stockpile accounting | `SimPlayerManager.cpp`, `recordIntelligentConceptualMinerYield()` and `recordResourceAwareConceptualStockpileYield()` | Still records conceptual/resource-aware totals after simulated acquisition. For intelligent samples, the label is now the exact resource type, with exact source identity attached in provenance. |
| Remain stationed | `SimPlayerController.cpp`, `finishIntelligentSample()` | If retained, local state becomes `intelligentAssignmentStationed=true`, controller `state=WAITING`, and arrival checks stop through `shouldContinueArrivalChecks()`. |
| Reassignment / clear | `SimPlayerManager.cpp`, `clearMinerIntelligentTargetAssignment()`, timeout/clear checks in `logMinerIntelligentTargetingDecisions()` | Clears for explicit lifecycle timeout, zone change, dead/incap, combat, path failure, reserve satisfied, demand/resource invalid, emergency disabled, or failed station transition. |

## Legacy / Experimental Miner Flows

### Legacy Conceptual Wander / Survey / Sample Loop

- File/function: `SimPlayerController.cpp`, `SimMinerController::startSimLoop()`, `performSurvey()`, `finishSurvey()`, `goToResource()`, `onArrived()`, `performSample()`, `finishSample()`.
- Trigger condition: A `SimMinerController` starts or restarts and `intelligentAssignmentPending` is false.
- Config gate: Spawned by miner `spawnGroups`; tuned by `minerConfig`.
- Current default/current value: Active. `spawnGroups` currently creates 10 artisan miners with `behavior="gather_resources"`.
- Movement: Yes. Chooses a random navmesh/fallback point 100-200m away.
- Posture/animation: Yes. `manipulate_high` survey; `CROUCHED` + `sample`; then `UPRIGHT` + `stop_sample`.
- Assignment clearing: No direct intelligent assignment clear from normal `finishSample()`.
- Conceptual yield: Yes, via `recordConceptualMinerYield()`.
- Simulated acquisition: No.
- Can run at same time as stationed lifecycle: Not intentionally. It can run before intelligent activation and after intelligent assignment clear/failure. A stale retry/task may restart it unexpectedly.
- Interference risk: Possible conflict. It can delay activation while survey/move/sample is in progress, and a stale retry could move a stationed miner if it calls `startSimLoop()` after stationing.

### Legacy Fake Sampling / Kneel Animation

- File/function: `SimPlayerController.cpp`, `SimMinerController::performSample()` and `performIntelligentSample()`.
- Trigger condition: Legacy arrival or intelligent stationed sample tick.
- Config gate: Legacy sample duration from `minerConfig.sampleDurationMs`; intelligent sample result delay from internal P.3.4.2 constant.
- Active: Active as animation for both paths.
- Movement: No movement inside the sample function; it clears patrol points and sets movement state `OBLIVIOUS`.
- Posture/animation: Yes, crouch and `sample`.
- Assignment clearing: No by itself.
- Conceptual yield: Only when corresponding finish function records yield.
- Simulated acquisition: Only intelligent stationed finish can record simulated acquisition.
- Interference risk: Possible visual ambiguity. The same animation is used for legacy conceptual sampling and stationed exact-resource sampling, so visual state alone cannot prove which path is active.

### Old Conceptual-Only Sample Completion

- File/function: `SimPlayerController.cpp`, `SimMinerController::finishSample()`.
- Trigger condition: `FINISH_SAMPLE` fires while `intelligentSampleActive=false`.
- Config gate: `SimMinerConfig.yieldEnabled`, `yieldConfig`.
- Active: Active.
- Movement: Calls `startSimLoop()` after completion, which can start another legacy survey/move unless an intelligent assignment is pending.
- Assignment clearing: No direct intelligent assignment clear.
- Conceptual yield: Yes, broad resource label from `targetResource`.
- Simulated acquisition: No.
- Interference risk: Possible conflict. If an intelligent assignment was queued during a legacy sample, the queued assignment starts only after this legacy finish calls `startSimLoop()`.

### Old Movement Loop / Retry Loop

- File/function: `SimPlayerController::moveTo()`, `onPathFound()`, `checkArrival()`, `onPathFailed()`, `SimRetryTask`.
- Trigger condition: Any controller move or path failure.
- Config gate: None specific to legacy; intelligent and legacy both use the same base movement.
- Active: Active.
- Movement: Yes. Patrol point movement, arrival watchdog, retry after path failure.
- Assignment clearing: `SimMinerController::onPathFailed()` clears intelligent assignment when active/pending; base retry schedules `startSimLoop()` after 5 seconds.
- Conceptual yield: No direct yield.
- Simulated acquisition: No.
- Interference risk: Possible conflict. `SimRetryTask` has no lifecycle generation guard. A retry scheduled from an earlier failure can call `startSimLoop()` later.

### Old Assignment Clear After Sample

- File/function: `SimPlayerManager.cpp`, `clearMinerIntelligentTargetAssignmentOnSampleComplete()`, `SimMinerController::finishIntelligentSample()`.
- Trigger condition: Sample complete path when stationed lifecycle is not enabled, or station transition fails.
- Config gate: `assignmentConfig.clearOnSampleComplete`, `stationedMinerConfig.enableStationedLifecycle`.
- Active: Guarded. Current config has stationed lifecycle enabled, and `clearMinerIntelligentTargetAssignmentOnSampleComplete()` returns immediately when stationed lifecycle is enabled.
- Movement: If station transition fails, `finishIntelligentSample()` clears local assignment and calls `startSimLoop()`, returning to legacy loop.
- Conceptual yield: Intelligent yield can still be recorded for the sample.
- Simulated acquisition: Only recorded if stationed retention succeeds.
- Interference risk: No conflict for successful stationed samples. Possible conflict on failed station transition because it falls back into conceptual loop.

### Debug / Simulation-Only Planner Flows

- File/function: `logMinerTargetRecommendations()`, `logMinerTargetSimulations()`, `logMinerDensityTargetSimulations()`, `logMinerPathValidationSimulations()`.
- Trigger condition: Their scheduled manager tasks.
- Config gate: recommendation/simulation/density/path validation configs.
- Active: D.3 recommendation and D.4 target simulation are disabled; density target and path validation simulations are enabled.
- Movement: Only `logMinerIntelligentTargetingDecisions()` can activate movement. Recommendation/simulation/density/path validation tasks do not directly move miners.
- Assignment clearing: Density/path validation simulation can clear bad/expired assignments in some validation flows; primary runtime clear checks are in intelligent targeting.
- Conceptual yield: No.
- Simulated acquisition: No.
- Interference risk: Low for disabled recommendation/simulation; path validation is part of intended gating.

## Behavior Ownership Table

| Behavior | Current Owner | File / Function | Trigger | Config Gate | Active? | Conflicts? |
| --- | --- | --- | --- | --- | --- | --- |
| Miner spawning | SimPlayerManager | `spawnConfiguredGroups()`, `spawnFromConfig()` | Startup/config spawn | `spawnGroups` | Yes | No direct conflict, but it starts legacy loop immediately. |
| Legacy conceptual loop | SimMinerController | `startSimLoop()`, `performSurvey()`, `goToResource()`, `performSample()` | Controller start/restart/fallback | `minerConfig`, no explicit disable | Yes | Possible conflict with stationed if stale task calls `startSimLoop()`. |
| Demand scoring | SimPlayerManager | `buildDemandWeightedPressureResultsForMiners()` | Manager targeting interval | `demandWeightedMinerPlanSimulationConfig` and dependencies | Yes | No direct movement conflict. |
| Target assignment | SimPlayerManager | `logMinerIntelligentTargetingDecisions()` | Intelligent targeting interval | `minerIntelligentTargetingConfig.assignmentConfig.enabled` | Yes | Possible delay if controller is busy in legacy loop. |
| Density target | SimPlayerManager | `findMinerDensityTarget()` | Assignment planning | `minerDensityTargetSimulationConfig` | Yes | No direct movement conflict. |
| Path validation | SimPlayerManager | `logMinerPathValidationSimulations()` | Path validation interval | `minerPathValidationSimulationConfig` | Yes | Intended gate. |
| Movement activation | SimPlayerManager + SimMinerController | `logMinerIntelligentTargetingDecisions()` -> `requestIntelligentTargetAssignment()` -> `beginIntelligentTargetAssignment()` | Validated assignment + limited activation allowance | `mode=soak/limited`, `limitedActivationConfig.enabled` | Yes | Possible delay until `startSimLoop()` runs. |
| Movement execution | SimPlayerController | `moveTo()`, `onPathFound()`, `checkArrival()` | Any movement request | Base controller | Yes | Shared by legacy and intelligent movement. |
| Arrival handling | SimMinerController | `onArrived()` | Base arrival detection | Controller state | Yes | Branches by `intelligentAssignmentActive`. |
| Kneel/action animation | SimMinerController | `performSample()`, `performIntelligentSample()` | Sample start | Legacy sample or stationed sample | Yes | Visual ambiguity. |
| Conceptual sample | SimMinerController + SimPlayerManager | `finishSample()`, `recordConceptualMinerYield()` | Legacy `FINISH_SAMPLE` | `yieldConfig.enabled` | Yes | Can make miner/dashboard look productive without exact-resource coverage. |
| Intelligent conceptual/resource-aware yield | SimMinerController + SimPlayerManager | `finishIntelligentSample()`, `recordIntelligentConceptualMinerYield()` | Intelligent `FINISH_SAMPLE` | `yieldConfig.enabled` | Yes | Expected to coexist with simulated acquisition. |
| Stationed retention | SimPlayerManager | `transitionMinerIntelligentAssignmentToStationed()` | Intelligent sample finished | `enableStationedLifecycle` | Yes | Intended owner of retained assignment. |
| Repeated stationed sampling | SimMinerController + SimPlayerManager | `startStationedSample()`, `performIntelligentSample()`, repeat delay from `transition...` | Stationed sample retained | `enableStationedRepeatedSampling` | Yes | Intended owner; shares task/animation with legacy. |
| Simulated acquisition transaction | SimPlayerManager | `recordSimulatedAcquisitionTransactionFromController()` | Retained intelligent sample with readiness pass | `enableSimulatedAcquisitionTransactions` plus readiness gates | Yes | No real mutation; expected to fire with intelligent yield for same sample. |
| Assignment clearing | SimPlayerManager | `clearMinerIntelligentTargetAssignment()` | Timeouts, zone/death/combat, failures, failed station transition | Assignment config and safety gates | Yes | Possible fallback to legacy loop after clear. |
| Rebalance clearing | SimPlayerManager | Coverage/alignment assignment clear logic, `clearMinerIntelligentTargetAssignment()` | Coverage validity/rebalance diagnostics | Coverage/alignment logic | Partial/diagnostic; exact rebalance owner still spread through targeting logic | Possible if it clears stationed assignment then legacy loop resumes. |
| Emergency disable | SimPlayerManager | `minerIntelligentTargetingLimitedEmergencyDisabled` checks | Activation failure latch/config reload | `disableOnActivationFailure`, `disableOnFirstActivationFailure` | Yes | Prevents activation; miners fall back to legacy conceptual loop. |

## Config Inventory

| Config | Purpose | Default in C++ | Current Value | Needed Long-Term? | Notes |
| --- | --- | --- | --- | --- | --- |
| `enabled` | Master SimPlayer manager switch | false during load fail | true | Yes | Enables all configured SimPlayers. |
| `spawnGroups[].type` | Selects PvP vs miner controller | none | `miner` for artisans | Yes | Non-PvP uses `SimMinerController`. |
| `spawnGroups[].totalCount` | Number of bots | 0 unless config | 10 miners | Yes | Current miner population. |
| `spawnGroups[].templates` | Creature templates | fallback `artisan` | `artisan` | Yes | Miner template. |
| `spawnGroups[].behavior` | Lua behavior label | none | `gather_resources` | Maybe | C++ does not appear to branch on this for miners; redundant today. |
| `minerConfig.resources` | Legacy broad conceptual resource list | iron/gas/water/copper | iron/gas/water/copper | Obsolete/redundant | Used only by legacy conceptual loop. |
| `minerConfig.surveyDurationMs` | Legacy fake survey duration | 4000 | 4000 | Obsolete/redundant | Not used by stationed intelligent sample. |
| `minerConfig.sampleDurationMs` | Legacy sample duration | 15000 | 15000 | Obsolete/redundant | Intelligent samples now use 3000 ms game-derived result delay. |
| `minerConfig.minSearchRadius` | Legacy random navmesh target min radius | 100 | 100 | Obsolete/redundant | Legacy loop movement only. |
| `minerConfig.maxSearchRadius` | Legacy random navmesh target max radius | 200 | 200 | Obsolete/redundant | Legacy loop movement only. |
| `minerConfig.fallbackRadius` | Legacy fallback movement radius | 100 | 100 | Obsolete/redundant | Legacy loop movement only. |
| `minerConfig.logStateTransitions` | Legacy controller state logs | false | false | Maybe debug only | Useful to prove legacy loop is running. |
| `minerConfig.yieldConfig.enabled` | Legacy/intelligent conceptual yield gate | true | true | Maybe | Still gates conceptual/resource-aware stockpile updates. |
| `minerConfig.yieldConfig.minAmount` | Legacy random yield min | 5 | 5 | Obsolete for intelligent | Legacy only after P.3.4.2. |
| `minerConfig.yieldConfig.maxAmount` | Legacy random yield max | 25 | 25 | Obsolete for intelligent | Legacy only after P.3.4.2. |
| `minerConfig.yieldConfig.logYield` | Yield logging | false | true | Debug only | Emits `SimMiner yield` logs. |
| `minerConfig.summaryConfig.enabled` | Conceptual miner summary task | false | true | Maybe | Reports conceptual totals, not exact acquisition liveness. |
| `minerConfig.summaryConfig.intervalSeconds` | Summary cadence | 300 | 30 | Maybe | Diagnostic only. |
| `resourceIntelligenceConfig.enabled` | Active resource snapshot/scoring source | false | true | Yes | Provides live resource entries. |
| `resourceIntelligenceConfig.logTopResources` | Logs top resource intelligence rows | false | false | Debug only | No behavior effect. |
| `resourceIntelligenceConfig.summaryIntervalSeconds` | Resource intelligence cadence | 600 | 300 | Yes | Feeds resource snapshot freshness. |
| `resourceIntelligenceConfig.topN` | Resource intelligence log/table size | 10 | 10 | Maybe | Diagnostic. |
| `resourceScoringProfiles.enabled` | Curated scoring profiles | false | false | Maybe | Currently not active. |
| `minerTargetRecommendationConfig.*` | D.3 recommendations | disabled | disabled | Obsolete/redundant | Diagnostics only; no movement. |
| `minerTargetSimulationConfig.*` | D.4 round-robin simulation | disabled | disabled | Obsolete/redundant | Diagnostics only; no movement. |
| `minerDensityTargetSimulationConfig.enabled` | Density pocket search | false | true | Yes | Supplies accepted density target. |
| `minerDensityTargetSimulationConfig.intervalSeconds` | Density search cadence | 300 | 60 | Yes | Also affects target freshness. |
| `minerDensityTargetSimulationConfig.searchRadii` | Density search radii | fallback list | 250/500/1000/2000 | Yes | Target search behavior. |
| `minerDensityTargetSimulationConfig.samplesPerRadius` | Density sample count | 48 | 48 | Yes | Target quality/cost. |
| `minerDensityTargetSimulationConfig.minAcceptableDensity` | Density floor | 0.65 | 0.65 | Yes | Activation gate. |
| `minerDensityTargetSimulationConfig.preferredDensity` | Preferred density | 0.80 | 0.80 | Maybe | Scoring/tiebreak. |
| `minerDensityTargetSimulationConfig.requireNavmesh` | Require miner/navmesh state | true default reset | false current | Yes | Current config allows non-navmesh density targets. |
| `minerDensityTargetSimulationConfig.maxPathCheckAttempts` | Target path check attempts | 8 | 8 | Yes | Density target gating. |
| `minerDensityTargetSimulationConfig.distancePenaltyPerMeter` | Distance penalty | 0.02 | 0.02 | Yes | Target scoring. |
| `navAreaDensitySelectionConfig.enableNavAreaDensitySelection` | Active NavArea density selection | false | false | Future | Not active. |
| `navAreaDensitySelectionConfig.enableNavAreaDensityShadowMode` | Shadow-only NavArea diagnostics | true | true | Debug/future | Does not change target while active selection is false. |
| `navAreaDensitySelectionConfig.navArea*` | NavArea cache/sample/path/avoid/prefer options | mixed | configured | Future | Shadow diagnostics. |
| `reachabilityMemoryConfig.*` | Runtime-only reachability memory and scoring | enabled memory, preference false | memory true, preference true | Yes | Can influence candidate preference; not persistence. |
| `minerPathValidationSimulationConfig.enabled` | Path validation snapshot generation | false | true | Yes | Required by current activation gate. |
| `minerPathValidationSimulationConfig.intervalSeconds` | Path validation cadence | 300 | 60 | Yes | Snapshot freshness. |
| `minerPathValidationSimulationConfig.validateOnlyAcceptedDensityTargets` | Limit validation to accepted target | true | true | Yes | Keeps validation scoped. |
| `minerPathValidationSimulationConfig.maxPathDistance` | Path distance cap | 2500 | 2500 | Yes | Safety/trust gate. |
| `minerPathValidationSimulationConfig.maxPathNodes` | Path node cap | 256 | 256 | Yes | Safety/trust gate. |
| `minerIntelligentTargetingConfig.enabled` | Intelligent targeting scheduler | false | true | Yes | Main exact-resource assignment system. |
| `minerIntelligentTargetingConfig.mode` | off/shadow/limited/soak | off | soak -> limited behavior | Yes | Soak is limited mode with conservative controls. |
| `minerIntelligentTargetingConfig.intervalSeconds` | Targeting cadence | 300 | 60 | Yes | Assignment/activation cadence. |
| `minerIntelligentTargetingConfig.maxActiveMiners` | Evaluation count per interval | 1 | 10 | Yes | Not active mover cap. |
| `minerIntelligentTargetingConfig.requireDemandWeightedPlan` | Require D.6.6 plan | true | true | Yes | Prevents legacy plan activation. |
| `minerIntelligentTargetingConfig.requireAcceptedDensityTarget` | Require accepted density target | true | true | Yes | Safety gate. |
| `minerIntelligentTargetingConfig.requireValidPath` | Require verified path | true | true | Yes | Safety gate. |
| `minerIntelligentTargetingConfig.fallbackToConceptualLoop` | Allow conceptual fallback | false | false | Obsolete/risky | Kept off for single-owner intelligent miner work; use `legacyMinerLoopConfig` only for temporary testing. |
| `minerIntelligentTargetingConfig.rollbackOnFailureCount` | Failure count before rollback-held | 3 | 3 | Maybe | Activation safety. |
| `minerIntelligentTargetingConfig.logDecisionSummary` | Summary logs | true | true | Debug | Useful evidence. |
| `minerIntelligentTargetingConfig.logVerboseSwitchDecisions` | Per-miner verbose logs | false | false | Debug | Useful when proving conflicts. |
| `assignmentConfig.enabled` | Assignment cache | true | true | Yes | Required for lifecycle. |
| `assignmentConfig.ttlSeconds` | Legacy generic assignment TTL | 30 C++ reset | 600 | Maybe obsolete | Candidate/validated TTLs now more specific. |
| `assignmentConfig.candidateAssignmentTtlSeconds` | Candidate TTL | 180 C++ reset | 600 | Yes | Pre-validation expiration. |
| `assignmentConfig.validatedAssignmentTtlSeconds` | Validated TTL | 180 C++ reset | 600 | Yes | Waiting-for-activation expiration. |
| `assignmentConfig.queuedActivationTtlSeconds` | Queued activation TTL | 120 C++ reset | 240 | Yes | Can clear queued assignment. |
| `assignmentConfig.movementArrivalTimeout*` | Movement timeout | 600/min240/max1200 | same | Yes | Can clear active movement. |
| `assignmentConfig.movementArrivalSecondsPerMeter` | Dynamic movement timeout scaling | 0.75 | 0.75 | Yes | Movement timeout. |
| `assignmentConfig.sampleStartedTimeoutSeconds` | Sample timeout | 180 | 180 | Yes | Can clear stuck sample. |
| `assignmentConfig.preventNormalTtlForActiveMovement` | Prevent candidate TTL from clearing active movement/stationed | true | true | Yes | Important for stationed lifecycle. |
| `assignmentConfig.replaceOnlyWhenExpiredOrInvalid` | Retain live assignment | true | true | Yes | Coverage stability. |
| `assignmentConfig.clearOnSampleComplete` | Old clear-after-sample gate | true | true | Obsolete/risky | Guarded by stationed lifecycle, but name is legacy. |
| `assignmentConfig.clearOnCombat` | Clear on combat | true | true | Yes | Safety. |
| `assignmentConfig.clearOnIncapOrDeath` | Clear on incap/death | true | true | Yes | Safety. |
| `assignmentConfig.clearOnZoneChange` | Clear if miner leaves target planet | true | true | Yes | Safety. |
| `assignmentConfig.logAssignmentLifecycle` | Lifecycle logs | true | true | Debug | Useful evidence. |
| `assignmentConfig.logRetainedAssignments` | Retained assignment logs | false | false | Debug | Enable only when tracing. |
| `assignmentConfig.movementReadinessDiagnosticsEnabled` | Dashboard movement readiness | true | true | Debug/temporary | Could be demoted later. |
| `limitedActivationConfig.enabled` | Allows real controller movement in limited mode | false | true | Yes | Main movement gate. |
| `limitedActivationConfig.maxActiveIntelligentMiners` | Active assignment cap | 1 C++ reset | 10 | Yes | Prevents too many active miners. |
| `limitedActivationConfig.maxActivationsPerInterval` | New activation cap | 1 C++ reset | 2 | Yes | Cadence safety. |
| `limitedActivationConfig.cooldownSecondsPerMiner` | Activation cooldown | 0 | 0 | Maybe | Safety if churn occurs. |
| `limitedActivationConfig.allowedZones` | Zone allow list | empty | empty | Maybe | Empty means all zones. |
| `limitedActivationConfig.requireSamePlanet` | Same planet requirement | true | true | Yes | Safety. |
| `limitedActivationConfig.disableOnFirstActivationFailure` | Interval stop after failure | true | true | Yes | Safety. |
| `limitedActivationConfig.disableOnActivationFailure` | Emergency latch | false | false | Maybe | Stronger safety disabled. |
| `limitedActivationConfig.logActivationLifecycle` | Activation logs | true | true | Debug | Useful evidence. |
| `limitedActivationConfig.logHealthSummary` | Health summary logs | true | true | Debug | Useful evidence. |
| `demandProfileSimulationConfig.*` | Demand profile diagnostics | false defaults | enabled | Maybe | Does not directly move miners. |
| `demandStateSimulationConfig.*` | Demand state/reserve diagnostics | false defaults | enabled | Yes-ish | Feeds pressure model and reserve interpretation. |
| `demandWeightedMinerPlanSimulationConfig.*` | Canonical demand-weighted planning | false defaults | enabled | Yes | Despite "Simulation" name, this feeds current intelligent targeting. |
| `marketSupplyObservationConfig.*` | Market supply observation | false current | disabled | Maybe | Current disabled; no miner movement. |
| `stockpileSnapshotSimulationConfig.*` | Memory-only stockpile dashboard | false defaults | enabled | Maybe | No movement/mutation. |
| `aiEconomyPersistenceConfig.persistConceptualMinerTotals` | Persist conceptual totals | false | false | Maybe later | Disabled; do not conflate with acquisition. |
| `persistentStockpileDemandConfig.*` | Read persistent conceptual baseline | false defaults | enabled | Maybe | Read-only demand input. |
| `aiTravelSimulationConfig.*` | Dashboard-only travel planning | enabled defaults | enabled | Future | Does not move miners. |
| `stationedMinerConfig.enableStationedLifecycle` | Retain assignment as stationed | false C++ reset | true | Yes | Core current path. |
| `stationedMinerConfig.enableStationedRepeatedSampling` | Repeat sample while stationed | false C++ reset | true | Yes | Core P.3.4.2 path. |
| `stationedMinerConfig.stationedRequireDemandStillValid` | Require demand profile | true | true | Yes | Safety. |
| `stationedMinerConfig.stationedRequireResourceStillActive` | Require resource identity fields | true | true | Yes | Safety, but currently not a live resource-map lookup. |
| `stationedMinerConfig.stationedRequireSamePlanet` | Require target zone field | true | true | Yes | Safety, but currently checks field, not live agent zone. |
| `stationedMinerConfig.stationedClearWhenReserveSatisfied` | Stop when reserve met | true | true | Yes | Can intentionally stop repeated acquisitions. |
| `realResourceAcquisitionConfig.enableRealResourceAcquisition` | Future real acquisition switch | false | false | Future only | Must remain false. |
| `realResourceAcquisitionConfig.acquisitionReadinessDiagnosticsEnabled` | Readiness/ledger gate | true | true | Yes | Sim ledger requires it. |
| `realResourceAcquisitionConfig.enableSimulatedAcquisitionTransactions` | Runtime-only ledger | true | true | Yes | P.3.4 path. |
| `realResourceAcquisitionConfig.simulatedAcquisitionLogTransactions` | Log ledger transactions | true | true | Debug | Useful evidence. |
| `realResourceAcquisitionConfig.simulatedAcquisitionMaxLedgerEvents` | Recent row retention | 200 | 200 | Yes | Retention only, not lifetime cap. |
| `realResourceAcquisitionConfig.requireStationedLifecycle` | Acquisition requires stationed | true | true | Yes | Safety. |
| `realResourceAcquisitionConfig.requireVerifiedActivationPath` | Acquisition requires verified path | true | true | Yes | Safety. |
| `realResourceAcquisitionConfig.requireKnownResourceSpawnIdentity` | Acquisition requires exact resource identity | true | true | Yes | Safety. |
| `realResourceAcquisitionConfig.requireDemandStillValid` | Acquisition requires demand | true | true | Yes | Safety. |
| `realResourceAcquisitionConfig.requireReserveBelowTarget` | Acquisition stops when reserve met | true | true | Yes | Likely intentional stop condition. |
| `realResourceAcquisitionConfig.maxAcquisitionsPerInterval` | Future real acquisition cap | 0 when real disabled | 0 | Future only | Does not block simulated ledger. |

## Conflict Analysis

| Question | Classification | Answer |
| --- | --- | --- |
| Can old fake sampling run while stationed sampling is enabled? | Possible conflict | Not intentionally during `intelligentAssignmentStationed=true`, because stationed samples use `START_STATIONED_SAMPLE`. However legacy `startSimLoop()` has no stationed guard, so a stale retry/manual start could restart fake survey/sample. |
| Can old movement/kneel logic move a stationed miner away from the deposit? | Possible conflict | Normal stationed path stops arrival checks and does not call legacy movement. A stale `SimRetryTask` or unexpected `startSimLoop()` can call legacy `performSurvey()`/`goToResource()` and move away. |
| Can old sample completion clear a stationed assignment? | No conflict for successful station | Normal `finishSample()` does not clear assignments. `clearMinerIntelligentTargetAssignmentOnSampleComplete()` returns when stationed lifecycle is enabled. Failed stationed transition can clear with explicit reason and restart legacy loop. |
| Can old conceptual yield and simulated acquisition both fire for the same sample? | Confirmed, intended for intelligent samples | `finishIntelligentSample()` records simulated acquisition if retained, then records intelligent conceptual/resource-aware yield. This is expected while simulation-only stockpile math exists. |
| Can old assignment TTL or sample timeout clear a valid stationed miner? | Mostly no conflict | Normal TTL is skipped for active/stationed when `preventNormalTtlForActiveMovement=true`. Candidate/validated/queued/moving/sample timeouts still apply before stationing. There is no max stationed duration after P.3.4.2. |
| Can old fake behavior make the miner appear to disappear? | Possible conflict | Legacy movement can send the miner to random navmesh/fallback points. If a stale retry or fallback runs, the dashboard may still show assignment history while the visible bot walks away. |
| Can multiple controllers/tasks fight over the same miner? | Possible conflict | The controllers map has one controller per agent, but multiple scheduled `ArrivalCheckTask`, `SimBehaviorTask`, and `SimRetryTask` instances can target the same controller without a lifecycle generation/cancel token. |
| Can a miner be visually doing old behavior while dashboard says stationed? | Possible conflict | Visual sample animation is shared. Dashboard `stationed` plus `stationedSampleTicks`/`simulatedAcquisitionTransactions` is stronger proof than crouch/sample animation. Stale legacy movement while stationed remains possible until proven otherwise. |

## Runtime Evidence To Collect

Logs to grep:

- `MinerTargetingSwitchDecision`
- `MinerTargetingSwitchDecisionSummary`
- `MinerIntelligentTargetActivation`
- `MinerIntelligentTargetArrival`
- `MinerIntelligentTargetAssignment`
- `MinerIntelligentActivationHealth`
- `SimMiner simulated acquisition transaction`
- `SimMiner intelligent conceptual yield provenance`
- `SimMiner yield:`
- `SimMiner: Loop started`
- `SimMiner: Survey started`
- `SimMiner: Destination selected`
- `SimMiner: Sample started`
- `SimMiner: Path failed`

Dashboard/API fields to watch:

- `coveragePlanner.stationedMiners`
- `coveragePlanner.stationedSamplingEnabled`
- `coveragePlanner.stationedSampleTicks`
- `coveragePlanner.lastStationedSampleAgeSeconds`
- `simulatedAcquisition.acquisitions`
- `simulatedAcquisition.resourcesAcquired`
- `simulatedAcquisition.simulatedAcquisitionTransactions`
- `simulatedAcquisition.ledgerRetainedRows`
- `simulatedAcquisition.exactResourceTotals`
- `acquisitionReadiness.acquisitionReadyMiners`
- `acquisitionReadiness.acquisitionBlockedReasons`
- `minerActivity.coverageActiveCount`
- `minerActivity.sampleTimeoutCount`
- `minerActivity.movementArrivalTimeoutCount`
- recent assignment history clear reasons and lifecycle statuses.

Runtime proof points:

- Current agent world position vs assignment `targetX/targetY/targetZ`.
- Current movement destination vs assignment target hash.
- Controller state and pending scheduled task type if a task inventory becomes available.
- Whether a `SimMiner: Loop started` log appears after `action=stationed` for the same miner.
- Whether `SimMiner yield:` broad labels appear for a miner currently shown as stationed.
- Whether `stationedSampleTicks` increments every roughly 25 seconds while `simulatedAcquisitionTransactions` also increments.
- Whether `lastStationedSampleAgeSeconds` remains below one or two loop intervals for active stationed miners.

## Recommended Cleanup Plan

Do not implement this in P.3.4.3-pre. Suggested order:

1. Add a no-behavior-change task inventory/lifecycle generation diagnostic so stale `SimRetryTask`, `ArrivalCheckTask`, and `SimBehaviorTask` callbacks can be identified per miner.
2. Disable or isolate the legacy conceptual wander/survey/sample loop for miners that are eligible for intelligent targeting.
3. Make stationed lifecycle the single owner of repeated sampling. Legacy `startSimLoop()` should not run while stationed.
4. Keep crouch/sample/stop_sample as cosmetic animation inside stationed sampling, but rename logs so legacy sample and stationed sample are distinguishable.
5. Demote or remove obsolete `minerConfig.resources`, legacy survey/sample duration, search radius, fallback radius, and `clearOnSampleComplete` naming after runtime proof.
6. Add liveness/disappearance diagnostics: current position vs stationed target, current movement destination, last task source, controller state, and last sample source.
7. Continue toward real acquisition only after the single-owner sampling model is proven stable.

## Safety Check

Current audited Sim miner acquisition/sampling paths:

- Do not call `extractResource()`.
- Do not call `ResourceSpawn::createResource()`.
- Do not create a real `ResourceContainer`.
- Do not mutate player inventory.
- Do not mutate vendors, market, crafting, or credits.
- Do not write acquisition persistence.
- Do update runtime conceptual totals and runtime simulated acquisition ledger.

Important caveats:

- `SimPlayerManager.cpp` includes `ResourceContainer.h` and has market observation code that can inspect public listing resource containers when `resolveResourceContainers=true`; current Lua config has market observation disabled and container resolution false.
- `aiEconomyPersistenceConfig.persistConceptualMinerTotals=false`; if enabled later, it would persist aggregate conceptual totals, not exact acquisition inventory.
- SimPlayer/PvP lifecycle cleanup can call `destroyObjectFromDatabase(true)` for SimPlayer objects; this is unrelated to miner acquisition but is still a persistence-affecting lifecycle path.
- `simulatedAcquisition.wouldCreateResourceContainer=true` is a diagnostic statement of future intent, not an object creation call.

## Audit Conclusion

The intended exact-resource stationed lifecycle is present and active, but it is not yet the only miner behavior path. The legacy conceptual loop remains active as startup/default/fallback behavior and shares the same controller, movement, sample animation, and behavior-task dispatch with the intended path.

The most important conflict to resolve next is task ownership: old scheduled retry/behavior/arrival tasks need runtime proof or a lifecycle generation guard before we can be certain that stationed miners cannot be pulled back into legacy movement.

## P.3.4.3 Update - Single Owner Miner Work Loop

P.3.4.3 makes the exact-resource intelligent/stationed lifecycle the owner of SimMiner work whenever intelligent targeting and stationed lifecycle are enabled. In that mode, `SimMinerController::startSimLoop()` no longer starts the old broad conceptual survey/move/sample loop unless explicit legacy fallback config is enabled. Pending exact-resource assignments are activated, stationed miners remain waiting for the stationed sample tick, and unassigned intelligent miners wait for planner ownership instead of selecting `iron`, `gas`, `water`, or `copper`.

The controller now uses a `workLoopGeneration` token on path-find, arrival, retry, survey-finish, sample-finish, and stationed-sample tasks. The generation advances when an intelligent assignment is accepted, movement begins, a miner becomes stationed, an assignment clears, a stationed repeat sample begins, or the legacy loop is explicitly started. Stale callbacks are ignored without manager or logger side effects and do not move or sample the miner.

Legacy and intelligent runtime logs are separated:

- `SimMinerLegacyLoopSuppressed`
- `SimMinerLegacyLoopStarted`
- `SimMinerLegacySurveyStarted`
- `SimMinerLegacyMoveStarted`
- `SimMinerLegacySampleStarted`
- `SimMinerLegacySampleFinished`
- `SimMinerIntelligentMoveStarted`
- `SimMinerStationedSampleStarted`
- `SimMinerStationedSampleFinished`

The dashboard/API exposes `minerActivity.legacyLoopSuppressedCount`, `legacyLoopStartedCount`, `intelligentLoopStartedCount`, and `lastSuppressedLegacyReason`. Runtime validation should show suppression counts when old entry points would have run, zero unexpected `SimMinerLegacyMoveStarted` lines for intelligent miners, and continued stationed exact-resource sampling/acquisition rows.

Legacy behavior can be temporarily re-enabled for test miners with:

```lua
legacyMinerLoopConfig = {
    enableLegacyConceptualLoop = true,
    allowLegacyFallbackWhenNoIntelligentAssignment = true,
    allowLegacyFallbackAfterIntelligentFailure = true,
    logLegacySuppression = true,
}
```

The default runtime config keeps those flags false and also sets `minerIntelligentTargetingConfig.fallbackToConceptualLoop=false`.

## P.3.4.4 Update - Miner Recovery Diagnostics

Miner recovery is now a separate memory-only monitor for intelligent assignments. It does not own extraction or movement. It reads the existing assignment/controller state and classifies miners as `healthyStationed`, `healthyMoving`, `awaitingValidation`, `stalledMoving`, `stalledStationedSampling`, `farFromStationTarget`, `zoneMismatch`, `controllerMissing`, `minerMissing`, `deadOrIncapacitated`, `inCombat`, `resourceInvalid`, `needsReassignment`, or `unknown`.

The recovery task runs on `minerRecoveryConfig.stuckCheckIntervalSeconds`. With the default `dryRun=true`, it only logs `MinerRecoveryStatus` and `MinerRecoveryDecision` diagnostics and updates the dashboard. If dry-run is explicitly disabled, the only automatic mutation currently allowed is clearing a stuck intelligent assignment through the existing assignment clear path, guarded by `maxAutomaticRecoveriesPerInterval` and `maxRecoveriesPerMinerPerHour`.

Dashboard/API proof fields live under `minerRecovery`: tracked rows, healthy count, needs-attention count, actions taken/skipped, status/reason counts, current/target coordinates, distance-to-target, assignment/movement/sample/acquisition ages, expected next stationed sample, and last recovery action.

Nudge, teleport-to-station, and respawn replacement are intentionally disabled in this phase. The dashboard surfaces copyable current and target coordinates instead. No real resource acquisition, `ResourceContainer` creation, inventory/vendor/market/crafting/credit mutation, path validation relaxation, movement speed change, global NavArea behavior change, player/non-SimPlayer teleport, or persistence write was added.
