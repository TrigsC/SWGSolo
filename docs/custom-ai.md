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

Force regeneration uses the same lifetime rule as the standard AI behavior and recovery events: the event reference is mutex-protected, canceled during despawn/world removal, and its weak `AiAgent` target is cleared before the agent can be recycled. Force setters mark the managed object dirty; force getters remain side-effect-free while preserving their established non-const generated ABI.

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

- `miner` with `totalCount = 4`
- `pvp_solo` with `totalCount = 0`

As of the inspected code, conceptual miners are enabled by config and PvP roamers are disabled by count. Miner behavior remains visual/conceptual and does not create real resources.

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
11. `finishSample` snapshots the completed resource and optional conceptual yield using primitive/string values, returns the agent upright, plays `stop_sample`, and calls `startSimLoop` again. It then records the memory-only yield from the snapshot without rereading the new loop's mutable target or holding the completed sample's agent work open.

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

- `SimMinerController::finishSample` prepares a completed-sample snapshot when `minerConfig.yieldConfig.enabled` is true.
- `prepareConceptualYield` chooses a random amount between `yieldConfig.minAmount` and `yieldConfig.maxAmount`; the snapshot contains only the completed conceptual resource, amount, source object ID, and logging flag.
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
| `spawnGroups[].totalCount` | Yes | Controls how many miners spawn; current miner value is `4` in the inspected config. |
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
| `spawnGroups[].minerConfig.yieldConfig.enabled` | Yes | Enables memory-only conceptual accounting after sample completion. Default is `true`; it has no effect unless miner spawn count is above zero. |
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
- Conceptual yield logs can be enabled per miner spawn group with `minerConfig.yieldConfig.logYield = true`. Opt-in diagnostics log entry before the totals mutex and completion after it is released, including resource label, amount, source bot object id, aggregate total, mutex wait time, and total accounting time.
- Periodic summary logs can be enabled per miner spawn group with `minerConfig.summaryConfig.enabled = true`. The manager logs a compact line with active miner count and current conceptual totals at `summaryConfig.intervalSeconds`.
- Summary logging is read-only and skips completely empty summaries when there are no active miners and no conceptual totals.
- `SimPathFindTask` always logs `SimPlayer: [Thread] EXCEPTION in findPath!` if pathfinding throws, even when debug macros are disabled.
- The Lua config has commented-out `print` debug checks.

Stability considerations:

- `SimPlayerManagerConfig.enabled` is a fail-closed master gate. The manager defaults to disabled in C++, disables itself before attempting Lua load, and returns from initialization before spawning or scheduling when the Lua switch is false or invalid. Every periodic task and late spawn/cycle/yield entry point rechecks the master gate, so an already queued task cannot perform Resource Intelligence, market, demand, path-validation, or miner work after disable.
- `SimPathFindTask`, `ArrivalCheckTask`, `SimBehaviorTask`, and `SimRetryTask` all hold weak references to the controller and bounce work back through `Core::getTaskManager()->executeTask`.
- `SimBehaviorTask` re-resolves the miner controller inside the task-manager lambda from a captured strong base-controller reference. It should not capture raw delayed `SimMinerController*` pointers because survey/sample callbacks can run after a controller is stopped or recycled.
- The periodic miner summary task is manager-owned and calls back through `SimPlayerManager::instance()`; it does not capture controller or AiAgent pointers.
- Conceptual yield accounting uses a dedicated `conceptualMinerTotalsMutex`, copies completed-sample primitive/string values before the miner starts its next loop, and copies the resource key again at the manager boundary so the map never depends on caller-owned mutable string storage. It performs no `AiAgent`, object-manager, resource-manager, scheduling, persistence, or economy calls while holding that mutex. Logging occurs only before lock acquisition or after lock release.
- Miner movement validates navmesh and fallback destinations against the ground-zone boundary before terrain-height or pathfinding calls. A random fallback that crosses the terrain edge is biased toward the planet center; if it is still invalid, the loop retries without querying out-of-bounds terrain. This prevents the long-running conceptual random walk from producing repeated `TerrainManager` stack traces.
- `checkArrival` locks the AiAgent while examining combat/death/movement state and updating patrol movement, but releases that lock before scheduling another task, restarting path selection, or invoking the behavior-specific `onArrived` callback.
- Resource Intelligence copies strong `ResourceSpawn` references while holding a short `ResourceManager` read lock, then releases the manager lock before locking and inspecting individual spawns. Density simulation follows the same manager-then-release/spawn-lock separation. This avoids holding the global resource-manager lock across per-spawn metadata or density work.
- The current miner can schedule repeated arrival checks every 500 ms while moving and every 1000 ms while waiting, incapped, or in combat.
- Pathfinding failure schedules a retry after 5000 ms by calling `startSimLoop` again.
- If miners are enabled, they will be always-active SimPlayers with `simAlwaysActive`, `simPlayerBot`, and `despawnOnNoPlayerInRange(false)` set by the manager.

Known limitations:

- Current miners are conceptual and visual only even when enabled through `totalCount`.
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
| Phase C | Persist abstract resource inventory safely. | C.1/C.2 provide the object/database bootstrap; C.3.1 can checkpoint aggregate conceptual totals behind a disabled-by-default switch. Demand state still does not consume them. |
| Phase D | Sell resource lots through a controlled vendor/market abstraction. | Introduce a constrained output path with caps, pricing rules, and audit logs. |
| Phase E | Connect to crafting/economy loops. | Only after resource accounting, persistence, and market limits are stable. |

### Phase C - Persistence Architecture Research

This section documents the research that informed C.1/C.2 and the later C.3.1 aggregate checkpoint. Live SimMiner accounting remains memory-only; C.3.1 periodically copies completed session totals into separate durable conceptual lots only when explicitly enabled.

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
| Phase C.1 | Define persisted economy data shape. | Implemented with `AiEconomyData` and `AiEconomyStockpileLot` IDL objects. |
| Phase C.2 | Add database registration and load-only bootstrap. | Implemented with the dedicated `aieconomy` object database and `AiEconomyManager` startup load/create validation. |
| Phase C.3.1 | Persist aggregate conceptual miner totals. | Implemented behind a disabled-by-default switch; one durable lot is upserted per non-zero conceptual label. |
| Phase C.3.2 | Prove restart survival and diagnostics. | Exercise repeated intervals/restarts, verify quantities and lot identity, and add repair-oriented diagnostics if needed. |
| Phase C.3.3/C.3.4 | Expose durable supply to demand state. | Add a separate disabled gate before D.6.2 may consume `persistentStockpileSupply`; keep demand integration independent from persistence writes. |
| Phase C.4 | Add admin/debug inspection. | Add read-only logs or tools to inspect persisted totals without player-facing economy effects. |
| Phase C.5 | Add migrations and repair tooling. | Add version migration, backup/export, and safe reset options before expanding the economy. |

Open questions:

- How should future durable stockpile ownership be divided below the galaxy-wide `AiEconomyManager` boundary: profession, faction, vendor, crafter, or another explicit scope?
- Should conceptual totals be galaxy-wide, planet-specific, resource-type-specific, or role-specific from the first persisted version?
- What is the acceptable crash-loss window for a low-population solo server: 5 minutes, 15 minutes, or one server tick batch?
- How much history is useful for supply/demand without creating unbounded database growth?
- Should persistence be disabled by default until migration and admin reset tooling exists?

### Phase C.1/C.2 - AI Economy Persistence Bootstrap

Phase C.1/C.2 implements the first durable AI economy foundation without connecting it to production, demand, market, or gameplay state. It follows the existing FRS manager-state pattern:

- A transient manager owns the runtime reference.
- One generated `ManagedObject` contains durable state.
- A dedicated Core3 object database stores that object.
- Startup loads the existing object or creates an empty default when the database is genuinely empty.

#### Added objects and ownership

The new files are:

- `server/zone/managers/aieconomy/AiEconomyData.idl`: the schema-versioned root data object.
- `server/zone/managers/aieconomy/AiEconomyStockpileLot.idl`: the future resource-lot shape.
- `server/zone/managers/aieconomy/AiEconomyManager.h/.cpp`: the transient persistence owner, bootstrap, validator, and narrow C.3.1 aggregate upsert boundary.

`AiEconomyManager` is intentionally separate from `SimPlayerManager`. Durable economy ownership must survive individual SimPlayer controllers, spawned NPCs, and future role implementations. This also keeps object persistence outside SimPlayer, `AiAgent`, conceptual-total, market-observation, and resource-manager lock scopes.

The root object currently stores:

- `schemaVersion`, currently `1`.
- `createdTimestamp`.
- `updatedTimestamp`.
- `nextStockpileEntryId`.
- A vector of `AiEconomyStockpileLot` references.

The future lot shape includes:

- Stable economy `entryId`.
- Conceptual label.
- Quantity and reserved quantity; available quantity is derived as `quantity - reservedQuantity`.
- Acquisition source.
- Resource lifecycle state.
- Owner scope.
- Identity confidence.
- Acquisition and update timestamps.
- Optional resource spawn object ID, generated name, exact type, class-chain snapshot, source planet/zone, active-at-acquisition state, demand-profile explanation, and quality tier.
- Optional OQ/CD/DR/HR/FL/MA/PE/SR/UT/CR stat snapshots. Missing stats default to `-1`.

There is no separate persisted boolean on each lot. Membership in the persisted `AiEconomyData` object is the durable/persisted semantic. D.6.5.1 simulation rows remain explicitly `persisted=false` because they are not members of this object.

#### Database and startup flow

`ObjectManager` registers and loads a dedicated root object database named `aieconomy`. C.3.1 additionally uses `aieconomylots` for referenced stockpile-lot objects, following the FRS split between `frsmanager` and `frsdata`. Neither database reuses `sceneobjects`, `resourcespawns`, MySQL, Lua configuration, or a player-facing object database.

`ZoneServerImplementation::startManagers` initializes `AiEconomyManager` immediately before `SimPlayerManager`. Startup then:

1. Opens the `aieconomy` object database.
2. Iterates its object keys through the normal object database iterator.
3. Resolves the single object through the Core3 object broker as `AiEconomyData`.
4. Creates and persists a default empty `AiEconomyData` through `ObjectManager::persistObject` only when the database has no object.
5. Validates the complete snapshot before publishing it as persistence-ready.
6. Emits one bounded startup diagnostic.

Example first-run log:

```text
AiEconomyPersistence loaded=true created=true version=1 stockpileLots=0 persistenceReady=true mode=load-only totalsImported=false persistentStockpileSupplyChanged=false
```

Example subsequent-start log:

```text
AiEconomyPersistence loaded=true created=false version=1 stockpileLots=0 persistenceReady=true mode=load-only totalsImported=false persistentStockpileSupplyChanged=false
```

No periodic persistence task is added in this phase.

#### Validation and fail-closed behavior

The loader accepts exactly one compatible `AiEconomyData` object. It fails closed for AI economy persistence when it encounters:

- An unavailable database.
- An object of the wrong generated type.
- More than one root economy object.
- An unsupported schema version.
- Missing, reversed, or implausibly future timestamps.
- A zero or non-monotonic next-entry ID.
- Too many stockpile lots.
- Null lots, duplicate/zero entry IDs, absurd quantities, or reservations greater than quantity.
- Missing identity fields or oversized metadata.
- Unknown acquisition-source, lifecycle, or identity-confidence values.
- Invalid resource-stat snapshots.

Validation copies lot references while holding only the root data lock, releases it, and then validates each lot under its own short lock. Object database loading and creation occur without holding SimPlayer/controller/`AiAgent`, conceptual-total, market-observation, `ResourceManager`, `AuctionManager`, or resource-object locks.

Invalid or incompatible state is not replaced with an empty object. The manager logs one clear error and leaves persistence unavailable. SimPlayer startup continues afterward, so visual miner behavior and all existing simulation-only tasks remain operational. No recovery path creates resources or mutates player-facing economy state.

#### Deliberate load-only boundary

This phase does **not**:

- Copy `SimPlayerManager::conceptualMinerTotals` into `AiEconomyData`.
- Add stockpile lots after startup.
- Populate D.6.2 `persistentStockpileSupply`.
- Change reserve ratios, pressure scores, D.4/D.6.6 plans, density/path simulations, or miner behavior.
- Import D.6.4 market observations. Public listings remain `owned=false` and `imported=false`.
- Persist active miners, destinations, patrol/path data, resource choices, animation/sample state, pending yield, diagnostics, or raw event history.
- Create `ResourceContainer`, inventory, vendor, bazaar/auction, harvester, crafting, credit, or other player-facing objects or transactions.
- Add a save cadence, migration, repair, admin mutation, reservation, consumption, or stockpile mutation API.

The live `SimPlayerManager` conceptual counters still reset on restart. D.6.5.1 stockpile-shaped logs remain memory-only simulations and continue reporting `persisted=false`. When C.3.1 is disabled, the root remains only a load/create proof; when C.3.1 is enabled, separate durable conceptual lots preserve completed aggregate output without changing the live counters or demand state.

The implemented follow-up is C.3.1 below. C.1/C.2 itself remains the load/create and validation boundary.

### Phase C.3.1 - Conceptual Miner Total Persistence

C.3.1 connects completed memory-only SimMiner totals to the persisted AI economy object while preserving every gameplay and demand-state boundary. It stores aggregate stockpile state rather than per-sample event history.

Configuration lives under `SimPlayerManagerConfig.aiEconomyPersistenceConfig`:

```lua
aiEconomyPersistenceConfig = {
    persistConceptualMinerTotals = false,
    intervalSeconds = 300,
    logSummary = true,
}
```

| Field | Behavior |
|---|---|
| `persistConceptualMinerTotals` | Disabled by default. No persistence task is scheduled unless this and the SimPlayerManager master switch are enabled and `AiEconomyManager` is persistence-ready. |
| `intervalSeconds` | Coarse checkpoint interval, clamped to 60-3600 seconds. |
| `logSummary` | Emits one compact successful-update line per interval that contains non-zero totals. |

The manager-owned task reloads this config at each interval while running. Disabling the block stops rescheduling. A missing or unreadable config also stops persistence rather than retaining a previously enabled write switch. Enabling it from a fully stopped state still requires the normal manager/server reload.

#### Aggregate snapshot and lock boundary

The task copies `SimPlayerManager::conceptualMinerTotals` while holding only `conceptualMinerTotalsMutex`. It then releases that mutex before validation, object locking, persistence, or logging. Empty labels, zero quantities, labels longer than 128 characters, and quantities above the persisted validation bound are rejected.

No controller, `AiAgent`, `ResourceSpawn`, `AuctionItem`, `ResourceContainer`, market object, or other mutable gameplay pointer is captured by the delayed task. The task calls the manager singletons when it executes.

Each accepted label is represented by one persisted aggregate lot:

- `conceptualLabel=<label>`
- `quantity=<durable startup baseline + current-session aggregate>`
- `reservedQuantity=0`
- `availableQuantity=quantity`
- `acquisitionSource=conceptual_miner`
- `resourceLifecycleState=conceptual`
- `ownerScope=galaxy`
- `identityConfidence=conceptual_label`
- Resource spawn object ID `0`
- Empty spawn name, exact resource type, class chain, planet, and zone
- Missing stat snapshots remain `-1`

The root remains in `aieconomy`; referenced `AiEconomyStockpileLot` objects are persisted in `aieconomylots`. New lots are persisted before their references are attached to the root, and object-database calls occur outside root and lot locks.

New lots are fully initialized by their generated constructor before `persistObject` is called. No `@dirty` mutator is invoked on an unregistered object. Once persisted, later quantity and root-reference changes use the generated `@dirty` methods rather than manually placing objects into the modified-object queue. This follows the FRS creation pattern and avoids exposing Core3's periodic backup pass to an unregistered lot pointer.

#### Idempotency and restart behavior

`AiEconomyManager` captures loaded conceptual-miner quantities as a startup baseline. Each checkpoint calculates a target quantity as:

```text
target persisted quantity = loaded startup quantity + current-session conceptual total
```

The manager then **sets** the lot to that target. It never adds the current lot quantity again. Therefore:

- Repeating a checkpoint with the same session total does not increase the lot.
- A label is matched by conceptual label plus its `conceptual_miner`/`conceptual`/`galaxy`/`conceptual_label` classification.
- Existing matching lots are updated in place.
- New non-zero labels create one lot with the next stable entry ID.
- Duplicate matching lots fail closed instead of being merged silently.
- Restart loads the existing lot and uses its quantity as the new session baseline.

Example first checkpoint:

```text
AiEconomyPersistenceConceptualTotals updated=true labels=4 createdLots=4 updatedLots=0 totalQuantity=3472 mode=persisted-conceptual totalsImported=true persistentStockpileSupplyChanged=false
```

Example later checkpoint:

```text
AiEconomyPersistenceConceptualTotals updated=true labels=4 createdLots=0 updatedLots=4 totalQuantity=3600 mode=persisted-conceptual totalsImported=true persistentStockpileSupplyChanged=false
```

After restart, loaded conceptual lots produce one bounded read-only summary:

```text
AiEconomyPersistenceStockpile loadedLots=4 conceptualMinerLots=4 totalQuantity=3600 mode=read-only persistentStockpileSupplyChanged=false
```

#### Fail-closed and demand-state boundary

The mutation path is serialized by a dedicated `AiEconomyManager` mutex. Root and lot locks are short and never overlap the conceptual-total lock. Invalid persisted state, duplicate conceptual lots, exhausted IDs, persistence exceptions, or failed post-update validation mark persistence unavailable and stop this task without stopping visual SimMiner behavior.

C.3.1 deliberately does not:

- Populate D.6.2 `persistentStockpileSupply`.
- Change `aiConceptualSupply`, reserve ratios, shortage/surplus state, pressure scores, or D.6.6 plans.
- Import D.6.4 public market quantities into AI ownership.
- Persist active miners, destinations, paths, animations, in-progress samples, pending yield, diagnostics, or raw event history.
- Create or mutate real resources, `ResourceContainer`, inventory, vendor, bazaar/auction, stockroom, harvester, crafting, credit, or player-facing objects.
- Change SimMiner targeting, movement, survey/sample timing, conceptual resource selection, or yield amounts.

Demand-state logs therefore continue to report `persistentStockpileSupply=0` unless the later C.3.3/C.3.4 read-only demand integration gate is explicitly enabled. Durable lots remain available through AI economy persistence diagnostics even when D.6.2 does not consume them.

Known limitation: these lots still have only `conceptual_label` identity confidence. They do not identify an exact `ResourceSpawn`, resource type, planet, density map, or stat snapshot.

#### Next phases

- **C.3.2 - Restart Survival and Diagnostics:** test repeated checkpoints and restarts, verify that lot counts remain stable, and add bounded audit/repair diagnostics only where evidence shows they are needed.
- **C.3.3/C.3.4 - Gated Persistent Supply Integration:** implemented below as a read-only, disabled-by-default D.6.2 consumer for validated durable conceptual baseline quantities. Persistence writes remain independent from demand calculations.

### Phase C.3.3/C.3.4 - Gated Persistent Supply Integration

C.3.3/C.3.4 is the first read-only demand-state consumer of validated durable conceptual stockpile lots. It allows D.6.2 to optionally include recovered AI-owned conceptual baseline supply in `persistentStockpileSupply` while preserving all existing write, miner, market, and gameplay boundaries.

Configuration lives under `SimPlayerManagerConfig.persistentStockpileDemandConfig`:

```lua
persistentStockpileDemandConfig = {
    enabled = false,
    includeConceptualMinerLots = true,
    logSummary = true,
}
```

| Field | Behavior |
|---|---|
| `enabled` | Disabled by default. When false, D.6.2 behavior remains unchanged and `persistentStockpileSupply=0`. |
| `includeConceptualMinerLots` | Allows validated conceptual-miner lots to contribute only their recovered durable baseline portion. |
| `logSummary` | Emits one compact read-only snapshot line per D.6.2 interval while the gate is enabled. |

The config is deliberately separate from `aiEconomyPersistenceConfig`, which controls write/checkpoint behavior. Enabling D.6.2 persistent supply reads does not enable checkpointing. Enabling checkpointing does not make D.6.2 consume persisted lots.

#### Double-counting boundary

C.3.1 persists conceptual lots with this target quantity:

```text
target persisted quantity = loaded startup quantity + current-session conceptual total
```

D.6.2 already counts the current-session total as `aiConceptualSupply`. Therefore C.3.3/C.3.4 must not feed the full current persisted lot quantity into demand state, or the current session would be counted twice.

The implemented method is `startup_baseline_only`. `AiEconomyManager` captures loaded conceptual-miner lot quantities into its startup baseline during initialization. D.6.2 calls a narrow read-only snapshot method that returns copied label/quantity pairs from that baseline only. It does not read mutable lot objects, lock stockpile lots during scoring, mark objects dirty, checkpoint data, or inspect current persisted aggregate quantities.

The resulting D.6.2 equation is:

```text
totalKnownSupply =
    current-session aiConceptualSupply
    + observed marketObservedSupply
    + recovered startup-baseline persistentStockpileSupply
```

#### Mapping and confidence

Persistent conceptual lots use exactly the same narrow profile mapping as D.6.2 conceptual live totals:

| Demand profile | Persistent conceptual labels |
|---|---|
| `composite_armor_supply` | `copper`, `iron` |
| `master_weaponsmith_staples` | `copper`, `iron` |
| `high_damage_weapon_components` | `copper`, `iron` |
| `chef_buff_foods` | `water` |
| `chef_high_value_consumables` | `water` |
| `production_infrastructure` | `copper`, `iron`, `gas` |

No exact `ResourceSpawn`, resource type, planet, density, stat, or quality is inferred from these labels. Matching persistent conceptual lots report `persistentStockpileConfidence=conceptual_label`. Overall `supplyConfidence` still prefers `exact_type`, then `coarse_family`, then `conceptual_label`, then `none`.

#### Diagnostics

When enabled, the task logs one bounded read-only summary:

```text
PersistentStockpileDemandSnapshot enabled=true conceptualMinerLots=4 baselineQuantity=3347 labels=copper=900,iron=800,gas=700,water=947 status=ready mode=read-only
```

D.6.2 lines keep the existing fields and add persistent diagnostics:

```text
DemandStateSimulation profile=production_infrastructure state=target desiredReserve=10000 aiConceptualSupply=159 marketObservedSupply=0 persistentStockpileSupply=2400 persistentStockpileLotsMatched=3 persistentStockpileQuantityMatched=2400 persistentStockpileConfidence=conceptual_label persistentStockpileLabels=copper=900,iron=800,gas=700 persistentStockpileMode=startup_baseline_only persistentStockpileStatus=ready totalKnownSupply=2559 supplyConfidence=conceptual_label mode=log-only
```

If the gate is disabled, no `PersistentStockpileDemandSnapshot` line is emitted and D.6.2 remains effectively unchanged with `persistentStockpileSupply=0`. If persistence is unavailable or invalid, D.6.2 continues with live conceptual and market supply only, logs `persistentStockpileStatus=unavailable` or `invalid` while enabled, and contributes zero persistent supply.

#### Safety boundaries

C.3.3/C.3.4 does not:

- Mutate `AiEconomyData` or `AiEconomyStockpileLot`.
- Mark any object dirty or call `persistObject`.
- Checkpoint from the demand-state task.
- Import market observations into AI-owned stockpile.
- Populate reservations or consumption.
- Create `ResourceContainer`, resource, inventory, vendor, bazaar/auction, stockroom, harvester, crafting, credit, or player-facing economy objects.
- Change SimMiner movement, target selection, patrol/path data, survey/sample timing, conceptual resource choice, yield amount, density simulation, path validation, or D.6.6 plan activation.

Locking remains one-way and copy-first: D.6.2 copies live conceptual totals under `conceptualMinerTotalsMutex`, copies market observation under `marketSupplyObservationMutex`, and copies persistent startup-baseline labels through `AiEconomyManager` without holding either of those locks. Scoring and logging occur after these snapshots are copied.

#### Test procedure

1. With `persistentStockpileDemandConfig.enabled=false`, verify D.6.2 still reports `persistentStockpileSupply=0` and emits no `PersistentStockpileDemandSnapshot`.
2. Enable C.3.1 checkpointing, let miners persist conceptual lots, then restart.
3. Enable `persistentStockpileDemandConfig.enabled=true` while D.6.2 is enabled.
4. Confirm D.6.2 shows `persistentStockpileStatus=ready`, `persistentStockpileConfidence=conceptual_label`, and non-zero `persistentStockpileSupply` only for mapped labels.
5. Confirm no double count: current-session totals remain under `aiConceptualSupply`, and persistent contribution equals the recovered startup baseline rather than the full current persisted aggregate.
6. If D.6.4 is also enabled, confirm `totalKnownSupply = aiConceptualSupply + marketObservedSupply + persistentStockpileSupply`.

#### Remaining limitations

- Persistent supply is still conceptual-label only and can overlap across demand profiles.
- It has no exact resource identity, quality, planet, density, expiration, allocation, reservation, consumption, or cost.
- It affects D.6.2 diagnostic reserve/pressure logs only. It does not directly feed D.3/D.4/D.5/D.6.6 miner behavior, target assignment, or gameplay.

The next persistence phase should stay diagnostic: prove restart behavior over several checkpoint/restart cycles and add bounded audit tooling before any reservation, consumption, or demand-weighted behavior switch consumes stockpile state operationally.

### C.4 / RI.4 - Read-Only Stockpile Inspection

C.4/RI.4 adds dashboard/API inspection for AI-owned conceptual stockpile state. It is observability only: the dashboard reads copied primitive/string snapshots from `SimPlayerManager` and `AiEconomyManager`, then renders them beside Resource Scout, Resource Coverage, and RI.3 recent intelligent yield provenance.

The inspection view separates three quantities that should not be confused:

- Current-session conceptual totals from `SimPlayerManager::conceptualMinerTotals`.
- Persisted conceptual-miner lot quantities from `AiEconomyData` / `AiEconomyStockpileLot`.
- Startup-baseline quantities captured by `AiEconomyManager` at load time and optionally consumed by D.6.2 when `persistentStockpileDemandConfig.enabled=true`.

The dashboard section reports persistence readiness, checkpoint configuration, persistent-demand configuration, loaded lot counts, conceptual-miner lot counts, persisted quantity, current-session quantity, available/reserved quantities, and per-label summaries. Per-label rows include the demand profiles that currently consume that conceptual label by reusing the same D.6.2 conceptual-label mapping. Bounded persisted-lot rows include entry id, conceptual label, quantity, available/reserved quantity, acquisition source, lifecycle, owner scope, identity confidence, optional source resource fields, and update age.

The view is deliberately clear that this is conceptual AI economy state rather than real SWG inventory. Rows use `identityConfidence=conceptual_label`, `yieldMode=conceptual`, and explicit safety fields such as `realResourceCreated=false`, `resourceContainerCreated=false`, `inventoryMutated=false`, and `economyMutated=false`.

C.4/RI.4 does not add persistence writes, alter C.3.1 checkpoint behavior, alter D.6.2 reserve/pressure math, add stockpile reservations or consumption, or change SimMiner targeting, activation, movement, sampling, conceptual labels, or yield amounts. It does not call real extraction APIs, create `ResourceContainer` objects, or mutate inventory, vendors, bazaar/auction, harvesters, crafting, credits, market state, or player-facing economy objects.

This bridges RI.3 provenance to future exact-resource-aware stockpiles: operators can now see recent exact-resource-aware conceptual yield explanations next to the persistent conceptual lots and startup baseline that demand state may read later, while the system still avoids claiming that real resource units exist.

### C.5 / RI.5 - Runtime Exact-Resource-Aware Conceptual Stockpile Aggregation

C.5/RI.5 adds a runtime-only aggregation layer for intelligent SimMiner conceptual yields that have RI.3 provenance. It does not persist exact-resource-aware rows yet. The goal is to prove the shape of exact-resource-aware conceptual stockpile data in the dashboard before adding any durable write path.

The source signal is `SimPlayerManager::recordIntelligentConceptualMinerYield`. After the existing broad conceptual total is credited, the manager copies the same RI.3 provenance snapshot into a bounded in-memory aggregate when the assignment identity is available as `identityConfidence=observed_resource_spawn`.

Rows are grouped by copied primitive/string provenance:

- Conceptual label.
- Source resource name.
- Source resource type.
- Source zone/planet.
- Selected demand profile.
- Identity confidence.
- Acquisition source, currently `intelligent_miner`.

Each row tracks conceptual quantity, event/sample count, first observed time, last observed time, latest density coordinate, latest density value, demand state, and pressure score. The dashboard exposes these rows as `resourceAwareStockpile` with `mode=runtime-read-only`, `runtimeOnly=true`, and `persisted=false`.

This differs from broad conceptual totals. The existing broad counters still answer "how much water/copper/iron/gas did conceptual miners produce this session?" The C.5/RI.5 rows answer "which exact observed resource opportunity did an intelligent assignment conceptually produce against?" Broad conceptual totals remain the accounting source for C.3.1 checkpointing and D.6.2 current-session supply. Resource-aware rows are explanatory, restart-volatile runtime aggregates and are not added back into demand math.

Double-counting is avoided by treating the new rows as provenance aggregates only. `recordIntelligentConceptualMinerYield` still calls the existing `recordConceptualMinerYield` once, exactly as before, then records the runtime provenance aggregate separately. The dashboard can show both views, but D.6.2 and C.3.1 continue to consume the broad conceptual totals unless a future explicitly gated phase changes that.

Safety boundaries:

- No real `ResourceSpawn` extraction or sampling API is called.
- No `ResourceContainer` is created.
- No player inventory, vendor, bazaar/auction, harvester, crafting, credit, market, reservation, consumption, or stockpile allocation path is touched.
- No `AiEconomyData` or `AiEconomyStockpileLot` write is added in this phase.
- No SimMiner targeting, activation caps, assignment selection, movement, sampling duration, yield amount, or conceptual label selection changes.
- Rows store only copied primitive/string metadata and are bounded in memory.

This prepares for future exact-resource-aware stockpile persistence and later AI crafting by proving a safe provenance key and dashboard shape without crossing into real economy behavior.

### D.7 / RI.6 - Economy Decision Audit and Dashboard Health Summary

D.7/RI.6 adds a read-only audit layer to the AI economy dashboard. It summarizes whether the current intelligence loop looks aligned without changing any planner, miner, stockpile, demand, or economy behavior.

The audit is computed inside the existing `/v1/aieconomy/dashboard/` snapshot path from data that has already been copied for other dashboard sections:

- `resourceScout` active demand opportunities.
- `resourceCoverage` covered, uncovered, and blocker status for top opportunities.
- RI.3 `recentIntelligentYields`.
- C.5/RI.5 `resourceAwareStockpile` runtime provenance aggregates.
- C.4/RI.4 `stockpileInspection`.
- D.5.8 limited activation health counters.
- D.6.2 demand profile summaries.

The top-level status values are intentionally conservative:

| Status | Meaning |
|---|---|
| `healthy` | Coverage, recent intelligent yield, resource-aware stockpile rows, stockpile readability, and safety flags are aligned. |
| `watch` | Data is present, but there are uncovered opportunities, no recent matching yield yet, no resource-aware rows yet, stockpile readability concerns, or concentration in one profile while other opportunities remain uncovered. |
| `blocked` | Activation health or coverage blockers indicate investigation is needed, such as path failures, activation failures, emergency disablement, or most uncovered opportunities blocked by path/density/planet constraints. |
| `no_data` | The dashboard snapshot has no useful demand, coverage, yield, or resource-aware stockpile data yet. |
| `unsafe` | Reserved for unexpected safety flags that would indicate real resources, containers, inventory, market, or economy mutation. |

The audit also emits a recommendation such as `keep_current`, `watch_uncovered_priority`, `investigate_blockers`, or `do_not_change_behavior_yet`, plus compact counts and a bounded profile audit. Profile rows compare demand state, coverage, recent intelligent yield quantity, and resource-aware stockpile quantity so operators can see whether shortage profiles are receiving attention or whether output is drifting toward surplus profiles.

D.7/RI.6 is deliberately read-only and should run before any future phase lets resource-aware stockpile data influence demand math or miner planning. It gives operators a single health view for the current loop while preserving the experimental boundary.

Safety boundaries:

- No miner targeting, D.6.6 planning, D.6.2 demand math, activation caps, movement, sampling, yield amount, or conceptual label selection changes.
- No C.3.1 checkpoint behavior changes and no new persistence writes.
- No stockpile reservations, consumption, allocation, crafting inputs, vendor output, or market output.
- No real resource extraction, no `ResourceSpawn::extractResource`, and no `ResourceContainer` creation.
- No player inventory, vendor, bazaar/auction, harvester, crafting, credit, market, or player-facing economy mutation.
- The audit stores no raw controller, agent, resource, zone, path, market, vendor, container, or inventory pointers.

### D.7.1 / RI.7 - Coverage Alignment Diagnostics

D.7.1/RI.7 adds a read-only explanation layer for the specific case where `resourceCoverage` reports top opportunities as uncovered while intelligent assignments exist. It does not change coverage rules or miner behavior; it explains why the existing matcher did or did not count an assignment as coverage.

The dashboard API now emits `coverageAlignmentDiagnostics` with:

- `opportunities`: bounded top-opportunity rows showing resource, profile, demand state, current coverage status, diagnosis, match counts, and the closest current assignment.
- `assignments`: bounded intelligent assignment rows showing target resource/type/zone, selected profile, matched top opportunity, coverage-alignment status, match reason, path validation status, path trust, density target status, age, and remaining TTL.
- `counts`: compact totals for exact matches, active matches, validated-but-not-active matches, candidate matches, untrusted path matches, stale matches, profile/resource/zone mismatches, normalized-key mismatches, config/travel reachability blockers, and assignments that are not tied to a top opportunity.
- `activeMinerZones`, `configuredMinerSpawnZones`, `samePlanetRequired`, and `travelSupported`: copied deployment context used to explain whether a valuable opportunity is actually reachable by the current limited miner path.

RI.7 also makes the diagnostics config-aware. If a top resource opportunity is on a planet that has no configured miner spawn zone, such as a Dathomir resource while miners only spawn from the configured Naboo/Corellia/Tatooine shuttleports, the opportunity is diagnosed as `unreachable_no_configured_miner_spawn_zone`. If a planet is configured but no active miner is currently local and travel is not implemented, it is diagnosed as `travel_required_unsupported`. These are dashboard explanations only; they do not change planner scoring, miner spawning, or movement.

This phase answers questions such as:

- Is a top opportunity uncovered because no assignment exists?
- Does an assignment target the same resource but a different demand profile?
- Does the resource/profile match but the miner zone does not match the opportunity zones?
- Is the assignment only a candidate, validated but not active, stale, or using an untrusted fallback path?
- Did resource keys normalize to the same value even though exact string matching differs?
- Is the resource on a planet outside the configured miner deployment zones?
- Would the miner need travel that the current limited activation path does not support?
- Is the assignment simply not mapped to any current top opportunity?

The dashboard adds a compact Coverage Alignment panel with opportunity-level reasons and assignment-level mapping. This should run before any planner or matcher tuning, because it distinguishes data interpretation problems from actual miner behavior problems.

Safety boundaries:

- Read-only dashboard/API only.
- No miner targeting, assignment selection, activation, movement, sampling, yield, demand math, or coverage-rule behavior changes.
- No persistence writes, stockpile reservations, crafting inputs, vendor output, market output, or economy mutations.
- No real resource extraction, no `ResourceSpawn::extractResource`, no `ResourceContainer` creation, and no player inventory mutation.
- Rows store only copied primitive/string metadata from existing dashboard snapshot inputs and current intelligent assignment state.

### T.1 / D.8 - Travel Plan Simulation and AI Population Dashboard

T.1/D.8 adds a read-only travel planning layer for the dashboard. The goal is to explain what future travel-aware AI would probably want to do, without moving, recycling, despawning, respawning, selling, staging, or changing any miner behavior.

The dashboard API now emits:

- `travelPlanSimulation`: simulation-only travel plan rows, bounded by config, with `travelImplemented=false`, `travelSupported=false`, `behaviorChanged=false`, and explicit safety flags.
- `aiPopulation`: active AI/miner population counts by role, assignment state, idle/blocked state, current zone, configured miner spawn zones, simulated plan counts, and travel support flags.
- `resourceRush`: a compact summary of local versus remote high-priority resource opportunities and the top remote opportunity, if present.

Remote resource rush planning compares active demand/resource opportunities against current active miner zones and configured miner spawn zones. If a valuable opportunity is remote, the simulation can produce `resource_rush` rows showing which active miner would hypothetically travel, from which zone, to which resource zone, and why. These rows use copied primitive/string metadata only and recommend `travel_when_supported`.

Hub-return planning is also simulation-only. `SimPlayerManagerConfig.aiTravelSimulationConfig.homeHub` defines a future Coronet/Corellia resource hub, using the same approximate Coronet hangout coordinates already configured for the Corellia shuttleport. The dashboard can emit `hub_return` rows for miners away from Corellia with recommendation `return_to_hub_when_selling_supported`. This does not add selling, staging, vendor, bazaar, inventory, or market behavior.

Configuration lives under `SimPlayerManagerConfig.aiTravelSimulationConfig`:

```lua
aiTravelSimulationConfig = {
    enabled = true,
    maxPlans = 20,
    includeResourceRushPlans = true,
    includeHubReturnPlans = true,
    homeHub = {
        enabled = true,
        key = "coronet_resource_hub",
        zone = "corellia",
        city = "coronet",
        x = -155.0,
        y = -4722.0,
        purpose = "sell_resources",
    },
}
```

The economy audit can now distinguish remote high-priority opportunities pending travel support from local assignment or path blockers. A remote opportunity with simulation rows should remain `watch`, with recommendation `enable_travel_or_add_local_miners_later`, instead of being treated as an immediate behavior failure.

This phase prepares for future travel behavior by making the intended decisions visible first. It also gives operators a scalable population view for monitoring many AI without parsing logs. Existing PvP simulated travel/recycle behavior remains a separate precedent; T.1/D.8 does not change or reuse that behavior for miners.

Safety boundaries:

- No actual travel, shuttle use, recycle, despawn, respawn, miner relocation, or PvP travel behavior changes.
- No miner targeting, assignment selection, activation, movement, sampling, yield amount, conceptual label selection, D.6.2 demand math, D.6.6 planning, or activation cap changes.
- No persistence writes, stockpile reservations, consumption, selling, vendor output, bazaar output, crafting input/output, credit, market, or player-facing economy behavior.
- No real resource extraction, no `ResourceSpawn::extractResource`, no `ResourceContainer` creation, and no player inventory mutation.
- Dashboard rows store only copied primitive/string metadata from existing dashboard snapshot inputs, active controllers, and current intelligent assignment state.

### P.1 / D.8.1 - Path Validation Explanation and Dashboard Diagnostics

P.1/D.8.1 adds a read-only path validation explanation layer for intelligent SimMiner assignments. It exists because coverage alignment can show that an assignment should cover a local opportunity while the limited activation path still refuses to activate it due to `pathValidationStatus=failed` or `pathTrustStatus=directFallbackUnverified`.

The dashboard API now emits `pathValidationDiagnostics` with compact counts and bounded rows. Each row is copied from current assignment state plus the latest path validation snapshot for that miner, when available. The row can include miner id, assignment status, demand profile, target resource/type/zone, density target status, path validation status, path trust status, reject reason, miner and target coordinates, validation target coordinates, straight-line distance, path distance, path node count, direct fallback flag, validation age, assignment age, coordinate drift distance, and a human-readable reason.

The diagnostics distinguish path and target states such as:

- `direct_fallback_unverified`: pathfinder returned only the unverified start/end fallback, so activation remains blocked.
- `path_too_long`, `exceeds_max_path_distance`, and `too_many_path_nodes`: a path or target exceeded configured validation limits.
- `no_path` and `path_exception`: no usable path was produced or path validation failed with an exception.
- `target_mismatch` and `density_target_coordinate_mismatch`: the assignment target drifted from the coordinate used by the latest validation snapshot.
- `stale`: the latest validation snapshot is older than the configured freshness window.
- `miner_not_in_navmesh`, `target_outside_navmesh`, and `bad_terrain_or_height`: copied navmesh/terrain checks indicate the miner or target is suspect.
- `path_validation_unavailable` and `unknown_path_failure`: the dashboard does not have enough copied data to explain more precisely yet.

Known versus unknown navmesh detail is explicit. The validator can report whether the miner was in navmesh when the validation was scheduled, whether the target coordinate was checked against nearby navmesh areas, whether that target was inside one of those areas, and the target terrain height delta. If a field is not safely known, the API and UI mark it unavailable/unknown instead of inferring terrain or navmesh certainty from logs.

The dashboard adds a Path Validation panel near Coverage Alignment. It shows candidate counts, failed validations, direct fallback count, stale count, target drift count, navmesh/terrain warning count, and a table with miner, target, path state, distance, navmesh, and human reason. The Economy Health audit also includes these path diagnostic counts in its blocker summary so a generic watch state can point at the specific local path validation issue.

`directFallbackUnverified` still remains blocked. P.1/D.8.1 does not relax `pathTrustStatus=verifiedPath`, does not allow fallback paths to activate, and does not change path validation thresholds. It only explains why a candidate cannot become an active assignment.

Safety boundaries:

- Read-only dashboard/API only.
- No path trust gate relaxation and no direct fallback activation.
- No miner targeting, assignment selection, movement, patrol queue, sampling, yield amount, conceptual label selection, travel behavior, D.6.2 demand math, D.6.6 planning, or activation cap changes.
- No persistence writes, stockpile reservations, consumption, crafting, vendors, bazaar/auction, credits, market, or player-facing economy behavior.
- No real resource extraction, no `ResourceSpawn::extractResource`, no `ResourceContainer` creation, and no player inventory mutation.
- Rows store only copied primitive/string/number/boolean values and do not retain raw agent, controller, resource, zone, path-node, market, vendor, container, or inventory pointers.

### P.2 / D.8.2 - NavArea-Backed Pathable Density Target Selection

P.2/D.8.2 adds a shadow-mode NavArea-backed density candidate layer for SimMiner/economy targeting. The goal is to start moving density target selection away from raw random terrain samples and toward cached pathable staging candidates, without changing miner behavior by default.

NavAreas are used as the scalable decision layer because they are already persisted per planet, loaded into the active-area tree, and backed by Recast/Detour navmesh when available. The first implementation keeps a memory-only sample cache keyed by `planet:NavAreaName`, where the NavArea name comes from `NavArea::getMeshName()` with safe fallbacks. Cached rows store only copied data: sample position, planet, source NavArea, coarse source role such as `city`, `poi_region`, or `region`, validation result, validation timestamp, use count, rejection count, confidence, and generated time.

`NavArea::containsPoint` is not treated as proof of true pathability. It is only a coarse active-area coverage check. Samples are considered pathable candidates only when they come from `PathFinderManager::getSpawnPointInArea` while a loaded NavArea overlaps the sampled area. The existing P.1 path validation layer remains the final trust gate for assignment activation, and `pathTrustStatus=verifiedPath` is still required.

Full path validation is deliberately budgeted and not newly invoked by this phase. P.2 records `navAreaMaxPathValidationsPerCycle` and exposes path validation budget counters, but the first safe implementation uses Detour-backed spawn sampling plus the existing P.1 validation pass rather than adding new per-candidate `findPath` calls. This avoids pathfinding spikes while still exposing cache hit/miss, sample generation, candidate scoring, and fallback reasons.

Generic interiors are initially avoided with `navAreaAvoidGenericInteriors=true`. Building interiors are not generally represented by useful region NavAreas, and the economy AI should stay on outdoor/city/POI staging until explicit validated interior anchors exist.

Configuration lives under `SimPlayerManagerConfig.navAreaDensitySelectionConfig`:

```lua
navAreaDensitySelectionConfig = {
    enableNavAreaDensitySelection = false,
    enableNavAreaDensityShadowMode = true,
    navAreaSampleCacheTtlSeconds = 900,
    navAreaMaxSamplesPerArea = 8,
    navAreaMaxSampleAttemptsPerCycle = 16,
    navAreaMaxPathValidationsPerCycle = 0,
    navAreaAvoidGenericInteriors = true,
    navAreaPreferCityAndPoiRegions = true,
}
```

Dashboard/API diagnostics are exposed as `navAreaDensitySelection` and include:

- `navAreaCandidatesConsidered`
- `navAreaSamplesGenerated`
- `navAreaSampleCacheHits`
- `navAreaSampleCacheMisses`
- `navAreaSamplesValidated`
- `navAreaSamplesRejected`
- `navAreaRejectionReasons`
- `densityCandidatesConsidered`
- `densitySelectedCandidateScore`
- `densitySelectionMode`
- `pathValidationBudgetUsed`
- `pathValidationSkippedBudget`
- `fallbackToLegacySamplingCount`
- `directFallbackPathCount`
- `confirmedPathCount`
- `indoorCandidateRejectedCount`

The dashboard adds a NavArea Density panel next to Path Validation. It shows the shadow/active mode, runtime cache size, candidate/sample/cache counts, fallback count, recent cached samples, and rejection reasons. Shadow logs use `NavAreaDensitySelection` and include whether the NavArea candidate would have selected a different target.

Default behavior is unchanged. With `enableNavAreaDensitySelection=false` and `enableNavAreaDensityShadowMode=true`, the feature only records diagnostics and logs would-select decisions. Active replacement is reserved for a later test by setting `enableNavAreaDensitySelection=true` and `enableNavAreaDensityShadowMode=false`; even then, existing density/path fallback behavior remains available when the cache is empty, confidence is low, or budgets are exhausted.

Safety boundaries:

- Runtime/dashboard-only cache; no persistence writes.
- No new real resource extraction, no `ResourceSpawn::extractResource`, no `ResourceContainer` creation, and no inventory/economy mutation.
- No travel, selling, vendor, bazaar, crafting, credit, market, or player-facing economy behavior.
- No default miner behavior change; shadow mode leaves legacy density targets and assignments untouched.
- No raw `NavArea`, `ResourceSpawn`, controller, agent, path-node, market, vendor, container, or inventory pointers are retained.

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
| Phase D.2 - Curated scoring profile config, read-only | Add disabled-by-default profile definitions for a handful of high-value schematics. | Implemented as log-only curated recommendations; no miner targeting. |
| Phase D.2.1 - Broad top-list eligibility cleanup | Add coarse resource-family filters to broad profession top-list logs. | Implemented as log-only filtering; curated profiles remain unchanged. |
| Phase D.3 - Log-only miner target recommendations | Show which resource a miner or scout would choose for a profile. | Implemented as disabled-by-default recommendation logs; no movement or gathering changes. |
| Phase D.4 - Miner target selection simulation | Simulate one profile/resource plan per active miner while miners continue current conceptual loops. | Implemented as disabled-by-default simulation logs; no target assignment or behavior change. |
| D.5-prep - Density target simulation | Find a nearby acceptable density pocket for each simulated plan. | Implemented as disabled-by-default coordinate logs; now prefers the D.6.6 demand-weighted plan when available and falls back to D.4 only diagnostically. |
| Phase D.5.1 - Density simulation explainability | Explain successful and rejected density candidates with thresholds, counts, ranks, and rejection reasons. | Implemented as diagnostics-only logging; selection behavior is unchanged. |
| Phase D.5.2 - Path validation simulation | Run the existing pathfinder against accepted simulated density targets without assigning paths to miners. | Implemented as disabled-by-default asynchronous validation logs; no movement or controller callback occurs. |
| Phase D.5.3 - Behavior-switch design and rollback | Decide whether a miner would be allowed to use intelligence-selected targets, with shadow diagnostics and rollback counters. | Implemented as disabled-by-default shadow logs only; actual miner behavior remains unchanged. |
| Phase D.5.4/D.5.5 - Target validation alignment and assignment cache | Align density/path validation with the D.6.6 demand-weighted target and retain a stable assignment long enough for validation. | Implemented as diagnostics and controller-safe cache; no movement activation by default. |
| Phase D.5.6 - Limited intelligent miner target activation | Allow one miner to queue and follow one validated retained assignment behind explicit limited-mode gates. | Implemented as opt-in behavior; default config still prevents real activation. |
| Phase D.5.7 - Limited activation stability and path validation tightening | Make limited activation require an explicitly trusted path result and add lifecycle diagnostics for queued, started, arrived/sample-started, sample-finished, and cleared assignments. | Implemented as diagnostics and fail-closed gating; activation breadth and economy behavior are unchanged. |
| Phase D.5.8 - Limited activation soak controls and gradual scale-up | Add controlled scale-up knobs, per-miner cooldown, zone allowlist, emergency stop, and compact activation health summaries. | Implemented as manager-only soak controls around the existing limited activation path; defaults keep one-miner behavior. |
| Phase D.5 - Optional miner targeting switch | Add a disabled-by-default switch for miners to use resource-intelligence targets. | First possible behavior change; requires careful testing and rollback. |
| Phase D.6 - Hot-item demand profile research | Replace toy schematic assumptions with player-goal, component-chain, server-phase, resource-rush, and stockpile concepts. | Documentation only; no scoring or miner behavior changes. |
| Phase D.6.1 - Dynamic demand simulator | Apply configurable hot-item demand weights to active-resource recommendation logs. | Implemented as disabled-by-default log-only scoring; disconnected from miner targeting. |
| Phase D.6.1.1 - Demand eligibility tightening | Apply profession-guide allowlists and improve premium/bulk explainability. | Implemented as log-only filtering; Chef generic-organic false positives are removed. |
| Phase D.6.2 - Demand-state model | Compare desired reserves, memory-only conceptual supply, and active resource opportunity. | Implemented as disabled-by-default pressure logs; no miner or economy behavior changes. |
| Phase D.6.4 - Market supply read-only integration | Observe public bazaar and player-vendor resource listings as an additional demand-state supply signal. | Implemented as disabled-by-default read-only snapshots; no listings, resources, or transactions are changed. |
| Phase D.6.5 - Persistent AI stockpile research | Define what AI-owned stockpile state should survive restart and how it should recover safely. | Documented design only; no persistence implementation. |
| Phase D.6.5.1 - Log-only stockpile snapshot simulation | Project memory-only conceptual totals into the proposed stockpile row shape and optionally show market references beside them. | Implemented as disabled-by-default simulation logs; no persistent supply or ownership state is created. |
| Phase D.6.6 - Demand-weighted miner plan simulation | Use demand pressure to choose simulated miner profiles instead of round-robin assignment. | Implemented as a separate disabled-by-default simulation task; actual miner behavior remains deferred. |

Open questions:

- Which schematics should seed the first curated profiles: common player staples, AI economy staples, or admin-selected goals?
- Should component-chain scoring recurse one level first, or should it use manual component demand multipliers?
- Should resource density affect schematic score, or remain a separate "can gather enough here" score?
- Should expired resources remain in intelligence as historical market/trend data?
- How should future demand distinguish "best possible resource" from "good enough for bulk production"?

### Phase D.2 - Curated Resource Scoring Profiles

Phase D.2 adds a disabled-by-default curated scoring profile system on top of the Resource Intelligence MVP. It is diagnostics and intelligence only.

This phase answers:

- "If the AI were a weaponsmith, chef, or architect, which currently active resources would it care about and why?"

This phase does not answer:

- "What should miners actually do?"

Safety boundaries:

- No gameplay behavior changes.
- No SimMiner targeting, movement, survey timing, sampling timing, yield amount, or conceptual inventory changes.
- No persistence.
- No `ResourceContainer` creation.
- No player inventory, vendor, bazaar, auction, crafting output, harvester, credit, or player-facing economy changes.
- No Smart Doctor, entertainer, LLM, chat, or IDL changes.

Configuration:

Curated profile logging is configured in `MMOCoreORB/bin/scripts/managers/sim_player_manager.lua`:

```lua
resourceScoringProfiles = {
    enabled = false,
    profiles = {
        {
            key = "weaponsmith_dl44",
            category = "weaponsmith",
            description = "DL44-style weapon profile",
        },
        {
            key = "chef_ahrisa",
            category = "chef",
            description = "Ahrisa food profile",
        },
        {
            key = "architect_mining_unit",
            category = "architect",
            description = "Mining unit component profile",
        },
    },
}
```

`resourceScoringProfiles.enabled` defaults to `false`. The profile logger also depends on `resourceIntelligenceConfig.enabled`; if the Resource Intelligence snapshot task is disabled, curated profiles do not run.

Current C++ profile definitions:

| Profile key | Category | Required resource families | Preferred stats | Intent |
|---|---|---|---|---|
| `weaponsmith_dl44` | `weaponsmith` | `metal`, `copper`, `aluminum` | `CD`, `OQ`, `SR` | Approximate DL44-style weapon/resource interest without parsing weapon schematics dynamically. |
| `chef_ahrisa` | `chef` | `vegetable`, `fruit` | `OQ`, `PE`, `FL`, `DR` | Approximate Ahrisa-style food resource interest. |
| `architect_mining_unit` | `architect` | `steel`, `metal`, `gas` | `UT`, `HR`, `SR`, `OQ` | Approximate mining-unit component resource interest. |

Implementation shape:

- `SimPlayerManager` reads `resourceScoringProfiles.enabled` and configured profile keys from Lua.
- Profile definitions are hard-coded in `SimPlayerManager.cpp` as a small internal `ResourceScoringProfile` structure.
- The implementation does not load draft schematics.
- The implementation does not parse target templates dynamically.
- The implementation does not recurse through component chains.
- The implementation reuses the active `ResourceSpawn` snapshot already collected by Resource Intelligence.

Eligibility:

- A resource is eligible for a profile when its active spawn type or copied class chain matches one of the profile's required resource families.
- Matching is intentionally simple and read-only. For example, `copper_borocarbitic` can match `copper`, and `gas_inert` can match `gas`.
- Component template requirements are not modeled in this phase.

Scoring:

- Only eligible active resources are scored for a profile.
- Preferred stats are combined as a simple weighted average.
- The first preferred stat receives the strongest weight; later stats still contribute.
- Missing or zero stats do not crash and do not count as perfect.
- Scores are approximate advisory signals, not crafting math.

Logging behavior:

- When `resourceIntelligenceConfig.enabled = true` and `resourceScoringProfiles.enabled = true`, each Resource Intelligence snapshot logs at most one recommendation per enabled curated profile.
- Logs include profile key, category, top resource name, resource type, score, matched family, contributing stats, description, and `mode=log-only`.
- If a profile has no eligible active resource, the logger emits a compact no-match line for that profile.
- Heuristic top-resource logging remains controlled separately by `resourceIntelligenceConfig.logTopResources`.

Example log shape:

```text
ResourceIntelligence profile weaponsmith_dl44 category=weaponsmith topResource=Ababuglu type=copper_borocarbitic score=842 reason=eligible copper family; high CD=880/OQ=910/SR=650 description="DL44-style weapon profile" mode=log-only
```

Limitations:

- Profiles are curated and approximate.
- Profile definitions are hard-coded C++ examples, not dynamic schematic parsing.
- Resource quantities, contribution values, experimental groups, component chains, density, current demand, supply, and expiration urgency are not part of the score yet.
- The profile system does not choose miner targets.
- Recommendations are not persisted.
- Recommendations are not consumed by SimMiners, SimCrafters, vendors, consumers, factions, or any gameplay system.

Recommended next phase:

Phase D.3 should add log-only miner target recommendations. That future phase should report what a miner or scout would choose based on curated profile scores while still leaving SimMiner behavior unchanged.

### Phase D.2.1 - Broad Top-List Eligibility Cleanup

Phase D.2.1 cleans up the older broad heuristic profession top-list logs so they stop recommending resources that are obviously not useful for the category. This is a read-only/log-only scoring presentation fix.

Problem fixed:

- `ResourceIntelligence top weaponsmith` could rank high-OQ meat, fruit, or water because it only looked at weapon-ish stats and did not check resource family.
- `ResourceIntelligence top chef` could rank fiberplast, steel, or ore because it only looked at OQ/PE/FL/DR-like stats and did not check food eligibility.
- `ResourceIntelligence top architect` could rank meat, seafood, or fruit because it only looked at broad structural stats and did not check construction resource families.

Current behavior:

- The generic top list remains broad and unconstrained.
- Broad profession lists now apply coarse class/type eligibility filters before ranking.
- Curated profile logs are unchanged and remain the preferred source for schematic-like recommendations.
- Scores are still heuristic and diagnostic. They do not feed miner behavior or any economy output.

Broad eligibility families:

| Broad list | Eligible families |
|---|---|
| Generic | Unfiltered. |
| Weaponsmith | `metal`, `copper`, `aluminum`, `steel`, `iron`, `gemstone`, `ore`, `mineral` |
| Armorsmith | `metal`, `steel`, `iron`, `copper`, `aluminum`, `bone`, `hide`, `chitin`, `fiberplast`, `wool`, `synthetic`, `ore`, `mineral` |
| Chef | `meat`, `vegetable`, `fruit`, `cereal`, `seafood`, `water`, `milk` |
| Architect | `metal`, `steel`, `iron`, `aluminum`, `copper`, `ore`, `mineral`, `gas`, `chemical`, `water`, `energy`, `wood`, `fiberplast`, `crystalline`, `gemstone` |

Safety boundaries:

- No SimMiner movement, targeting, survey timing, sampling timing, yield, or conceptual inventory changes.
- No persistence.
- No `ResourceContainer` creation.
- No player inventory, vendor, bazaar, auction, crafting output, harvester, credit, or player-facing economy changes.
- No Smart Doctor, entertainer, LLM, chat, or IDL changes.

### Phase D.3 - Log-Only Miner Target Recommendations

Phase D.3 adds disabled-by-default periodic logs showing what each active SimMiner, or future resource scout, would choose if it were allowed to use Resource Intelligence. It does not assign targets or change miner behavior.

Configuration:

`MMOCoreORB/bin/scripts/managers/sim_player_manager.lua` defines:

```lua
minerTargetRecommendationConfig = {
    enabled = false,
    intervalSeconds = 300,
    topN = 1,
    profiles = {
        "weaponsmith_dl44",
        "chef_ahrisa",
        "architect_mining_unit",
    },
    includeAllActiveMiners = true,
}
```

Fields:

| Field | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Schedules the log-only recommendation task when enabled. |
| `intervalSeconds` | `300` | Recommendation log interval, clamped in C++ to a safe range. |
| `topN` | `1` | Maximum recommendations per miner/profile. |
| `profiles` | Curated profile keys | Limits recommendations to named curated scoring profiles. |
| `includeAllActiveMiners` | `true` | Logs recommendations for every active `SimMinerController`; when false, only the first active miner is logged. |

Generation flow:

1. `SimPlayerManager` schedules a manager-owned `MinerTargetRecommendationTask` only when `minerTargetRecommendationConfig.enabled` is true.
2. The task calls back through `SimPlayerManager::instance`; it does not capture raw controller pointers.
3. The manager copies active `ResourceSpawn` metadata into local `ResourceIntelligenceEntry` rows using the same snapshot helper used by Resource Intelligence logging.
4. The manager iterates currently tracked `SimMinerController` entries.
5. For each miner, it safely reads the miner object id and current zone/planet from the associated `AiAgent`.
6. For each configured curated profile, it evaluates active resources with the existing curated profile eligibility and stat scoring.
7. It prefers eligible resources available on the miner's current planet.
8. If no same-planet match exists, it logs the best galaxy-wide match with `travelRequired=true`.
9. If no eligible active resource exists for a profile, it logs `noEligibleTarget=true`.

Example same-planet output:

```text
MinerTargetRecommendation miner=281475013716520 zone=tatooine profile=chef_ahrisa category=chef target=Eulovie type=vegetable_tubers_tatooine zones=tatooine score=847 reason=eligible vegetable family; high OQ=737/PE=812/FL=975/DR=920 mode=log-only
```

Example travel-required output:

```text
MinerTargetRecommendation miner=281475013716520 zone=corellia profile=chef_ahrisa category=chef target=Eulovie type=vegetable_tubers_tatooine zones=tatooine score=847 reason=eligible vegetable family; high OQ=737/PE=812/FL=975/DR=920 travelRequired=true mode=log-only
```

Example no-target output:

```text
MinerTargetRecommendation miner=281475013716520 zone=tatooine profile=chef_ahrisa category=chef noEligibleTarget=true mode=log-only
```

Safety boundaries:

- No SimMiner movement changes.
- No SimMiner destination selection changes.
- No SimMiner `targetResource` assignment or real-resource target assignment.
- No SimMiner survey/sample timing changes.
- No SimMiner conceptual resource selection changes.
- No SimMiner yield changes.
- No persistence.
- No real resource extraction and no `ResourceContainer` creation.
- No player inventory, vendor, bazaar, auction, crafting output, harvester, credit, or player-facing economy changes.

Limitations:

- Recommendations are log-only and are not stored on the miner.
- Same-planet matching uses the copied `ResourceSpawn` zone list, not density maps or actual pathable survey locations.
- Recommendations do not account for resource density, remaining lifetime, current supply, or demand pressure beyond curated profile score.
- D.3 itself does not select a single plan; D.4 provides that separate simulation-only view.
- No persistence exists.
- No economy output exists.

Next implemented phase:

### Phase D.4 - Miner Target Selection Simulation

Phase D.4 adds a disabled-by-default simulation that chooses one intended resource plan per active miner. It bridges D.3's detailed recommendations to future targeting without writing any target into the miner or changing the conceptual gather loop.

Configuration:

```lua
minerTargetSimulationConfig = {
    enabled = false,
    intervalSeconds = 300,
    profileWeights = {
        weaponsmith_dl44 = 1.0,
        chef_ahrisa = 1.0,
        architect_mining_unit = 1.0,
    },
    preferSamePlanet = true,
    samePlanetBonus = 150,
    travelPenalty = 100,
    assignmentMode = "round_robin",
}
```

| Field | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Schedules simulation-only logging. |
| `intervalSeconds` | `300` | Simulation interval, clamped between 30 and 3600 seconds. |
| `profileWeights` | `1.0` per curated profile | Multiplies each profile's base score. Values are clamped between `0.0` and `10.0`; zero disables that profile for simulation. |
| `preferSamePlanet` | `true` | Applies the same-planet bonus and travel penalty when calculating adjusted scores. |
| `samePlanetBonus` | `150` | Added to the weighted score when the resource is active on the miner's current planet. |
| `travelPenalty` | `100` | Subtracted from the weighted score when travel would be required. |
| `assignmentMode` | `round_robin` | Distributes miners across enabled curated profiles using stable controller ordering. Unsupported values fall back to `round_robin`. |

Selection algorithm:

1. A manager-owned `MinerTargetSimulationTask` runs only when the simulation is enabled.
2. Active `ResourceSpawn` metadata is copied into local Resource Intelligence rows before scoring.
3. Active miners are visited in the stable ordering of `SimPlayerManager::controllers`.
4. Round-robin assignment selects one enabled curated profile for each miner.
5. Every eligible active resource for the assigned profile receives a base curated-profile score.
6. The simulation calculates `adjustedScore = baseScore * profileWeight`.
7. When `preferSamePlanet` is true, local resources receive `samePlanetBonus` and remote resources receive `travelPenalty`.
8. The highest adjusted score becomes the miner's simulated plan.
9. If the assigned profile has no eligible active resource, the simulation evaluates the other enabled profiles and chooses the best available fallback.
10. Exactly one plan line, or one `noEligibleTarget` line, is logged per active miner per interval.

Example:

```text
MinerTargetSimulation miner=281475014066178 zone=naboo assignedProfile=weaponsmith_dl44 category=weaponsmith target=Gowane type=copper_polysteel zones=naboo baseScore=790 adjustedScore=940 samePlanet=true travelRequired=false assignmentMode=round_robin reason=round_robin profile assignment; eligible copper family; high CD=976/OQ=543/SR=761 mode=simulation-only
```

D.3 and D.4 serve different diagnostics:

- D.3 logs detailed recommendations for every configured profile.
- D.4 logs one simulated decision per miner.
- Both remain independently disabled by default. Operators should normally enable only the view needed for a test session to avoid noisy duplicate output.

Safety boundaries and limitations:

- The simulated plan is not stored on `SimMinerController` or `AiAgent`.
- `targetResource`, patrol points, movement destinations, survey timing, sample timing, conceptual resource selection, and yield accounting are unchanged.
- No real resource extraction or `ResourceContainer` creation occurs.
- No persistence or economy output exists.
- Selection is not density-aware and does not calculate a path to the resource.
- Round-robin ordering is stable for the currently tracked controller set, but assignments can shift when miners are added or removed.
- Profile weights are coarse demand controls, not schematic quantities or a market model.

Phase D.5 should be an optional, disabled-by-default miner targeting behavior switch. It should not begin until D.4 simulation logs have been observed over multiple resource shifts and show sensible, stable choices.

### D.5-prep - Density Target Simulation

D.5-prep adds a disabled-by-default simulation that finds a nearby candidate gathering coordinate for the ResourceSpawn selected by the D.4 plan. It is read-only and simulation-only: the coordinate is logged but never assigned to the miner.

Configuration:

```lua
minerDensityTargetSimulationConfig = {
    enabled = false,
    intervalSeconds = 300,
    searchRadii = { 250, 500, 1000, 2000 },
    samplesPerRadius = 48,
    minAcceptableDensity = 0.65,
    preferredDensity = 0.80,
    requireNavmesh = true,
    maxPathCheckAttempts = 8,
    distancePenaltyPerMeter = 0.02,
}
```

| Field | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Schedules density target simulation. |
| `intervalSeconds` | `300` | Simulation interval, clamped between 30 and 3600 seconds. |
| `searchRadii` | `250, 500, 1000, 2000` | Expanding search radii in meters. Valid configured radii are sorted before use. |
| `samplesPerRadius` | `48` | Maximum deterministic density probes per radius, clamped between 8 and 256. |
| `minAcceptableDensity` | `0.65` | Minimum density required to select a candidate. Clamped to `0.0-1.0`. |
| `preferredDensity` | `0.80` | Marks a selected candidate as a preferred pocket in the log. It cannot be lower than the acceptable threshold. |
| `requireNavmesh` | `true` | When the miner starts inside a navmesh, require a selected candidate to be inside a nearby navmesh area. Open-world miners remain eligible for terrain candidates. |
| `maxPathCheckAttempts` | `8` | Bounds navmesh-area checks for improving candidates. Despite the historical config name, this phase does not execute full pathfinding. |
| `distancePenaltyPerMeter` | `0.02` | Subtracts a small distance cost from the density score within the current radius. |

Research findings:

- `ResourceMap::getDensityAt(resourceName, zoneName, x, y)` is the canonical read-only density lookup and returns a concentration value that is normally in the `0.0-1.0` range.
- The player survey implementation in `ResourceSpawner::sendSurvey` probes a small local square grid and chooses its highest-density point.
- Core3 does not expose a reusable method that returns the best density coordinate for an arbitrary resource and search area.
- `PathFinderManager::getSpawnPointInArea` generates a new random point; it does not validate an already selected density coordinate.
- Running full `findPath` calls for dozens of simulation probes would be expensive and would mix pathfinding work into a read-only observability task.
- D.5-prep therefore uses bounded deterministic probing, zone-boundary checks, terrain height lookup, and conservative navmesh-area validation. Actual route validation remains future D.5 work.

Selection flow:

1. The manager reproduces the current D.4 round-robin simulated plan without storing it.
2. If the selected ResourceSpawn is not active on the miner's current planet, the task logs `travelRequired=true` and does not query density in the wrong zone.
3. Candidate points use a deterministic golden-angle disk pattern seeded from miner and ResourceSpawn object ids.
4. The task probes the configured radii from smallest to largest.
5. Density reads occur while holding the ResourceManager lock; copied candidate values are evaluated after releasing it.
6. Out-of-bounds candidates and density values at or below zero are ignored.
7. Candidate score is approximately `density * 1000 - distance * distancePenaltyPerMeter`.
8. When required for a miner currently inside a navmesh, improving candidates must also be contained by a nearby navmesh area.
9. The best acceptable candidate in the nearest qualifying radius is logged, and larger radii are not searched.
10. Terrain height supplies the logged `z` coordinate only after a candidate has passed density and navigation checks.

Example successful simulation:

```text
MinerDensityTargetSimulation miner=281475014188707 zone=tatooine profile=chef_ahrisa resource=Eulovie type=vegetable_tubers_tatooine target=(x:-1234.5,y:4567.8,z:12.3) density=0.82 distance=376.0 searchRadius=500 samplesChecked=96 candidateCount=96 acceptedCandidateRank=1 minAcceptableDensity=0.650 navmeshChecked=false mode=simulation-only reason=nearest preferred pocket
```

Example remote-resource result:

```text
MinerDensityTargetSimulation miner=281475014188707 zone=corellia profile=chef_ahrisa resource=Eulovie type=vegetable_tubers_tatooine zones=tatooine travelRequired=true noSamePlanetDensityTarget=true noDensityTarget=true minAcceptableDensity=0.650 candidateCount=0 rejectReason=wrongPlanet mode=simulation-only
```

Safety boundaries and limitations:

- No SimMiner target, destination, patrol point, movement state, conceptual resource, timing, or yield value is changed.
- No real sampling or extraction method is called.
- No resources or `ResourceContainer` objects are created.
- No persistence or economy output exists.
- Density candidates are approximations from bounded probes, not global maxima.
- `requireNavmesh` is a conservative area-containment check, not proof that a complete path from miner to candidate exists.
- Density and D.4 simulation logs are separate tasks. Operators should avoid enabling D.3, D.4, and D.5-prep simultaneously unless detailed comparison output is intentional.

#### Phase D.5.1 - Density Simulation Explainability

Phase D.5.1 makes D.5-prep results diagnosable without changing density probes, thresholds, scoring, radius expansion, navmesh checks, or selected coordinates.

Every rejected density simulation now includes `rejectReason`:

| Reason | Meaning |
|---|---|
| `belowMinDensity` | At least one positive-density point was observed, but the best density remained below `minAcceptableDensity`. |
| `noNavmesh` | One or more points met the density threshold, but the strongest checked candidates were outside an acceptable navmesh area. |
| `wrongPlanet` | The D.4 ResourceSpawn is not active on the miner's current planet, so no local density lookup was attempted. |
| `noDensityMap` | The resource manager, spawner, resource map, or selected active ResourceSpawn could not be resolved when probing began. |
| `noValidCandidate` | No usable in-bounds point with positive density was available, or no D.4 resource plan could be produced. |
| `allCandidatesRejected` | Defensive catch-all for candidates that existed but were rejected outside the more specific threshold and navmesh cases. |

Rejected target logs include:

- `bestDensity`: highest density observed across the radii actually searched.
- `minAcceptableDensity`: configured threshold used by the unchanged selection algorithm.
- `candidateCount`: number of in-bounds coordinates whose density was evaluated.
- `searchedRadii`: radii reached before the search ended.
- `rejectReason`: overall reason no target was selected.
- `bestRejectedDensity`, `bestRejectedDistance`, and `bestRejectedReason` when a meaningful rejected candidate is available.

For `belowMinDensity`, the best rejected candidate is the highest-density observed point. For `noNavmesh`, it is the strongest density-and-distance candidate that failed navmesh-area validation.

Example threshold rejection:

```text
MinerDensityTargetSimulation miner=281475014188707 zone=naboo profile=chef_ahrisa resource=Neki type=fruit_flowers_naboo noDensityTarget=true bestDensity=0.785 minAcceptableDensity=0.800 candidateCount=192 searchedRadii=250,500,1000,2000 rejectReason=belowMinDensity bestRejectedDensity=0.785 bestRejectedDistance=412.0 bestRejectedReason=belowMinDensity mode=simulation-only
```

Successful logs retain their existing fields and add:

- `minAcceptableDensity`
- `candidateCount`
- `acceptedCandidateRank`

`acceptedCandidateRank` is `1` when no navmesh validation is required. When navmesh validation is required, it reports the selected candidate's rank among the strongest candidates checked in the accepted radius.

Example accepted target:

```text
MinerDensityTargetSimulation miner=281475014188707 zone=tatooine profile=chef_ahrisa resource=Eulovie type=vegetable_tubers_tatooine target=(x:3508.2,y:-4910.1,z:5.0) density=0.715 distance=447.8 searchRadius=500 samplesChecked=96 candidateCount=96 acceptedCandidateRank=3 minAcceptableDensity=0.700 navmeshChecked=true mode=simulation-only reason=nearest acceptable pocket
```

This phase is observability-only. It does not modify SimMiner movement, target state, resource/profile selection, conceptual yield, extraction, persistence, inventory, or economy systems.

#### D.5.2 - Path Validation Simulation

D.5.2 adds disabled-by-default route validation for the density coordinate produced by the existing D.4 and D.5-prep simulation pipeline. It answers whether Core3's pathfinder can produce a bounded route from the miner's copied current position to that simulated coordinate. It never gives the returned path to `SimPlayerController`, never queues patrol points, and never changes the miner.

Configuration:

```lua
minerPathValidationSimulationConfig = {
    enabled = false,
    intervalSeconds = 300,
    validateOnlyAcceptedDensityTargets = true,
    maxPathDistance = 2500,
    maxPathNodes = 256,
}
```

| Field | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Schedules path validation simulation. |
| `intervalSeconds` | `300` | Validation interval, clamped between 30 and 3600 seconds. |
| `validateOnlyAcceptedDensityTargets` | `true` | Validates only coordinates accepted by D.5-prep. When false, the best diagnostic rejection candidate may be validated for investigation, but it is still never assigned. |
| `maxPathDistance` | `2500` | Rejects a target before pathfinding when its straight-line distance exceeds this value, and rejects a returned route longer than this value. Clamped to `100..10000`. |
| `maxPathNodes` | `256` | Rejects returned paths with excessive node counts. Clamped to `2..2048`. |

Execution and lifetime safety:

1. A manager-owned periodic task copies the same active `ResourceSpawn` metadata used by Resource Intelligence.
2. It prefers the D.6.6 demand-weighted profile/resource plan, using the older D.4 round-robin plan only as a diagnostic fallback when no demand-weighted plan is available.
3. Wrong-planet and missing-density-target cases are logged as skipped.
4. For a usable coordinate, the manager copies primitive identifiers, names, positions, density, and limits into a standalone `MinerPathValidationTask`.
5. The task keeps only a managed `Zone` reference. It captures no raw or delayed `SimMinerController*` or `AiAgent*`.
6. The worker task calls the same synchronous `PathFinderManager::findPath` API used by `SimPathFindTask`, then logs and deletes the returned path. It does not invoke `onPathFound`, `moveTo`, `findNextPosition`, or any movement callback.

Example validated route:

```text
MinerPathValidationSimulation miner=281475014318303 zone=tatooine profile=architect_mining_unit resource=Goree type=steel_quadranium targetSource=demand_weighted_plan target=(x:3380.5,y:-4756.6,z:5.0) density=0.663 distance=248.6 densityTarget=accepted pathFound=true pathNodes=37 pathDistance=312.4 directFallback=false mode=simulation-only
```

Example failed route:

```text
MinerPathValidationSimulation miner=281475014318295 zone=tatooine profile=weaponsmith_dl44 resource=Gowane type=copper_polysteel targetSource=demand_weighted_plan target=(x:5220.5,y:-4252.0,z:175.8) density=0.687 distance=411.2 densityTarget=accepted pathFound=false pathNodes=0 pathDistance=0.0 directFallback=false rejectReason=noPath mode=simulation-only
```

When the path task validates a D.6.6 target, it also emits a compact provenance line:

```text
DemandWeightedMinerTargetValidation miner=281475014318303 zone=tatooine selectedProfile=architect_mining_unit targetResource=Goree targetType=steel_quadranium targetSource=demand_weighted_plan densityTargetStatus=accepted pathValidationStatus=valid matchesSwitchDecision=true mode=diagnostic-only
```

Important pathfinder limitation:

- For world-to-world requests, Core3 may return a two-node start/end fallback when a route could not be evaluated.
- D.5.2 marks this as `pathFound=false`, `directFallback=true`, and `rejectReason=directFallbackUnverified` rather than treating it as proof of pathability.
- This conservative rule may reject a genuinely direct route because the public API does not distinguish that case from its failure fallback. That ambiguity must be resolved before behavior-changing targeting relies on the result.

Other rejection or skip reasons include `pathException`, `tooManyPathNodes`, `pathTooLong`, `exceedsMaxPathDistance`, `noAcceptedDensityTarget`, `wrongPlanet`, and unavailable resource snapshots.

Safety boundaries and limitations:

- Pathfinding is synchronous inside a scheduled worker task, matching existing SimPlayer pathfinding practice; it is not performed while holding an `AiAgent` or `ResourceManager` lock.
- Resource and miner state are copied before the path task is scheduled.
- No target, destination, `targetResource`, patrol point, blackboard value, survey/sample state, timing, or yield is changed.
- No extraction, resource/container creation, persistence, inventory, or economy path is used.
- Validation observes the route returned at one moment. It does not guarantee that dynamic world conditions will remain unchanged.
- D.5.2 does not test cross-planet travel; remote resources are logged as `wrongPlanet`.

#### Phase D.5.3 - Miner Intelligent Targeting Switch Design and Rollback

D.5.3 adds a disabled-by-default switch-decision layer for future intelligent SimMiner targeting. It answers whether a miner would be allowed to leave the current random conceptual loop and use an intelligence-selected target, but it does not activate that behavior. The only implemented mode is shadow diagnostics.

Configuration:

```lua
minerIntelligentTargetingConfig = {
    enabled = false,
    mode = "off",
    intervalSeconds = 300,
    maxActiveMiners = 1,
    requireDemandWeightedPlan = true,
    requireAcceptedDensityTarget = true,
    requireValidPath = true,
    fallbackToConceptualLoop = true,
    rollbackOnFailureCount = 3,
    logDecisionSummary = true,
}
```

| Field | Default | Meaning |
|---|---|---|
| `enabled` | `false` | Enables the D.5.3 diagnostic task. |
| `mode` | `"off"` | `off` emits no switch-decision logs. `shadow` emits diagnostics only. Invalid modes fail closed to `off`. |
| `intervalSeconds` | `300` | Diagnostic interval, clamped to `30..3600`. |
| `maxActiveMiners` | `1` | Maximum miners evaluated per interval. Additional miners are counted as capped in the summary. |
| `requireDemandWeightedPlan` | `true` | Requires a D.6.6-style demand-weighted plan before a miner would be eligible. |
| `requireAcceptedDensityTarget` | `true` | Requires an accepted same-planet density target for the selected resource. |
| `requireValidPath` | `true` | Requires a matching cached D.5.2 path-validation result with `pathFound=true`. |
| `fallbackToConceptualLoop` | `true` | Documents that the current conceptual loop remains the fallback. |
| `rollbackOnFailureCount` | `3` | Number of consecutive failed decisions before diagnostics mark `rollbackHeld=true`. |
| `logDecisionSummary` | `true` | Emits one compact per-interval summary. |

Decision flow:

1. The manager copies active miner identity, zone, position, and navmesh status under short `AiAgent` locks.
2. It recomputes a D.6.6-style demand-weighted plan from copied Resource Intelligence, conceptual supply, optional market-observation snapshots, and D.6.6 config values.
3. It probes a density target for the selected simulated resource using the existing D.5-prep bounded search.
4. It reads the latest copied D.5.2 path-validation snapshot for the same miner/profile/resource/type, `targetSource=demand_weighted_plan`, and matching density coordinate.
5. It applies the configured gates and logs `wouldActivate=true|false`.
6. It always logs `actualActivation=false`; no behavior-changing activation exists in this phase.

Example successful shadow decision:

```text
MinerTargetingSwitchDecision miner=281475017397855 zone=corellia mode=shadow selectedProfile=chef_high_value_consumables demandState=surplus pressureScore=482.5 targetResource=Ptohi targetType=fruit_fruits_naboo targetSource=demand_weighted_plan samePlanet=true travelRequired=false densityTargetStatus=accepted pathValidationStatus=valid wouldActivate=true actualActivation=false fallbackReason=shadowMode fallbackToConceptualLoop=true rollbackHeld=false failureCount=0 assignmentReason="highest demand pressure; same-planet opportunity" decisionBasis=demandWeightedMinerPlanSimulation diagnosticOnly=true mode=diagnostic-only
```

Example fail-closed decision:

```text
MinerTargetingSwitchDecision miner=281475017397855 zone=naboo mode=shadow selectedProfile=production_infrastructure demandState=surplus pressureScore=443.5 targetResource=Toahiiam targetType=iron_doonium targetSource=demand_weighted_plan samePlanet=true travelRequired=false densityTargetStatus=accepted pathValidationStatus=failed pathRejectReason=noPath wouldActivate=false actualActivation=false fallbackReason=pathValidationFailed fallbackToConceptualLoop=true rollbackHeld=false failureCount=2 assignmentReason="highest demand pressure; same-planet opportunity" decisionBasis=demandWeightedMinerPlanSimulation diagnosticOnly=true mode=diagnostic-only
```

Summary example:

```text
MinerTargetingSwitchDecisionSummary activeMiners=4 evaluated=1 wouldActivate=0 fallback=1 capped=3 noPlan=0 noDensityTarget=0 pathRejected=1 rollbackHeld=0 mode=shadow diagnosticOnly=true
```

Safety boundaries:

- D.5.3 does not call `moveTo`, queue patrol points, assign `targetResource`, change miner blackboard state, alter survey/sample timing, change conceptual resource selection, or change yield accounting.
- It does not write `AiEconomyData`, checkpoint conceptual totals, mark persistence objects dirty, or consume/produce stockpile.
- It does not create `ResourceContainer` objects, resources, inventory items, vendor listings, bazaar entries, crafting output, harvester output, or credits.
- It copies primitive/string snapshots and releases locks before scoring and logging. A temporary managed `Zone` reference may be used during the immediate density probe, but D.5.3 does not retain it or capture it in delayed work. It does not capture delayed raw `SimMinerController*`, `AiAgent*`, `ResourceSpawn*`, `AuctionItem*`, or `ResourceContainer*` pointers.

Known limitations:

- D.5.3 accepts a cached path result only if it was produced from `targetSource=demand_weighted_plan` and matches the current D.6.6 profile, resource name, resource type, and density coordinate. If the latest path validation is for a different simulated plan or stale coordinate, D.5.3 logs `pathValidationStatus=target_mismatch`, `fallbackReason=pathValidationTargetMismatch`, and fails closed when `requireValidPath=true`.
- D.6.6 remains intentionally separate from C.3.3/C.3.4 persistent stockpile pressure unless a later explicitly gated phase connects those signals.
- Enabling D.5.3 from a fully stopped state follows the existing SimPlayer config pattern and normally requires manager/server reload. While running, it reloads its own config each interval.

#### Phase D.5.4/D.5.5 - Demand-Weighted Target Validation Alignment and Assignment Cache

D.5.4 aligns the validation chain around the same simulated target that D.5.3 would use for a future behavior switch:

```text
D.6.6 demand-weighted miner plan
-> D.5 density target for that profile/resource
-> D.5.2 path validation for that density target
-> D.5.3 switch decision
```

D.5.4 testing showed that the target source alignment worked, but strict shadow decisions still failed later with `pathValidationTargetMismatch`. That was expected because miners continued their old conceptual loop and could move between D.6.6 selection, density probing, path validation, and the next D.5.3 decision. D.5.5 adds a manager-owned per-miner intelligent target assignment cache so those diagnostics can refer to one stable target long enough for validation to catch up.

The density and path validation logs include `targetSource`. The preferred source is `demand_weighted_plan`; `round_robin_plan` remains only as a diagnostic fallback if D.6.6 cannot produce a plan. The path-validation task records copied primitive/string snapshots containing profile, resource name, resource type, target source, target coordinate, density, path result, and timestamp. D.5.3 only treats a cached path as valid when all of these match the current D.6.6 switch target or the retained assignment target.
`DemandWeightedMinerTargetValidation.matchesSwitchDecision=false` can appear before an assignment exists or when the validation is for a stale target; it should become `true` only when the path result matches the retained assignment.

Assignment config:

```lua
minerIntelligentTargetingConfig = {
    enabled = true,
    mode = "shadow",
    maxActiveMiners = 1,
    requireDemandWeightedPlan = true,
    requireAcceptedDensityTarget = true,
    requireValidPath = true,
    fallbackToConceptualLoop = true,
    rollbackOnFailureCount = 3,
    logDecisionSummary = true,
    assignmentConfig = {
        enabled = true,
        ttlSeconds = 180,
        replaceOnlyWhenExpiredOrInvalid = true,
        clearOnSampleComplete = true,
        clearOnCombat = true,
        clearOnIncapOrDeath = true,
        clearOnZoneChange = true,
        logAssignmentLifecycle = true,
    },
    limitedActivationConfig = {
        enabled = false,
        maxActivationsPerInterval = 1,
        requireSamePlanet = true,
        disableOnFirstActivationFailure = true,
        logActivationLifecycle = true,
    },
}
```

If valid-path gating is enabled, the effective assignment TTL is clamped high enough to span the targeting interval plus the path-validation interval. This prevents an assignment from expiring before D.5.2 has a chance to validate it.

Assignment contents are copied primitive/string fields only:

- Miner object id.
- Created, updated, and expiration timestamps.
- `targetSource=demand_weighted_plan`.
- Selected demand profile, demand state, and pressure score.
- Target resource name/type and target zone.
- Density coordinate and density value.
- Density/path status and whether path validation matched.
- Assignment status such as `candidate` or `validated`.

Assignment lifecycle examples:

```text
MinerIntelligentTargetAssignment miner=281475017397855 action=created targetSource=demand_weighted_plan selectedProfile=chef_high_value_consumables targetResource=Ptohi targetType=fruit_fruits_naboo targetZone=naboo x=1234.0 y=-5678.0 z=12.3 densityTargetStatus=accepted pathValidationStatus=not_checked ttlSeconds=180 mode=shadow
MinerIntelligentTargetAssignment miner=281475017397855 action=retained selectedProfile=chef_high_value_consumables targetResource=Ptohi ageSeconds=61 remainingSeconds=119 pathValidationStatus=valid mode=shadow
MinerIntelligentTargetAssignment miner=281475017397855 action=cleared clearReason=expired selectedProfile=chef_high_value_consumables targetResource=Ptohi ageSeconds=181 mode=shadow
```

Expected diagnostic outcomes:

- `wouldActivate=true` means a D.6.6 demand-weighted plan or retained assignment exists, an accepted same-planet density target exists, and a matching D.6.6-sourced path-validation snapshot has `pathFound=true`.
- `actualActivation=false` still means no behavior change occurs.
- `fallbackReason=pathValidationTargetMismatch` means a path snapshot exists but was for a different target source, profile/resource, or density coordinate.
- `fallbackReason=pathValidationUnavailable` usually means the D.5.2 task has not produced a matching snapshot yet, or path validation simulation is disabled.
- `assignmentStatus=validated` means the assignment target has a matching successful path-validation snapshot.
- `assignmentStatus=candidate` means the assignment exists but has not yet received a valid matching path result.

Switch-decision example:

```text
MinerTargetingSwitchDecision miner=281475017397855 zone=naboo mode=shadow selectedProfile=chef_high_value_consumables demandState=critical pressureScore=1848.6 targetResource=Ptohi targetType=fruit_fruits_naboo targetSource=demand_weighted_plan samePlanet=true travelRequired=false densityTargetStatus=accepted pathValidationStatus=valid pathRejectReason=none assignmentStatus=validated assignmentAgeSeconds=65 assignmentMatchesValidation=true assignmentTargetResource=Ptohi assignmentTargetType=fruit_fruits_naboo assignmentTargetZone=naboo assignmentClearReason=none wouldActivate=true actualActivation=false fallbackReason=shadowMode diagnosticOnly=true mode=diagnostic-only
```

D.5.5 intentionally did not call `moveTo`, queue patrol points, or change miner movement. It proved the assignment and validation chain before adding any behavior switch.

Safety boundaries:

- No D.5.5 path mutates `AiEconomyData`, `AiEconomyStockpileLot`, market listings, inventory, vendors, bazaar, crafting, harvesters, credits, or player-facing economy.
- No `ResourceContainer` objects or real resources are created.
- No miner destination, patrol point, movement state, survey timing, sample timing, conceptual resource selection, or yield amount is changed.
- The assignment cache stores no `AiAgent`, controller, `ResourceSpawn`, path node, `ManagedObject`, `Zone`, `AuctionItem`, or resource-container references.
- Locks are limited to cache map updates and copied snapshots; formatting/logging happens outside those locks.
- `clearOnSampleComplete` applies only to assignment-aware intelligent sample completion. The current unrelated conceptual sample loop does not clear assignments, because doing so would recreate the validation drift D.5.5 is designed to diagnose.

Test procedure:

1. Run with `mode="shadow"`, assignment caching enabled, and `requireValidPath=true`.
2. Confirm `MinerIntelligentTargetAssignment action=created` appears for the evaluated miner.
3. Confirm later density/path logs show the same assignment target or `reason=retained assignment target`.
4. Confirm `MinerTargetingSwitchDecision` reports assignment fields.
5. Confirm `actualActivation=false` always remains true for this phase.

#### Phase D.5.6 - Limited Intelligent Miner Target Activation

D.5.6 adds the first opt-in behavior change for SimMiners. It allows one evaluated miner to accept one retained, validated D.5.5 assignment and queue assignment-aware movement toward that density coordinate. The default repo config still runs in shadow mode with `limitedActivationConfig.enabled=false`, so no movement change occurs until both gates are explicitly enabled.

Limited activation config:

```lua
minerIntelligentTargetingConfig = {
    enabled = true,
    mode = "limited",
    maxActiveMiners = 1,
    requireDemandWeightedPlan = true,
    requireAcceptedDensityTarget = true,
    requireValidPath = true,
    fallbackToConceptualLoop = true,
    rollbackOnFailureCount = 3,
    logDecisionSummary = true,
    assignmentConfig = {
        enabled = true,
        ttlSeconds = 180,
        replaceOnlyWhenExpiredOrInvalid = true,
        clearOnSampleComplete = true,
        clearOnCombat = true,
        clearOnIncapOrDeath = true,
        clearOnZoneChange = true,
        logAssignmentLifecycle = true,
    },
    limitedActivationConfig = {
        enabled = true,
        maxActivationsPerInterval = 1,
        requireSamePlanet = true,
        disableOnFirstActivationFailure = true,
        logActivationLifecycle = true,
    },
}
```

Activation gates:

- `minerIntelligentTargetingConfig.enabled=true`.
- `mode="limited"`.
- `limitedActivationConfig.enabled=true`.
- A retained assignment exists, is not expired, and has `targetSource=demand_weighted_plan`.
- The assignment has accepted density and matching valid path validation.
- The assignment matches the same miner, profile, resource name, resource type, zone, and density coordinate.
- The miner has a valid controller and zone, is alive, is not incapacitated, and is not in combat.
- Same-planet targeting is required when `requireSamePlanet=true`.
- The miner is not rollback-held.
- The interval activation cap has not been reached.

Assignment-aware controller behavior:

1. The manager does not interrupt the miner mid-survey, movement, or sample. It queues a copied primitive assignment on the `SimMinerController`.
2. The controller starts the intelligent assignment at the next safe `startSimLoop()` boundary.
3. The controller moves to the retained density coordinate using the existing pathing machinery.
4. `SimMinerController::onArrived()` now has an assignment-aware branch. Intelligent arrivals log `MinerIntelligentTargetArrival` and start the visual sample animation without falling through the old conceptual arrival path.
5. The intelligent sample still records only conceptual yield. It chooses the conceptual yield label with the existing `pickRandomResource()` path, not from the exact `ResourceSpawn` target.
6. On sample completion, the local assignment state is cleared and the manager cache is cleared when `clearOnSampleComplete=true`.
7. On path failure, expired assignment, combat, incap/death, missing zone, zone mismatch, or invalid target, the controller logs fallback and returns to the old conceptual loop.

Example logs:

```text
MinerTargetingSwitchDecision miner=281475017397855 zone=naboo mode=limited selectedProfile=chef_high_value_consumables targetResource=Ptohi targetType=fruit_fruits_naboo assignmentStatus=validated assignmentMatchesValidation=true wouldActivate=true actualActivation=true activationResult=queued fallbackReason=none limitedActivationEnabled=true diagnosticOnly=false mode=limited
MinerIntelligentTargetActivation miner=281475017397855 action=queued selectedProfile=chef_high_value_consumables targetResource=Ptohi targetType=fruit_fruits_naboo targetZone=naboo x=1234.0 y=-5678.0 z=12.3 density=0.764 pathValidationStatus=valid mode=limited
MinerIntelligentTargetActivation miner=281475017397855 action=started selectedProfile=chef_high_value_consumables targetResource=Ptohi targetType=fruit_fruits_naboo targetZone=naboo x=1234.0 y=-5678.0 z=12.3 density=0.764 pathValidationStatus=valid mode=limited
MinerIntelligentTargetArrival miner=281475017397855 selectedProfile=chef_high_value_consumables targetResource=Ptohi targetType=fruit_fruits_naboo arrivalResult=sample_started yieldMode=conceptual conceptualResource=water mode=limited
MinerIntelligentTargetAssignment miner=281475017397855 action=cleared clearReason=sampleComplete selectedProfile=chef_high_value_consumables targetResource=Ptohi ageSeconds=42 mode=limited
```

Failure example:

```text
MinerIntelligentTargetActivation miner=281475017397855 action=fallback fallbackReason=controllerStateNotSafe selectedProfile=chef_high_value_consumables targetResource=Ptohi targetType=fruit_fruits_naboo targetZone=naboo mode=limited
```

Safety boundaries:

- No limited activation occurs unless `mode="limited"` and `limitedActivationConfig.enabled=true`.
- At most one activation is accepted per interval by default.
- No real resource extraction occurs.
- No `ResourceContainer`, inventory item, vendor listing, bazaar entry, harvester output, crafting output, credit transfer, market purchase, reservation, consumption, or persistence write is created.
- No `AiEconomyData` or `AiEconomyStockpileLot` object is modified from D.5.6.
- The exact ResourceSpawn target is not converted into a conceptual yield label. Conceptual yield remains conceptual and uses the existing random conceptual label path.
- No delayed task captures raw controller, `AiAgent`, `ResourceSpawn`, path-node, market, or resource-container pointers.

Test procedure:

1. Start with the current shadow config and confirm `actualActivation=false`.
2. Set `mode="limited"`, `limitedActivationConfig.enabled=true`, and `assignmentConfig.clearOnSampleComplete=true`.
3. Keep `maxActiveMiners=1` and `maxActivationsPerInterval=1`.
4. Confirm exactly one switch decision can show `actualActivation=true activationResult=queued` per interval.
5. Confirm the controller later logs `MinerIntelligentTargetActivation action=started`.
6. Confirm arrival logs are assignment-aware and include `yieldMode=conceptual`.
7. Confirm the assignment clears with `clearReason=sampleComplete`.
8. Confirm conceptual yield logs continue and no player-facing economy/resource/container logs appear.

Known limitations:

- Activation is queued at the next safe miner loop boundary rather than interrupting the current sample or path.
- The miner still records conceptual output, not real resource output.
- Activation uses one validated density coordinate; it does not yet support dynamic retargeting while en route.
- If the assignment expires before the controller reaches the safe boundary, activation fails closed and the old conceptual loop continues.

The next possible phase should observe limited activation over longer runs, then decide whether to support more than one active miner or add richer fallback/retargeting controls.

#### Phase D.5.7 - Limited Activation Stability and Path Validation Tightening

D.5.7 keeps D.5.6's limited activation behavior narrow while making the logs easier to trust during longer tests. It does not add new miner capacity, new targeting behavior, resource extraction, persistence, or economy output.

Path validation trust tightening:

- `MinerPathValidationSimulation` now emits `pathTrustStatus`.
- A normal multi-node path that passes distance/node limits is marked `pathTrustStatus=verifiedPath`.
- `directFallbackUnverified`, `noPath`, `pathTooLong`, `tooManyPathNodes`, path exceptions, stale validation, target mismatch, and coordinate mismatch remain fail-closed.
- `MinerTargetingSwitchDecision` now reports both `pathValidationStatus` and `pathTrustStatus`.
- Limited activation requires `pathValidationStatus=valid`, `assignmentMatchesValidation=true`, and `pathTrustStatus=verifiedPath`.
- A direct start/end fallback from the pathfinder is still treated as unverified and cannot activate a miner.

Assignment lifecycle diagnostics:

```text
MinerIntelligentTargetActivation miner=281475017397855 action=queued ... pathTrustStatus=verifiedPath queuedState=sampling queuedDuringSample=true previousSampleYieldMayFollow=true mode=limited
MinerIntelligentTargetActivation miner=281475017397855 action=started ... queuedAgeSeconds=18 mode=limited
MinerIntelligentTargetArrival miner=281475017397855 ... arrivalResult=sample_started yieldMode=conceptual conceptualResource=water mode=limited
MinerIntelligentTargetAssignment miner=281475017397855 action=cleared clearReason=sampleComplete ... validatedAgeSeconds=60 queuedAgeSeconds=60 activatedAgeSeconds=79 sampleStartedAgeSeconds=80 sampleFinishedAgeSeconds=95 pathValidationStatus=valid pathTrustStatus=verifiedPath lastActivationResult=started lastFailureReason=none mode=limited
```

The `queuedDuringSample` and `previousSampleYieldMayFollow` fields explain the normal ordering where a miner can accept a limited activation at the next loop boundary while the previous conceptual sample's yield log appears nearby. The activation still starts only through the assignment-aware path and keeps yield conceptual.

Safety boundaries:

- No activation occurs unless the existing D.5.6 limited-mode gates are explicitly enabled.
- D.5.7 does not increase `maxActiveMiners` or `maxActivationsPerInterval`.
- No miner activates from `directFallbackUnverified` or any other untrusted path result.
- No `ResourceContainer`, real resource, inventory item, vendor/bazaar/auction listing, harvester output, crafting output, credit transfer, AI stockpile mutation, or persistence write is created.
- No demand profile, density selection, yield amount, conceptual resource selection, or market observation behavior is changed.

Test procedure:

1. Keep `maxActiveMiners=1`, `maxActivationsPerInterval=1`, `requireValidPath=true`, and `limitedActivationConfig.enabled=true`.
2. Confirm every activation-capable `MinerTargetingSwitchDecision` has `pathTrustStatus=verifiedPath`.
3. Confirm `directFallbackUnverified` appears only as a failed/untrusted path validation and never as `actualActivation=true`.
4. Confirm assignment clear logs include lifecycle ages for validation, queued, activation start, sample start, and sample finish when those stages occurred.
5. Confirm conceptual yield logs still use the existing SimMiner conceptual labels and no player-facing economy objects are created.

Known limitations:

- The lifecycle timestamps are diagnostic only and are not persisted.
- `sampleStartedAgeSeconds` represents the assignment-aware arrival/sample-start boundary; it is not a real resource extraction event.
- The feature still supports only the explicitly capped limited activation path. Broader activation should wait until long-run logs show stable trusted-path behavior.

#### Phase D.5.8 - Limited Activation Soak Controls and Gradual Scale-Up

D.5.8 adds soak-test controls around the D.5.6/D.5.7 limited activation path. It is meant to let operators test one miner for longer runs, then gradually raise caps when logs stay healthy. It does not change demand scoring, density targeting, path validation math, conceptual yield, persistence, or economy output.

Additional limited activation config:

```lua
minerIntelligentTargetingConfig = {
    mode = "limited",
    maxActiveMiners = 1,
    limitedActivationConfig = {
        enabled = true,
        maxActiveIntelligentMiners = 1,
        maxActivationsPerInterval = 1,
        cooldownSecondsPerMiner = 0,
        allowedZones = {},
        requireSamePlanet = true,
        disableOnFirstActivationFailure = true,
        disableOnActivationFailure = false,
        logActivationLifecycle = true,
        logHealthSummary = true,
    },
}
```

Field behavior:

- `maxActiveIntelligentMiners` limits assignments currently queued, moving, or sampling through the intelligent path. The default remains `1`.
- `maxActivationsPerInterval` still limits new activation requests accepted in one manager interval.
- `cooldownSecondsPerMiner` prevents the same miner from accepting another new intelligent activation until the cooldown expires. The default `0` preserves previous behavior.
- `allowedZones` is an optional allowlist. Empty means all zones are allowed. A non-empty list skips activation for miners outside those zones without marking that as a failure.
- `disableOnActivationFailure` is an emergency latch for real activation failures. It is off by default and resets when limited activation is disabled or the option is turned off.
- Controlled skips from caps, cooldown, disallowed zones, or interval disablement do not trip the failure latch.

Health summary:

```text
MinerIntelligentActivationHealth active=1 attempted=1 started=1 arrivals=1 sampleFinished=1 pathFailed=0 expired=0 rollbackHeld=0 controlledSkips=0 cooldownSkips=0 activeCapSkips=0 zoneSkips=0 activationFallbacks=0 maxActive=1 maxActivationsPerInterval=1 cooldownSeconds=0 emergencyDisabled=false mode=limited
```

The counters are interval summaries:

- `attempted` counts controller activation requests.
- `started` counts assignment-aware movement starts.
- `arrivals` counts assignment-aware arrival/sample-start events.
- `sampleFinished` counts assignment-aware conceptual sample completion.
- `pathFailed` counts assignment-aware path failures.
- `expired` counts assignment clears caused by expiration.
- `controlledSkips` counts cap/cooldown/zone/emergency skips that are expected during soak tests.
- `activationFallbacks` counts actual activation failures that should be investigated.

Safety boundaries:

- The default scale remains one active intelligent miner and one new activation per interval.
- Raising caps is operator-controlled; no automatic scale-up occurs.
- No real resources, `ResourceContainer` objects, inventory, vendors, bazaar/auction listings, harvesters, crafting output, credits, market purchases, reservations, stockpile mutations, or persistence writes are created.
- The old conceptual fallback loop remains the fallback for all skipped or failed limited activations.
- All health state is manager memory only and resets on server restart.

Recommended soak procedure:

1. Run with `maxActiveIntelligentMiners=1`, `maxActivationsPerInterval=1`, and `cooldownSecondsPerMiner=0`.
2. Confirm `activationFallbacks=0`, `pathFailed=0`, and `emergencyDisabled=false` across several intervals.
3. Add a small cooldown such as `cooldownSecondsPerMiner=120` if the same miner repeats too often.
4. Optionally set `allowedZones = { "tatooine" }` or another test planet to isolate runs.
5. Only after long clean runs, raise `maxActiveIntelligentMiners` and `maxActivationsPerInterval` one step at a time.

Known limitations:

- The health counters are operational diagnostics, not persisted metrics.
- The active count is based on retained manager assignments with queued/started/sample-started statuses.
- D.5.8 still does not make exact-resource yield or real extraction decisions. It only makes limited activation safer to observe at small scale.

#### Phase D.5.8.1 - Config and Log Consolidation

D.5.8.1 is a cleanup-only pass. It does not change miner targeting behavior, activation gates, demand scoring, conceptual yield, AI economy persistence, market observation, or any player-facing economy path.

Planner and diagnostic intent:

- D.6.6 demand-weighted planning is now the canonical planner for intelligent SimMiner targeting.
- D.3 miner target recommendations remain useful for resource-intelligence debugging, but they are not required for normal limited-activation soak.
- D.4 round-robin target simulation remains a legacy diagnostic/fallback comparison, not the preferred targeting signal.
- D.5 density and D.5.2 path validation remain required because they validate the exact D.6.6/assignment target before limited activation can proceed.
- D.5.3 switch decisions remain useful, but per-miner lines are now summary-first: stable no-change intervals rely on summaries, while activation-capable decisions, activation failures, path failures, trust failures, target mismatches, and rollback-held cases still emit detailed lines.
- `MinerIntelligentActivationHealth` is the primary D.5 limited-activation soak signal.

Operator mode comments were added to `sim_player_manager.lua`:

| Mode | Meaning |
|---|---|
| `observe` | Resource, demand, market, stockpile, and planner logs only; no intelligent targeting or activation. |
| `shadow` | Assignment and would-activate diagnostics; `actualActivation=false`. |
| `limited` | Explicitly gated miners may move to verified retained assignments; yield remains conceptual. |
| `soak` | Alias for limited mode plus health summaries, cooldowns, caps, and emergency latch for longer runs. Internally this normalizes to the existing `limited` activation path. |

The three activation caps are intentionally separate:

| Field | Meaning |
|---|---|
| `maxActiveMiners` | Number of miners evaluated by D.5.3/D.5.5 switch-decision and assignment logic per interval. It is not the active mover cap. |
| `maxActiveIntelligentMiners` | Number of miners currently queued, moving, or sampling through the intelligent assignment path. |
| `maxActivationsPerInterval` | Number of new intelligent activations accepted in one manager interval. |

Log-noise reductions:

- `assignmentConfig.logRetainedAssignments=false` suppresses repeated `MinerIntelligentTargetAssignment action=retained` lines during stable soak intervals.
- Creation, validation/update, failure, clear, activation, and near-expiry assignment lifecycle lines remain available through `logAssignmentLifecycle=true`.
- `logVerboseSwitchDecisions=false` keeps full `MinerTargetingSwitchDecision` lines focused on activation-capable decisions, activation outcomes, path/trust failures, assignment mismatches, and rollback-held cases.
- `MinerTargetingSwitchDecisionSummary` and `MinerIntelligentActivationHealth` include compact aggregate counts such as assignment mismatches, path trust rejections, controlled skips, cooldown skips, active-cap skips, zone skips, and activation fallbacks.
- Routine base SimPlayer movement/path trace logs remain gated behind `DEBUG_SIMPVP`; exception/failure and assignment-aware activation lifecycle logs remain visible.

Safety gates intentionally remain mandatory:

- `SimPlayerManagerConfig.enabled`.
- `minerIntelligentTargetingConfig.mode`.
- `limitedActivationConfig.enabled`.
- `requireDemandWeightedPlan`, `requireAcceptedDensityTarget`, and `requireValidPath`.
- `pathTrustStatus=verifiedPath`.
- The retained assignment cache and TTL/clear-on-combat/incap/death/zone-change rules.
- `maxActiveIntelligentMiners` and `maxActivationsPerInterval`.
- AI economy persistence write/read gates.
- Market observation read-only separation.
- No real resources, no `ResourceContainer` objects, no player inventory/vendor/bazaar/crafting/harvester/credit mutations, and no persistence writes from activation.

### RI.3 - Exact-Resource-Aware Conceptual Yield Provenance

RI.3 connects the Resource Scout and Resource Coverage views to completed intelligent SimMiner samples without changing what the miner actually produces. When an assignment-aware miner completes its conceptual sample, the manager records a small bounded in-memory provenance row before clearing the retained assignment.

The row is dashboard/API metadata only. It copies primitive/string facts that were already present in the intelligent assignment:

- Miner object ID.
- Conceptual label and amount credited by the existing conceptual yield path.
- Source resource name, source resource type, source zone, density coordinate, and density when available.
- Selected demand profile, demand state, and pressure score when available.
- Assignment creation time and assignment age.
- Explicit safety fields: `yieldMode=conceptual`, `identityConfidence=observed_resource_spawn`, `realResourceCreated=false`, `resourceContainerCreated=false`, `inventoryMutated=false`, and `economyMutated=false`.

RI.3 does not call real sampling or extraction APIs, does not call `ResourceSpawn::extractResource`, does not create `ResourceContainer` objects, and does not mutate player inventory, vendors, bazaar/auction listings, harvesters, crafting output, credits, market state, or AI economy persistence. The credited conceptual totals remain the existing memory-only conceptual accounting, and the exact `ResourceSpawn` target is not converted into the credited conceptual label.

This phase is a bridge from read-only resource intelligence to future exact-resource-aware stockpile lots. The dashboard can now explain that a miner conceptually produced a broad label because it completed an intelligent assignment against a named active resource opportunity, while still making clear that no real resource units exist.

### D.6 - Hot-Item Demand Profile Research

D.2's curated profiles proved that active `ResourceSpawn` objects can be filtered and scored, but names such as `weaponsmith_dl44` are test fixtures rather than economic truth. A schematic being craftable does not mean players create it often, that its resources deserve equal priority, or that it should influence future miner behavior.

This section separates facts visible in Core3's crafting data from player-demand policy. The repository can establish ingredient eligibility, component chains, quantities, experimental weights, and gameplay effects. It cannot, by itself, establish which products dominate a particular server's player market. Population, combat meta, loot availability, profession distribution, server age, custom balance changes, and operator goals all affect that answer.

#### Demand profile vocabulary

| Concept | Meaning | Appropriate use |
|---|---|---|
| Schematic profile | Resource requirements and experimental weights for one draft schematic and its components. | Mechanical eligibility and quality scoring. It is evidence, not demand by itself. |
| Profession profile | Broad resource interests shared by many schematics in one profession. | Discovery and coarse reporting. Too broad to drive production or mining alone. |
| Hot-item demand profile | A curated family of goods players repeatedly consume, replace, or treat as progression staples. | Primary future demand input, with operator-adjustable priority. |
| Server-best resource opportunity | A currently active spawn whose relevant weighted stats are exceptional for one or more demanded products. | Temporary resource-rush pressure while the spawn is active. |
| Stockpile value | Continued value of already gathered units after the source `ResourceSpawn` leaves shift. | Historical inventory valuation and future production planning. |

A future AI economy should index many schematics so it understands eligibility, but should activate demand only for a bounded set of player-relevant goals. Otherwise thousands of low-use schematics would dilute the signal from armor, weapons, consumables, harvesters, factories, and unusually valuable resource spawns.

#### Armorsmith demand: `composite_armor_supply`

Composite armor is a strong mature-server demand family because a complete suit uses repeated copies of the same expensive component chain, while final pieces and layers expose quality-sensitive resistance, integrity, encumbrance, and durability properties. The repo supports treating it as an armor supply chain rather than one schematic:

- Nine composite piece schematics exist under `draft_schematic/clothing`, including chest, helmet, leggings, boots, gloves, bracers, and biceps.
- A composite chest directly consumes `ore_intrusive`, `fuel_petrochem_solid_known`, `fiberplast_naboo`, `aluminum`, `copper_beyrllius`, and `hide_wooly`, plus four composite armor segments and clothing components.
- The helmet uses the same raw families and three composite segments. Other suit pieces repeat the same pattern at different quantities.
- A basic composite segment consumes `metal`, `steel`, and three armor layers.
- The advanced segment replaces its direct raw slots with exact gates: `iron_colat`, `steel_kiirium`, and `copper_polysteel`, plus three armor layers.
- A representative kinetic layer consumes `metal`, `petrochem_inert_polymer`, and `gemstone_armophous`.
- Composite segments, layers, and final armor pieces repeatedly weight `OQ`, `SR`, `UT`, and `MA`. Those stats feed effectiveness, integrity, encumbrance, special resistance, and durability properties.

Important correction: broad families such as `bone` or generic `hide` should not be assumed to belong in every composite profile. The representative composite chain specifically uses wooly hide and several exact inorganic types. Other armor families can create separate demand, but schematic eligibility must remain precise.

Likely profile:

| Field | Research recommendation |
|---|---|
| Key | `composite_armor_supply` |
| Primary phase | `mature_server`, with gated preparation during `early_server` |
| Item families | Composite suit pieces, composite segments, armor layers, synthetic cloth, reinforced fiber panels |
| Exact gating resources | `ore_intrusive`, `fuel_petrochem_solid_known`, `fiberplast_naboo`, `copper_beyrllius`, `hide_wooly`; advanced segment gates include `iron_colat`, `steel_kiirium`, `copper_polysteel` |
| Broad supporting families | Metal, steel, aluminum, inert polymer, amorphous gemstone |
| Quality emphasis | `OQ`, `SR`, `UT`, `MA`, interpreted per slot/component rather than averaged globally |
| Server-best sensitivity | High for layer and segment inputs that affect armor performance |
| Stockpile sensitivity | High because exact-type and high-stat spawns may not be available when later suits are ordered |

Early-server demand is gated by profession progression, exact resource availability, component capacity, and customer purchasing power. During that period, usable lower-tier armor and infrastructure may matter more than ideal composite. Once the required resource chain and master crafting capacity exist, repeated full-suit and replacement demand can make composite a durable economy staple. This lifecycle interpretation is an architectural inference; Core3's scripts define the products but do not contain market history.

#### Weaponsmith demand

The repo does not contain reliable evidence that one final weapon, including the DL44, is universally the player-meta choice. Weapon demand depends on combat professions, balance era, loot schematics, exceptional components, and server-specific tuning. The safest initial demand model is therefore a curated family of high-end weapon components plus an admin-selected set of final staples.

Representative final schematics inspected include:

- `DE-10 Pistol`: exact and broad metal inputs, two power handlers, a scope, `copper_diatium`, and a loot barrel.
- `FWG5 Pistol`: iron/ferrous metal plus projectile feed, projectile barrel, and scope components.
- `DLT20a Rifle` and `E11 Carbine`: broad metal plus power handler, blaster rifle barrel, scope, and stock.
- `Bowcaster`: `iron_doonium`, `steel_quadranium`, power handlers, barrel, feed mechanism, and scope.
- `Long Vibro Axe`: `steel_ditanium`, copper/non-ferrous metal, vibro units, and `steel_duralloy`.

The component templates show why one generic weaponsmith score is insufficient:

| Component family | Representative resource gates | Important weights |
|---|---|---|
| Advanced blaster pistol/rifle barrels | `steel_rhodium`, `steel_duralloy`, metal; rifle also uses `steel_duranium`; both use `armophous_ryll` | `CD` weighted more heavily than `SR` across damage, speed, wounds, durability, and range |
| Advanced blaster power handler | Inert polymer, `copper_diatium`, `ore_carbonate_ostrine`, `aluminum_phrik`, `gas_reactive_irolunn` | `CD` weighted more heavily than `OQ` |
| Advanced scope | `aluminum_chromium`, inert polymer, green diamond, copper | `OQ` |
| Advanced stock | `wood_deciduous_corellia`, `aluminum_linksteel` | `SR` |
| Advanced vibro unit | `copper_desh`, type-3 solid petrochemical fuel, `copper_platinite` | `UT` |
| Advanced reinforcement/sword cores | Exact steel or iron types | `UT` |
| Projectile barrel | Exact steel/iron families | `HR` and `SR`, not the blaster barrel's `CD`/`SR` pair |

Final ranged weapon templates commonly use `CD` and `OQ`, while the inspected melee vibro axe uses `SR`. A demand engine must preserve these component-specific profiles instead of assigning every weaponsmith resource the same `CD/OQ/SR/UT` blend.

Recommended initial profiles:

- `master_weaponsmith_staples`: operator-selected final pistol, carbine, rifle, heavy, and melee products appropriate to this server's current player meta.
- `high_damage_blaster_components`: advanced blaster barrels and power handlers, emphasizing their exact resource gates and `CD`-heavy quality.
- `high_damage_melee_components`: vibro units and reinforcement/sword cores, emphasizing exact types and `UT` or the final weapon's applicable stat.
- `weapon_support_components`: scopes and stocks where `OQ` or `SR` matters even when they are not the headline damage component.

The profile keys above describe demand families, not claims that every listed final weapon is equally popular. Initial hot-item membership should be admin-configured and revisited from observed server behavior.

#### Chef demand

Chef demand is naturally consumable and effect-oriented. The repo exposes both the buff/effect and the resource-quality weights, making it possible to distinguish a desirable combat consumable from an arbitrary food schematic.

Representative products:

| Product | Gameplay effect in template | Direct resource chain | Quality weights |
|---|---|---|---|
| Citros Snow Cake | `attack_accuracy` | Sweet cake mix, `fruit_fruits`, `wheat_wild`, medium additive; the cake mix itself uses domesticated wheat, berries, and fruit | `OQ`, `PE`, `FL`, `DR`, with stronger weight on PE/FL and quantity-related PE/DR |
| Pikatta Pie | `dodge_attack` | Wheat, fruit, water, bantha butter, medium additive | Same common food quality pattern: `OQ`, `PE`, `FL`, `DR` |
| Vasarian Brandy | Mind, focus, and willpower modifiers | Alcohol, fruits, berries, drink container, medium additive | `OQ`, `PE`, `FL`, `DR` |
| Vagnerian Canape | Focus and willpower modifiers | Dough, berries, fruit, carbosyrup, heavy additive | `OQ`, `PE`, `FL`, `DR` |
| Bivoli Tempari | Wound-treatment modifier | Protato, carbosyrup, carnivore meat, vegetable, heavy additive | `OQ`, `PE`, `FL`, `DR` |
| Veghash | Creature-harvesting modifier | Soypro, cereal, vegetable, organic, medium additive | `OQ`, `PE`, `FL`, `DR` |
| Exo-Protein Wafers / Flameout | Damage-mitigation effects | Meat/hide or alcohol/reactive gas/tubers, plus additives | Common food weights; Exo-Protein also uses `SR` in one experimental property |

Likely profiles:

- `chef_buff_foods`: repeat-use combat, healing, harvesting, and profession-support foods grouped by actual effect.
- `chef_high_value_consumables`: products where strong resources materially improve effect strength, duration, quantity, filling, or market desirability.
- `chef_component_supply`: alcohol, cake mixes, butter, dough, carbosyrup, containers, and additives needed to manufacture hot products at scale.

Citros Snow Cake is a useful concrete example because it grants attack accuracy and consumes a multi-stage ingredient chain. It should not imply that all fruit or wheat has equal value: eligibility, the experimental property being optimized, current stock, and demand for the effect all matter.

#### Architect and production infrastructure demand

Production infrastructure creates both quality demand and large-volume bulk demand:

- A mineral mining installation directly consumes 200 steel, 300 metal, 150 ore, 100 additional metal, and 200 chemical, plus structural and extraction components.
- A heavy mineral mining installation directly consumes 400 steel, 600 metal, 200 and 300 additional metal, and 400 chemical, plus multiple wall, turbine, storage, and ore-mining components.
- Equipment, food, and structure factories each directly consume 300 steel and 650 ore, plus wall, turbine, storage, and manufacturing components.
- The ore mining unit consumes 100 steel, 150 steel, 150 metal, 120 steel, and 75 inert gas.
- Ore mining unit extraction rate weights `HR`, `SR`, and `UT`, with `UT` weighted twice.
- Harvester deed extraction and storage properties use `HR`, `SR`, `UT`, and `MA`.
- Factory deed build rate, storage, and durability use category-specific combinations: equipment factories use `OQ`, `MA`, and `UT`; food and structure factories use `DR` and `UT`.

Likely profile:

| Field | Research recommendation |
|---|---|
| Key | `production_infrastructure` |
| Primary phase | `early_server` and continuing replacement/expansion demand |
| Item families | Harvesters, factories, mining units, turbines, wall/storage modules, manufacturing mechanisms |
| Bulk resource demand | Steel, metal, ore, chemical, and component feedstock |
| Quality-critical demand | Exact component inputs whose `HR`, `SR`, `UT`, `MA`, `OQ`, or `DR` weights affect extraction, build rate, storage, or durability |
| Server-best sensitivity | High for extraction-rate components; lower for purely bulk structural slots |
| Stockpile sensitivity | High because infrastructure consumes large quantities and expands future production capacity |

This profile should distinguish "best available for the extraction component" from "cheap eligible material for a bulk wall or foundation slot." Treating every unit in a 600-metal installation slot as server-best material would waste scarce resources and misrepresent player crafting practice.

Shipwright and droid production are adjacent infrastructure/equipment economies, but this pass does not claim a complete hot-item list for them. They should receive separate research because their component trees and JTL stat priorities are substantial.

#### Server-phase awareness

| Phase | Expected demand behavior |
|---|---|
| `early_server` | Broad demand for harvesters, factories, crafting components, profession tools, usable weapons, and attainable armor. Infrastructure and quantity can outrank perfection. |
| `mature_server` | Demand concentrates around optimized composite armor, master-level weapon/component families, high-value consumables, replacements, and expanded production capacity. |
| `resource_rush` | A rare active spawn exceeds a demanded profile's quality threshold. Scouts and gatherers prioritize it before despawn, even if current inventory is adequate. |
| `stockpile_phase` | The spawn has left shift, but previously gathered units retain value for future crafting. Demand shifts from acquisition opportunity to inventory allocation and conservation. |

These phases should overlap rather than form a one-way global state. A mature server can still have an early-style infrastructure shortage, and each resource can independently enter an active rush or stockpile phase.

#### Stockpile economics

Core3's active `ResourceSpawn` lifecycle and crafted-resource identity make active intelligence and historical inventory different concerns:

- Active spawn intelligence answers what exists now, where it can be gathered, its density, and how long the opportunity may remain.
- Historical/stockpiled intelligence answers what the economy already owns, which exact spawn supplied it, its stat vector, what demanded profiles it serves, and whether a better replacement is likely.
- A despawn removes gathering access; it does not retroactively invalidate containers or resources already gathered from that spawn.
- Therefore a server-best spawn can continue influencing production and prices long after it leaves shift.
- Future AI should not discard a demand match merely because the originating spawn is no longer active.

Persistence is intentionally not designed or implemented here. The architectural requirement is only that future durable inventory preserve enough spawn identity and stat metadata to value stock after despawn without pretending the source is still gatherable.

#### Proposed demand profile model

The following is conceptual documentation, not runtime Lua:

```lua
demandProfiles = {
    composite_armor_supply = {
        category = "armorsmith",
        serverPhases = { "mature_server", "resource_rush", "stockpile_phase" },
        priority = 100,
        itemFamilies = {
            "composite armor pieces",
            "composite armor segments",
            "armor layers",
        },
        componentProfiles = {
            "composite_final_piece",
            "composite_segment",
            "armor_layer",
        },
        resourceEligibility = {
            exactTypes = {
                "fiberplast_naboo",
                "copper_beyrllius",
                "iron_colat",
                "steel_kiirium",
                "copper_polysteel",
            },
            families = { "metal", "steel", "aluminum", "hide_wooly" },
        },
        qualityGoals = {
            armor_effectiveness = { "OQ" },
            integrity = { "OQ", "SR" },
            encumbrance = { "OQ", "UT", "MA" },
        },
        serverBestSensitive = true,
        stockpileSensitive = true,
        bulkSlotsMayUseLowerQuality = true,
        evidence = "curated_repo_and_admin_policy",
    },
}
```

A future model should also carry:

- Runtime demand weight and enabled state.
- Server-phase applicability.
- Exact item/component membership.
- Per-slot resource eligibility and quantity.
- Per-property stat weights rather than one category-wide average.
- Minimum acceptable quality and server-best thresholds.
- Current conceptual supply, desired reserve, and consumption rate.
- Active-spawn opportunity value versus stockpile value.
- Bulk-slot policy so scarce premium resources are not consumed where quality has little value.
- Confidence/source metadata distinguishing repository mechanics, administrator policy, and observed player demand.

Only demanded profiles should influence future miner selection. An all-schematic index remains useful for eligibility lookup and explanation, but inactive or low-priority schematics should contribute little or no acquisition pressure.

#### Recommended next phase

The next implementation phase identified by this research was **D.6.1 - Dynamic Demand Simulator, log-only**:

1. Define a small initial set of hot-item profiles: `composite_armor_supply`, `master_weaponsmith_staples`, `high_damage_weapon_components`, `chef_buff_foods`, `chef_high_value_consumables`, and `production_infrastructure`.
2. Keep all profile output read-only and disconnected from miners.
3. Allow an administrator to enable profiles and adjust demand weights at runtime without restarting the server.
4. Log how demand, stock targets, server phase, active resource quality, and same-planet availability would modify existing Resource Intelligence recommendations.
5. Preserve a clear explanation for every score contribution and every disabled profile.
6. Collect observations before any demand-weighted miner targeting is considered.

Open questions that the repository cannot answer:

- Which final weapons are true staples for this server's current combat meta?
- What armor resistance layouts and enhancement choices players actually buy most often?
- Which chef effects have sustained consumption rather than occasional niche demand?
- How much infrastructure already exists, and when does replacement demand become more important than expansion?
- What quality threshold should trigger a "server-best" rush for each slot instead of merely identifying the current best spawn?
- Should actual player crafting, vendor sales, and resource-container holdings eventually inform demand, or should a low-population server remain primarily administrator-directed?

External historical/player-behavior references consulted for context:

- [SWGEmu Wiki: Advanced Guide Armor Fundamentals](https://swgemulator.fandom.com/wiki/Advanced_Guide_Armor_Fundamentals)
- [SWGEmu Wiki: Chef](https://swgemulator.fandom.com/wiki/Chef)
- [SWGEmu Wiki: Guide to Weaponsmith](https://swgemulator.fandom.com/wiki/Guide_to_Weaponsmith)
- [SWGEmu Wiki: Crafting](https://swgemulator.fandom.com/wiki/Crafting)
- [SWGEmu Wiki: Galaxy Harvester](https://swgemulator.fandom.com/wiki/Galaxy_Harvester)
- [SWG Wiki: Resources](https://swg.fandom.com/wiki/Resource)

These community references support the profession and player-behavior context, while the concrete ingredient and weighting claims above come from the inspected Core3 scripts. Popularity claims remain intentionally configurable rather than hard-coded.

### D.6.1 - Dynamic Demand Simulator

D.6.1 implements a disabled-by-default, manager-owned logging task that applies hot-item demand policy to the existing read-only `ResourceIntelligenceEntry` snapshot. It does not read or update SimMiner controllers, D.3 recommendations, D.4 plans, density targets, path-validation results, conceptual yields, or any player-facing economy state.

Configuration lives in `bin/scripts/managers/sim_player_manager.lua`:

```lua
demandProfileSimulationConfig = {
    enabled = false,
    intervalSeconds = 300,
    serverPhase = "mature_server",
    logTopN = 3,
    profiles = {
        composite_armor_supply = {
            enabled = true,
            weight = 1.0,
            priority = 100,
        },
        -- Other curated hot-item profiles follow the same shape.
    },
}
```

| Field | Behavior and guardrail |
|---|---|
| `enabled` | Defaults to `false`. No demand task is scheduled when disabled. |
| `intervalSeconds` | Periodic log interval, clamped to 30-3600 seconds. |
| `serverPhase` | One of `early_server`, `mature_server`, `resource_rush`, or `stockpile_phase`; invalid values retain `mature_server`. |
| `logTopN` | Number of active-resource recommendations per enabled profile, clamped to 1-20. |
| `profiles.<key>.enabled` | Enables or suppresses one recognized profile. |
| `profiles.<key>.weight` | Demand multiplier, clamped to 0.0-10.0. Zero disables effective demand. |
| `profiles.<key>.priority` | Relative policy priority, clamped to 0-1000. Zero disables effective demand. |

While the simulator is enabled, each scheduled interval re-reads only this demand table through a private task-local Lua state. Operators can therefore adjust phase, weights, priorities, profile switches, interval, and `logTopN` without reloading spawn groups or restarting the server. Setting `enabled = false` is also observed at the next interval and stops rescheduling; because no polling task remains after that, re-enabling a stopped or startup-disabled simulator still requires the normal manager/server configuration load.

Only the six known D.6 profile keys are consumed. Unknown Lua profile keys are ignored, so a typo cannot create an unreviewed scoring policy or crash startup:

- `composite_armor_supply`
- `master_weaponsmith_staples`
- `high_damage_weapon_components`
- `chef_buff_foods`
- `chef_high_value_consumables`
- `production_infrastructure`

The profile definitions remain intentionally curated C++ data rather than dynamic schematic parsing. Each profile contains:

- Server phases in which it is active.
- Known exact or qualified resource type gates from inspected schematic/component chains.
- Premium families where resource quality is expected to matter.
- Bulk families that are eligible supply but should not be described as premium.
- Explicit stat weights.
- `serverBestSensitive` and `stockpileSensitive` explanation flags.

For each eligible active resource, the simulator calculates:

1. `baseScore`: weighted average of the profile's available relevant stats. Missing stats add no value and do not count as perfect.
2. Eligibility bonus: 100 for an exact/qualified type match, 50 for a premium-family match, or 10 for a bulk-family match.
3. `demandScore = (baseScore + eligibilityBonus) * weight * (priority / 100)`.

This is an explainable pressure signal, not exact crafting mathematics. Separating `baseScore`, eligibility tier, profile `weight`, and `priority` makes it possible to tell whether a recommendation came from exceptional resource quality or administrator demand policy.

Example output:

```text
DemandProfileSimulation profile=composite_armor_supply category=armorsmith phase=mature_server rank=1 resource=Gowane type=copper_polysteel zones=tatooine demandScore=912 baseScore=812 priority=100 weight=1 reason=exact type copper_polysteel; weighted OQ=.../SR=.../UT=.../MA=...; premiumQuality=true; bulkEligible=false; stockpileSensitive=true; serverBestSensitive=true mode=log-only
```

Compact skip lines explain `disabledProfile`, `inactiveForServerPhase`, `noEligibleActiveResource`, unavailable resource infrastructure, or an empty active-resource set. Logging is bounded by the configured interval and `logTopN`.

Safety boundaries:

- No SimMiner movement, destination, target, survey, sample, timing, or yield state is read or changed.
- Demand scores do not feed D.3 recommendations or D.4/D.5 simulation plans.
- No `ResourceContainer`, resource object, inventory item, vendor listing, bazaar entry, crafting output, harvester output, credit transfer, or persistence record is created.
- The task copies active `ResourceSpawn` metadata under the existing short `ResourceManager` lock, then releases that lock before scoring and logging.
- No delayed task captures a controller, `AiAgent`, or `ResourceSpawn` pointer.

Known limitations:

- Profiles approximate researched hot-item/component families; they do not parse all schematics or recurse component chains.
- Server phase is one configured policy value rather than a derived economic condition.
- Active resources are scored without stock quantities, consumption rates, player orders, historical spawns, or persisted stockpiles.
- Live tuning is file-driven and interval-based; there is no player-facing or administrator command, and a fully disabled task does not poll for re-enablement.
- `serverBestSensitive` and `stockpileSensitive` are explanatory metadata only.

D.6.2 now supplies the **log-only demand-state model** that compares desired reserve, conceptual supply, and active-spawn opportunity. Demand still must not influence miner selection until profile eligibility, phase behavior, and pressure logs have been validated with real server resource populations.

### Profession Guide Research - Resource Demand

This section records profession-specific resource guidance from archived SWG crafting guides and cross-checks it against representative Core3 schematics and crafted-object templates. It is research for a future D.6.1.1 profile-tightening pass only. No scoring, configuration, miner, crafting, or economy behavior is changed by this section.

The guides are historical player documents rather than authoritative current-server telemetry. They are valuable because they describe how experienced crafters separated premium inputs from grind or bulk material, but exact popularity, balance, and market behavior still need administrator policy and observation on this server.

#### Sources

The original SWGEmu archive returned HTTP 403 during this research. Complete pages with the same archive IDs were inspected through the SWG Reckoning archive mirror.

| Profession | Original SWGEmu archive | Mirror inspected |
|---|---|---|
| Chef | [Chef guide](https://www.swgemu.com/archive/scrapbookv51/data/20080108200838/chef_guide1.html) | [Chef guide mirror](https://swgreckoning.org/archive/data/20080108200838/chef_guide1.html) |
| Armorsmith | [Forum guide](https://www.swgemu.com/archive/scrapbookv51/data/20070213112516/index.html), [armor guide](https://www.swgemu.com/archive/scrapbookv51/data/20080108214940/armorsmith_guide.html) | [Forum guide mirror](https://swgreckoning.org/archive/data/20070213112516/index.html), [armor guide mirror](https://swgreckoning.org/archive/data/20080108214940/armorsmith_guide.html) |
| Weaponsmith | [Forum guide](https://www.swgemu.com/archive/scrapbookv51/data/20070127215810/index.html), [weaponsmith guide](https://www.swgemu.com/archive/scrapbookv51/data/20080108214940/weaponsmith_guide.html) | [Forum guide mirror](https://swgreckoning.org/archive/data/20070127215810/index.html), [weaponsmith guide mirror](https://swgreckoning.org/archive/data/20080108214940/weaponsmith_guide.html) |
| Doctor | [Doctor guide](https://www.swgemu.com/archive/scrapbookv51/data/20080108200838/doctor_guide.html) | [Doctor guide mirror](https://swgreckoning.org/archive/data/20080108200838/doctor_guide.html) |
| Architect | [Forum guide](https://www.swgemu.com/archive/scrapbookv51/data/20070126164620/message_004.html), [architect guide](https://www.swgemu.com/archive/scrapbookv51/data/20080108200838/architect_guide.html) | [Forum guide mirror](https://swgreckoning.org/archive/data/20070126164620/message_004.html), [architect guide mirror](https://swgreckoning.org/archive/data/20080108200838/architect_guide.html) |

Representative Core3 files under `bin/scripts/object/draft_schematic` and `bin/scripts/object/tangible` were also inspected to verify current ingredient type strings and experimental properties. Guide statements about player demand are kept separate from mechanical eligibility visible in those scripts.

#### Chef

The Chef guide describes three experimentation goals:

- Flavor and Texture increases duration.
- Nutritional Value increases the positive effect or reduces a negative side effect.
- Quantity increases charges.

The guide treats food flora, meat, milk, and water as routine Chef inputs. Its recipe list includes cereal, wheat, oats, rice, corn, fruit, berries, flowers, vegetables, beans, greens, fungi, tubers, several meat classes, milk, and water. Current Core3 food schematics confirm that common buff foods and drinks use these specific families and commonly weight `OQ`, `PE`, `FL`, and `DR`.

Hide and bone are real Chef ingredients, but only for particular recipes. Historical examples include Exo-Protein Wafers, Caramelized Pkneb, Jawa Beer, Cho-Nor-Hoola, Garrmorl, and Deneelian Fizz Pudding. Some recipes similarly require a specific gas, metal, mineral, oil, radioactive, or wood input. This does **not** justify treating every `organic` resource as eligible for every Chef demand profile.

Recommended interpretation:

| Concern | Recommendation |
|---|---|
| Broad Chef allowlist | `flora_food`, fruit, berries, cereal, wheat, oats, rice, corn, vegetables and their subtypes, meat and its requested subtypes, milk, and water |
| Exact-recipe-only inputs | Hide, bone, wood, gas, metal, mineral, ore, radioactive, petrochemical, and other inorganic families |
| Preferred stats | `OQ`, `PE`, `FL`, and `DR`; include `SR` only where the inspected product actually uses it |
| Premium resources | Exact inputs for active high-value buff foods where the relevant OQ/PE/FL/DR blend materially improves strength, duration, filling, or charges |
| Bulk resources | Grinding inputs and slots whose contribution is absent or unimportant for the selected product |
| Stockpile-sensitive inputs | Exceptional milk, planet-specific crops, fruit, vegetables, cereal, meat, additives, and exact niche inputs for a demanded recipe |
| Must not be broadly eligible | Generic `organic`, generic `creature`, hide, bone, chitin, or arbitrary flora merely because one Chef schematic somewhere accepts them |

`chef_buff_foods` should therefore use a bounded food-family allowlist. Hide, bone, reactive gas, inert gas, and similar materials belong only in narrower item-family profiles such as an explicitly enabled Exo-Protein, Garrmorl, or Citros Snow Cake chain.

#### Armorsmith

The Armorsmith guide identifies four central stats:

- `OQ` contributes broadly and is described as the most important general armor resource stat.
- `SR` drives general and special resistance.
- `MA` reduces encumbrance/HAM costs and is especially important for armor layers.
- `UT` contributes durability, but is often less commercially important than resistance and encumbrance.

Composite armor is a mature-server staple because complete suits repeatedly consume segments and layers. The guide distinguishes unlayered composite, standard layered composite, kinetic variants, stun layers, and heavy composite rather than applying one stat blend to every slot. It also calls out wooly, scaley, leathery, and bristley hides for different armor families, with wooly hide particularly important. Current Core3 composite schematics further narrow the chain with exact resources such as wooly hide, Naboo fiberplast, Beyrllius copper, Colat iron, Kiirium steel, Polysteel copper, inert polymer, and amorphous gemstone.

Recommended interpretation:

| Profile or slot | Eligible resource direction | Preferred stats |
|---|---|---|
| `composite_armor_supply` | Exact composite final-piece and segment gates, metal, steel, aluminum, appropriate fiberplast, wooly hide, inert polymer, and amorphous gemstone | Primarily `OQ` and `SR`, with `UT` and `MA` only as required by the actual slot |
| `armor_layers` | Exact layer resource types, including the correct gas, ore, polymer, gemstone, metal, hide, or bone family for that layer | `OQ`/`MA` for encumbrance-sensitive layers; `OQ`/`SR` for resistance |
| `organic_armor_supply` | Only armor families whose schematics explicitly request the matching hide, bone, or chitin subtype | Slot-specific `OQ`, `SR`, and `MA` |
| Bulk armor inputs | Low-contribution filler or structural slots identified from the exact schematic | Use eligible stock without spending server-best material |

Premium/server-best pressure should focus on exact layer, segment, and final-piece inputs that affect resistance or encumbrance. Rare exact ores, gemstones, polymers, metals, and high-stat armor hides remain stockpile-sensitive after despawn. Generic bone, hide, chitin, or metal should not enter `composite_armor_supply` unless the selected component chain actually accepts it.

#### Weaponsmith

The Weaponsmith guides make a strong distinction between firearm and melee demand:

- Firearms broadly depend on `CD` and `OQ`.
- Melee weapons broadly depend on `SR`.
- Component requirements vary and must follow the proportional weights shown by the actual schematic.
- A 100% single-stat slot should not be diluted by unrelated high stats.

The guides describe power handlers, barrels, ammunition feeds, stocks, and scopes as common component families. Copper is especially valuable for early firearm power handlers because of conductivity. They also identify high-quality weapon powerups as persistent demand for high-`OQ` metal and chemical. By contrast, the profession grind consumes very large amounts of ordinary steel, especially through projectile barrels; that is bulk demand rather than premium demand.

The historical guide notes Kammris, Doonium, and Plumbum iron as examples of valuable challenge-planet spawns, but the exact best resource is server- and shift-specific. Current Core3 components reinforce the need for slot profiles: advanced blaster barrels emphasize `CD`/`SR`, advanced power handlers emphasize `CD`/`OQ`, scopes use `OQ`, stocks use `SR`, vibro units and reinforcement cores use `UT`, and projectile barrels can use `HR`/`SR`.

Recommended interpretation:

| Demand profile | Allowlist direction | Preferred stats |
|---|---|---|
| `high_damage_blaster_components` | Exact barrel and power-handler types plus eligible copper, aluminum, steel, iron/ferrous metal, gemstones, inert polymer, reactive gas, and chemical only where required by those components | Component-specific `CD`/`OQ` or `CD`/`SR` |
| `high_damage_melee_components` | Exact vibro-unit, reinforcement-core, sword-core, steel, iron, copper, wood, and petrochemical types | Actual slot weight, commonly `SR` or `UT` |
| `weapon_support_components` | Exact scope, stock, feed, grip, and related component resources | Scope `OQ`, stock `SR`, and the inspected weights for other components |
| `weapon_powerups` | Eligible metal and chemical | High `OQ` as described by the guide and verified against the selected powerup schematic |
| `weaponsmith_bulk_supply` | Steel and other eligible grind or mass-production inputs | Quantity and cost; do not consume server-best stock automatically |

`master_weaponsmith_staples` should remain an administrator-selected set of final items. The archived guides and repository do not establish one universal weapon meta. Broad gas, chemical, metal, or mineral families should not be accepted merely because some weapon schematic uses them.

Premium and stockpile pressure should follow exact advanced-component gates and exceptional relevant stats. Grind steel and quality-insensitive upgrade-kit material are bulk supply. The AI should never rank an unrelated high-stat resource above an eligible component input.

#### Doctor

The Doctor guide emphasizes repeat demand for stimpacks, wound packs, cures, buffs, resurrection medicine, biological components, chemical components, solid or liquid delivery components, and factory production. It recommends maintaining access to flora, chemical, water, mineral, and other harvesting capacity rather than relying on one generic resource source.

Current Core3 medicine schematics provide a more precise eligibility map:

- Wound packs use resources such as seeds, chemicals, vegetables/tubers, petrochemical polymers, and crafted delivery/effect components.
- Buff packs use avian meat with reactive or generic gas in several advanced recipes.
- Disease and poison paths use non-ferrous metal, insect meat or vegetable fungi, chemicals, radioactive material, and liquid petrochemicals.
- Cures use flora food, corn, fungi, inert or reactive gas, and crafted liquid-delivery components.
- Some basic medicines accept broad `organic` and `inorganic`, but advanced products quickly narrow to specific types.

The crafted medicine and chemistry component templates repeatedly use `OQ` and `PE`, often with `UT`; `DR` appears in resilience, dispersal, duration, disease, poison, and buff-related properties, while `CD` appears in several disease, poison, and area-delivery products. These must be interpreted per product and component rather than as one Doctor-wide average.

Recommended future profiles:

| Demand profile | Recommended eligibility | Preferred stats |
|---|---|---|
| `doctor_stim_woundpacks` | Exact seeds, flora, vegetable/tuber, chemical, polymer, organic/inorganic, and component types required by selected high-use packs | Usually `OQ`/`PE`/`UT`, plus exact component weights |
| `doctor_buffs` | Exact avian meat, gas, organic/inorganic, delivery shell, duration mechanism, and biologic controller requirements | `OQ`/`PE`, with `UT` or `DR` where exposed |
| `doctor_cures` | Exact flora food, corn, fungi, inert/reactive gas, liquid suspension, dispersal, and biologic-controller requirements | Usually `OQ`/`PE`; include `DR` or other stats only from the selected cure chain |
| `doctor_disease_poison` | Exact non-ferrous metal, insect meat or fungi, chemical/radioactive/liquid petrochemical, and advanced chemistry components | Product-specific `OQ`, `PE`, `DR`, and `CD` |

Doctor demand is both premium and volumetric: high-use medicine consumes factory crates, while the best resources improve effectiveness, duration, or charges. Exceptional medicinal crops, chemicals, gases, water/liquid inputs, and exact component feedstock are stockpile-sensitive. Generic organic, inorganic, meat, metal, gas, or chemical should not qualify for every medical profile.

The Doctor guide is an older document and does not provide a complete modern stat matrix. D.6.1.1 should therefore add Doctor profile candidates conservatively and derive each stat vector from current Core3 templates before enabling it.

#### Architect

The Architect guides describe two different economies:

1. Quality-sensitive infrastructure, especially maximum-BER harvesters and high-rating crafting stations.
2. Extremely large-volume structures, factories, furniture, walls, and profession-grind production where resource quality often does not matter.

For harvester extraction components, the guide uses `HR`, `SR`, and `UT`, with `UT` counted twice in the cited formula. Storage emphasizes `MA` and `UT`. Crafting stations consume high-`CD` metal, usually copper, while their control and sensor components favor high `OQ`. The guide explicitly notes that ore without conductivity can fill a non-contributing mineral slot and that lubricating oil or inert gas lacking the requested stats can be suitable in some slots.

Architect bulk demand is substantial: the guide estimates roughly 60% ore, 30-35% other metal, plus chemicals and reactive/inert gases, and notes that houses and furniture generally do not benefit from premium resource quality. Harvesters were a high-demand product because customers sought maximum BER, while factories were more commodity-like and their demand followed profession changes and production expansion.

Recommended interpretation:

| Demand profile | Allowlist direction | Preferred stats |
|---|---|---|
| `harvester_extraction_components` | Exact ore-mining/extraction unit inputs, steel, metal, ore, lubricant, inert gas, and other required component feedstock | `HR`, `SR`, `UT` with the exact slot weights; storage uses `MA`/`UT` |
| `crafting_station_quality` | Exact station, control-unit, sensor, copper/metal, ore, polymer, gas, and component inputs | High `CD` for the station metal; high `OQ` for appropriate components |
| `production_infrastructure_bulk` | Steel, metal, ore, chemical, reactive gas, inert gas, and exact structural component feedstock | Quantity/cost unless the slot contributes to a quality property |
| `houses_factories_furniture_bulk` | Exact building/factory/furniture resource and component families | Primarily quantity and availability |

High-HR/SR/UT extraction inputs and high-CD/OQ crafting-station inputs are premium and server-best sensitive. Ore, steel, metal, chemicals, gases, walls, modules, and factory feedstock are stockpile-sensitive because of their volume even when quality is unimportant. The future scorer should not waste premium resources in a bulk slot that contributes no relevant stat.

#### Cross-profession demand rules

The guide evidence supports several general AI-economy rules:

- Eligibility comes before quality. A perfect stat vector is worthless to a profile that cannot use the resource.
- Exact type and class-chain matching should override broad profession shortcuts.
- Premium and bulk demand must be separate. Both can be economically important, but only premium demand should create server-best pressure.
- Stat weights belong to a product slot or component chain, not merely a profession.
- Despawned resources retain stockpile value. Exact, exceptional, or high-volume inputs should remain economically visible after their active spawn ends.
- Current hot-item membership is policy. Repository and guide evidence can prove mechanics, but administrators should choose which final products matter to this server.

Recommended initial family allowlists:

| Profession | Broad families safe for coarse discovery | Families requiring exact-profile evidence |
|---|---|---|
| Chef | Food flora, named crop families, meat subtypes, milk, water | Hide, bone, chitin, wood, gas, metal, mineral, radioactive, petrochemical, generic organic |
| Armorsmith | Metal/steel/aluminum, appropriate fiberplast, explicit armor hide families | Bone, chitin, generic hide, gases, ores, gemstones, polymers, and exact composite/layer gates |
| Weaponsmith | Metal families used by enabled firearm/melee components | Gas, chemical, petrochemical, gemstones, wood, radioactive, and exact advanced-component types |
| Doctor | Medicinal flora/crops, selected meat types, chemical and liquid families used by enabled medicine | Broad organic/inorganic, arbitrary creature resources, metals, gases, radioactive, petrochemicals unless the product requires them |
| Architect | Metal, steel, ore, mineral, chemical, gas for bulk infrastructure discovery | Premium extraction/station scoring and exact components, where slot contribution determines value |

#### Recommended D.6.1.1 changes

The next implementation should remain disabled by default and log-only:

1. Tighten `chef_buff_foods` and `chef_high_value_consumables` to explicit food flora, crop, meat, milk, and water families. Remove generic `organic` as a shortcut.
2. Permit hide, bone, gas, mineral, metal, and other unusual Chef inputs only through explicitly enabled item or component profiles.
3. Split Armorsmith eligibility into composite final-piece, segment, layer, and other armor-family policies. Do not add all hide, bone, or chitin to composite.
4. Split Weaponsmith scoring by barrel, power handler, projectile, melee core, scope, stock, powerup, and bulk supply. Preserve each component's actual stat vector.
5. Split Architect demand into premium extraction components, premium crafting stations, and bulk infrastructure. Add an explicit bulk flag so high-stat stock is not favored where quality contributes nothing.
6. Add conservative Doctor candidates for stim/wound packs, buffs, cures, and disease/poison products. Build their allowlists and stat weights from the current Core3 schematic/component chain.
7. Keep final hot-item membership, profile weight, and server phase administrator-configurable. Archived guides inform defaults; they should not silently define this server's meta.
8. Add explainability fields that identify `exactType`, `eligibleFamily`, `componentSlot`, `premium`, or `bulk` as the reason a resource entered a recommendation.

D.6.1.1 should first prove that tightened profiles reject false positives in logs. It must remain disconnected from D.3/D.4 miner recommendations until profession-specific eligibility is stable across several real resource shifts.

### D.6.1.1 - Demand Profile Eligibility Tightening

D.6.1.1 applies the profession-guide findings to the existing disabled-by-default `DemandProfileSimulation`. This remains a read-only, log-only cleanup. It does not feed demand scores into SimMiner recommendations, plans, movement, density probing, path validation, survey/sample behavior, or conceptual yield.

#### Chef eligibility

`chef_buff_foods` and `chef_high_value_consumables` no longer use generic `organic` or reactive gas as broad eligibility shortcuts. Their curated broad allowlist is now limited to food and water families:

- `seafood`
- `meat`, with `meat_egg` explained as `egg`
- `fruit`
- `vegetable`, with specific labels for beans, tubers, and greens
- `cereal`
- `wheat`
- `rice`
- `corn`
- `oats`
- `milk`
- `water`

The profiles retain a small number of existing exact food types, such as fruit, wheat, vegetable, and carnivore-meat variants. Fungi remains eligible only through the existing exact `vegetable_fungi` entry rather than a new broad shortcut.

The generic Chef profiles exclude hide, bone, chitin, wool, fiberplast, petrochemicals, chemicals, metals, minerals, ores, gases, crystalline resources, and gemstones. Core3 does contain individual Chef recipes that use some of these unusual inputs. Those recipes require future explicitly enabled item/component profiles; their existence is not grounds for admitting the entire family into generic Chef demand.

False positives fixed:

- Hide, bone, and chitin can no longer enter Chef demand through `organic`.
- Reactive gas can no longer enter generic Chef demand merely because a niche drink uses it.
- Seafood matches are explained as `eligibleFamily=seafood`, not meat.
- Egg resources are explained as `eligibleFamily=egg`, not generic meat.
- Beans, tubers, and greens retain their specific family names where that class-chain information is available.

#### Other profile tightening

- `composite_armor_supply` remains limited to documented composite exact types, steel, aluminum, fiberplast, wooly hide, inert petrochemical, amorphous gemstone, and its existing metal/ore bulk tier. Generic hide, bone, chitin, and organic resources are not admitted.
- Weaponsmith profiles retain their curated exact component resources and bounded metal/component families. Logs now expose the actual matched family, such as copper, steel, aluminum, iron, petrochemical, gemstone, ore, gas, or the qualified exact type.
- `production_infrastructure` continues to treat steel and metal as quality-sensitive inputs. Ore, chemical, inert gas, and reactive gas are explicitly bulk-eligible. Architect bulk matches use a constant supply signal rather than weighted resource quality, so a high-stat bulk gas or chemical is not presented as a server-best quality recommendation.

#### Explainability

Demand recommendation reasons now distinguish:

```text
reason=exactType=copper_polysteel; weighted OQ=.../SR=...; premiumQuality=true; bulkEligible=false
```

from:

```text
reason=eligibleFamily=seafood; weighted PE=.../FL=...; premiumQuality=true; bulkEligible=false
```

and quality-neutral Architect bulk supply:

```text
reason=eligibleFamily=chemical; bulk supply; quality not scored; premiumQuality=false; bulkEligible=true
```

Exact-type matching and family matching choose the most specific configured match. `premiumQuality` and `bulkEligible` continue to state which eligibility tier admitted the resource.

`componentSlot` is not logged in this phase because the six current profiles aggregate several component slots and cannot name one honestly without splitting the profile. Slot-level explainability belongs with future component-specific profiles.

#### Doctor profile status

The proposed `doctor_stim_packs`, `doctor_wound_packs`, `doctor_buffs`, and `doctor_cures` profiles are intentionally deferred. Adding them safely requires:

1. Selecting the concrete high-use medicine schematics for this server.
2. Following their delivery, duration, biologic-effect, dispersal, resilience, and infection-amplifier component chains.
3. Assigning the current Core3 `OQ`, `PE`, `UT`, `DR`, and `CD` weights per product or component slot.
4. Adding each profile to startup configuration and live reload as disabled-by-default policy.

Deferral avoids replacing the Chef `organic` false positive with an equally broad Doctor `organic`/`inorganic` shortcut.

Safety boundaries remain unchanged:

- `demandProfileSimulationConfig.enabled` remains `false`.
- No resource or `ResourceContainer` is created.
- No inventory, vendor, bazaar, auction, crafting output, harvester, credit, or persistence path is used.
- No Smart Doctor, entertainer, chat, or LLM file is involved.
- No IDL or player-facing behavior is changed.

### D.6.2 - Demand-State Model

D.6.2 adds a disabled-by-default manager task that compares desired profile reserves with the limited supply information currently available and the best active resource opportunity from the tightened D.6.1.1 matcher. It produces diagnostic pressure scores only. It does not write demand state to controllers, miners, resources, inventories, or persistence.

Configuration lives in `bin/scripts/managers/sim_player_manager.lua`:

```lua
demandStateSimulationConfig = {
    enabled = false,
    intervalSeconds = 300,
    logTopN = 3,
    supplyMode = "conceptual_totals",
    activeOpportunityWeight = 1.0,
    shortageWeight = 1.0,
    surplusDampening = 0.5,
    profiles = {
        composite_armor_supply = {
            enabled = true,
            desiredReserve = 5000,
            lowStockThreshold = 0.35,
            criticalStockThreshold = 0.10,
        },
        -- The other five D.6 demand profiles use the same shape.
    },
}
```

| Field | Default and guardrail |
|---|---|
| `enabled` | `false`; no D.6.2 task is scheduled unless explicitly enabled. |
| `intervalSeconds` | 300, clamped to 30-3600. |
| `logTopN` | 3, clamped to 1-20; results are sorted by descending pressure. |
| `supplyMode` | `conceptual_totals`; other values are ignored. |
| `activeOpportunityWeight` | 1.0, clamped to 0.0-10.0. |
| `shortageWeight` | 1.0, clamped to 0.0-10.0. |
| `surplusDampening` | 0.5, clamped to 0.0-1.0. |
| `profiles.<key>.enabled` | Enables one of the six known D.6 profiles for state calculation. Unknown keys are ignored. |
| `desiredReserve` | Profile reserve target, clamped to 0-100,000,000. Zero produces `state=disabledReserve`. |
| `lowStockThreshold` | Reserve-ratio threshold, clamped to 0.0-1.0. |
| `criticalStockThreshold` | Critical threshold, clamped to 0.0-1.0 and never allowed above `lowStockThreshold`. |

While the task remains enabled, it reloads this configuration at each interval. Reserves, thresholds, weights, profile switches, interval, and `logTopN` can therefore change without restarting. Disabling the task stops rescheduling at the next interval. As with D.6.1, enabling it from a fully stopped state still requires the normal manager/server configuration load.

#### Supply model

D.6.2 reads `SimPlayerManager::conceptualMinerTotals` through the existing short mutex-protected snapshot. It does not alter how SimMiner yields are generated or recorded.

The current totals are labels such as `copper`, `iron`, `gas`, and `water`, not real `ResourceSpawn` identities. Mapping is deliberately narrow:

| Demand profile | Conceptual labels used as coarse hints |
|---|---|
| `composite_armor_supply` | `copper`, `iron` |
| `master_weaponsmith_staples` | `copper`, `iron` |
| `high_damage_weapon_components` | `copper`, `iron` |
| `chef_buff_foods` | `water` |
| `chef_high_value_consumables` | `water` |
| `production_infrastructure` | `copper`, `iron`, `gas` |

This mapping does not claim that conceptual iron is Kiirium steel, that copper is Polysteel, or that water represents all Chef inputs. It is only a broad family hint:

- `supplyConfidence=coarse_family` when one or more mapped live conceptual totals exist.
- `supplyConfidence=conceptual_label` when the only known supply is gated persistent conceptual baseline supply.
- `supplyConfidence=none` when no meaningful conceptual label exists for the profile.
- `exact_type` is reserved for a future inventory model and is not emitted in D.6.2.

Each line exposes:

- `aiConceptualSupply`
- `marketObservedSupply=0`
- `persistentStockpileSupply=0` unless the separate C.3.3/C.3.4 gate is enabled and ready
- `totalKnownSupply`
- `supplyLabels`

The zero-valued market and persistent fields make missing supply sources explicit. SimMiner totals are one signal, not the whole economy. Live SimMiner totals still reset on server restart; C.3.3/C.3.4 can separately report validated durable baseline conceptual lots when explicitly enabled.

#### Reserve state and pressure

For a positive desired reserve:

```text
reserveRatio = totalKnownSupply / desiredReserve
shortageUnits = max(desiredReserve - totalKnownSupply, 0)
surplusUnits = max(totalKnownSupply - desiredReserve, 0)
```

State selection is:

1. `critical` when `reserveRatio <= criticalStockThreshold`.
2. `low` when `reserveRatio <= lowStockThreshold`.
3. `target` when supply remains below reserve but above the low threshold.
4. `surplus` when supply meets or exceeds reserve.
5. `disabledReserve` when `desiredReserve` is zero.

The active opportunity is the highest eligible active `ResourceSpawn` under the existing D.6.1.1 profile definition. It uses baseline demand scoring with the same exact-type, premium-family, bulk-family, stat, and explainability rules. Profile phase applicability follows the D.6.1 `serverPhase`. Resource metadata is copied under the existing short `ResourceManager` lock before scoring.

Pressure is intentionally simple:

```text
shortagePressure =
    (1.0 - min(reserveRatio, 1.0)) * 1000 * shortageWeight

opportunityPressure =
    activeOpportunityScore * activeOpportunityWeight

pressureScore =
    opportunityPressure * surplusDampening       when state=surplus
    shortagePressure + opportunityPressure       otherwise
```

`disabledReserve` has zero pressure. The formula is diagnostic rather than economic truth.

Example output:

```text
DemandStateSimulation profile=chef_buff_foods state=critical desiredReserve=5000 aiConceptualSupply=450 marketObservedSupply=0 persistentStockpileSupply=0 totalKnownSupply=450 supplyMode=conceptual_totals supplyConfidence=coarse_family supplyLabels=water=450 reserveRatio=0.09 shortageUnits=4550 activeOpportunityScore=640 pressureScore=1550 activeResource=Ssavvei type=seafood_fish_dantooine premiumQuality=true bulkEligible=false reason="critical reserve; active eligibleFamily=seafood; weighted PE=.../FL=..." mode=log-only
```

When no active resource snapshot is available, D.6.2 logs one compact warning and still reports reserve pressure from the conceptual supply snapshot with `activeOpportunityScore=0`. When a profile has no eligible active resource, its reserve state remains visible without inventing an opportunity.

Safety boundaries:

- No controller, `AiAgent`, target, patrol point, blackboard, movement, density, path, survey, sample, timing, or yield state is read or changed.
- No `ResourceContainer`, resource, inventory item, vendor listing, bazaar/auction entry, crafted output, harvester output, credit transfer, or persistence record is created.
- Conceptual totals and active-resource metadata are copied in separate short lock scopes. Pressure calculation and logging occur after those locks are released.
- The task owns no raw delayed controller, agent, or resource pointer.
- D.6.2 remains disconnected from D.3/D.4 miner recommendation and plan selection.

Known limitations:

- Multiple profiles can count the same coarse conceptual label because no exact inventory allocation exists yet.
- Conceptual supply has no quality, spawn identity, location, expiration, consumption, reservation, or ownership.
- `persistentStockpileSupply` remains zero unless C.3.3/C.3.4 is enabled and `AiEconomyManager` has validated startup-baseline conceptual lots. `marketObservedSupply` remains zero unless the separate D.6.4 observer is enabled and has produced a snapshot.
- Active opportunity represents the best currently active eligible resource, not density, pathability, acquisition cost, or obtainable quantity.
- Reserve targets are administrator policy and do not yet respond to real production or consumption.

Subsequent phases added D.6.5 stockpile research, D.6.5.1 stockpile-shaped diagnostics, and the separate D.6.6 demand-weighted plan simulator. D.6.2 remains the shared pressure formula and does not itself assign miner plans.

### D.6.4 - Market Supply Read-Only Integration

D.6.4 populates the existing D.6.2 `marketObservedSupply` field from public resource listings. It is a disabled-by-default observation task and a manager-owned primitive snapshot. It does not buy, sell, retrieve, reprice, transfer, split, merge, expire, create, or delete any listing or resource.

Configuration lives in `bin/scripts/managers/sim_player_manager.lua`:

```lua
marketSupplyObservationConfig = {
    enabled = false,
    intervalSeconds = 300,
    maxListingsScanned = 5000,
    includeBazaar = true,
    includePlayerVendors = true,
    includeVendorStockrooms = false,
    includePlayerInventory = false,
    includePrivateContainers = false,
    minQuantity = 1,
    logTopN = 5,
}
```

| Field | Default and guardrail |
|---|---|
| `enabled` | `false`; no market observation task is scheduled unless explicitly enabled. |
| `intervalSeconds` | 300, clamped to 60-3600. |
| `maxListingsScanned` | 5,000, clamped to 100-50,000. The current scan visits bazaar lists before player-vendor lists. |
| `includeBazaar` | Includes public `FORSALE` listings from `AuctionsMap::getBazaarTerminalData`. |
| `includePlayerVendors` | Includes public `FORSALE` listings from `AuctionsMap::getVendorTerminalData`. |
| `includeVendorStockrooms` | `false`; stockroom scanning is not implemented. Setting it true only reports it as deferred. |
| `includePlayerInventory` | `false`; private player inventory is never scanned in this phase. |
| `includePrivateContainers` | `false`; private containers are never scanned in this phase. |
| `minQuantity` | 1, clamped to 1-100,000,000. |
| `logTopN` | 5, clamped to 1-20 profile summaries per observation interval. |

While enabled, the task reloads this config at each observation interval. Disabling it clears the manager's observation snapshot and stops rescheduling, restoring D.6.2 to `marketObservedSupply=0`. Enabling a fully stopped observer still requires the normal manager/server configuration load.

#### Sources and snapshot flow

Core3 represents both bazaar sales and player-vendor sales through `AuctionManager`, `AuctionsMap`, and terminal-specific `TerminalItemList` collections. D.6.4 uses those existing public-sale indexes:

1. Obtain the galaxy-level bazaar and/or player-vendor terminal lists under the existing `AuctionsMap` accessors.
2. Copy managed `AuctionItem` references from each terminal list under its read lock.
3. Copy only `FORSALE` listing primitives: item ID, vendor ID, owner ID, listed price, and bazaar/vendor source.
4. Resolve the listed scene object through `ZoneServer::getObject`, which uses the existing object broker. Missing or unavailable objects are skipped; the observer does not load objects from a database itself.
5. Accept only `ResourceContainer` objects meeting `minQuantity`.
6. Copy quantity and a strong `ResourceSpawn` reference under the resource-container lock, release it, then copy resource name, exact type, class chain, stats, and shift metadata under a separate resource lock.
7. Resolve the public terminal's planet when its vendor/bazaar object and zone are available.
8. Release all game-object locks before profile matching, aggregation, sorting, median calculation, or logging.

The observation cache contains only per-profile primitive/string aggregates. It retains no `AuctionItem`, vendor, `ResourceContainer`, or `ResourceSpawn` pointer.

Only public sale listings are observed. The following are excluded:

- Vendor stockrooms and expired/sold/retrieved items.
- Offers to vendors or private offers.
- Player inventories, bank contents, private containers, factory crates, and harvester hoppers.
- Listings whose scene object is unavailable or is not a `ResourceContainer`.
- Any non-resource listed item.

Despawned resources can remain valid market stock. Therefore, a listed container's retained `ResourceSpawn` metadata is eligible for market matching even when that spawn is no longer active. This is separate from D.6.2 active-opportunity scoring, which still inspects active resource shifts only.

#### Profile matching and confidence

Each observed resource row is evaluated through the same tightened D.6.1.1 profile matcher used by demand simulation:

- An exact curated type match contributes quantity with `marketSupplyConfidence=exact_type`.
- A premium or bulk family match contributes quantity with `marketSupplyConfidence=coarse_family`.
- No eligible match contributes no supply to that profile.
- If a profile aggregate contains both family and exact matches, its confidence is `exact_type`.

The same listed quantity can contribute to multiple demand profiles when the resource is eligible for each. This is demand visibility, not inventory reservation or allocation.

D.6.2 combines the latest observation snapshot as:

```text
totalKnownSupply =
    aiConceptualSupply
    + marketObservedSupply
    + persistentStockpileSupply
```

Overall `supplyConfidence` prefers `exact_type`, then `coarse_family`, then `none`. When market observation is disabled or no snapshot has run, all market fields remain zero, `none`, or `unavailable`, and the original D.6.2 conceptual-supply behavior is preserved.

Additional D.6.2 fields include:

- `marketListingsMatched`
- `marketQuantityMatched`
- `marketSupplyConfidence`
- `marketCheapestPricePerUnit`
- `marketMedianPricePerUnit`
- `marketTopResource`
- `marketTopType`

The cheapest and median values use the listing price divided by container quantity. The median is an unweighted median across matching listings. These are advertised/current listing prices, not completed sale prices, and an auction listing's price may not represent a final buyout value. Missing or invalid price information is logged as `unavailable`.

Example observation output:

```text
MarketSupplyObservation enabled=true listingsScanned=1240 resourceContainersObserved=42 matchedResourceListings=37 totalQuantity=48250 sources=bazaar,player_vendor mode=read-only
MarketSupplyObservation profile=high_damage_weapon_components matchedListings=4 matchedQuantity=12000 confidence=exact_type topResource=Gowane type=copper_polysteel cheapestPPU=8.5 medianPPU=12 mode=read-only
```

Example demand-state output:

```text
DemandStateSimulation profile=high_damage_weapon_components state=surplus desiredReserve=3000 aiConceptualSupply=223 marketObservedSupply=12000 marketListingsMatched=4 marketQuantityMatched=12000 marketSupplyConfidence=exact_type persistentStockpileSupply=0 totalKnownSupply=12223 supplyConfidence=exact_type reserveRatio=4.074 shortageUnits=0 surplusUnits=9223 activeOpportunityScore=747 pressureScore=373.5 marketCheapestPricePerUnit=8.5 marketMedianPricePerUnit=12 marketTopResource=Gowane marketTopType=copper_polysteel reason="reserve met by known supply including observed market supply; active opportunity dampened" mode=log-only
```

Safety boundaries:

- No miner/controller state, target, route, density result, survey/sample timing, or conceptual yield is read or changed.
- No resource or `ResourceContainer` is created, modified, split, merged, transferred, or destroyed.
- No inventory, vendor, bazaar, auction, stockroom, harvester, crafting, credit, or persistence mutation is called.
- No private holdings are scanned.
- The delayed task is manager-owned and captures no raw object/controller pointer.
- Market supply changes only diagnostic reserve and pressure logs. It remains disconnected from D.3/D.4 miner recommendations and all miner behavior.

Known limitations:

- The hard listing cap is global and bazaar-first, so very large bazaar populations can consume the cap before player vendors are visited.
- The snapshot is interval-based and can become stale between scans.
- Unavailable listed scene objects are skipped rather than loaded through a new persistence path.
- Price statistics do not model completed sales, listing age, taxes, auction bids, price manipulation, or quantity-weighted median.
- Profile overlap can count one listing in several profile views.
- No stockroom, private inventory, historical-sale, scarcity, or velocity signal exists yet.
- `persistentStockpileSupply` remains zero unless the separate C.3.3/C.3.4 persistent stockpile demand gate is enabled and ready. Public market observation never becomes AI-owned stockpile.

D.6.5 subsequently documented persistent stockpile boundaries, D.6.5.1 added a memory-only stockpile-shaped snapshot, and D.6.6 added separate demand-weighted plan logs. All remain diagnostic; actual miner behavior changes remain deferred.

### D.6.5 - Persistent AI Stockpile Research/Design

D.6.5 defines what durable AI-owned inventory should mean before any persistence code is written. The stockpile is an internal accounting model for goods owned or controlled by the AI economy. It is not a disguised player inventory, vendor, bazaar, harvester, or `ResourceContainer` system.

The design goal is restart-stable economic continuity:

- Completed AI production should not disappear merely because Core3 restarts.
- Resource identity and quality should remain meaningful after the source `ResourceSpawn` leaves shift.
- Demand-state calculations should distinguish AI-owned supply from public market supply and currently active gathering opportunities.
- Recovery must never manufacture player-visible resources or mutate the live economy.
- The data model should support future miners, crafters, vendors, consumers, services, factions, and guild-like AI organizations without coupling durable state to one transient NPC.

This section records the D.6.5 research and architecture guidance. The later C.1/C.2 bootstrap now provides an empty schema object, dedicated database, and load-only manager, but D.6.5 itself still adds no stockpile contents or economy behavior.

#### AI-owned stockpile boundary

An entry belongs in the future persistent AI stockpile only after the AI economy has completed an acquisition or production event and accepted ownership of the output.

| State or asset | AI-owned stockpile? | Rationale |
|---|---|---|
| Completed conceptual SimMiner output | Yes, after a future durable accounting boundary is implemented | It is completed AI production, although current labels such as `iron` and `copper` provide only low-confidence family identity. |
| Future AI harvester output | Yes | Completed AI-controlled extraction should become owned supply after the extraction transaction succeeds. |
| Future AI-crafted intermediate components | Yes | Components may be reserved or consumed by later production stages and must survive crafter/controller replacement. |
| Future AI-crafted finished goods held for later allocation | Yes | They are AI-owned output even before assignment to a vendor or consumer. |
| Future AI vendor reserve inventory | Yes | Goods intentionally held by an AI vendor remain AI-owned until a later controlled sale transfers ownership. |
| Future AI crafter or consumer reserved inputs | Yes, with reservation metadata | Reservation changes availability, not ownership. Reserved units remain in the stockpile until consumed or transferred. |
| Future controlled market purchases | Yes, after a successful purchase boundary | A purchase should enter stockpile only after the future transaction reports success. |
| Administrative seed inventory | Yes, when explicitly identified and audited | Seeded supply needs a distinct acquisition source so it cannot be mistaken for mined or purchased production. |
| Current in-progress miner survey, travel, sample, or pending yield | No | Work in progress may be retried or lost after restart. Only completed output should cross the durable boundary. |
| Active `ResourceSpawn` opportunity | No | A spawn is available knowledge, not owned inventory. |
| Public bazaar or player-vendor listing | No | D.6.4 observes public supply that another owner may remove or sell at any time. |
| Player inventory, bank, or private container | No | Player-owned assets are outside the AI economy ownership boundary. |
| Vendor stockroom contents not explicitly owned by an AI vendor model | No | Existing stockrooms are player-facing market state and must not be repurposed as AI persistence. |
| Harvester hopper contents | No | Hopper state belongs to the existing installation/owner lifecycle until a future explicit AI acquisition transaction transfers output. |
| Real resource containers encountered in the world | No | Observation or reference does not imply AI ownership. |

Ownership should be manager-scoped rather than controller-scoped. A transient SimMiner, SimCrafter, or vendor actor may report a completed event, but destroying or recycling that actor must not destroy authoritative stockpile state.

#### Recommended stockpile entry data

A future stockpile entry should preserve both inventory accounting and a snapshot of the resource identity needed to value it later. A `ResourceSpawn` object ID alone is insufficient because the spawn can leave shift and future code must not depend on resolving a live object to understand historical stock.

Recommended resource-entry fields:

| Field | Purpose |
|---|---|
| `entryId` | Stable AI-economy identifier for audit, migration, reservation, and correction. It should not be a player-facing object ID. |
| `resourceSpawnObjectId` | Source `ResourceSpawn` object ID when known. Zero or absent is valid for conceptual/admin/future crafted entries. |
| `resourceSpawnName` | Generated spawn name, retained after despawn. |
| `resourceType` | Exact Core3 resource type such as `copper_polysteel` when known. |
| `resourceClassChain` | Snapshot of the type/class/STF ancestry used for future eligibility matching. |
| `resourceStats` | Snapshot of available `OQ`, `CD`, `DR`, `HR`, `FL`, `MA`, `PE`, `SR`, `UT`, and `CR` values. Missing stats remain missing rather than being treated as zero-quality or perfect. |
| `conceptualLabel` | Original broad label such as `copper`, `iron`, `gas`, or `water` when exact spawn identity is unavailable. |
| `quantity` | Total owned quantity in the entry. Must be non-negative and bounded. |
| `reservedQuantity` | Total quantity committed but not consumed. Must remain between zero and `quantity`. |
| `availableQuantity` | Derived as `quantity - reservedQuantity`; it should not be separately authoritative unless required for serialization compatibility. |
| `reservations` | Bounded records keyed by reservation or owner/profile identity, with quantity and purpose. This prevents one unit from being promised to multiple future roles. |
| `sourcePlanet` / `sourceZone` | Acquisition location when known. Useful for provenance, faction policy, logistics, and audit. |
| `acquisitionSource` | Enumerated origin such as `conceptual_miner`, `future_ai_harvester`, `market_purchase`, `admin_seed`, or `future_ai_crafter`. |
| `acquiredTimestamp` | Time at which AI ownership began. |
| `lastUpdatedTimestamp` | Last quantity, reservation, metadata, or ownership change. |
| `activeAtAcquisition` | Whether the associated resource was active when acquired. |
| `resourceLifecycleState` | Current interpretation such as `active`, `inactive`, `despawned`, `conceptual`, or `unknown`. This is metadata, not a command to the resource system. |
| `matchedDemandProfiles` | Demand profiles the entry matched when acquired or last evaluated. These should be treated as cached explanations, not permanent truth. |
| `identityConfidence` | `exact_type`, `coarse_family`, `conceptual_label`, or `unknown`. |
| `ownerScope` | Ownership partition such as `galaxy`, profession, AI faction, AI guild, AI vendor, or AI crafter. |
| `qualityTier` | Optional explainable classification such as bulk/good/premium/server-best under a named scoring version. |
| `profileScoreSnapshot` | Optional bounded profile-key/score/reason snapshot for historical audit. Current scoring should be recomputable; the snapshot records why acquisition looked valuable at that time. |
| `schemaVersion` / `scoringVersion` | Version context required to migrate serialized entries and interpret historical cached classifications. |

Future crafted-item stockpile entries will need a related but distinct shape:

- Crafted template/component identity rather than `ResourceSpawn` identity.
- Quantity and reservation fields using the same accounting rules.
- Input provenance or batch references only when useful for audit.
- Quality/experimental result snapshots appropriate to the crafted item.
- No assumption that every crafted good maps to a resource stat vector.

Resource and crafted-item entries should share ownership, quantity, reservation, timestamps, and acquisition-source concepts without forcing unlike assets into one ambiguous type.

#### Relationship to ResourceSpawn and ResourceContainer

The persistent AI stockpile should be an abstract manager-owned ledger, not a collection of fake world objects.

- A stockpile entry may reference a real `ResourceSpawn` by object ID and exact type.
- The entry must copy spawn name, type hierarchy, stats, source zones, and acquisition-time lifecycle metadata while that information is safely available.
- The copied metadata remains authoritative for historical stockpile interpretation after the live spawn despawns or becomes unavailable.
- A future active-resource refresh may update only lifecycle interpretation and other explicitly mutable intelligence fields. It must not silently replace the acquisition-time stat snapshot.
- Two spawns of the same resource type but different generated names/stats should normally remain separate lots until a deliberate aggregation policy proves they are interchangeable.
- Conceptual-label entries must not be upgraded to exact resources merely because a compatible spawn is currently active. That would invent provenance and quality.

The following are explicitly rejected as persistence backends:

- Creating real or hidden `ResourceContainer` objects solely to represent the ledger.
- Placing AI stock in player inventory, banks, structures, vendor stockrooms, factories, or harvester hoppers.
- Publishing stock as bazaar/vendor listings merely to make it durable.
- Treating `ResourceSpawn` persistence as inventory persistence.

A later, deliberate economy bridge may materialize a bounded quantity from the ledger into real game objects. That operation would require its own transactional design, ownership checks, audit trail, rollback behavior, and player-facing safeguards. It is outside D.6.5.

#### Feeding D.6.2 demand-state

The future stockpile manager should expose a read-only primitive snapshot to D.6.2. Demand-state should not iterate or lock the authoritative persisted object while scoring active resources or logging.

Future supply composition remains:

```text
totalKnownSupply =
    aiConceptualSupply
    + marketObservedSupply
    + persistentStockpileSupply
```

The signals have different meanings:

| Signal | Ownership | Durability | Interpretation |
|---|---|---|---|
| `aiConceptualSupply` | AI-produced hint, but currently only manager memory | Resets on restart | Low-detail supply from current conceptual SimMiner totals. |
| `marketObservedSupply` | Not AI-owned | Public listing snapshot only | Supply that may be purchasable but can disappear without AI action. |
| `persistentStockpileSupply` | AI-owned | Intended to survive restart | Durable quantity available or reserved for the relevant profile under explicit ownership rules. |
| Active resource opportunity | Not owned | Current resource shift only | A gathering opportunity score, not supply. It must not be added to `totalKnownSupply`. |

Recommended confidence precedence:

1. `exact_type` when the stockpile entry preserves an exact resource type and stat snapshot sufficient for the current profile matcher.
2. `coarse_family` when only a validated family/class match is available.
3. `conceptual_label` for broad legacy entries such as `iron` or `water`.
4. `unknown` or `none` when the entry cannot be matched responsibly.

An exact-type persistent entry is stronger evidence than a coarse conceptual total. It does not make market supply AI-owned, and it does not prove that an active spawn is still gatherable.

Reservation semantics must prevent false abundance:

- `quantity` reports total AI ownership.
- `reservedQuantity` reports committed supply.
- `availableQuantity` reports supply available for new allocations.
- A profile may count units reserved specifically for that same profile as covered reserve, but it must not offer those units to another profile.
- Independent D.6.2 profile views may continue to show overlapping eligibility for diagnosis, but a future allocation phase must resolve competition before any consumption or production decision.
- Demand-state logs should eventually expose `persistentOwnedSupply`, `persistentReservedSupply`, `persistentAvailableSupply`, stockpile confidence, and matched lot count rather than hiding reservation pressure in one number.

`persistentStockpileSupply` should remain zero until a gated implementation can load a validated stockpile snapshot. C.3.3/C.3.4 provides the first conceptual-baseline-only version of that gate; future exact stockpile models still need richer identity, reservation, and confidence handling. Invalid or unavailable durable state must not be replaced with invented supply.

#### Restart and recovery behavior

Stockpile recovery must complete before demand-state, demand-weighted planning, AI crafting, vendor, or consumer tasks can treat persistent supply as available.

Recommended startup sequence:

1. Initialize Core3 object databases through the normal server lifecycle.
2. Load the dedicated AI economy/stockpile data object.
3. Validate top-level schema version and ownership partitions.
4. Parse entries into a temporary recovery snapshot rather than mutating the live manager incrementally.
5. Validate identifiers, metadata sizes, timestamps, quantities, reservations, confidence values, and enum fields.
6. Preserve valid despawned-resource metadata without requiring the source `ResourceSpawn` to resolve.
7. Quarantine or reject invalid entries with compact audit diagnostics.
8. Publish one immutable/read-only runtime snapshot only after validation completes.
9. Mark stockpile recovery ready.
10. Allow D.6.2 and later AI economy tasks to start.

Validation rules should include:

- Reject negative quantities.
- Clamp or quarantine absurd quantities above a documented upper bound rather than allowing overflow.
- Reject `reservedQuantity > quantity`, or repair it only under an explicit, logged migration rule.
- Bound class chains, profile lists, reservation lists, strings, and historical score snapshots.
- Reject unknown future schema versions unless a compatible reader is explicitly available.
- Preserve unknown-but-bounded metadata only if forward-compatibility rules are defined; otherwise quarantine it.
- Treat a missing live `ResourceSpawn` as normal for historical stock, not corruption.
- Never infer exact type, stats, or provenance from a conceptual label during recovery.

Failure behavior should be fail-closed:

- A missing stockpile can initialize as empty only when policy explicitly permits first-run creation.
- A corrupt or incompatible existing stockpile should not be silently replaced with empty state, because that would destroy durable economic history.
- If authoritative data is invalid, disable persistent stockpile contribution and dependent mutation features while leaving unrelated Core3 gameplay operational.
- Demand-state may continue with `persistentStockpileSupply=0` and a clear `stockpileStatus=invalid/unavailable` diagnostic.
- Recovery must never synthesize real resources, create containers, publish listings, transfer credits, or modify player-facing state.
- Recovery should not block normal server startup indefinitely; validation needs bounded work and clear error reporting.

Orderly shutdown may later request a final save, but crash safety must not depend solely on shutdown hooks. The eventual implementation should define an acceptable bounded loss window and use Core3's normal dirty/update/commit lifecycle.

#### Persistence mechanism comparison

| Option | Advantages | Risks and disadvantages | Recommendation |
|---|---|---|---|
| Dedicated manager-owned persisted `ManagedObject` and object database | Follows the existing FRS-style manager-data precedent; uses Core3 object IDs, generated serialization, dirty/update/commit flow, and recovery lifecycle; keeps transient actors separate from durable ownership. | Requires a future IDL object, database registration, versioning, migration, lock discipline, and recovery tooling. | Preferred future implementation. |
| Manager-owned JSON file | Human-readable and easy to prototype outside the object schema. | Requires custom atomic writes, fsync/rename policy, backups, path/permission handling, concurrent access rules, corruption recovery, and migration code outside normal Core3 persistence. | Useful only as an export/debug format or temporary prototype, not authoritative state. |
| MySQL table | Easy external reporting and SQL queries; explicit relational schema. | Adds schema migrations and a second authoritative persistence style for in-game state; raises transaction, deployment, and operational coupling concerns. | Possible future analytics mirror, not the first stockpile authority. |
| Real `ResourceContainer` objects | Naturally carries exact resource identity and quantity and can integrate with existing gameplay later. | Prematurely materializes conceptual AI state, couples durability to object/container lifecycles, risks player visibility and duplication, and cannot cleanly represent reservations or aggregate ownership without more objects. | Explicitly not recommended for D.6.5 implementation. |
| Player inventory, vendor, bazaar, factory, or harvester objects | Reuses existing persisted player-facing systems. | Violates ownership boundaries, can affect gameplay, creates cleanup and permission hazards, and makes AI recovery dependent on unrelated object lifecycles. | Never use as the stockpile persistence backend. |

The likely future shape is:

- A transient `AiEconomyManager` or focused stockpile service owns locking, validation, snapshots, and APIs.
- One dedicated persisted data object owns durable schema-versioned stockpile state.
- SimPlayer and future role controllers submit completed primitive acquisition/consumption events through narrow manager methods.
- Demand-state receives copied read-only aggregate rows and never receives mutable persisted objects.
- Player-facing materialization, if ever added, is a separate boundary and transaction rather than an incidental side effect of persistence.

This recommendation refines the broader Phase C persistence research. It does not authorize implementation yet.

#### Future API boundary

The eventual manager API should be narrow, transactional in intent, and independent of `AiAgent` or controller pointers. Illustrative methods:

```text
addStockpile(resourceMetadata, quantity, acquisitionSource, ownerScope)
reserveStockpile(profileKey, quantity, reservationOwner)
releaseReservation(reservationId, quantity)
consumeStockpile(reservationId, quantity, consumptionReason)
transferStockpile(entryId, quantity, destinationScope)
snapshotStockpileForDemandState()
auditStockpile(filter)
```

Design rules:

- Inputs should be copied primitive/string metadata and quantities, not delayed raw `ResourceSpawn`, `ResourceContainer`, controller, or `AiAgent` pointers.
- Mutation methods should return explicit success/failure and the affected entry/reservation identifier.
- Validation and accounting should happen under one small stockpile ownership lock or another clearly documented lock hierarchy.
- Persistence dirtying should occur after a valid in-memory mutation without calling player-facing systems.
- Logging, scoring, active-resource inspection, market inspection, and expensive formatting should occur outside the stockpile lock.
- `snapshotStockpileForDemandState` should return immutable aggregate data and confidence fields, not the authoritative mutable map.
- `auditStockpile` must be read-only and bounded.
- Consumption must not permit negative totals, double consumption, or consumption beyond a reservation without an explicit policy.
- Idempotency/event identifiers should be considered before miner or crafter tasks can retry completed operations.

No API listed here exists yet.

#### Explicit non-goals

D.6.5 does not:

- Change C++ or Lua runtime behavior.
- Change Lua configuration behavior.
- Add or modify IDL.
- Register an object database.
- Add save, load, migration, or recovery code.
- Persist conceptual miner totals.
- Create or modify `ResourceContainer` objects or real resources.
- Read or mutate player inventory, banks, private containers, vendor stockrooms, bazaar/auction listings, factories, harvester hoppers, crafting output, credits, or player-facing economy state.
- Change SimMiner targeting, movement, destination selection, conceptual resource selection, survey/sample timing, density simulation, path validation, yield amount, or yield accounting.
- Feed stockpile pressure into miner plans or behavior.

#### Implemented follow-up

**D.6.5.1 - Log-Only Stockpile Snapshot Simulation** is implemented below as the safe follow-up to this design.

The phase remains memory-only and adds no persistence. It:

- Project current conceptual totals into stockpile-shaped rows with `identityConfidence=conceptual_label`.
- Optionally display D.6.4 market observations beside the rows while clearly marking them `owned=false`; market quantity must not be imported into AI stockpile.
- Exercise proposed field names, ownership scopes, confidence precedence, lifecycle labels, quantity bounds, and reservation/availability calculations.
- Produce copied read-only snapshots for D.6.2 without changing `persistentStockpileSupply` unless the simulation logs a separate hypothetical value.
- Validate that exact-type, coarse-family, and conceptual entries produce understandable demand-state explanations.
- Test restart expectations only as documented scenarios; all simulated rows would still disappear on restart.

The later C.1/C.2 bootstrap defines the IDL shape, object database registration, and load-only recovery proof. C.3.1 now checkpoints the same underlying conceptual totals into authoritative aggregate lots, but it does not import D.6.5.1 market-reference rows or alter demand-weighted planning.

### D.6.5.1 - Log-Only Stockpile Snapshot Simulation

D.6.5.1 implements the memory-only diagnostic proposed by D.6.5. It exercises the future stockpile vocabulary without creating an authoritative stockpile, persistence schema, object database, resource object, or player-facing asset.

Configuration lives under `SimPlayerManagerConfig.stockpileSnapshotSimulationConfig`:

```lua
stockpileSnapshotSimulationConfig = {
    enabled = false,
    intervalSeconds = 300,
    logTopN = 10,
    includeConceptualMinerTotals = true,
    includeMarketObservation = false,
}
```

| Field | Behavior |
|---|---|
| `enabled` | Disabled by default. The manager schedules no stockpile snapshot task unless both this field and the SimPlayerManager master switch are enabled. |
| `intervalSeconds` | Logging interval, clamped to 30-3600 seconds. |
| `logTopN` | Maximum combined owned-lot and market-reference rows logged per interval, clamped to 1-20. |
| `includeConceptualMinerTotals` | Projects the current memory-only conceptual totals into simulated owned lots. |
| `includeMarketObservation` | Copies the existing D.6.4 aggregate market snapshot into non-owned reference rows. It does not scan the market itself or import market quantity into stockpile. |

The manager-owned task reloads this config at each interval while it is running. Disabling the block stops rescheduling. Enabling it from a fully stopped state still requires the normal manager/server reload, matching the other optional simulation tasks. The task maintains no cache of its own, so disabling it leaves no simulated stockpile state to clear.

#### Simulated owned rows

Each non-zero conceptual total becomes one local stockpile-shaped row:

- `owned=true`
- `conceptualLabel=<iron|gas|water|copper|configured label>`
- `identityConfidence=conceptual_label`
- `acquisitionSource=conceptual_miner`
- `resourceLifecycleState=conceptual`
- `ownerScope=galaxy`
- `quantity=<current total>`
- `reservedQuantity=0`
- `availableQuantity=quantity`
- `persisted=false`

Conceptual totals are copied while holding only `conceptualMinerTotalsMutex`. Sorting, formatting, and logging occur after that lock is released. Rows contain strings and primitive quantities only; they retain no controller, `AiAgent`, `ResourceSpawn`, `ResourceContainer`, auction, or vendor object pointer.

Example:

```text
StockpileSnapshotSimulation enabled=true simulatedOwnedLots=4 totalOwnedQuantity=1234 identityConfidence=conceptual_label ownerScope=galaxy includeConceptualMinerTotals=true includeMarketObservation=false marketReferenceRows=0 persistentStockpileSupplyChanged=false mode=simulation-only
StockpileSnapshotSimulation lot=1 owned=true conceptualLabel=copper quantity=500 reservedQuantity=0 availableQuantity=500 acquisitionSource=conceptual_miner resourceLifecycleState=conceptual ownerScope=galaxy identityConfidence=conceptual_label persisted=false mode=simulation-only
```

These rows are projections of the live session totals, not additional accounting records. The rows and live counters disappear on restart. If C.3.1 is separately enabled, authoritative persisted conceptual lots survive independently and are reported by `AiEconomyManager`; D.6.5.1 still does not read those lots.

#### Optional market references

When `includeMarketObservation=true`, the task copies D.6.4's already-aggregated per-demand-profile snapshot under `marketSupplyObservationMutex`, releases the lock, and logs bounded reference rows:

```text
StockpileSnapshotSimulation marketReference=1 owned=false profile=high_damage_weapon_components resource=Gestic type=aluminum_titanium quantity=30000 listings=2 identityConfidence=exact_type source=market_observation imported=false mode=simulation-only
```

The quantity is the aggregate public quantity matched to that demand profile, while `resource` and `type` identify the largest listing represented by the aggregate. Profile overlap means the same public listing may appear in more than one profile reference. These rows are therefore diagnostics, not stockpile lots, ownership claims, or quantities suitable for summing into AI-owned inventory.

If D.6.4 is disabled or has no snapshot, no market-reference rows are produced. The stockpile task does not read `AuctionItem`, `ResourceContainer`, bazaar, vendor, inventory, or private-container objects directly.

#### Demand-state boundary

D.6.5.1 does not modify D.6.2:

- `persistentStockpileSupply` remains zero because this simulation task does not feed reserve calculations. C.3.3/C.3.4 may separately populate it from validated persisted baseline lots when explicitly enabled.
- `aiConceptualSupply` remains the existing coarse memory-only signal.
- `marketObservedSupply` remains owned and populated only by D.6.4.
- No simulated lot is fed into reserve ratios, pressure scores, miner plans, or gameplay.

The summary explicitly logs `persistentStockpileSupplyChanged=false` to make that boundary visible during testing.

#### Safety and limitations

- D.6.5.1 itself adds no save/load or recovery path. C.1/C.2 load the persisted root and C.3.1 may update durable conceptual lots, but this simulation task does not read from or write to either database.
- No real resource or `ResourceContainer` is created, mutated, transferred, or destroyed.
- No player inventory, bank, private container, vendor stockroom, bazaar/auction listing, factory, harvester hopper, crafting output, or credit balance is read or changed by this task.
- SimMiner targeting, movement, destinations, conceptual resource selection, survey/sample timing, density simulation, path validation, yield amount, and yield accounting are unchanged.
- The task is manager-owned and captures no raw delayed controller or game-object pointer.
- The snapshot has only conceptual-label identity and cannot represent exact spawn stats, acquisition planet, timestamps, reservations, or historical despawned lots.
- Market references use D.6.4 profile aggregates rather than individual listing rows and must not be interpreted as AI ownership.
- `logTopN` bounds the combined row output. Owned conceptual lots are logged first, followed by any remaining market-reference slots.

The next step is to observe these rows over several miner and market intervals and confirm that ownership, confidence, reservation, and lifecycle labels remain understandable. After that, **D.6.5.2 - Stockpile Persistence Contract and Recovery Proof** should define and review the schema/version/load boundary before any durable implementation. The implemented D.6.6 demand-weighted miner-plan simulation remains separate and log-only.

### D.6.6 - Demand-Weighted Miner Plan Simulation

D.6.6 adds a second, independent miner-plan simulator driven by D.6.2 demand pressure. It answers which demand profile and active resource each current SimMiner would prioritize if demand-aware planning were enabled later. It does not replace D.4's `MinerTargetSimulation`; the original round-robin simulator remains available under its existing config and log name.

Configuration:

```lua
demandWeightedMinerPlanSimulationConfig = {
    enabled = false,
    intervalSeconds = 300,
    logTopN = 20,
    samePlanetBonus = 150,
    travelPenalty = 100,
    maxMinersPerProfile = 2,
    minimumPressureThreshold = 1.0,
    strongPressureRatio = 1.5,
}
```

| Field | Behavior |
|---|---|
| `enabled` | `false` by default. The task requires the SimPlayerManager master switch and never starts otherwise. |
| `intervalSeconds` | Simulation cadence, clamped to 30-3600 seconds. |
| `logTopN` | Maximum per-miner plan/no-plan lines logged per interval, clamped to 1-100. All active miners are still evaluated before the summary is emitted. |
| `samePlanetBonus` | Added to a profile/resource candidate when the active resource is available in the miner's current zone; clamped to 0-1000. |
| `travelPenalty` | Subtracted when the active resource is not available in the miner's current zone; clamped to 0-1000. |
| `maxMinersPerProfile` | Soft distribution cap, clamped to 1-100. |
| `minimumPressureThreshold` | Profiles below this D.6.2 pressure are excluded; clamped to 0-1,000,000. |
| `strongPressureRatio` | A capped profile may receive another simulated miner only when its raw pressure is at least this multiple of the best uncapped alternative; clamped to 1.0-10.0. |

The task reloads its own config, the D.6.1 server phase, and the D.6.2 reserve/threshold settings into D.6.6-owned copies each interval while running. It does not invoke or mutate D.6.2's live-reload state, so the two periodic tasks can run independently. Disabling the task stops rescheduling. Enabling it from a stopped state still requires normal manager/server configuration loading.

D.6.6 uses the per-profile `enabled` flags inside `demandStateSimulationConfig`. The separate D.6.2 periodic logger does not need to be enabled, but at least one demand-state profile must be enabled with a positive reserve and an eligible active resource before the planner can produce a plan.

#### Inputs

D.6.6 uses the existing read-only inputs:

- The current active SimMiner controller set, copied into primitive rows containing only miner object ID and zone name.
- Memory-only conceptual totals copied under `conceptualMinerTotalsMutex`.
- D.6.4 public-market aggregate quantities copied under `marketSupplyObservationMutex` when market observation is enabled.
- Active `ResourceSpawn` metadata copied through the Resource Intelligence snapshot.
- The six D.6 demand profile definitions and D.6.1.1 eligibility rules.
- D.6.2 desired reserves, thresholds, shortage/opportunity weights, surplus dampening, and profile-enabled flags.

The state and pressure arithmetic is shared with D.6.2:

```text
shortagePressure =
    (1.0 - min(reserveRatio, 1.0)) * 1000 * shortageWeight

opportunityPressure =
    activeOpportunityScore * activeOpportunityWeight

pressureScore =
    opportunityPressure * surplusDampening       when state=surplus
    shortagePressure + opportunityPressure       otherwise
```

The D.6.6 planner still does not import D.6.5.1 simulated rows or C.3.3/C.3.4 persistent baseline supply. Any future demand-weighted planner consumption of persistent stockpile pressure needs a separate gate and review.

#### Selection algorithm

1. Sort active miners by object ID for stable logs.
2. Calculate D.6.2 pressure for every enabled, phase-active demand profile.
3. Exclude profiles with no eligible active resource, a disabled reserve, or pressure below `minimumPressureThreshold`.
4. For each miner and remaining profile, choose the best eligible active resource after applying `samePlanetBonus` or `travelPenalty` to the resource's demand score.
5. Add the same location adjustment to profile pressure.
6. Divide the adjusted pressure by `existingAssignments + 1`. This load-balancing factor makes an equally pressured unused profile more attractive than a profile already assigned several miners.
7. Prefer candidates below `maxMinersPerProfile`.
8. Permit a capped profile to overflow only when its raw pressure is at least `strongPressureRatio` times the best uncapped alternative. If every profile is capped, compare the strongest and second-strongest capped profiles; a sole eligible profile may accept overflow because no alternative exists.
9. Produce one local simulated plan or explicit no-plan result per miner. No plan is retained after logging.

This is deterministic for a stable miner/resource/demand snapshot. It is not a scheduler, reservation, target assignment, or claim on a resource spawn.

Example:

```text
DemandWeightedMinerPlanSimulation miner=281475014066178 zone=naboo selectedProfile=composite_armor_supply pressureScore=1542.0 rawPressureScore=1542.0 locationAdjustedResourceScore=940.0 adjustedPlanScore=1692.0 selectionRank=1 demandState=critical target=Gowane type=copper_polysteel zones=naboo,tatooine resourceDemandScore=790 samePlanet=true travelRequired=false profileAssignmentCount=1 assignmentReason="highest demand pressure; same-planet opportunity" mode=simulation-only
DemandWeightedMinerPlanSimulation miner=281475014066182 zone=tatooine selectedProfile=production_infrastructure pressureScore=1210.0 rawPressureScore=1210.0 locationAdjustedResourceScore=1020.0 adjustedPlanScore=1360.0 selectionRank=1 demandState=low target=Goree type=steel_quadranium zones=tatooine resourceDemandScore=870 samePlanet=true travelRequired=false profileAssignmentCount=1 assignmentReason="load-balanced demand pressure; same-planet opportunity" mode=simulation-only
DemandWeightedMinerPlanSimulation summary activeMiners=4 eligibleProfiles=5 plansProduced=4 plansLogged=4 truncated=false mode=simulation-only
```

The additional diagnostic fields do not participate in selection:

- `rawPressureScore` is the unmodified D.6.2 profile pressure and is equal to the retained `pressureScore` field.
- `locationAdjustedResourceScore` is the selected resource's demand score after the existing same-planet bonus or travel penalty.
- `adjustedPlanScore` is the existing load-balanced candidate score used for normal candidate comparison: location-adjusted profile pressure divided by `existingAssignments + 1`.
- `selectionRank` is calculated after selection by ranking the selected candidate's existing `adjustedPlanScore` against all candidates evaluated for that miner. A strong-pressure cap overflow can therefore legitimately select a rank greater than one.
- A per-miner `noEligiblePlan=true` line reports the three score fields as `unavailable` and `selectionRank=0`; summary and task-level skip lines remain aggregate diagnostics.

The log namespace is deliberately distinct:

- `MinerTargetSimulation` is the D.4 curated test-profile round-robin simulation.
- `DemandWeightedMinerPlanSimulation` is the D.6 demand-state pressure simulation.

Both may be enabled for comparison, although doing so increases diagnostic output.

#### Safety boundaries

- No miner controller field, `targetResource`, destination, patrol point, blackboard, movement state, density result, path result, survey/sample timer, conceptual resource selection, yield amount, or yield total is changed.
- No selected plan or assignment count survives the task invocation.
- No delayed task captures a controller, `AiAgent`, `ResourceSpawn`, or other raw gameplay pointer.
- Resource and supply data are copied under their existing short lock scopes. Scoring, balancing, and logging occur afterward.
- No real resource or `ResourceContainer` is created, changed, transferred, split, merged, or destroyed.
- No inventory, bank, private container, vendor, bazaar/auction listing, factory, harvester, crafting output, credit balance, persistence record, or player-facing economy state is changed.
- No IDL or object database is added.
- Existing D.3, D.4, density, and path-validation behavior is unchanged.

#### Limitations and next phase

- Conceptual supply remains broad and memory-only.
- Market supply is an observed public signal, not owned or reserved supply.
- The planner does not know density, route feasibility, extraction quantity, travel logistics, production capacity, consumption rate, or persistent stockpile availability.
- Profile assignment counts are local to one simulation interval and are not reservations.
- The same resource can satisfy several simulated profile plans because no allocation system exists.
- `samePlanet=true` indicates spawn availability on the zone, not a reachable density pocket.

The implemented follow-up is **D.6.6.1 - Demand-Weighted Plan Calibration and Explainability**, documented below. Actual miner target assignment remains disabled until demand pressure, density, pathability, and future stockpile semantics can be combined without creating conflicting authorities.

### D.6.6.1 - Demand-Weighted Plan Calibration and Explainability

D.6.6.1 adds bounded diagnostics around the existing demand-weighted simulation. It does not change profile pressure, resource scoring, assignment balancing, profile caps, or the selected simulated plans.

The existing per-miner line retains all prior fields and adds:

- `candidatesEvaluated`: eligible D.6.6 profile/resource plans considered for the miner.
- `candidatesCapped`: candidates already at `maxMinersPerProfile`.
- `candidatesTravelPenalized`: candidates without a same-zone resource opportunity.
- `candidatesRejected`: all unselected candidates.
- `candidatesLostHigherScore`: candidates rejected because the selected plan had a higher adjusted score.
- `candidatesLostStrongPressureOverflow`: uncapped alternatives displaced by the existing strong-pressure overflow policy.
- `rejectedCandidates`: a bounded profile/reason list. Reasons include `cappedByMaxMinersPerProfile`, `lostToHigherAdjustedPlanScore`, `lostToStrongPressureOverflow`, and stable tie-break variants. A rejected off-planet candidate receives a `travelPenaltyAnd...` prefix.

No-plan rows retain `noEligiblePlan=true` and now distinguish `noEligibleActiveResource`, `allProfilesBelowMinimumPressure`, `inactiveServerPhase`, `noEnabledDemandProfile`, and `allCandidatesCappedWithoutStrongPressure`. Profile-wide exclusions are reported once per interval rather than repeated for every miner:

- `disabledProfile`
- `inactiveServerPhase`
- `belowMinimumPressure`
- `noEligibleActiveResource`

Example:

```text
DemandWeightedMinerPlanSimulation miner=281475014066178 zone=naboo selectedProfile=composite_armor_supply pressureScore=1542.0 rawPressureScore=1542.0 locationAdjustedResourceScore=940.0 adjustedPlanScore=1692.0 selectionRank=1 candidatesEvaluated=5 candidatesCapped=1 candidatesTravelPenalized=2 candidatesRejected=4 candidatesLostHigherScore=3 candidatesLostStrongPressureOverflow=0 rejectedCandidates="master_weaponsmith_staples:lostToHigherAdjustedPlanScore,chef_buff_foods:travelPenaltyAndLostToHigherAdjustedPlanScore" demandState=critical target=Gowane type=copper_polysteel samePlanet=true travelRequired=false mode=simulation-only
```

Each interval also emits a compact comparison between the legacy D.4 round-robin concept and D.6.6. The comparison reuses the same copied miner and active-resource snapshot; it does not run the D.4 task or retain either plan. Because D.4 test keys and D.6.6 demand keys are intentionally different namespaces, `sameProfile` and `differentProfile` compare profession category, explicitly logged as `comparisonBasis=category`. Target comparison uses the selected `ResourceSpawn` object ID.

```text
DemandWeightedMinerPlanComparison minersEvaluated=4 comparisonBasis=category sameProfile=2 differentProfile=2 sameTargetResource=1 differentTargetResource=3 noD4Plan=0 noD66Plan=0 mode=simulation-only
```

The D.6.6.1 calibration summary is also emitted once per interval:

```text
DemandWeightedMinerPlanCalibration activeMiners=4 eligibleProfiles=5 profilesDisabled=0 profilesInactivePhase=0 profilesBelowPressure=0 profilesNoEligibleResource=1 profilesCapped=2 plansProduced=4 noPlanCount=0 overflowAssignments=1 comparisonChangedProfiles=2 comparisonChangedTargets=3 rejectedProfiles="chef_high_value_consumables:noEligibleActiveResource" mode=simulation-only
```

All new output is bounded by the six curated demand profiles and the existing `logTopN` per-miner limit. The comparison and calibration summaries appear only while `demandWeightedMinerPlanSimulationConfig.enabled` is active. There is no new config switch, task, stored plan, mutation API, target assignment, or gameplay authority.

Actual SimMiner movement, destinations, patrol points, conceptual resource selection, survey/sample timing, density/path simulation, yield accounting, inventory, markets, crafting, credits, persistence, and player-facing economy remain unchanged. Actual demand-weighted miner behavior remains deferred until these diagnostics are observed across resource shifts and acceptance/rollback criteria are defined.

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
- Conceptual miner spawn group with count `4`.
- PvP solo spawn group is present but configured with count `0`.
- PvP loiter, movement, combat scan, shuttle cycling, and death recycling.
- Always-active AiAgent behavior for SimPlayers.
- SimPlayer recycle cleanup is guarded so dead/incap checks happen under the old bot lock, but world/database destruction happens after that lock is released.

### Current disabled or inactive features

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

## P.2.2 / P.2.3 - Miner Assignment Lifecycle Hardening

P.2.2/P.2.3 hardens the intelligent miner assignment lifecycle before any stronger forced movement behavior is enabled. The assignment lifecycle status is now treated as authoritative and monotonic: path validation refreshes can update current/latest validation fields, but they must not move an active assignment backward from `queued`, `activation_started`, `sample_started`, or `sample_complete` to `candidate` or `validated`.

Assignments now carry an `assignmentGenerationId` and stable `targetHash` built from target source, demand profile, resource name/type, zone, and bucketed target coordinates. Path validation snapshots carry a monotonically increasing `validationSnapshotId`, the assignment generation when available, and the same target hash. This prevents diagnostics from relying only on miner object id when deciding whether a validation snapshot belongs to a live assignment.

Validation state is split into three meanings:

- lifecycle status: current assignment lifecycle such as `candidate`, `validated`, `queued`, `activation_started`, `sample_started`, or `sample_complete`.
- activation validation: the verified decision-time validation copied when activation is accepted.
- latest validation: the newest diagnostic snapshot for the miner, which may be newer than the activation decision and may describe a mismatch, stale check, or failed path.

Dashboard/API rows now expose `assignmentGenerationId`, `targetHash`, `validationSnapshotId`, `activationSnapshotId`, activation validation status/trust, latest validation status/trust, target hash mismatch state, and whether a lifecycle downgrade was prevented. If `densityTargetCoordinateMismatch`, `profileResourceMismatch`, `assignmentGenerationMismatch`, or `targetHashMismatch` appears, it should be interpreted first as snapshot-to-assignment drift unless the activation validation also failed.

Cleared assignments are copied into a bounded runtime-only `recentAssignmentHistory` list. This preserves generation id, target hash, lifecycle timestamps, clear reason, latest validation, activation validation, and yield-related context after the live assignment is removed. This is memory-only and is not persisted.

The dashboard also emits `movementReadiness`. This is a read-only diagnostic for a future forced-movement phase. Readiness requires a valid lifecycle state, verified activation/decision validation, matching generation/target hash identity, no lifecycle downgrade, no mismatch, and available activation cap. It does not force movement, change targeting, relax path trust, change scoring, alter yields, create resources, create `ResourceContainer` objects, mutate inventory, or write persistence.

## P.2.4 - Movement / Arrival Timeout Hardening

P.2.4 separates assignment freshness from active movement lifetime. The broad assignment TTL now applies to stale `candidate` and never-activated `validated` assignments only. Once an intelligent miner assignment reaches `queued`, `activation_started`, or `sample_started`, normal candidate TTL no longer clears it when `preventNormalTtlForActiveMovement` is enabled.

Active lifecycle states use separate timeouts:

- `queuedActivationTimeout` clears a queued assignment that never starts.
- `movementArrivalTimeout` clears an `activation_started` assignment that does not arrive within a conservative movement timeout.
- `sampleTimeout` clears a `sample_started` assignment that does not finish sampling.

Movement arrival timeout is distance-aware when validation path distance is available. The timeout is computed from `movementArrivalTimeoutMinSeconds + pathDistance * movementArrivalSecondsPerMeter`, then clamped between `movementArrivalTimeoutMinSeconds` and `movementArrivalTimeoutMaxSeconds`. If no path distance is known, `movementArrivalTimeoutSeconds` is used.

Dashboard/API rows now expose lifecycle timeout reason, lifecycle timeout age, lifecycle timeout seconds, movement timeout remaining seconds, sample timeout remaining seconds, activation path distance, latest path distance, and whether normal TTL was skipped for active movement. The `minerActivity`, `movementReadiness`, and health-window sections expose counts for candidate expiry, validated expiry, queued activation timeout, movement arrival timeout, sample timeout, expired-while-active prevention, and normal TTL skips.

Cleared assignment history records lifecycle status at clear, clear reason, movement/sample ages, timeout used, activation snapshot id, activation path trust, latest path trust, and the normal-TTL protection flag. A row with `clearReason=movementArrivalTimeout` means the miner had activation-time validation and movement was allowed, but arrival did not happen before the movement-specific timeout. A row with `clearReason=expired` now means a stale candidate or validated assignment expired before active movement.

This does not change demand scoring, density selection, path validation, NavArea selection, activation caps, movement speed, yield amount, persistence, inventory, resources, containers, market, vendor, crafting, or credit behavior. It is a movement lifecycle hardening step before any stronger movement forcing is considered.

## P.2.5 - Reachability Calibration & Explainability

P.2.5 adds runtime-only reachability calibration metrics for intelligent miner candidate selection. The goal is to explain where density-selected opportunities are lost before activation, without relaxing validation or changing movement behavior.

The dashboard/API section is `reachabilityCalibration`. It is `runtime-rolling-read-only`, meaning counters accumulate in memory for the current server process and are not persisted. The section reports:

- `validationFunnel`: assignment-level candidate counts for generated, first validated, and first rejected candidates.
- `densityConversion`: density target conversion from chosen to validated, activated, and sample complete.
- `validationOutcomes`: path validation attempt outcomes, including `verifiedPath`, `directFallbackVerified`, `directFallbackUnverified`, `pathRejected`, and `pathGenerationFailed`, with count, share, and average distance.
- `byPlanet`: candidate, validation, rejection, activation, and completion counts per planet.
- `byResourceClass`: the same funnel grouped by broad resource type prefix, such as `water`, `gas`, `iron`, or `copper`.
- `byDensitySource`: the same funnel grouped by source, currently usually `demand_weighted_plan`.
- `byDistanceBand`: the same funnel grouped into `0-128m`, `128-256m`, `256-512m`, and `512m+`.
- `topFailureReasons`: normalized blockers such as `trustInsufficient`, `pathGenerationFailed`, `validationDistanceExceeded`, and `candidateExpiredBeforeValidation`.

Interpretation guidance:

- If `directFallbackUnverified` dominates validation outcomes, the pathfinder is returning start/end fallback paths that remain intentionally untrusted.
- If failures cluster in `512m+`, the selected density targets may be too far away for reliable local pathing.
- If one planet has a much lower validation success percentage than others, inspect that planet's navmesh, terrain, configured miner spawn points, and density target coordinates.
- If chosen-to-validated is low but validated-to-activated is high, candidate/pathability selection is the bottleneck.
- If validated-to-activated is low, activation caps, cooldowns, lifecycle state, or readiness blockers are more likely than density pathability.
- If activated-to-sample-complete is low, use P.2.4 movement/arrival timeout diagnostics and controller lifecycle logs.

Assignment funnel counts and validation outcome counts intentionally answer different questions. The assignment funnel tracks candidate lifecycle conversion. Validation outcomes track path validation attempts, so repeated validation retries can increase outcome counts without creating new assignments.

This phase does not change demand scoring, density scoring, density weighting, path validation acceptance, NavArea behavior, movement, activation caps, sampling, yield amounts, persistence, resources, containers, inventory, vendors, bazaar, market, crafting, credits, or economy state. It only exposes calibration data needed before considering stronger movement forcing or reachability tuning.

## P.2.6 - Verified Reachability Memory + Candidate Preference

P.2.6 adds runtime-only reachability memory for intelligent miner density targets. The memory is keyed by copied primitive/string provenance: planet, resource name, resource type, selected demand profile, target source, and a rounded coordinate bucket. It records validation attempts, verified path counts, direct fallback failures, activations, sample completions, average path distance, average density, and a simple confidence score.

Memory collection is enabled by default through `reachabilityMemoryConfig.enableReachabilityMemory=true`. Candidate preference is explicitly disabled by default through `enableReachabilityCandidatePreference=false`, so the system records what it would prefer in shadow mode but does not change selected targets unless the operator intentionally enables the gate.

The dashboard/API section is `reachabilityMemory`. It reports:

- `topSuccessfulBuckets`: buckets with verified paths or sample completions.
- `topRejectedBuckets`: buckets with direct fallback/unverified path history.
- `byPlanet`, `byResourceType`, and distance-band aggregates.
- Shadow counters such as `shadowWouldSelectDifferentCount` and `shadowPreferredVerifiedHistoryCount`.
- Active preference counters such as `activePreferenceUsedCount` and `activePreferenceFallbackCount`, which should remain zero while preference is disabled.

When candidate preference is disabled, reachability memory does not change density scoring, demand scoring, path validation, assignment selection, activation, movement, sampling, yield amounts, persistence, inventory, resources, containers, market, vendor, crafting, credits, or economy state. When preference is enabled later, it only nudges density candidate ranking toward buckets with verified/sample-complete history and away from repeatedly unverified buckets; normal path validation and `verifiedPath` activation gates still remain mandatory.

This is restart-volatile by design. It prepares the AI economy to prefer known-pathable density pockets in a later active phase without persisting location memory or crossing into real resource/economy mutation.

## P.3.1 / P.3.2 - Coverage Slots + Stationed Miner Lifecycle

P.3.1/P.3.2 moves intelligent SimMiner diagnostics toward stable resource coverage instead of movement frequency. The dashboard/API now exposes `coveragePlanner`, a memory-only, read-only section derived from live demand/resource/assignment state. It includes desired coverage slots, covered slots, coverage gap, stationed/moving/sampling/unassigned miners, profile/resource coverage rows, uncovered needs, rebalance candidates, and station duration/sample summaries.

The retained assignment lifecycle adds `stationed`. With `stationedMinerConfig.enableStationedLifecycle=false` by default, behavior is unchanged. When enabled, a successful intelligent conceptual sample may retain its assignment as stationed coverage instead of clearing on `sampleComplete`, preserving assignment generation, target hash, activation snapshot, target resource identity, demand profile, and zone.

Repeated stationed sampling is separately gated by `enableStationedRepeatedSampling=false`. If explicitly enabled, it reuses only the existing conceptual yield path and remains bounded by sample interval, jitter, max sample count, max station duration, demand/resource/planet checks, and reserve-satisfied clearing.

Safety boundaries remain unchanged: no demand scoring change, no resource scoring change, no path trust relaxation, no movement speed change, no NavArea behavior change, no real extraction, no `ResourceContainer` creation, no inventory/vendor/market/crafting/credit mutation, and no persistence writes.
