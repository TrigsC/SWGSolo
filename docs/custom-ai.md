# Custom AI Systems Overview

This fork contains several custom AI systems built on top of SWGEmu/Core3. Their shared goal is to make a low-population server feel more alive without requiring every interaction to come from a real player.

The current systems cover several different kinds of AI work:

- Simulated players that move through the world, gather resources conceptually, and roam as PvP targets.
- AI-powered NPC conversations routed through spatial chat.
- Smart service NPCs, including Doctor, Dancer, and Musician buffer NPCs.
- Local LLM integration through Ollama for NPC dialogue and intent classification.
- Engine-level extensions to `AiAgent` so Lua scripts can trigger selected gameplay effects.

This document describes the system as it currently exists. It intentionally treats current behavior as the source of truth, including places where design is incomplete or technical debt exists.

# System Inventory

## AiAgent Engine Extensions

### Purpose

The custom AiAgent engine extensions expose additional gameplay actions to Lua and add internal state needed by custom AI behaviors. These changes make it possible for Lua screenplays to ask NPCs to apply buffs, start entertainer performances, use force-like resource pools, and remain active as simulated players even when no real players are nearby.

### Main files

- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgent.idl`
- `MMOCoreORB/src/server/zone/objects/creature/ai/AiAgentImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/LuaAiAgent.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/ai/LuaAiAgent.h`
- `MMOCoreORB/src/server/zone/objects/creature/buffs/BuffCRC.h`

### Runtime responsibilities

`AiAgent.idl` declares the custom fields and native methods that must survive IDL regeneration. Current custom additions include:

- `currentForcePoints` and `maxForcePoints`.
- `forceRegenerationEvent`.
- Force helper methods such as `setMaxForce`, `getMaxForce`, `setCurrentForce`, `getCurrentForce`, `activateForcePowerRegen`, and `doForceRegen`.
- Medical buff bridge methods:
  - `healEnhanceCreatureTarget(CreatureObject target, String statKey)`
  - `wipeMedicalEnhanceBuffs(CreatureObject target)`
  - `wipeEnhanceBuffs(CreatureObject target, unsigned int flags)`
- SimPlayer flags:
  - `simPlayerBot`
  - `simAlwaysActive`
  - `getSimPlayerBot`
  - `setSimPlayerBot`
  - `getSimAlwaysActive`
  - `setSimAlwaysActive`

`AiAgentImplementation.cpp` contains the concrete gameplay behavior. Current responsibilities include:

- Applying medical enhance buffs for health, strength, constitution, action, quickness, and stamina.
- Wiping medical, dance, and music enhancement buffs according to bit flags.
- Healing wounds and shock wounds as part of buff wiping.
- Managing AI force regeneration through `AiForceRegenerationEvent`.
- Modifying despawn and behavior activation rules so SimPlayers can remain active even with no real players nearby.
- Adjusting `findNextPosition` behavior for SimPlayers.

`LuaAiAgent.cpp` and `LuaAiAgent.h` expose selected C++ behavior to Lua. These bindings are used by the Smart Doctor, Smart Dancer, Smart Musician, and older AI chat skill flows.

`BuffCRC.h` defines CRC constants for the custom medical and performance buffs. These constants are used by the custom buff bridge methods.

### Dependencies

- Lua scripts depend on `LuaAiAgent` methods being registered by name.
- `AiAgentImplementation.cpp` depends on `BuffCRC`, `BuffType`, `Buff`, `SkillManager`, `PerformanceManager`, and `EntertainingSession`.
- SimPlayer behavior depends on `AiAgent` custom flags to avoid ordinary despawn/idle behavior.
- IDL generation must preserve custom declarations and generated signatures.

## AI Chat System

### Purpose

The AI chat system lets NPCs respond to player spatial chat. It supports three major paths:

- Deterministic Smart Doctor routing.
- LLM-assisted recruiter intent classification.
- General LLM-flavored NPC dialogue.

### Main files

- `MMOCoreORB/bin/scripts/custom_scripts/ai_brain.lua`
- `MMOCoreORB/bin/scripts/custom_scripts/ai_config.lua`
- `MMOCoreORB/bin/scripts/custom_scripts/ai_logger.lua`
- `MMOCoreORB/bin/scripts/custom_scripts/ai_registry.lua`
- `MMOCoreORB/bin/scripts/screenplays/custom/aiGlobalChatHandler.lua`
- `MMOCoreORB/src/server/zone/objects/player/PlayerObjectImplementation.cpp`

### Runtime responsibilities

`ai_config.lua` centralizes the custom AI service configuration. It currently controls whether general/recruiter LLM behavior is enabled, which Ollama URL/model to use, the LuaSocket timeout value, whether Smart Doctor LLM flavor is allowed, and AI logging defaults.

`ai_brain.lua` is the current Lua AI service client. It uses LuaSocket and cjson when they are available:

- `socket.http`
- `ltn12`
- `cjson`

It sends HTTP requests to the configured URL. The default is:

```text
http://ollama_brain:11434/api/generate
```

The default configured model is:

```text
llama3.2
```

Public functions currently include:

- `AiBrain.getChatResponse(player_input, npc_profile, player_context, npc_context)`
- `AiBrain.getRecruiterIntent(player_input, player_stats_context)`
- `AiBrain.getDoctorFlavorLine(phase, slots, memoryTopic)`
- `AiBrain.askBrain(player_input, npc_profile, player_context, npc_context)`, a compatibility wrapper for older Padawan/legacy callers.

These public functions normalize nil or non-string inputs where practical and return deterministic fallback values when the LLM client, JSON parsing, or dependency loading fails.

`ai_logger.lua` is the central logging helper for custom AI Lua systems. It reads `AiConfig.logging`, formats messages as:

```text
[AI][doctor][WARN] message
```

The default logging level is `warn`, so error/warn diagnostics are visible while normal chat routing, heartbeat, and debug traces remain quiet. Current logging categories are:

- `doctor`
- `entertainer`
- `chat`
- `llm`
- `simplayer`
- `bridge`

`ai_registry.lua` maps NPCs to profiles. It currently supports lookup by internal creature template name, direct profile-key lookup for older callers, and fallback lookup by template path. Lookup is guarded so invalid scene objects or failed `LuaAiAgent` construction return nil instead of unwinding chat routing. Important profile concepts include:

- `role = "smart_doctor"`
- `role = "recruiter"`
- `call_signs`
- `system_prompt`
- `skills`

Current mapped examples include:

- `light_jedi_padawan` -> `padawan`
- `rebel_recruiter` -> `rebel_recruiter`
- `imperial_recruiter` -> `imperial_recruiter`
- `stormtrooper` -> `imperial_trooper`
- `commoner` -> `citizen`
- `smart_doctor_buffer` -> `smart_doctor`

`aiGlobalChatHandler.lua` attaches a `SPATIALCHATSENT` observer to players. The player login hook is currently in `PlayerObjectImplementation.cpp`, which calls:

```text
AiGlobalChatHandler:onPlayerLoggedIn(playerCreature)
```

Once a player sends spatial chat, the handler:

1. Reads the chat message.
2. Finds a nearby responder by keyword/call sign.
3. Falls back to the player's hard target if it has an AI profile.
4. Checks range.
5. Loads the responder profile from `AiRegistry`.
6. Routes by profile role.

### Dependencies

- Requires global Core3 Lua functions such as `createObserver`, `dropObserver`, `getChatMessage`, `spatialChat`, and `getSceneObject`.
- Depends on `AiRegistry.getProfile`.
- Depends on `AiBrain` for LLM-backed flows.
- Depends on recruiter screenplay functions for recruiter actions.
- Depends on `SmartDoctorBuffer` being loaded for `role = "smart_doctor"` routing.

## Smart Doctor

### Purpose

The Smart Doctor is the most developed service NPC behavior. It provides a deterministic doctor buffer flow with optional flavor dialogue. Core gameplay decisions are implemented in Lua; actual buff application is delegated to the C++/Lua bridge.

### Main files

- `MMOCoreORB/bin/scripts/screenplays/custom/smartDoctorBuffer.lua`
- `MMOCoreORB/bin/scripts/custom_scripts/smart_doctor_dialogue.lua`
- `MMOCoreORB/bin/scripts/custom_scripts/smart_doctor_config.lua`
- `MMOCoreORB/bin/scripts/mobile/corellia/smart_doctor_buffer.lua`

### Runtime responsibilities

`smartDoctorBuffer.lua` owns the live behavior:

- Spawns `smart_doctor_buffer` NPCs at configured/default spawn points.
- Handles player chat routed from `AiGlobalChatHandler`.
- Detects buff requests.
- Negotiates price confirmation.
- Charges player cash credits.
- Wipes existing medical enhancements.
- Applies a sequence of medical buff steps.
- Tracks current service target.
- Maintains an in-memory queue.
- Handles cancellation, timeout, range checks, combat checks, and pause/resume.
- Emits spatial chat responses.

The current default buff sequence is:

```text
health -> strength -> constitution -> action -> quickness -> stamina
```

Each buff step calls:

```lua
AiAgentBridge.applyMedicalBuffStep(pDoctor, pPlayer, stepKey)
```

Before buffing, the doctor attempts to wipe medical enhancement state by calling:

```lua
AiAgentBridge.wipeMedicalBuffs(pDoctor, pPlayer)
```

The bridge delegates to `LuaAiAgent(pDoctor):healEnhanceCreatureTarget(...)` and `LuaAiAgent(pDoctor):wipeEnhanceBuffs(..., 1)`. The `1` flag corresponds to the medical wipe path in `AiAgentImplementation.cpp`.

`smart_doctor_config.lua` is now loaded by `smartDoctorBuffer.lua` and owns gameplay/service tuning defaults such as price, buff sequence, timing, range, queue limits, and spawn points. If the config module fails to load, `smartDoctorBuffer.lua` keeps equivalent embedded fallback defaults so the behavior can still start. `AiConfig.smartDoctor.llmFlavorEnabled` remains authoritative for the optional Smart Doctor LLM flavor toggle; `SmartDoctorConfig` is for deterministic gameplay/service tuning.

### Queue flow

The current queue flow is:

1. Player asks for buffs.
2. If doctor is idle, player enters `NEGOTIATING`.
3. If doctor is busy, player is added to `st.queue`.
4. The doctor estimates queue ETA based on remaining steps and configured step delay.
5. When current work finishes or cancels, `advanceToNextTarget` pops the next valid queued player.

Queue membership is kept in Lua tables:

- `st.queue`
- `st.queueSet`

Current queue state is not fully persisted across restart or Lua state reset.

### Negotiation flow

The current negotiation flow is:

1. Player requests buffs.
2. Doctor quotes price.
3. A confirmation timeout event is scheduled.
4. Player confirms, declines, cancels, or times out.
5. On confirmation, doctor validates range/combat/dead state.
6. Doctor charges credits.
7. Doctor starts buffing.

Confirmation words include values such as:

- `yes`
- `y`
- `ok`
- `okay`
- `do it`
- `sure`
- `sounds good`
- `yep`

Decline/cancel words include values such as:

- `no`
- `nope`
- `nah`
- `cancel`
- `stop`
- `abort`
- `nevermind`

### Payment handling

Payment is handled inside `tryChargePlayer` in `smartDoctorBuffer.lua`.

Current behavior:

- Checks `CreatureObject(pPlayer):getCashCredits()`.
- Calls `CreatureObject(pPlayer):subtractCashCredits(price)`.
- Sends a system message confirming purchase.

The current default price is `5000` credits from `SmartDoctorConfig.price`, with an equivalent fallback in `smartDoctorBuffer.lua` if the config module cannot be loaded.

### Buff application sequence

Buffing is event-driven:

1. `startBuffingNow` sets state to `BUFFING`.
2. It schedules `applyNextStep`.
3. `applyNextStep` validates the target.
4. It says progress lines at selected steps.
5. It calls `applyBuffStep`.
6. It increments `stepIndex`.
7. It schedules the next step after `SmartDoctorConfig.step_delay_ms`.
8. When all steps are complete, it clears current state and advances the queue.

Default step delay is `4500` ms.

### Persistence behavior

The Smart Doctor uses two kinds of state:

- Lua global state in `_G.SmartDoctorGlobalState`.
- Core3 datastore values through `readData` and `writeData`.

Persisted values include:

- Current doctor state.
- Negotiating player id.
- Negotiation expiration.
- Current player id.
- Current buff step index.
- Pause start time.

Notably, the full queue is not persisted.

Ephemeral player memory is stored in `_G.SmartDoctorBuffer_Memory`. It is used for small memory topics in completion dialogue and has a TTL.

### Dialogue behavior

`smart_doctor_dialogue.lua` provides deterministic fallback dialogue. It also contains LLM flavor code through `AiBrain.getDoctorFlavorLine`, but that path is disabled by default unless `AiConfig.smartDoctor.llmFlavorEnabled == true`.

Current effective behavior is deterministic dialogue.

### Current limitations

- Queue state is not fully persisted.
- `pcall` protects Lua errors but cannot reliably protect against all C++ binding failures.
- The doctor behavior still owns the full state machine and calls bridge methods directly.

## Smart Entertainers

### Purpose

Smart Entertainers provide simple NPC service behaviors for performance buffs. They are smaller than Smart Doctor and currently implemented as separate Dancer and Musician screenplays.

### Main files

- `MMOCoreORB/bin/scripts/screenplays/custom/smartDancerBuffer.lua`
- `MMOCoreORB/bin/scripts/screenplays/custom/smartMusicianBuffer.lua`
- `MMOCoreORB/bin/scripts/custom_scripts/smart_entertainer_helper.lua`

### Spawn behavior

Both scripts define their own spawn point tables and spawn NPCs through `spawnMobile`.

Current behavior:

- Smart Dancer spawns `entertainer` mobiles at cantina-like locations.
- Smart Musician also spawns `entertainer` mobiles.
- Both assign custom object names using safe name-only calls.
- Both register as global screenplays in `screenplays.lua`.

### Performance behavior

Smart Dancer:

- Starts dancing through `AiAgentBridge.startDance`.
- Periodically checks whether the NPC is still dancing.
- Restarts dancing if interrupted.

Smart Musician:

- Gives the NPC a configured instrument, defaulting to `object/tangible/instrument/slitherhorn.iff`.
- Tries to equip the instrument through containment transfer.
- Starts music through `AiAgentBridge.startMusic`.
- Periodically checks whether the NPC is still playing.
- Re-equips/restarts music if interrupted.

### Buff behavior

Smart Dancer:

- Registers `WASWATCHED`.
- On watch, schedules a short deferred buff event, then rechecks player status and range.
- Calls `AiAgentBridge.wipeDanceBuffs`.
- Calls `AiAgentBridge.applyDanceMindBuff`.

Smart Musician:

- Registers `WASLISTENEDTO`.
- On listen, schedules a short deferred buff event, then rechecks player status and range.
- Calls `AiAgentBridge.wipeMusicBuffs`.
- Calls `AiAgentBridge.applyMusicBuffs`.

The bridge delegates to the raw `LuaAiAgent` bindings. The `2` and `4` flags correspond to dance and music enhancement wipe paths in C++.
The deferred event keeps buff mutation out of the immediate `/watch` and `/listen` observer callback, where `PlayerManager` may still be holding entertainer/player locks.

### Shared concepts

The two scripts remain separate behaviors but share `smart_entertainer_helper.lua` for generic safe operations:

- Spawn-point driven service NPCs.
- Safe custom naming.
- Range checks.
- Heartbeat events to maintain performance.
- Bridge calls through `AiAgentBridge`.
- Immediate buff application after watch/listen observer events.

Behavior-specific details such as spawn locations, observer names, dance/song choices, buff values, and Musician instrument handling remain in the individual screenplay files.

## SimPlayer System

### Purpose

The SimPlayer system is a C++-heavy behavior system for simulated player-like NPCs. It supports configurable spawn groups, pathfinding-based movement, PvP roaming, and conceptual resource gathering behavior.

### Main files

- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerManager.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPlayerController.cpp`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.h`
- `MMOCoreORB/src/server/zone/objects/creature/simplayer/SimPvPController.cpp`
- `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`
- `MMOCoreORB/src/server/zone/ZoneServerImplementation.cpp`

### Spawn management

`ZoneServerImplementation.cpp` calls:

```text
SimPlayerManager::instance()->initialize()
```

`SimPlayerManager` loads `scripts/managers/sim_player_manager.lua` using its own Lua instance. That config defines:

- `enabled`
- `shuttleports`
- `spawnGroups`

Current configured groups include:

- `miner` with `totalCount = 0`
- `pvp_solo` with `totalCount = 3`

As of the inspected code, PvP roamers are enabled by config and miners are disabled by count.

### PvP roaming

PvP bots are controlled by `SimPvPController`.

Current behavior:

- Spawn at a shuttleport.
- Move to a configured hangout location.
- Loiter for a randomized time.
- Scan nearby player creatures.
- Attack enemy faction players if valid and attackable.
- Return to shuttle.
- Wait for shuttle readiness.
- Cycle to a new location through `SimPlayerManager`.
- Recycle after death with a delay.

Faction is inferred from config or template names such as `rebel`, `imperial`, or `stormtrooper`.

### Shuttle travel

The system does not appear to use normal player travel commands. Instead, it simulates travel by cycling the bot:

1. Bot returns to shuttle.
2. `cyclePvPBotWhenShuttleReady` checks the nearest shuttle travel point.
3. Once boardable, or after timeout, it calls `cyclePvPBot`.
4. The old bot is cleaned up.
5. A replacement is spawned at a new configured shuttleport.

This creates the appearance of roaming between locations.

Operational stability rule: SimPlayer recycle code should keep AiAgent object-lock scopes small. Shuttle readiness checks touch `PlanetManager`, and old-bot cleanup calls `destroyObjectFromWorld` / `destroyObjectFromDatabase`; these should run outside the old bot's own `Locker` scope to avoid silent lock inversions during timed roam/recycle tasks.

### SimMiner / Resource Gatherer Behavior

`SimMinerController` is the current simulated resource gatherer controller. It is implemented in `SimPlayerController.h` and `SimPlayerController.cpp`; there is no separate `SimMinerController.cpp` in the inspected tree.

Current enabled status:

| Source | Current value | Runtime effect |
|---|---:|---|
| `SimPlayerManagerConfig.enabled` | `true` | Allows `SimPlayerManager` to load config and spawn configured groups. |
| Miner spawn group `totalCount` | `0` | No miners spawn from default startup config. |
| Miner spawn group `templates` | `light_jedi_sentinel`, `artisan` | Would be used if `totalCount` were raised above zero. |
| Miner spawn group `behavior` | `gather_resources` | Loaded into `SpawnGroup.behavior`, but not used by controller selection. Non-`pvp` groups become miners. |
| Miner spawn group `minerConfig` | Present with current defaults | Configures conceptual resource names, survey/sample timings, movement search radii, fallback radius, optional state-transition logging, and memory-only conceptual yield accounting. |

Startup and spawn flow:

1. `ZoneServerImplementation.cpp` calls `SimPlayerManager::instance()->initialize()`.
2. `SimPlayerManager::initialize` calls `loadLuaConfig` and then `spawnConfiguredGroups`.
3. `loadLuaConfig` runs `scripts/managers/sim_player_manager.lua` through a dedicated C++ `Lua` instance.
4. The manager loads `enabled`, `shuttleports`, and each spawn group's `type`, `totalCount`, `behavior`, `faction`, `templates`, and optional `minerConfig`.
5. `spawnConfiguredGroups` loops each group `totalCount` times. Because the miner count is currently `0`, the miner group is skipped.
6. If enabled in the future, `spawnFromConfig` would pick a random shuttleport and random template, spawn an AiAgent creature, set SimPlayer flags, and choose the controller.
7. Controller selection is based on `g.type.beginsWith("pvp")`. Non-PvP groups, including the current `type = "miner"`, get `SimMinerController`.

Controller state machine:

| State | Used by miner today | Meaning in current miner flow |
|---|---|---|
| `IDLE` | Yes | Initial state and retry/holding state. |
| `DECIDING` | Yes | Set when miner starts a new loop and chooses a resource string. |
| `SURVEYING` | Yes | Set while the miner performs the survey animation and waits for the survey timer. |
| `CALCULATING_PATH` | Yes | Set by shared `moveTo` while pathfinding is requested. |
| `PERFORMING_ACTION` | No direct miner assignment found | Declared and available to shared/older behavior, but current miner survey/sample phases use `SURVEYING` and `SAMPLING`. |
| `MOVING` | Yes | Set after a valid path is returned. |
| `SAMPLING` | Yes | Set while the miner crouches, plays the sample animation, and waits for the sample timer. |
| `WAITING` | Shared | Used by shared dead/incap handling; not part of normal miner gather loop. |

Current miner loop:

1. `startSimLoop` sets state to `DECIDING`.
2. `pickRandomResource` picks one conceptual resource string from `minerConfig.resources`, falling back to `iron`, `gas`, `water`, and `copper` if the configured list is missing or empty.
3. `performSurvey` sets state to `SURVEYING`, sets movement state to `OBLIVIOUS`, ensures upright posture, plays `manipulate_high`, and schedules a `SimBehaviorTask` using `minerConfig.surveyDurationMs`, currently 4000 ms by default.
4. `finishSurvey` calls `goToResource(targetResource)`.
5. `goToResource` chooses a movement destination. The selected resource name is not used to query real resource pools.
6. `moveTo` schedules `SimPathFindTask`, which calls `PathFinderManager::instance()->findPath`.
7. `onPathFound` copies the returned path, clears follow/watch/target/combat state, clears patrol points, writes blackboard `moveMode = RUN`, queues patrol nodes, switches the AiAgent to `PATROLLING`, activates AI behavior, and schedules `ArrivalCheckTask`.
8. `checkArrival` runs repeatedly while moving. It queues more path nodes, calls `findNextPosition(2.0f, false)`, detects arrival within roughly 4 meters, and uses a stuck watchdog to reapply the next step and reactivate AI behavior.
9. `onArrived` calls `performSample`.
10. `performSample` sets state to `SAMPLING`, clears patrol points, sets movement state to `OBLIVIOUS`, crouches the agent, plays `sample`, and schedules a `SimBehaviorTask` using `minerConfig.sampleDurationMs`, currently 15000 ms by default.
11. `finishSample` records an optional memory-only conceptual yield for the selected resource, returns the agent upright, plays `stop_sample`, and calls `startSimLoop` again.

Movement and destination behavior:

- `pickDestinationInNavMesh` first requires `agent->isInNavMesh()`.
- It chooses a radius from `minerConfig.minSearchRadius` to `minerConfig.maxSearchRadius`, currently equivalent to the old approximately 100 to 200 meter search range.
- It calls `PathFinderManager::instance()->getSpawnPointInArea(area, zone, result, true)`.
- If no navmesh destination is found, `goToResource` falls back to a random point using `minerConfig.fallbackRadius`, currently 100 meters, and uses `zone->getHeight` for Z.
- The shared pathing code requests a path after 100 ms and retries failed/short paths after 5000 ms.

Resource and economy behavior:

The current miner does not create real resources. It simulates activity visually and records optional conceptual amounts in memory only.

Static inspection found:

- No calls from `SimMinerController` into `ResourceManager`.
- No calls into `ResourceSpawn`.
- No creation of `ResourceContainer` objects.
- No inventory transfer.
- No credits, vendor, bazaar, auction, harvester, crafting, or database persistence integration.
- `ResourceManager.h` and `ResourceSpawn.h` are included in `SimPlayerController.cpp`, but the inspected miner methods do not use those APIs.

Conceptual accounting:

- `SimMinerController::finishSample` calls `recordConceptualYield` when `minerConfig.yieldConfig.enabled` is true.
- `recordConceptualYield` chooses a random amount between `yieldConfig.minAmount` and `yieldConfig.maxAmount`.
- `SimPlayerManager::recordConceptualMinerYield` stores aggregate totals in `SimPlayerManager::conceptualMinerTotals`, keyed by conceptual resource string.
- Totals are C++ memory only. They are not persisted, exposed to players, turned into game objects, or connected to resource pools.
- Optional yield logs are controlled by `minerConfig.yieldConfig.logYield`, which defaults to `false`.
- Optional periodic summary logs are controlled by `minerConfig.summaryConfig`, which defaults to disabled.

Config values and actual consumption:

| Config field | Consumed by C++ | Current miner effect |
|---|---|---|
| `enabled` | Yes | If false, no SimPlayer groups spawn. |
| `shuttleports` | Yes | Provides possible spawn locations for all groups, including miners if enabled. |
| `spawnGroups[].type` | Yes | Non-`pvp` type selects `SimMinerController`. |
| `spawnGroups[].totalCount` | Yes | Controls how many miners would spawn; current value is `0`. |
| `spawnGroups[].templates` | Yes | Provides random miner creature templates; fallback is `artisan` if empty. |
| `spawnGroups[].behavior` | Loaded only | Stored but not used for miner behavior selection. |
| `spawnGroups[].faction` | Loaded, mostly PvP-oriented | Not meaningful for current miner loop. |
| `spawnGroups[].minerConfig.resources` | Yes | Provides conceptual resource labels. Missing or empty lists fall back to `iron`, `gas`, `water`, `copper`. |
| `spawnGroups[].minerConfig.surveyDurationMs` | Yes | Survey animation delay. Default is 4000 ms. Values are clamped to a safe range. |
| `spawnGroups[].minerConfig.sampleDurationMs` | Yes | Sampling animation delay. Default is 15000 ms. Values are clamped to a safe range. |
| `spawnGroups[].minerConfig.minSearchRadius` | Yes | Minimum navmesh search radius. Default is 100 meters. Values are clamped. |
| `spawnGroups[].minerConfig.maxSearchRadius` | Yes | Maximum navmesh search radius. Default is 200 meters. If lower than min, it is raised to min. |
| `spawnGroups[].minerConfig.fallbackRadius` | Yes | Random fallback movement radius when navmesh destination selection fails. Default is 100 meters. |
| `spawnGroups[].minerConfig.logStateTransitions` | Yes | Optional miner state-transition logs. Default is `false`, so normal gameplay remains quiet. |
| `spawnGroups[].minerConfig.yieldConfig.enabled` | Yes | Enables memory-only conceptual accounting after sample completion. Default is `true`, but no miners spawn while `totalCount = 0`. |
| `spawnGroups[].minerConfig.yieldConfig.minAmount` | Yes | Minimum conceptual yield per completed sample. Default is 5 and values are clamped. |
| `spawnGroups[].minerConfig.yieldConfig.maxAmount` | Yes | Maximum conceptual yield per completed sample. Default is 25, values are clamped, and values lower than min are raised to min. |
| `spawnGroups[].minerConfig.yieldConfig.logYield` | Yes | Optional per-sample conceptual yield log. Default is `false`, so normal gameplay remains quiet. |
| `spawnGroups[].minerConfig.summaryConfig.enabled` | Yes | Enables periodic read-only summary logging for conceptual miner totals and active miner count. Default is `false`. |
| `spawnGroups[].minerConfig.summaryConfig.intervalSeconds` | Yes | Summary logging interval. Default is 300 seconds, clamped between 30 and 3600 seconds. |

Logs and debug output:

- `DEBUG_SIMPLAYER` in `SimPlayerManager.cpp` gates most manager startup/spawn/cycle logs.
- `DEBUG_SIMPVP` in `SimPlayerController.cpp` gates shared movement logs and the miner logs.
- Miner-specific state logs can also be enabled per miner spawn group with `minerConfig.logStateTransitions = true`.
- Miner-specific debug strings include loop start, selected conceptual resource, survey start/finish, destination selection, path failure/retry, arrival, sample start, and sample completion.
- Conceptual yield logs can be enabled per miner spawn group with `minerConfig.yieldConfig.logYield = true`; logs include resource label, generated amount, source bot object id, and aggregate total.
- Periodic summary logs can be enabled per miner spawn group with `minerConfig.summaryConfig.enabled = true`. The manager logs a compact line with active miner count and current conceptual totals at `summaryConfig.intervalSeconds`.
- Summary logging is read-only and skips completely empty summaries when there are no active miners and no conceptual totals.
- `SimPathFindTask` always logs `SimPlayer: [Thread] EXCEPTION in findPath!` if pathfinding throws, even when debug macros are disabled.
- The Lua config has commented-out `print` debug checks.

Stability considerations:

- `SimPathFindTask`, `ArrivalCheckTask`, `SimBehaviorTask`, and `SimRetryTask` all hold weak references to the controller and bounce work back through `Core::getTaskManager()->executeTask`.
- `SimBehaviorTask` re-resolves the miner controller inside the task-manager lambda from a captured strong base-controller reference. It should not capture raw delayed `SimMinerController*` pointers because survey/sample callbacks can run after a controller is stopped or recycled.
- The periodic miner summary task is manager-owned and calls back through `SimPlayerManager::instance()`; it does not capture controller or AiAgent pointers.
- `checkArrival` locks the AiAgent while examining combat/death/movement state and while updating patrol movement.
- The current miner can schedule repeated arrival checks every 500 ms while moving and every 1000 ms while waiting, incapped, or in combat.
- Pathfinding failure schedules a retry after 5000 ms by calling `startSimLoop` again.
- If miners are enabled, they will be always-active SimPlayers with `simAlwaysActive`, `simPlayerBot`, and `despawnOnNoPlayerInRange(false)` set by the manager.

Known limitations:

- Miners are disabled by default via `totalCount = 0`.
- Resource names are configurable conceptual labels, but they are not tied to the live SWG resource pool.
- The selected resource string does not influence destination selection.
- Surveying and sampling are visual/conceptual only; no real extraction occurs.
- Miner tuning values are exposed through Lua config, but only for the current conceptual loop.
- The loaded `behavior = "gather_resources"` value is descriptive only in current controller selection.
- Conceptual totals reset on server restart.
- Conceptual totals are not visible to players.
- Conceptual totals are not connected to vendors, crafting, resource pools, harvesters, credits, containers, inventory, or persistence.
- Active miner count in summary logs means currently tracked `SimMinerController` entries in `SimPlayerManager::controllers`, not a persisted population metric.
- There is no miner market output or crafting input.

Suggested future phases:

| Phase | Goal | Notes |
|---|---|---|
| Phase A | Make current miner behavior observable and configurable. | Implemented for conceptual resources, survey/sample durations, movement radii, and optional state-transition logging. Miner count remains disabled by default. |
| Phase B | Record conceptual gathered resource amounts in memory/log only. | Implemented with manager-owned in-memory aggregate totals and optional yield logging. No real economy systems are touched. |
| Phase B.2 | Add periodic read-only miner summary logging. | Implemented with disabled-by-default `summaryConfig`; empty zero-miner summaries are skipped to avoid log noise. |
| Phase C | Persist abstract resource inventory safely. | Store simple per-miner or per-system resource counters without creating SWG resource containers yet. |
| Phase D | Sell resource lots through a controlled vendor/market abstraction. | Introduce a constrained output path with caps, pricing rules, and audit logs. |
| Phase E | Connect to crafting/economy loops. | Only after resource accounting, persistence, and market limits are stable. |

### Phase C - Persistence Architecture Research

This section documents persistence research for future AI economy state. It is architecture guidance only; no persistence is implemented by the current SimMiner work.

Current state:

- SimMiner conceptual totals live in `SimPlayerManager::conceptualMinerTotals`.
- Totals are memory-only and reset on server restart.
- SimMiner output does not create `ResourceContainer` objects, credits, vendor stock, auction entries, crafting inputs, harvesters, player inventory, or database records.
- Summary logging and per-yield logging are observability tools only.

Existing Core3 persistence patterns inspected:

| Pattern | Examples | How it works | Fit for AI economy |
|---|---|---|---|
| Persistent game objects through `ObjectManager` and Berkeley/ObjectDatabase | `sceneobjects`, `playerstructures`, `buffs`, `guilds`, `cityregions`, `resourcespawns`, `frsmanager`, `frsdata` loaded in `ObjectManager.cpp` | Objects are created or loaded through `ObjectManager`, serialized through `ManagedObject`/IDL/generated serialization, marked persistent, and flushed by Core3's object database update/commit threads. | Best fit for authoritative AI economy state if represented as a small manager-owned data object. |
| Manager-owned persisted data object | `FrsManagerImplementation::loadFrsData` loads `frsmanager`; `FrsManagerData.idl` stores manager state; missing data is created and persisted. | Runtime manager stays transient, but durable state lives in a separate `ManagedObject` persisted in a dedicated object database. | Strongest precedent for AI economy state. Keeps behavior code separate from durable state and uses existing recovery flow. |
| Persisted gameplay objects and data components | Droid module data components, vendor data components, structure permission lists, buffs, missions | State is stored on actual game objects or components using `toBinaryStream`/`parseFromBinaryStream` or `addSerializableVariable`, then saved with the owning object. | Good for future real objects, but poor for conceptual economy state because it would couple AI economy data to player/game object lifecycles. |
| Server MySQL via `ServerDatabase` | Account/session/schema metadata, character lists, login/account data | MySQL schema migrations are managed in `ServerDatabase::updateDatabaseSchema`; queries are explicit SQL. | Useful for operational/account data, but higher risk and heavier than needed for abstract in-game economy counters. Requires schema changes and careful migration policy. |
| Lua/config files | `sim_player_manager.lua`, AI config files | Defines startup configuration and tunables. | Good for defaults and tuning only. Runtime economy state should not be written back to Lua config. |
| Manager-only memory | Current `SimPlayerManager::conceptualMinerTotals` | Fast, simple, and safe while experimenting. | Good for Phase B observability, but insufficient once server restart should preserve economy state. |

Recommended persistence approach:

Use a dedicated manager-owned persisted data object backed by Core3's existing object database system. The future shape should be similar to FRS:

1. Add an AI economy data object, for example `AiEconomyData`, generated through IDL and persisted as a `ManagedObject`.
2. Store it in a dedicated object database, for example `aieconomy`, loaded by `ObjectManager` during database initialization.
3. Let a future `AiEconomyManager` or `SimPlayerManager` load that object on startup by iterating the database, similar to `FrsManagerImplementation::loadFrsData`.
4. If the object is missing, create one default data object and persist it.
5. Keep runtime controllers and SimPlayers transient; they should update the manager-owned economy data through narrow methods.
6. Save by updating the persistent data object through the normal object database dirty/update flow, not by writing ad hoc files.

Why this is preferred:

- It follows an existing Core3 manager-state pattern instead of inventing a parallel save system.
- It avoids real SWG resource containers and player-facing economic objects until the conceptual model is stable.
- It can use generated serialization for maps, counters, timestamps, and version fields.
- It has a clear recovery model: load one authoritative economy state object at manager startup or create defaults if absent.
- It keeps future schema migration inside Core3's object/IDL evolution rather than hand-maintained custom files.

Persistence options comparison:

| Option | Advantages | Disadvantages | Recovery behavior | Operational risk | Suitability |
|---|---|---|---|---|---|
| Dedicated persisted `ManagedObject` in a new object database | Matches FRS manager-state precedent; works with Core3 serialization, object IDs, update threads, and database tooling; avoids SQL schema churn. | Requires IDL and object database registration in a future PR; needs versioning discipline. | Manager loads existing object; if missing, creates default object; if corrupt, can fail closed and keep economy disabled. | Medium, because it touches object persistence, but bounded and idiomatic. | Recommended for Phase C. |
| Reuse an existing persisted manager database such as `frsmanager` or unrelated tables | No new database registration. | Pollutes unrelated systems and makes support/debugging confusing. | Ambiguous ownership and cleanup. | High. | Not recommended. |
| MySQL tables through `ServerDatabase` | Easy to query externally; explicit schema; good for analytics. | Requires schema migrations, SQL escaping, operational DB coupling, and more policy around transactions. | Depends on table existence and migration success. | Medium to high. | Possible later for analytics/reporting, not first durable game-state storage. |
| Manager-owned JSON/save file | Easy to inspect and hand edit; no IDL. | Bypasses Core3 object database conventions, needs atomic write/backup logic, path configuration, corruption handling, and permissions handling. | Must invent load/repair/fallback behavior. | Medium. | Not recommended unless object DB proves impractical. |
| Store conceptual state on SimPlayer NPC objects | Uses existing object persistence if SimPlayers become persistent. | SimPlayers are currently spawned/transient service actors; controller lifecycle and despawn/recycle would threaten economy state. | Lost if NPC object is destroyed or recreated incorrectly. | High. | Not recommended. |
| Store conceptual state as real `ResourceContainer`/inventory/vendor objects | Integrates with economy eventually. | Violates current conceptual boundary; risks players receiving fake resources before rules are ready. | Complex and player-visible. | High. | Future Phase E or later only, after abstraction is proven. |
| Keep memory-only forever | Very low implementation complexity. | Restart erases economy history and prevents long-running supply/demand simulation. | No recovery. | Low runtime risk, high product limitation. | Good only through Phase B/B.2. |

Recommended persisted categories:

| Category | Persist? | Reason |
|---|---|---|
| Conceptual resource totals by label | Yes | This is the first durable inventory layer for AI economy loops. |
| Conceptual inventory by AI role/group | Yes, once roles exist | Future SimCrafters, vendors, and consumers need stable inventories independent of individual spawned NPC lifetimes. |
| Supply metrics | Yes | Supply/demand balancing needs restart-stable trend inputs. |
| Demand metrics | Yes | Demand should evolve over real server uptime and survive restarts. |
| Production statistics | Yes, aggregated | Useful for tuning and audit logs; keep raw event history bounded. |
| Consumption statistics | Yes, aggregated | Needed for demand modeling and future vendor/crafter loops. |
| Vendor inventory state | Yes, when conceptual vendors exist | Should survive restart, but should remain conceptual until a controlled market abstraction exists. |
| Economic trend snapshots | Yes, bounded rolling window | Store coarse time buckets, not unbounded event logs. |
| Active SimMiner count | No | Runtime observation only; derives from current controllers. |
| Current SimMiner destinations/resources in progress | Usually no | Losing in-progress work on restart is acceptable; completed output should be the durable boundary. |
| Debug/summary log state | No | Logs are operational output, not economy state. |
| LLM conversation state | No for economy Phase C | Keep separate from economy persistence unless a future memory system is explicitly designed. |

Recommended save cadence:

- Record conceptual changes in memory immediately through manager APIs.
- Mark the persisted economy data dirty after meaningful state changes.
- Flush on a coarse interval, such as every 5 to 15 minutes, to avoid writing after every sample.
- Force a save during orderly shutdown if the manager lifecycle provides a safe hook.
- Keep per-event logging independent from persistence; logging should not be required for recovery.

Restart recovery behavior:

1. Load the dedicated AI economy object database during object database initialization.
2. At AI economy manager startup, load the single economy state object.
3. Validate version, resource labels, non-negative counters, and timestamp sanity.
4. If missing, create a default empty economy state.
5. If corrupt or incompatible, fail closed: disable AI economy production, keep SimMiner visual behavior safe, and log a clear error.
6. Never synthesize real resources or player-visible goods during recovery.

Failure scenarios to design for:

- Server crash after memory totals change but before flush: accept bounded loss up to the save interval.
- Corrupt persisted object: keep a backup/export path or recovery command in a later PR; do not let corrupt economy state block normal server startup.
- Version mismatch after IDL changes: include a version field and migration path.
- Counter overflow or bad values: clamp or reject negative/absurd totals on load.
- Partial future feature rollout: economy persistence should tolerate missing roles such as crafters/vendors/consumers.
- Accidental player-facing integration: keep conceptual inventory APIs separate from real resource/container APIs until a deliberate economy phase connects them.

Future implementation phases:

| Phase | Goal | Notes |
|---|---|---|
| Phase C.1 | Define persisted economy data shape. | Create an IDL design for `AiEconomyData` with version, resource totals, role inventories, coarse stats, and timestamps. |
| Phase C.2 | Add database registration and load-only bootstrap. | Register a dedicated object database and load/create the data object without changing miner output yet. |
| Phase C.3 | Save conceptual miner totals. | Periodically copy `SimPlayerManager` totals into the persisted data object; keep restart loss bounded by interval. |
| Phase C.4 | Add admin/debug inspection. | Add read-only logs or tools to inspect persisted totals without player-facing economy effects. |
| Phase C.5 | Add migrations and repair tooling. | Add version migration, backup/export, and safe reset options before expanding the economy. |

Open questions:

- Should AI economy ownership remain in `SimPlayerManager`, or should a new `AiEconomyManager` own durable state while SimPlayers report production events?
- Should conceptual totals be galaxy-wide, planet-specific, resource-type-specific, or role-specific from the first persisted version?
- What is the acceptable crash-loss window for a low-population solo server: 5 minutes, 15 minutes, or one server tick batch?
- How much history is useful for supply/demand without creating unbounded database growth?
- Should persistence be disabled by default until migration and admin reset tooling exists?

### SWG Resource System Research

This section captures how the real Core3 resource system works so future AI economy persistence does not hard-code around the current SimMiner placeholder labels.

Main files inspected:

- `MMOCoreORB/src/server/zone/managers/resource/ResourceManager.idl`
- `MMOCoreORB/src/server/zone/managers/resource/ResourceManagerImplementation.cpp`
- `MMOCoreORB/src/server/zone/managers/resource/ResourceShiftTask.h`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/ResourceSpawner.h`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/ResourceSpawner.cpp`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/resourcetree/ResourceTree.h`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/resourcetree/ResourceTree.cpp`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/resourcetree/ResourceTreeEntry.h`
- `MMOCoreORB/src/server/zone/managers/resource/resourcespawner/resourcetree/ResourceAttribute.h`
- `MMOCoreORB/src/server/zone/objects/resource/ResourceSpawn.idl`
- `MMOCoreORB/src/server/zone/objects/resource/ResourceSpawnImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/resource/ResourceContainer.idl`
- `MMOCoreORB/src/server/zone/objects/resource/ResourceContainerImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/resource/SpawnDensityMap.h`
- `MMOCoreORB/bin/scripts/managers/resource_manager.lua`
- `MMOCoreORB/bin/scripts/managers/resource_manager_spawns.lua`
- Crafting examples under `MMOCoreORB/bin/scripts/object/draft_schematic`, `MMOCoreORB/bin/scripts/object/weapon`, and `MMOCoreORB/bin/scripts/object/tangible/food`

#### Resource hierarchy

Core3 does not treat resources as flat labels such as `iron` or `water`. It builds a resource tree from the client data table `datatables/resource/resource_tree.iff` in `ResourceTree::buildTreeFromClient`. Each `ResourceTreeEntry` contains:

- `type`: the final internal resource type, such as `copper_borocarbitic`.
- Plain-English class chain, such as `Inorganic`, `Mineral`, `Metal`, `Non-Ferrous Metal`, `Copper`, `Conductive Borcarbitic Copper`.
- STF/internal class chain, such as `inorganic`, `mineral`, `metal`, `metal_nonferrous`, `copper`, `copper_borocarbitic`.
- Per-type attribute ranges.
- Pool limits (`mintype`, `maxtype`, `minpool`, `maxpool`).
- Zone restriction, if the type is planet-specific.
- Survey tool type.
- Resource container template CRC.
- Random-name class for generated spawn names.

The active in-game resource is a `ResourceSpawn`, not the tree entry itself. A `ResourceSpawn` stores a generated `spawnName`, final `spawnType`, class chains, generated stat values, active planet spawn maps, spawned/despawned timestamps, survey tool type, container CRC, and extraction counters.

Example from `resource_manager_spawns.lua`:

| Layer | Example value |
|---|---|
| Category/root | `Inorganic` |
| Resource category | `Mineral` |
| Resource class | `Metal` |
| Resource subclass | `Non-Ferrous Metal` |
| Family/type group | `Copper` |
| Specific resource type | `Conductive Borcarbitic Copper` / `copper_borocarbitic` |
| Spawn name | `Ababuglu` |

Other examples:

- `iron_kammris`: `Inorganic -> Mineral -> Metal -> Ferrous Metal -> Iron -> Kammris Iron`.
- `water_vapor_lok`: `Water -> Lokian Water Vapor`, with `zoneRestriction = "lok"`.
- `rice_wild_rori`: `Organic -> Flora Resources -> Flora Food -> Cereal -> Rice -> Wild Rice -> Rori Wild Rice`.
- `bone_horn_rori`: `Organic -> Creature Resources -> Creature Structural -> Horn -> Rori Horn`.
- `gas_reactive_organometallic`: `Inorganic -> Gas -> Reactive Gas -> Known Reactive Gas -> Unstable Organometallic Reactive Gas`.

Internal identification:

- Resource type is the stable class/type identifier used by schematics and pools, for example `iron`, `copper`, `chemical`, `metal`, or `copper_borocarbitic`.
- Resource spawn name is the generated active resource name used for specific harvested stacks, for example `Ababuglu`.
- `ResourceSpawn` has an object ID because it is a persisted game object in the `resourcespawns` object database.
- `ResourceContainer` stores a reference to the `ResourceSpawn`, so a stack knows its spawn name, spawn type, and spawn object ID through that reference.

#### Resource statistics

Resource stats are stored on `ResourceSpawn::spawnAttributes` as a `VectorMap<string, int>`. `ResourceAttribute` maps stat strings to `CraftingManager` stat constants. `ResourceSpawnImplementation::getValueOf` maps those constants back to stored attribute names.

| Abbreviation | Stored attribute | Meaning in Core3 naming | Notes |
|---|---|---|---|
| `CR` | `res_cold_resist` | Cold Resistance | Used by resource weighting when schematics request `CR`. |
| `CD` | `res_conductivity` | Conductivity | Common in weapon and electronics-style weights. |
| `DR` | `res_decay_resist` | Decay Resistance | Common in food/spice and structural durability-style weights. |
| `HR` | `res_heat_resist` | Heat Resistance | Available as a resource stat and crafting weight. |
| `FL` | `res_flavor` | Flavor | Common on food/organic resources and chef outputs. |
| `MA` | `res_malleability` | Malleability | Common on metals/structural resources. |
| `PE` | `res_potential_energy` | Potential Energy | Common on food/energy-related outputs. |
| `OQ` | `res_quality` | Overall Quality | Broadly used across crafting categories. |
| `SR` | `res_shock_resistance` | Shock Resistance | Used by several weapon/explosive/structural outputs. |
| `UT` | `res_toughness` | Unit Toughness | Used by durability, armor, and some weapon outputs. |

Generation and ranges:

- `ResourceTree::buildTreeFromClient` reads each resource type's stat names and min/max gates from the resource tree data table.
- `ResourceSpawner::createResourceSpawn` calls `randomizeValue(min, max)` for each resource attribute.
- `randomizeValue` uses the per-type min/max range and applies `lowerGateOverride` and `spawnThrottling` from `resource_manager.lua`.
- Admin-created specific resources clamp manually supplied stat values to `1..1000`.
- The normal resource tree has per-type gates; future AI code should not assume every stat exists on every resource or that every stat uses the full `1..1000` range.

#### Resource spawn lifecycle

Startup flow:

1. `ResourceManagerImplementation::initialize` loads resource config from `scripts/managers/resource_manager.lua`.
2. It initializes `ResourceSpawner` pools and the resource tree.
3. `ResourceSpawner::start` calls `loadResourceSpawns` and then `shiftResources`.
4. `loadResourceSpawns` loads persisted `ResourceSpawn` objects from the `resourcespawns` object database.
5. If the database is empty and `buildInitialResourcesFromScript = 1`, it loads initial resources from `scripts/managers/resource_manager_spawns.lua`.

Shift flow:

- `ResourceManagerImplementation::startResourceSpawner` schedules `ResourceShiftTask` using `averageShiftTime`.
- The default config uses `averageShiftTime = 7200000` ms, or 2 hours.
- `ResourceManagerImplementation::shiftResources` calls `ResourceSpawner::shiftResources` and schedules the next shift.
- `ResourceSpawner::shiftResources` updates the random, fixed, native, minimum, and manual pools.
- Expired or invalid resources are despawned by removing their spawn maps and setting their pool to `NOPOOL`.
- Replacement resources are created by the pool update code when pool rules require them.

Duration rules from `resource_manager.lua` and `ResourceSpawner::getRandomExpirationTime`:

- Organic resources last between `6 * aveduration` and `22 * aveduration`.
- Inorganic resources last between `6 * aveduration` and `11 * aveduration`.
- JTL resources last between `13 * aveduration` and `22 * aveduration`.
- Default `aveduration` is `86400` seconds.

Planet and galaxy scope:

- `ResourceSpawn` can be planet-specific through `zoneRestriction`.
- Zone-specific resource types are detected when the type name contains an active zone suffix, such as `_rori`, `_lok`, or `_dantooine`.
- Native pool resources are configured to spawn one planet-restricted resource per listed type on each planet.
- Non-restricted resources can have spawn maps on multiple active zones.
- `ResourceSpawn::spawnMaps` is keyed by zone name and stores a `SpawnDensityMap` for each planet where that spawn is active.

Spawn density and concentration:

- `ResourceSpawn::createSpawnMaps` builds per-zone density maps.
- `SpawnDensityMap` uses simplex noise with a random seed, a map modifier, and max density.
- Ore uses a broader map modifier than non-ore resources.
- High density maps produce roughly `0.90..0.99` max density, medium `0.75..0.95`, and low `0.50..0.75`.
- `ResourceSpawn::getDensityAt(zone, x, y)` returns `0` when the resource is not in shift or has no map for that zone.

Pools:

- `minimumPool` keeps configured resource families always present in at least the configured count.
- `randomPool` chooses weighted entries from broad types such as `metal`, `ore`, `gas`, and `water`.
- `fixedPool` keeps configured entries such as `iron` and JTL resource types present.
- `nativePool` creates planet-restricted organic/native resources.
- `manualPool` owns admin-created resources.

#### Harvesting and sampling

Manual survey flow:

1. The player uses a survey tool and resource name.
2. `ResourceManagerImplementation::sendSurvey` forwards to `ResourceSpawner::sendSurvey`.
3. `sendSurvey` validates the active `SurveySession`, survey tool, resource name, and player zone.
4. It samples a grid around the player using `resourceMap->getDensityAt`.
5. It sends `SurveyMessage` results and may create/update a survey waypoint at the highest-density point.

Manual sampling flow:

1. `ResourceManagerImplementation::sendSample` forwards to `ResourceSpawner::sendSample`.
2. `sendSample` validates the survey session/tool/resource/zone, applies action cost, plays the sample animation, reads density at the player's current location, and schedules sample result processing.
3. `ResourceSpawner::sendSampleResults` validates density and skill thresholds.
4. Units are calculated from density, surveying skill, random chance, city sample-size modifiers, optional gamble, and rich sample location bonuses.
5. If enough units are extracted, it calls `resourceSpawn->extractResource(zoneName, unitsExtracted)`.
6. It creates or stacks a `ResourceContainer` in player inventory through `addResourceToPlayerInventory`.
7. It awards resource harvesting XP and notifies sample observers.

Harvester and other extraction flows:

- Harvester/installation code stores resource output as `ResourceContainer` objects in hoppers.
- Foraging, fishing, creature/droid harvesting, director tooling, and admin commands all route through `ResourceManager`/`ResourceSpawner` helpers that resolve a current `ResourceSpawn`, call extraction, and create/add `ResourceContainer` stacks.
- The common real-output object is still `ResourceContainer` referencing `ResourceSpawn`.

Resource availability effects:

- Density at the current planet coordinate directly affects manual sampling success and units.
- Skill thresholds can prevent sampling low-density spots.
- `ResourceSpawn::inShift` gates density lookup; expired resources effectively disappear from survey/sampling.
- The inspected `SpawnDensityMap` stores `totalUnits` and `unitsHarvested`, but the current `ResourceSpawn::extractResource` implementation only increments `unitsInCirculation`; no code path inspected here consumes `SpawnDensityMap::unitsHarvested`.

What real miners use:

- Player survey/sampling systems use active resource name, planet, position, survey tool type, density map, survey skill, action/mind costs, and resource spawn metadata.
- Harvesters use active resource spawn/container state through installation hopper logic.
- Current SimMiner uses none of these real resource APIs.

#### Crafting resource usage

Crafting uses resource type requirements and resource stat weights rather than flat resource labels.

Schematic ingredient requirements:

- Draft schematics list `resourceTypes`, `resourceQuantities`, and contribution values.
- Examples:
  - `pistol_blaster_dl44.lua` requires generic `metal` plus specific weapon components.
  - `clothing_armor_ris_bracer_l.lua` requires specific resource types such as `armophous_vendusii`, `fuel_petrochem_solid_known`, `fiberplast_talus`, `aluminum_chromium`, `copper_platinite`, and `hide_wooly_rori`, plus crafted components.
  - `droid_r2_advanced.lua` requires `chemical` plus droid components.

Experimental weighting:

- Crafted object templates provide `numberExperimentalProperties`, `experimentalProperties`, `experimentalWeights`, groups, subgroups, min/max values, precision, and combine types.
- `SharedTangibleObjectTemplate` converts those Lua arrays into `ResourceWeight` groups.
- `ResourceWeight` maps strings such as `CD`, `OQ`, `SR`, and `UT` to numeric crafting stat identifiers.
- `ResourceLabratory::setInitialCraftingValues` computes a weighted sum from the resources slotted into the manufacture schematic and uses it to set experimental percentages.
- Experimentation later modifies those percentages, but the maximum possible value is bounded by the initial resource-weight result.

Concrete examples:

- Weapons such as `pistol_dl44.lua` use many `CD` and `OQ` weights for damage, speed/efficiency, range, and action/mind/health costs.
- Grenades use `OQ` and `SR` heavily.
- Food/drink examples under `tangible/food/crafted` commonly use `OQ`, `PE`, `FL`, and `DR`; some use `SR`.
- Spice and creature bio effect food examples use `DR`, `OQ`, and `UT`.
- Some droid/electronics and ship component examples use `CD`, `OQ`, `UT`, `SR`, and `PE`.

Implication:

- There is no single universal "best resource" value in the inspected Core3 path.
- A resource can be excellent for one item category and poor for another depending on required resource type and weighted stats.
- A future AI economy must model both eligibility (`resourceTypes`) and quality (`experimentalProperties`/`experimentalWeights`) if it wants to approximate player demand.

#### Market behavior implications

Conceptual labels like `iron`, `copper`, `gas`, and `water` are insufficient because:

- They may describe broad families, not active spawn names.
- They do not encode specific subtype, such as `iron_kammris` versus `iron_doonium`.
- They do not encode generated stat values.
- They do not encode planet availability or density maps.
- They do not encode whether a resource is currently in shift.
- They do not identify a `ResourceSpawn` object.
- They do not distinguish real inventory (`ResourceContainer`) from abstract AI accounting.
- They cannot answer whether a resource satisfies a schematic's exact `resourceTypes` requirement.
- They cannot answer whether a resource is valuable for a profession's stat weighting.

Attributes that can influence player demand:

- Resource type/family eligibility for popular schematics.
- Current active spawn name and object ID.
- Stat vector and weighted quality for important crafting outputs.
- Planet availability and density, because gatherability affects supply.
- Time remaining before despawn.
- Quantity in circulation or player/vendor supply.
- Whether the resource is organic, inorganic, energy, JTL, native, recycled, or planet-restricted.
- Container type and stackability when it becomes real inventory.

#### SimMiner gap analysis

What SimMiner currently models:

- Conceptual resource label selection from configurable strings.
- Visual survey animation.
- Movement to a random navmesh/fallback destination.
- Visual sample animation.
- Memory-only conceptual yield totals keyed by selected label.
- Optional per-yield and periodic summary logs.

What real Core3 resources contain:

- Resource tree type and class hierarchy.
- Generated active spawn name.
- `ResourceSpawn` object ID.
- Per-stat generated values.
- Spawn/despawn timestamps.
- Planet/zone restrictions.
- Per-planet density maps.
- Survey tool type.
- Resource container template CRC.
- Pool ownership.
- Real extraction and inventory object paths through `ResourceContainer`.

Missing from SimMiner:

- No `ResourceManager` lookup.
- No `ResourceSpawn` identity.
- No real resource type validation.
- No resource stats.
- No planet density/concentration check.
- No survey tool concept.
- No in-shift/despawn awareness.
- No real unit extraction.
- No `ResourceContainer`.
- No interaction with harvesters, hoppers, vendors, bazaar, crafting, credits, player inventory, or persistence.
- No demand model based on schematics or experimentation weights.

#### Future persistence considerations

Research-only recommendations:

- Persisting only `iron=123` is too lossy for any economy that may later touch crafting or markets.
- The first persistent AI economy format may still keep conceptual counters, but it should be versioned and explicitly named as abstract/conceptual.
- If future AI miners gather real or semi-real resources, persisted entries should likely include:
  - Conceptual quantity.
  - Resource type, such as `copper_borocarbitic`.
  - Resource spawn name, if tied to an active spawn.
  - Resource spawn object ID, if the spawn is expected to still exist.
  - Class chain or final class snapshot.
  - Stat map snapshot.
  - Planet/zone source.
  - Gather timestamp.
  - Whether the source spawn was active at gather time.
  - AI role/group owner.
- If the economy remains abstract, persisted entries should still include enough metadata to migrate from broad labels to typed resources later.
- Aggregated supply/demand trends should persist as coarse time buckets, not unbounded per-sample event history.
- Runtime-only state should remain transient: active miner count, current destination, current animation phase, in-progress sample, current path, and debug counters.
- Real `ResourceContainer` objects should not be created merely to persist abstract AI inventory.
- Player inventory, hopper contents, vendor stock, and bazaar listings should not be used as persistence backends for conceptual state.
- If future AI economy state references a real `ResourceSpawn`, recovery must handle despawned or missing spawns by preserving historical metadata without pretending that the spawn is still active.
- Before a real economy bridge is implemented, define a strict boundary between abstract AI inventory and Core3's real resource/container APIs.

### Galaxy Resource Intelligence Network

This section describes a future shared AI knowledge layer for resource discovery and economic reasoning. It is architecture research only. It does not define persistence schemas, database objects, new classes, or runtime behavior.

The core question is: what should the AI economy understand before deciding how to save it?

#### Purpose

The Galaxy Resource Intelligence Network would be a shared intelligence layer that all AI economic roles can consult and update. It should not be a per-miner backpack or a replacement for Core3's real `ResourceManager`. Instead, it should be a common knowledge model that turns live resource system facts into AI-usable economic signals.

Future goals:

- Track discovered active resources.
- Evaluate resource quality by profession/use case.
- Track which resources are still active and when they are likely to expire.
- Estimate available supply, scarcity, and gatherability.
- Estimate demand from crafters, vendors, consumers, factions, and guilds.
- Let multiple AI roles make decisions from the same resource knowledge instead of each role inventing its own view.

The network should remain clearly separate from real player inventory, `ResourceContainer` stacks, vendors, bazaar listings, credits, and crafting output until a later explicit economy-integration phase.

#### Shared knowledge model

A future intelligence entry should represent a known resource opportunity, not just a raw counter. Useful fields to understand conceptually include:

| Knowledge area | Examples | Why it matters |
|---|---|---|
| Resource identity | Resource type, active spawn name, `ResourceSpawn` object ID if live, class chain | Lets AI distinguish `iron_kammris` from `iron_doonium` and broad `iron` requests from specific active spawns. |
| Spawn state | Planet/zone, active/inactive, spawned/despawned timestamps, time remaining | Prevents AI decisions based on expired resources. |
| Resource stats | OQ, CD, DR, HR, FL, MA, PE, SR, UT, CR when present | Enables profession-specific quality evaluation. |
| Gatherability | Planet availability, density estimate, known rich areas, survey confidence | Helps miners choose where to operate and helps vendors understand supply risk. |
| Supply signal | Known conceptual stock, recent production rate, scarcity estimate | Helps crafters/vendors decide whether to consume or conserve a resource. |
| Demand signal | Professions/items that want this resource, current requested quantities, priority | Helps miners gather what the economy needs instead of random labels. |
| Evaluation scores | Weaponsmith score, armorsmith score, chef score, architect score, generic value score | Lets different AI roles share one resource catalog while making role-specific decisions. |
| Confidence and freshness | Who discovered it, last observed time, observation count, stale flag | Prevents old scout data from being treated as authoritative forever. |

This model should be read as information requirements, not a storage design.

#### Resource Scout concept

A future SimMiner can evolve into a resource scout before it becomes a real economic harvester. This is a safer intermediate role because scouting can inspect and classify resources without creating real resources or moving goods through the economy.

Future scout responsibilities:

- Inspect active `ResourceSpawn` data through a controlled engine/service API.
- Identify the resource type hierarchy and generated spawn name.
- Read available stats and planet restrictions.
- Determine whether the spawn is active and estimate time remaining.
- Estimate gatherability from known density information or survey-like observations.
- Classify usefulness by profession or demand category.
- Publish a finding into the shared intelligence layer.
- Update stale findings when a resource despawns or a better observation is made.

Possible scout outputs:

- "High-OQ/CD metal active on Corellia; useful for weaponsmith demand."
- "Flavor-heavy organic food resource active on Rori; useful for chef demand."
- "Planet-restricted water is available but low priority because supply is already high."
- "Previously useful spawn expired; mark as stale and remove from active gathering plans."

Important boundary:

- A scout should not create `ResourceContainer` objects.
- A scout should not modify player inventory, vendors, bazaar, harvesters, credits, or crafting.
- A scout should publish knowledge, not goods.

#### Shared knowledge versus per-miner knowledge

| Model | Advantages | Disadvantages | Recommendation |
|---|---|---|---|
| Per-miner inventory | Simple mental model; supports individual worker identity; useful for future role flavor. | Duplicates global state, can strand important knowledge on despawned/recycled NPCs, makes economy-wide demand harder to balance. | Use only for local carried/assigned output once real production exists. Do not make it the primary resource intelligence source. |
| Per-miner memory | Good for short-term behavior, local patrol decisions, and avoiding repeated failed actions. | Volatile, fragmented, and hard to aggregate across the galaxy. | Keep transient and behavior-focused: current target, recent failed locations, short cooldowns. |
| Shared galaxy intelligence | Gives miners, crafters, vendors, consumers, factions, and guilds one common source of resource truth; supports supply/demand coordination; safer for persistence planning. | Requires careful authority, freshness, and conflict rules; can become too broad if it stores every event forever. | Recommended as the primary future economy knowledge layer. Keep it bounded, versioned, and separated from real inventory. |

Recommended split:

- Per-miner memory should answer: "What am I doing right now, and what did I just try?"
- Per-miner inventory should answer: "What output has this worker personally produced or been assigned?"
- Shared galaxy intelligence should answer: "What does the AI economy know about resources, supply, demand, and value?"

#### AI role interaction model

All future AI economic roles should be able to reference the same intelligence layer while keeping role-specific decisions separate.

| Role | How it could use shared intelligence |
|---|---|
| SimMiners | Choose scouting/gathering targets based on active resources, demand scores, scarcity, planet access, and expiration risk. |
| SimCrafters | Select resources that satisfy schematic requirements and maximize profession-specific stat scores. |
| SimVendors | Adjust conceptual inventory, pricing, and restock requests based on supply scarcity and demand signals. |
| SimConsumers | Generate demand for goods, indirectly increasing demand for the resources used to craft those goods. |
| PvP factions | Prioritize patrols, raids, or defense around valuable resource regions without needing their own duplicate resource catalog. |
| Future AI guilds | Coordinate large goals, such as "stockpile chef organics" or "support weaponsmith production for faction conflict." |

Interaction principle:

- The intelligence network should provide facts and scores.
- Behavior systems should make decisions.
- Engine systems should perform gameplay effects.

This keeps generated intelligence from directly causing unsafe gameplay side effects.

#### Resource scoring architecture

Future scoring should be role-aware and use Core3 crafting facts rather than a single global quality number. Scores should be explainable enough for logs and debugging.

Potential score families:

| Score | Inputs | Example interpretation |
|---|---|---|
| Generic value score | OQ, rarity, active time remaining, demand count, scarcity | "Generally useful and scarce." |
| Weaponsmith demand score | Eligibility for weapon schematics, CD/OQ/SR/UT weights, current weapon demand | "Useful for high-demand weapon production." |
| Armorsmith demand score | Armor schematic eligibility, OQ/UT/SR/DR/MA-style durability and resistance relevance, component demand | "Useful for armor segments or required RIS-style resources." |
| Chef demand score | Food resource eligibility, FL/PE/OQ/DR/SR relevance, consumer demand | "Useful for food/drink crafting or buffs." |
| Architect demand score | Structural resource eligibility, OQ/DR/UT/MA-style durability relevance, building/component demand | "Useful for structures and installation components." |
| Droid/electronics score | Chemical/metal/electronics eligibility, CD/OQ/PE/SR relevance | "Useful for droid and electronic components." |
| Faction logistics score | Scarcity, combat production demand, location, faction strategy | "Worth guarding, disrupting, or prioritizing." |

Scoring concepts:

- Eligibility comes first: a resource must satisfy the requested type or class requirement before high stats matter.
- Scores should account for missing stats. A resource without `FL` should not receive chef flavor credit.
- Scores should be role-specific because the same resource can be valuable to one profession and unimportant to another.
- Scores should preserve explainability, such as "high because OQ/CD are strong and weaponsmith demand is active."
- Scores should degrade or become stale when the resource is near despawn or no longer active.
- Scores should not create gameplay effects by themselves.

Future scoring could be layered:

1. Raw stat normalization by resource type.
2. Schematic eligibility matching.
3. Profession-specific weighted score.
4. Supply/demand adjustment.
5. Freshness/expiration adjustment.
6. Final priority score for behavior systems.

#### Supply and demand awareness

The intelligence layer should distinguish resource knowledge from economic pressure.

Supply signals might include:

- Known conceptual totals.
- Recent production rate.
- Number of active scouts/miners assigned.
- Known density/gatherability.
- Known or estimated time until despawn.
- Whether equivalent resource types are currently available.

Demand signals might include:

- Crafting goals waiting for inputs.
- Vendor restock requests.
- Consumer demand by product category.
- Faction or guild strategic goals.
- Scarcity of substitutes.
- Manual admin-configured priorities for a solo/low-population server.

Supply and demand should be separate inputs so the AI can explain decisions:

- "Gather because demand is high and supply is low."
- "Ignore because resource is excellent but expires soon and no current role needs it."
- "Scout because stats are unknown and the type could satisfy a high-priority schematic."

#### Expiration awareness

Real resources are temporary. The intelligence network should treat active status and freshness as first-class concepts.

Future behavior should account for:

- Active versus expired resource spawns.
- Time remaining until despawn.
- Confidence that a scout observation is still valid.
- Whether a planned gather route can complete before expiration.
- Whether an expired resource should remain as historical market knowledge but not an active target.

This is one reason persistence should not simply save active `ResourceSpawn` IDs without metadata. On restart or later load, a referenced spawn may be gone, but its historical stats and demand impact may still be useful for trend analysis.

#### Recommended evolution path

This roadmap defines knowledge and behavior evolution only. It intentionally does not design persistence yet.

| Phase | Goal | Expected benefit | Risk to manage |
|---|---|---|---|
| Phase D - Resource intelligence | Build a read-only/shared knowledge concept around active resources, quality, supply, demand, and expiration. | AI roles can reason from the same resource facts before any real economy writes occur. | Avoid turning intelligence updates into gameplay effects. |
| Phase E - Supply and demand modeling | Add abstract pressure signals from crafters, vendors, consumers, factions, and admin priorities. | Miners gather for reasons instead of random labels. | Keep models bounded and explainable; avoid runaway demand loops. |
| Phase F - AI crafting | Let SimCrafters consume conceptual inputs and produce conceptual outputs using schematic-aware resource evaluation. | Connect resource intelligence to production planning without immediately touching real player markets. | Maintain strict separation from real crafting outputs until deliberately bridged. |
| Phase G - AI market participation | Introduce controlled vendor/market behavior based on conceptual stock and demand. | The galaxy economy becomes visible and interactive in limited, auditable ways. | Prevent inflation, fake resource leakage, and uncontrolled player-facing goods. |
| Phase H - Persistent living economy | Decide persistence after the intelligence model is stable and the economy knows what state matters. | Restart can preserve meaningful economic state rather than arbitrary early counters. | Persistence must preserve abstractions without locking in bad resource identifiers. |

Recommended ordering:

1. Define the intelligence vocabulary.
2. Define scout observations.
3. Define scoring inputs and explanations.
4. Define supply/demand signals.
5. Validate the model with logs and read-only summaries.
6. Research schematic-aware scoring before letting miners target real active resources.
6. Only then design persistence.

Open questions:

- Should intelligence be galaxy-wide first, or planet-specific from the beginning?
- Should AI scouts inspect all active resources globally or only resources discoverable from their current planet?
- How much density information should be known globally versus learned through scout movement?
- Should demand be driven first by configured goals, simulated consumers, or actual player market observations?
- Should future PvP factions react to valuable resource zones before market participation exists?
- What resource score explanations are needed in logs so economy decisions are debuggable?

### Resource Intelligence MVP

This is the first read-only implementation step toward the Galaxy Resource Intelligence Network. It is observability only.

Current implementation:

- Configured in `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua` with `resourceIntelligenceConfig`.
- Implemented in `SimPlayerManager` as a periodic read-only snapshot task.
- Disabled by default with `enabled = false`.
- Top-resource logging is separately disabled by default with `logTopResources = false`.
- Does not change SimMiner targeting, movement, survey timing, sampling timing, conceptual yield accounting, or spawn counts.
- Does not create resources, `ResourceContainer` objects, inventory items, vendor stock, bazaar listings, harvester output, credits, or crafting output.

Default config:

```lua
resourceIntelligenceConfig = {
    enabled = false,
    logTopResources = false,
    summaryIntervalSeconds = 600,
    topN = 10,
}
```

Read-only access path:

1. `SimPlayerManager` schedules a `ResourceIntelligenceTask` only when `resourceIntelligenceConfig.enabled` is true.
2. The task calls `SimPlayerManager::logResourceIntelligenceSummary`.
3. The manager obtains `ZoneServer` through `ServerCore::getZoneServer`.
4. It obtains `ResourceManager` through `ZoneServer::getResourceManager`.
5. It locks `ResourceManager` while reading `ResourceSpawner::getResourceMap`.
6. It iterates current `ResourceMap` entries and skips null or inactive/non-shift resources.
7. It copies primitive metadata into local snapshot rows before logging.

Metadata currently read:

| Field | Source | Notes |
|---|---|---|
| Resource spawn object ID | `ResourceSpawn::getObjectID` | Useful for identifying live spawns in logs. |
| Spawn name | `ResourceSpawn::getName` | The generated active resource name. |
| Spawn type | `ResourceSpawn::getType` | The internal resource type, such as `copper_borocarbitic`. |
| Class/type chain | `ResourceSpawn::getStfClass` | Logged/copied as the STF class chain when present. |
| Active status | `ResourceSpawn::inShift` | Only in-shift resources are included in the MVP snapshot. |
| Zone availability | `ResourceSpawn::getSpawnMapZone` | Captures the zones where the spawn has maps. |
| Despawn timestamp | `ResourceSpawn::getDespawned` | Spawn timestamp is not currently exposed through a public getter, so it is not read to avoid IDL changes. |
| Survey tool type | `ResourceSpawn::getSurveyToolType` | Captured for future scout/tool reasoning. |
| Stats | `ResourceSpawn::getValueOf(attributeName)` | Reads OQ/CD/DR/HR/FL/MA/PE/SR/UT/CR when present. |

Initial heuristic score families:

| Score | Current heuristic | Notes |
|---|---|---|
| `genericScore` | Weighted average of OQ plus available resource stats. | OQ has extra weight; missing stats are ignored, not treated as perfect. |
| `weaponsmithScore` | CD, OQ, SR, UT. | Inspired by common weapon stat weights, but not schematic-exact. |
| `armorsmithScore` | OQ, UT, SR, DR, MA. | Approximate armor/structural quality signal. |
| `chefScore` | OQ, PE, FL, DR. | Approximate food/drink usefulness signal. |
| `architectScore` | OQ, DR, UT, MA. | Approximate structure/component usefulness signal. |

Logging behavior:

- If `enabled = false`, no resource intelligence task is scheduled.
- If `enabled = true`, the task logs a compact read-only snapshot summary at `summaryIntervalSeconds`.
- If `logTopResources = true`, the task logs up to `topN` resources per score family.
- Logs are intentionally heuristic and diagnostic. They do not feed gameplay decisions.

Limitations:

- Scoring is approximate and intentionally early.
- Scoring is not tied to exact draft schematics yet.
- No density sampling or best-location discovery is performed.
- No persistence exists.
- No miner targeting uses these scores.
- No shared resource memory exists yet beyond the periodic snapshot/log output.
- No `ResourceContainer` objects are created.
- No real economy systems are touched.
- Spawned timestamp is not read because `ResourceSpawn` currently exposes `getDespawned` but not `getSpawned`; the MVP avoids IDL changes.

Recommended next step:

Do not jump to persistence yet. The next safest step should be either:

- Schematic-aware scoring research, mapping common draft schematic `resourceTypes` and `experimentalProperties` into better profession scores.
- Miner target-selection simulation in logs only, where the system reports what a scout would choose without changing actual SimMiner behavior.

### Schematic-Aware Resource Scoring Research

This section documents how to evolve the current Resource Intelligence MVP from broad heuristic scores into schematic-aware scores. It is research only. No miner targeting, persistence, resource creation, player inventory, vendor, bazaar, crafting, harvester, credit, or player-facing economy behavior is implemented by this section.

#### Research scope

Files and systems inspected:

- Draft schematic Lua under `MMOCoreORB/bin/scripts/object/draft_schematic`, including weapon, armor, food, structure, and droid examples.
- Crafted output and component templates under `MMOCoreORB/bin/scripts/object/weapon`, `MMOCoreORB/bin/scripts/object/tangible/component`, and `MMOCoreORB/bin/scripts/object/tangible/food`.
- `MMOCoreORB/src/templates/intangible/DraftSchematicObjectTemplate.cpp`
- `MMOCoreORB/src/templates/SharedTangibleObjectTemplate.cpp`
- `MMOCoreORB/src/templates/crafting/resourceweight/ResourceWeight.h`
- `MMOCoreORB/src/server/zone/objects/manufactureschematic/ManufactureSchematicImplementation.cpp`
- `MMOCoreORB/src/server/zone/objects/manufactureschematic/ingredientslots/IngredientSlot.h`
- `MMOCoreORB/src/server/zone/objects/manufactureschematic/ingredientslots/ResourceSlot.h`
- `MMOCoreORB/src/server/zone/managers/crafting/labratories/ResourceLabratory.cpp`
- `MMOCoreORB/src/server/zone/managers/crafting/labratories/SharedLabratory.cpp`
- `MMOCoreORB/src/server/zone/objects/resource/ResourceSpawn.idl`
- `MMOCoreORB/src/server/zone/objects/resource/ResourceSpawnImplementation.cpp`

#### Draft schematic structure

Draft schematic Lua files define what ingredients are eligible for a craft and how much is needed. The parser in `DraftSchematicObjectTemplate::parseVariableData` reads these fields into vectors, and `DraftSchematicObjectTemplate::readObject` turns each row into a `DraftSlot`.

Important fields:

| Field | Meaning | Runtime destination |
|---|---|---|
| `craftingToolTab` | Crafting category/tab used by the client and crafting systems. | `DraftSchematicObjectTemplate::craftingToolTab` |
| `assemblySkill` | Skill used for assembly rolls. | `DraftSchematicObjectTemplate::assemblySkill` |
| `experimentingSkill` | Skill used for experimentation rolls. | `DraftSchematicObjectTemplate::experimentingSkill` |
| `ingredientTemplateNames` | String table references for slot names. | `DraftSlot::setStringId` |
| `ingredientTitleNames` | Slot title keys. | `DraftSlot::setStringId` |
| `ingredientSlotType` | Slot behavior: resource slot, identical component slot, mixed component slot, optional variants. | `DraftSlot::setSlotType`; later mapped to `ResourceSlot` or `ComponentSlot` |
| `resourceTypes` | Eligibility string for each slot. For resource slots this is a resource class/type; for component slots this can be an object template path. | `DraftSlot::setResourceType` and then `IngredientSlot::setContentType` |
| `resourceQuantities` | Required amount for each slot. | `DraftSlot::setQuantity`; later used by `SharedLabratory::getWeightedValue` |
| `contribution` | Per-slot contribution value carried on the `DraftSlot`. | `DraftSlot::setContribution` |
| `targetTemplate` | Crafted output/component template path. | Stored as CRC; used to load `SharedTangibleObjectTemplate` for resource weights |
| `additionalTemplates` | Alternative visual/output templates. | Stored on draft schematic template |

Representative examples:

| Category | Draft schematic | Resource and component shape |
|---|---|---|
| Weapon component | `draft_schematic/weapon/component/blaster_pistol_barrel.lua` | Five raw resource slots: `metal`, `metal`, `metal`, `gemstone`, `metal`; quantities 10, 8, 6, 1, 3. |
| Armor component | `draft_schematic/armor/armor_segment_composite.lua` | Raw `metal` and `steel` slots plus component slots for armor layers and segment enhancement templates. |
| Food | `draft_schematic/food/dish_ahrisa.lua` | Raw `vegetable_greens` and `fruit_flowers` plus component/object-template slots for `dish_soypro` and additive. |
| Structure component | `draft_schematic/structure/component/structure_light_ore_mining_unit.lua` | Raw `steel`, `metal`, and `gas_inert` slots in large quantities. |
| Droid/electronics | `draft_schematic/droid/component/crafting_module_weapon.lua` | Raw `aluminum`, `gas_inert`, and `metal` slots. |

This means future scoring cannot look only at final item categories. Many high-value schematics depend on intermediate components. A first scorer can evaluate direct raw resource slots, but a complete scorer will eventually need to walk component chains or maintain curated component demand profiles.

#### Experimental weighting structure

Crafted target templates define the resource-stat weights that become experimental attributes. `DraftSchematicObjectTemplate::getResourceWeights` loads the target template through `TemplateManager` and returns the target template's `resourceWeights`. Those weights are built by `SharedTangibleObjectTemplate` from:

- `numberExperimentalProperties`
- `experimentalProperties`
- `experimentalWeights`
- `experimentalGroupTitles`
- `experimentalSubGroupTitles`
- `experimentalMin`
- `experimentalMax`
- `experimentalPrecision`
- `experimentalCombineType`

`SharedTangibleObjectTemplate` groups the flat `experimentalProperties` and `experimentalWeights` arrays into `ResourceWeight` rows. `ResourceWeight::convertStringValue` maps resource stat strings to crafting stat codes:

| Template string | Resource stat |
|---|---|
| `CR` | Cold Resistance |
| `CD` | Conductivity |
| `DR` | Decay Resistance |
| `HR` | Heat Resistance |
| `FL` | Flavor |
| `MA` | Malleability |
| `PE` | Potential Energy |
| `OQ` | Overall Quality |
| `SR` | Shock Resistance |
| `UT` | Unit Toughness |
| `BK` | Bulk |
| `XX` / unknown | Filler/no resource stat |

Representative target-template examples:

| Target template | Experimental signal |
|---|---|
| `object/tangible/component/weapon/blaster_pistol_barrel.iff` | Damage/range/durability rows use `CD` and `SR`, often with `CD` weighted higher than `SR`. |
| `object/weapon/ranged/pistol/pistol_dl44.iff` | Weapon final item rows commonly use repeated `CD` and `OQ` pairs across damage, efficiency, durability, and range attributes. |
| `object/tangible/component/armor/armor_segment_composite.iff` | Armor effectiveness and encumbrance rows use combinations of `OQ`, `SR`, `UT`, and `MA`. |
| `object/tangible/food/crafted/dish_ahrisa.iff` | Food rows use `OQ`, `PE`, `FL`, and `DR`; quantity has strong `PE`/`DR` weighting in this example. |
| `object/tangible/component/structure/light_ore_mining_unit.iff` | Extraction efficiency uses `HR`, `SR`, and `UT`, with `UT` weighted more heavily. |
| `object/tangible/component/droid/crafting_module_weapon.iff` | Droid module effectiveness/durability uses `CD` and `OQ`. |

`ResourceLabratory::setInitialCraftingValues` then evaluates each `ResourceWeight` by:

1. Adding an experimental attribute for the target row.
2. Iterating the row's stat weights.
3. Calling `SharedLabratory::getWeightedValue` for each stat.
4. Combining those stat values by the `ResourceWeight` percentages.
5. Converting the weighted sum into max/current experimentation percentages.

`SharedLabratory::getWeightedValue` computes a quantity-weighted average across filled ingredient slots. For raw resource slots it reads `ResourceSlot::getCurrentSpawn` and then `ResourceSpawn::getValueOf(type)`. For custom ingredient component slots it can read `CustomIngredient::getValueOf(type)` when the slotted component carries resource-derived values.

Implication: a schematic-aware AI score should model expected contribution to a chosen schematic or profile, including ingredient quantity, eligibility, and target-template resource weights. A flat family score is useful for scouting, but it cannot explain whether a resource is good for a specific item.

#### Eligibility matching

Resource eligibility is enforced by `ResourceSlot::add`: the incoming object must be a `ResourceContainer`, its spawn must match any existing `currentSpawn` for that slot, and `incomingResource->getSpawnObject()->isType(contentType)` must be true.

`ResourceSpawn::isType` checks both `stfSpawnClasses` and `spawnClasses`. A future read-only scorer should use the same rule conceptually:

1. Read the schematic slot's `resourceType`.
2. Skip component/object-template slots unless the scorer has component-chain support.
3. For raw resource slots, treat a `ResourceSpawn` as eligible when `spawn->isType(slotResourceType)` would be true.
4. Score only eligible spawns for that slot.
5. Weight slot influence by `resourceQuantities` and, later, demand for the item/component.

Examples:

- A generic `metal` slot should accept any active spawn whose class chain includes `metal`.
- A `copper` slot should accept copper subtypes because their class chain should include `copper`.
- A specific subtype such as `copper_borocarbitic` should require that exact class/type match.
- Food resources such as `vegetable_greens`, `fruit_flowers`, `meat`, or `water` should be matched through the resource class chain, not a broad conceptual label.
- Chemical, gas, mineral, ore, and energy resources should use the same class-chain eligibility rule.
- Planet-specific resources are still resources with type/class metadata, but availability and density should remain separate intelligence facts.

Object-template strings such as `object/tangible/component/armor/shared_armor_layer.iff` are not raw resource types. They represent component requirements. A resource scorer should not mark a live `ResourceSpawn` eligible for those slots unless it is recursively scoring the component schematic that produces the required component.

#### Profession/category scoring

The current Resource Intelligence MVP has broad score families: `weaponsmithScore`, `armorsmithScore`, `chefScore`, and `architectScore`. These are useful for discovery logs, but they are intentionally too broad.

Future scoring should move from "profession likes these stats" to "resource is eligible for these high-priority schematic slots and improves these experimental rows."

| Category | Representative examples | Important resource types | Important stat weights | Current heuristic fit |
|---|---|---|---|---|
| Weaponsmith | `blaster_pistol_barrel`, `pistol_dl44` | Metal, gemstone, subtype metals, weapon components | Component barrel example emphasizes `CD`/`SR`; final weapon templates commonly emphasize `CD`/`OQ`. | Current `CD/OQ/SR/UT` is directionally useful but overvalues `UT` for examples where exact templates care mostly about `CD`, `OQ`, or `SR`. |
| Armorsmith | `armor_segment_composite` | Metal, steel, armor layers, armor segment enhancements | `OQ`, `SR`, `UT`, and `MA` appear in armor effectiveness/encumbrance/resistance rows. | Current `OQ/UT/SR/DR/MA` is broad; `DR` may be useful for some armor-adjacent profiles but was not central in the inspected composite segment target. |
| Chef | `dish_ahrisa` | Vegetables, fruits, meats, water, additives, prepared food components | `OQ`, `PE`, `FL`, and `DR`, with row-specific weights. | Current `OQ/PE/FL/DR` matches the inspected food example well, but it lacks eligibility and cannot distinguish greens from flowers from additives. |
| Architect/structures | `structure_light_ore_mining_unit`, house/city/factory deeds | Steel, metal, gas, structure components | Structure component example uses `HR`, `SR`, `UT`, with `UT` weighted heavily; many deeds/components use `DR`, `UT`, `MA`, or `OQ` depending on target. | Current `OQ/DR/UT/MA` misses `HR`/`SR` for mining-unit efficiency and is too broad across all structure outputs. |
| Droid/electronics | `crafting_module_weapon`, droid component schematics | Aluminum, copper, steel, metal, gas, ore, chemical, electronics components | Droid module example uses `CD`/`OQ`; other droid components vary by electronics/mechanical role. | No dedicated current score exists; droid/electronics should start as curated profiles rather than borrowing generic weaponsmith/architect scores. |
| Generic/high-value | Active resources eligible for many common slots or rare high-demand slots | Broad class-chain coverage plus high-stat outliers | OQ plus category-specific rare stat combinations | Current `genericScore` is useful for scouting but should not drive production decisions alone. |

#### Explainability requirements

Future logs should explain both eligibility and score composition. A good explanation should include:

- Resource spawn name and type.
- Matched schematic/profile.
- Matched ingredient slot and required resource type.
- Quantity required by that slot.
- Stats used and their values.
- Target-template weight profile.
- Whether component-chain demand is direct or inferred.
- Whether the score is advisory, log-only, simulated, or behavior-driving.

Example future log shape:

```text
[ResourceIntelligence] Ababuglu copper_borocarbitic weaponsmithScore=842 eligibleFor=blaster_pistol_barrel:emitter_nozzle slotType=metal qty=10 stats=CD:880 SR:650 weightProfile=expDamage CD:2 SR:1 mode=log-only
```

For higher-level category logs:

```text
[ResourceIntelligence] top chef: Kima vegetable_greens score=791 eligibleFor=dish_ahrisa:greens qty=20 stats=OQ:910 PE:760 FL:620 DR:480 rows=nutrition/flavor/quantity/filling mode=log-only
```

#### Implementation recommendation

Do not jump directly to full dynamic schematic parsing for behavior decisions. The safest path is a hybrid, staged approach:

| Option | Advantages | Disadvantages | Recommendation |
|---|---|---|---|
| Curated scoring profiles | Small, reviewable, easy to explain, can target known high-value examples, avoids parsing every schematic edge case at once. | Incomplete; requires manual maintenance; can reflect designer bias. | Best first implementation step after this research. |
| Fully dynamic schematic parsing | Most complete; can eventually use loaded template data and exact `ResourceWeight` rows; reduces manual duplication. | Higher complexity; must handle component recursion, optional slots, magic schematics, category labeling, and performance. | Good long-term goal after curated profiles prove the model. |
| Admin-configured demand profiles | Gives server owners control over economy goals; can steer low-population economy intentionally. | Needs validation, documentation, and guardrails to avoid runaway demand or confusing scores. | Useful as an overlay after curated profiles exist. |

Recommended first technical shape for a future PR:

1. Add read-only curated scoring profile configuration for a small set of representative schematics.
2. Resolve profile entries against loaded `DraftSchematicObjectTemplate` and target-template `ResourceWeight` data where safe.
3. Log top eligible active resources per curated profile.
4. Keep all output advisory/log-only.
5. Do not alter SimMiner targeting until target recommendations have been validated in logs.

This gives the AI economy a stronger scoring vocabulary without committing to persistence, live mining decisions, or real crafting output.

#### Future phases after this research

| Phase | Goal | Behavior impact |
|---|---|---|
| Phase D.1 - Schematic-aware scoring design/research | Document exact draft schematic, target-template, resource weight, and eligibility paths. | Documentation only. |
| Phase D.2 - Curated scoring profile config, read-only | Add disabled-by-default profile definitions for a handful of high-value schematics. | Log-only; no miner targeting. |
| Phase D.3 - Log-only miner target recommendations | Show which resource a miner or scout would choose for a profile. | No movement or gathering changes. |
| Phase D.4 - Miner target selection simulation | Simulate route/resource choices in memory/logs while miners continue current conceptual loops. | No behavior change unless explicitly enabled in a later phase. |
| Phase D.5 - Optional miner targeting switch | Add a disabled-by-default switch for miners to use resource-intelligence targets. | First possible behavior change; requires careful testing and rollback. |

Open questions:

- Which schematics should seed the first curated profiles: common player staples, AI economy staples, or admin-selected goals?
- Should component-chain scoring recurse one level first, or should it use manual component demand multipliers?
- Should resource density affect schematic score, or remain a separate "can gather enough here" score?
- Should expired resources remain in intelligence as historical market/trend data?
- How should future demand distinguish "best possible resource" from "good enough for bulk production"?

### Pathfinding interaction

`SimPlayerController` schedules `SimPathFindTask`, which calls `PathFinderManager::instance()->findPath`. Results are returned to the task manager and then queued into AiAgent patrol points.

The controller:

- Clears current patrol state.
- Writes blackboard movement mode.
- Adds path nodes as patrol points.
- Calls `findNextPosition`.
- Uses heartbeat-style arrival checks.
- Retries failed paths.

`AiAgentImplementation::findNextPosition` contains special handling for SimPlayers.

### Current enabled features

- SimPlayer manager initialization at ZoneServer startup.
- Lua config loading.
- PvP solo spawn group with count `3`.
- PvP loiter, movement, combat scan, shuttle cycling, and death recycling.
- Always-active AiAgent behavior for SimPlayers.
- SimPlayer recycle cleanup is guarded so dead/incap checks happen under the old bot lock, but world/database destruction happens after that lock is released.

### Current disabled or inactive features

- Miner/resource gathering is present but inactive through config because `totalCount = 0`.
- Some debug logging is controlled by commented-out debug macros.
- Some behavior selection is broad and based on template name inference.

# Lua to C++ Binding Map

| Lua function or call | C++ binding | Underlying gameplay effect |
|---|---|---|
| `LuaAiAgent(pNpc):healCreatureTarget(pPlayer)` | `LuaAiAgent::healCreatureTarget` -> `AiAgentImplementation::healCreatureTarget` | Heals a creature target using existing AiAgent heal logic. Used by Padawan and legacy AI skill handlers. |
| `LuaAiAgent(pDoctor):healEnhanceCreatureTarget(pPlayer, stepKey)` | `LuaAiAgent::healEnhanceCreatureTarget` -> `AiAgentImplementation::healEnhanceCreatureTarget` | Applies one medical enhance buff based on `stepKey`: health, strength, constitution, action, quickness, or stamina. |
| `LuaAiAgent(pDoctor):wipeMedicalEnhanceBuffs(pPlayer)` | `LuaAiAgent::wipeMedicalEnhanceBuffs` -> `AiAgentImplementation::wipeMedicalEnhanceBuffs` | Removes medical enhance buffs from the target. |
| `LuaAiAgent(pNpc):wipeEnhanceBuffs(pPlayer, flags)` | `LuaAiAgent::wipeEnhanceBuffs` -> `AiAgentImplementation::wipeEnhanceBuffs` | Removes medical/dance/music enhancement buffs depending on bit flags and heals related wounds/shock. |
| `LuaAiAgent(pDancer):startDancingByName(name)` | `LuaAiAgent::startDancingByName` | Creates/reuses an entertaining session and starts a dance by performance name. |
| `LuaAiAgent(pDancer):applyDanceMindBuff(pPlayer, amount, duration)` | `LuaAiAgent::applyDanceMindBuff` | Applies performance dance mind buff to target. |
| `LuaAiAgent(pMusician):startPlayingMusicByName(song)` | `LuaAiAgent::startPlayingMusicByName` | Creates/reuses an entertaining session, finds equipped playable instrument, starts music by performance name. |
| `LuaAiAgent(pMusician):applyMusicBuffs(pPlayer, amount, duration)` | `LuaAiAgent::applyMusicBuffs` | Applies music focus and willpower performance buffs to target. |

Current wipe flags used by Lua:

| Flag | Meaning in current C++ |
|---|---|
| `1` | Medical enhancement wipe. |
| `2` | Dance enhancement wipe. |
| `4` | Music enhancement wipe. |

# LLM Request Flow

Current player-to-LLM flow:

1. Player logs in.
2. `PlayerObjectImplementation.cpp` calls `AiGlobalChatHandler:onPlayerLoggedIn`.
3. `AiGlobalChatHandler` attaches a `SPATIALCHATSENT` observer to the player.
4. Player sends spatial chat.
5. Core3 chat handling notifies the observer.
6. `AiGlobalChatHandler:notifySpatialChatSent` reads the message with `getChatMessage`.
7. The handler searches nearby objects for a responder using:
   - Displayed name match.
   - Profile call signs.
   - Hard-target fallback.
8. `AiRegistry.getProfile` checks that the target is an AiAgent and returns nil on invalid object/binding failures.
9. `AiRegistry.getProfile` reads the creature template name through `LuaAiAgent:getCreatureTemplateName`.
10. If a profile is found, routing continues by role.
11. For `role = "smart_doctor"`, the handler calls `SmartDoctorBuffer:handleChat`.
12. For `role = "recruiter"`, the handler calls `AiBrain.getRecruiterIntent` and falls back safely if intent parsing or recruiter screenplay calls fail.
13. For normal profiles, the handler calls `AiBrain.getChatResponse` and falls back to deterministic text if the model path fails.
14. `ai_brain.lua` reads `AiConfig.llm` for enablement, URL, model, and timeout settings.
15. `ai_brain.lua` builds a prompt and sends a synchronous HTTP POST to Ollama.
16. Ollama returns JSON containing a response field.
17. `ai_brain.lua` decodes the response.
18. The handler emits NPC output through `spatialChat`.

Recruiter flow is different from general chat because the LLM is used for intent classification. The gameplay action is still performed by existing recruiter screenplay functions after the intent is parsed.

SimPlayer configuration and controllers do not currently call `AiBrain`, `AiRegistry`, `AiGlobalChatHandler`, or `AiAgentBridge`. SimPlayers use their own Lua config load path plus C++ controllers.

# Current Technical Debt

The following risks and limitations exist in the current codebase:

- LLM HTTP calls are synchronous and can block the Lua path that handles spatial chat.
- LLM URL/model/default timeout are centralized in `ai_config.lua`, but broader gameplay configuration remains scattered.
- Missing LuaSocket or cjson now falls back safely, but the system still depends on those libraries for actual LLM responses.
- Lua guards prevent common nil/type failures, but they cannot prevent C++-side deadlocks or blocking inside native bindings.
- Smart Doctor full queue state is not persisted.
- Some older AI paths still directly know raw `LuaAiAgent` method names.
- Lua-side `pcall` cannot guarantee safety from every C++ binding failure.
- Buff amounts, durations, spawn points, cells, prices, and route data are scattered or hardcoded.
- IDL regeneration can break custom AiAgent fields, methods, annotations, or generated C++ signatures if custom declarations are not preserved.
- SimPlayer behavior modifies broad AiAgent lifecycle behavior through `simAlwaysActive` and `simPlayerBot`.
- SimPlayer configuration is loaded through a separate Lua instance, not through the same screenplay include path.
- SimPlayer controller selection relies partly on template-name inference.
- `sim_player_manager.lua` declares PvP `minStaySeconds` / `maxStaySeconds`, but the inspected `SimPvPController` loiter logic currently uses its own C++ range.
- Large AI training artifacts and archives are currently untracked at the repository root.

# Architectural Boundaries

This section defines intended boundaries for future refactoring. These boundaries describe where responsibilities should live; they do not necessarily describe where all current code already lives.

## Engine Layer (C++)

The engine layer should contain deterministic, low-level gameplay primitives that must run inside Core3:

- Applying and removing buffs.
- Starting entertainer sessions when needed by NPC service behaviors.
- Reading/writing AiAgent movement, combat, and lifecycle flags.
- SimPlayer path following and task scheduling that require C++ engine access.
- Force pool and regen mechanics if they are part of AiAgent combat behavior.

The engine layer should not own conversational policy, pricing policy, queue policy, prompt construction, or NPC personality text.

## Lua Bridge Layer

The Lua bridge layer should provide safe, stable wrappers around engine calls:

- Validate pointers and object types.
- Normalize capability checks.
- Hide raw `LuaAiAgent` method names from behavior scripts.
- Provide clear return values where possible.
- Centralize flag meanings such as medical/dance/music wipe flags.

This layer now exists as `MMOCoreORB/bin/scripts/custom_scripts/ai_agent_bridge.lua` for the Smart Doctor, Smart Dancer, and Smart Musician custom bindings.

## AI Service Layer

The AI service layer should own AI routing and model interaction:

- NPC profile lookup.
- Role-based chat routing.
- LLM client configuration.
- AI logging configuration.
- Prompt construction.
- JSON parsing and fallback behavior.
- LLM timeout/failure behavior.

Gameplay side effects should remain outside model responses. The current recruiter path follows this pattern by using the LLM for intent and then calling deterministic recruiter screenplay functions.

## Behavior Layer

The behavior layer should own individual NPC workflows:

- Smart Doctor quote/confirm/payment/queue/buff state machine.
- Smart Dancer watch behavior.
- Smart Musician listen behavior.
- SimPlayer PvP/miner behavior selection.
- Spatial chat response choices for deterministic service flows.

Behavior scripts may call bridge/service layers but should avoid duplicating low-level binding checks or C++ method names.

Smart Dancer and Smart Musician now share `MMOCoreORB/bin/scripts/custom_scripts/smart_entertainer_helper.lua` for generic safe custom naming, audience validation, and heartbeat scheduling while keeping behavior-specific logic in their own screenplays.

## Configuration Layer

The configuration layer should own tunable values:

- Spawn points.
- Prices.
- Buff amount/duration.
- Queue limits and delays.
- Ollama URL/model.
- SimPlayer group counts and routes.
- LLM enable/disable switches.
- AI logging enablement, level, and category switches.

Current LLM configuration and AI logging configuration are centralized in `ai_config.lua`. Smart Doctor gameplay/service tuning is active in `smart_doctor_config.lua`, while other gameplay configuration is still split across Lua defaults and hardcoded strings.

# Future Refactoring Roadmap

## Phase 1 - Documentation and Stabilization

### Goal

Capture existing behavior, IDL boundaries, and runtime wiring before changing the system.

### Expected benefits

- Preserves institutional knowledge.
- Reduces risk during IDL regeneration.
- Gives contributors a map of Lua/C++ responsibilities.
- Makes later cleanup easier to review.

### Risks

- Documentation can drift if future code changes do not update it.
- Some behavior may remain unclear until runtime testing validates the documented path.

## Phase 2 - Lua Bridge Cleanup

### Goal

Introduce a Lua bridge wrapper module around `LuaAiAgent` custom calls while preserving behavior.

### Expected benefits

- Fewer direct raw binding calls in behavior scripts.
- Centralized flag constants.
- Easier migration if binding names or signatures change.
- Safer capability checks.

### Risks

- Wrapper mistakes could change behavior if not introduced conservatively.
- Existing scripts rely on no-op behavior from C++ methods; wrappers must preserve that unless intentionally changed later.

## Phase 3 - AI Service Isolation

### Goal

Separate LLM configuration and HTTP request handling from chat routing.

### Expected benefits

- Cleaner failure handling if Ollama is unavailable.
- Easier model/URL configuration.
- Better testing of recruiter intent and NPC chat flows.
- Clear boundary between deterministic gameplay and generated text.

### Risks

- Any change to synchronous/asynchronous behavior can affect chat timing.
- Model prompts and fallbacks need careful regression testing.

## Phase 4 - SimPlayer Expansion

### Goal

Stabilize and expand simulated player behaviors after the core boundaries are clear.

### Expected benefits

- More lifelike low-population worlds.
- Configurable PvP roamers and resource gatherers.
- Better reuse of pathfinding and lifecycle control.

### Risks

- SimPlayers can add persistent server load.
- Movement/pathfinding behavior can affect zone performance.
- Combat interactions must avoid griefing or unintended economy/PvP side effects.

## Phase 5 - Advanced AI Behaviors

### Goal

Build richer reusable AI behavior modules on top of the stabilized bridge and service layers.

### Expected benefits

- NPC social behavior.
- Role-specific service NPCs.
- More natural conversations.
- Better long-term extensibility.

### Risks

- LLM behavior must remain bounded by deterministic gameplay rules.
- Emergent behavior can be hard to test.
- More AI features increase the need for centralized configuration, observability, and fallback paths.
