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

`ai_registry.lua` maps NPCs to profiles. It currently supports lookup by internal creature template name and fallback lookup by template path. Important profile concepts include:

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
- On watch, checks player status and range.
- Calls `AiAgentBridge.wipeDanceBuffs`.
- Calls `AiAgentBridge.applyDanceMindBuff`.

Smart Musician:

- Registers `WASLISTENEDTO`.
- On listen, checks player status and range.
- Calls `AiAgentBridge.wipeMusicBuffs`.
- Calls `AiAgentBridge.applyMusicBuffs`.

The bridge delegates to the raw `LuaAiAgent` bindings. The `2` and `4` flags correspond to dance and music enhancement wipe paths in C++.

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

### Resource gathering concepts

`SimMinerController` exists and models a resource gathering loop:

1. Pick a resource type.
2. Perform a survey animation.
3. Pick a destination in navmesh or random nearby location.
4. Move there.
5. Perform sample animation.
6. Repeat.

Current resource selection is conceptual and simple. It chooses strings such as `iron`, `gas`, `water`, and `copper`. The current config has miner `totalCount = 0`, so this behavior is not currently active through the default SimPlayer config.

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
8. `AiRegistry.getProfile` checks that the target is an AiAgent.
9. `AiRegistry.getProfile` reads the creature template name through `LuaAiAgent:getCreatureTemplateName`.
10. If a profile is found, routing continues by role.
11. For `role = "smart_doctor"`, the handler calls `SmartDoctorBuffer:handleChat`.
12. For `role = "recruiter"`, the handler calls `AiBrain.getRecruiterIntent`.
13. For normal profiles, the handler calls `AiBrain.getChatResponse`.
14. `ai_brain.lua` reads `AiConfig.llm` for enablement, URL, model, and timeout settings.
15. `ai_brain.lua` builds a prompt and sends a synchronous HTTP POST to Ollama.
16. Ollama returns JSON containing a response field.
17. `ai_brain.lua` decodes the response.
18. The handler emits NPC output through `spatialChat`.

Recruiter flow is different from general chat because the LLM is used for intent classification. The gameplay action is still performed by existing recruiter screenplay functions after the intent is parsed.

# Current Technical Debt

The following risks and limitations exist in the current codebase:

- LLM HTTP calls are synchronous and can block the Lua path that handles spatial chat.
- LLM URL/model/default timeout are centralized in `ai_config.lua`, but broader gameplay configuration remains scattered.
- Missing LuaSocket or cjson now falls back safely, but the system still depends on those libraries for actual LLM responses.
- Smart Doctor full queue state is not persisted.
- Some older AI paths still directly know raw `LuaAiAgent` method names.
- Lua-side `pcall` cannot guarantee safety from every C++ binding failure.
- Buff amounts, durations, spawn points, cells, prices, and route data are scattered or hardcoded.
- IDL regeneration can break custom AiAgent fields, methods, annotations, or generated C++ signatures if custom declarations are not preserved.
- SimPlayer behavior modifies broad AiAgent lifecycle behavior through `simAlwaysActive` and `simPlayerBot`.
- SimPlayer configuration is loaded through a separate Lua instance, not through the same screenplay include path.
- SimPlayer controller selection relies partly on template-name inference.
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
