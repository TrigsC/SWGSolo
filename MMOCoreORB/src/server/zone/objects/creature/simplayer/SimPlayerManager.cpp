/*
 * SimPlayerManager.cpp
 * Fixed Attackable Flags
 */

#include "SimPlayerManager.h"
#include "SimPvPController.h" 
#include "server/zone/ZoneServer.h"
#include "server/ServerCore.h"
#include "server/zone/managers/creature/CreatureManager.h"
#include "server/zone/managers/creature/CreatureTemplateManager.h" 
#include "server/zone/managers/name/NameManager.h" 
#include "server/zone/objects/creature/ai/CreatureTemplate.h" 
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h" 
#include "templates/params/creature/ObjectFlag.h"
#include "server/zone/managers/planet/PlanetManager.h"
#include "server/zone/managers/planet/PlanetTravelPoint.h"

//#define DEBUG_POWERUPS

SimPlayerManager::SimPlayerManager() {
    setLoggingName("SimPlayerManager");
    lua = new Lua();
    lua->init();
}

SimPlayerManager::~SimPlayerManager() {
    if (lua != nullptr) {
        delete lua;
        lua = nullptr;
    }
}

void SimPlayerManager::initialize() {
    info("Initializing SimPlayer Manager...", true);
    info("SimPlayerManager::initialize this=" + String::valueOf((uint64)this), true);

    // Load Lua config. If it loads successfully, spawn groups.
    // If not, we fall back to the original hard-coded test spawn so
    // you can still verify the manager works.
    loadLuaConfig();
    spawnConfiguredGroups();

    // Fallback: Original behavior (useful if Lua file is missing / misparsed)
    //spawnSimPlayer("naboo", 4963.0f, -4892.0f, "stormtrooper");
}

void SimPlayerManager::loadLuaConfig() {
    info("DEBUG: Attempting to run Lua file: scripts/managers/sim_player_manager.lua", true);
    
    //struct LocationEntry {
    //    String planet;
    //    float x, y, z;          // Spawn Loc
    //    float hx, hy, hz;       // Hangout Loc
    //};
    struct LocationEntry {
        String planet;
        String name;
        float x = 0, y = 0, z = 0;
        float hx = 0, hy = 0, hz = 0;
    };

    try {
        lua->runFile("scripts/managers/sim_player_manager.lua");
    } catch (Exception& e) {
        error("DEBUG: CRITICAL LUA ERROR: " + e.getMessage());
        return;
    }

    LuaObject config = lua->getGlobalObject("SimPlayerManagerConfig");
    if (!config.isValidTable()) {
        error("DEBUG: 'SimPlayerManagerConfig' table NOT found!");
        return;
    }

    this->enabled = config.getBooleanField("enabled");
    info("DEBUG: Enabled == " + String::valueOf(this->enabled) + " For Load", true);
    if (!this->enabled) {
        config.pop();
        return;
    }

    allShuttleports.removeAll();
    spawnGroups.removeAll();

    // --- LOAD SHUTTLEPORTS ---
    LuaObject shuttles = config.getObjectField("shuttleports");
    if (shuttles.isValidTable()) {
        const char* planets[] = {"naboo", "tatooine", "corellia", "dantooine", "talus", "rori", "lok", "yavin4", "endor", "dathomir"};
        
        for (const char* pName : planets) {
            LuaObject planetTable = shuttles.getObjectField(pName);
            
            if (planetTable.isValidTable()) {
                int cityCount = planetTable.getTableSize();
                info("DEBUG: Found " + String::valueOf(cityCount) + " entries for planet: " + String(pName), true);
                
                for (int j = 1; j <= cityCount; ++j) {
                    LuaObject city = planetTable.getObjectAt(j);
                    
                    if (city.isValidTable()) {
                        String cityName = city.getStringField("name");
                        if (cityName.isEmpty()) cityName = "Unknown/Unnamed";
                        
                        LocationEntry entry;
                        entry.planet = pName;
                        entry.name = cityName;
                        bool validSpawn = false;

                        // 1. READ SPAWN (Then POP it immediately to clear stack)
                        LuaObject spawn = city.getObjectField("spawn");
                        if (spawn.isValidTable()) {
                            entry.x = spawn.getFloatAt(1);
                            entry.y = spawn.getFloatAt(2); 
                            entry.z = spawn.getFloatAt(3); 
                            validSpawn = true;
                            info("DEBUG: Loaded spawn for " + cityName + ": " + String::valueOf(entry.x) + "," + String::valueOf(entry.y) + "," + String::valueOf(entry.z), true);
                        } else {
                            error("DEBUG: Entry #" + String::valueOf(j) + " (" + cityName + ") in " + String(pName) + " is invalid! Check 'spawn' table.");
                        }
                        spawn.pop();

                        // 2. READ HANGOUT
                        if (validSpawn) {
                            LuaObject hangout = city.getObjectField("hangout");
                            if (hangout.isValidTable()) {
                                entry.hx = hangout.getFloatAt(1);
                                entry.hy = hangout.getFloatAt(2); 
                                entry.hz = hangout.getFloatAt(3);
                                info("DEBUG: Loaded Hangout for " + cityName + ": " + String::valueOf(entry.hx) + "," + String::valueOf(entry.hy), true);
                            } else {
                                // Fallback: Hangout = Spawn
                                info("DEBUG: No 'hangout' table found for " + cityName + ". Defaulting to spawn.", true);
                                entry.hx = entry.x;
                                entry.hy = entry.y;
                                entry.hz = entry.z;
                            }
                            hangout.pop(); // Pop hangout

                            ShuttleportLocation loc;
                            loc.planet = entry.planet;
                            loc.name = entry.name;
                            loc.spawn = Vector3(entry.x, entry.y, entry.z);
                            loc.hangout = Vector3(entry.hx, entry.hy, entry.hz);
                            allShuttleports.add(loc);
                        }
                    }
                    city.pop();
                }
            }
            planetTable.pop();
        }
    }
    shuttles.pop();

    if (allShuttleports.size() == 0) {
        error("DEBUG: ABORTING - No valid spawn locations found.");
        config.pop();
        return;
    }

    info("DEBUG: Successfully loaded " + String::valueOf(allShuttleports.size()) + " spawn locations.", true);

    // --- PROCESS GROUPS ---
    LuaObject groups = config.getObjectField("spawnGroups");
    if (groups.isValidTable()) {
        int groupCount = groups.getTableSize();
        
        for (int i = 1; i <= groupCount; ++i) {
            LuaObject group = groups.getObjectAt(i);
            String type = group.getStringField("type");
            SpawnGroup g;
            g.type = group.getStringField("type");
            g.totalCount = group.getIntField("totalCount");
            g.behavior = group.getStringField("behavior");
            g.faction = group.getStringField("faction");

            LuaObject templates = group.getObjectField("templates");
            if (templates.isValidTable()) {
                int tSize = templates.getTableSize();
                for (int t = 1; t <= tSize; ++t) {
                    g.templates.add(templates.getStringAt(t));
                }
            }
            templates.pop();

            info("DEBUG: Loaded Group " + String::valueOf(i) + " (" + g.type + ") totalCount=" + String::valueOf(g.totalCount), true);
            spawnGroups.add(g);
            group.pop();
        }
    }
    groups.pop();
    config.pop();
}

void SimPlayerManager::spawnSimPlayer(const String& planet, float x, float y, const String& templateName) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr) return;

    Zone* zone = zoneServer->getZone(planet);
    if (zone == nullptr) {
        info("Could not find zone: " + planet);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr) return;

    float z = zone->getHeight(x, y); 

    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0, x, z, y, 0);
    if (creature == nullptr) {
        info("Failed to spawn SimPlayer template: " + templateName);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr) return;

    // --- COSMETICS ---
    NameManager* nm = zoneServer->getNameManager();
    if (nm != nullptr) {
        // Use Type 0 to generate a Human Name (First Last) instead of TK-123
        String name = nm->makeCreatureName(0, creature->getSpecies()); 
        if (!name.isEmpty()) {
            agent->setCustomObjectName(name, true);
        }
    }

    // Reset default flags to clean slate
    agent->setCreatureBitmask(0); 
    agent->setDespawnOnNoPlayerInRange(false);

    toggleBot(agent);
}

void SimPlayerManager::toggleBot(AiAgent* agent) {
    if (agent == nullptr) return;

    uint64 oid = agent->getObjectID();

    if (controllers.contains(oid)) {
        info("Stopping SimPlayer for agent " + String::valueOf(oid), true);
        agent->eraseBlackboard("simAlwaysActive");
        controllers.drop(oid);
        
        agent->clearPatrolPoints();
        agent->clearSavedPatrolPoints();
        agent->setMovementState(AiAgent::OBLIVIOUS);
        agent->activateAiBehavior(true);
        agent->setSimPlayerBot(false);

        agent->destroyObjectFromWorld(true);
        agent->destroyObjectFromDatabase(true);
        return;
    } else {
        info("Starting SimPlayer for agent " + String::valueOf(oid), true);
        
        agent->setCustomAiMap(String("patrol").hashCode());
        agent->setAITemplate(); 
        
        agent->writeBlackboard("simAlwaysActive", true);
        agent->setSimAlwaysActive(true);
        agent->setSimPlayerBot(true); 
        agent->setDespawnOnNoPlayerInRange(false);

        Reference<SimPlayerController*> ctrl = nullptr;

        info("toggleBot: creating PvP controller using DEFAULT route (no spawn/hangout)", true);
        
        const CreatureTemplate* tmpl = agent->getCreatureTemplate();
        String tName = (tmpl != nullptr) ? tmpl->getTemplateName() : "";

        // NOTE: Keep this logic broad so new PvP templates (e.g. rebel_commando)
        // still get the correct controller when spawned via spawnSimPlayer().
        String lower = tName.toLowerCase();
        bool looksRebel = lower.beginsWith("rebel") || lower.contains("specforce") || lower.contains("light");
        bool looksImperial = lower.beginsWith("imperial") || lower.contains("stormtrooper") || lower.contains("dark");

        if (looksImperial) {
            agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);

            SimPvPController* pvp = new SimPvPController(agent, true);

            // Give it enough context to cycle
            String zoneName = (agent->getZone() != nullptr) ? agent->getZone()->getZoneName() : "unknown";
            pvp->setCycleContext(this, tName, "pvp_solo", zoneName, "toggleBot");

            ctrl = pvp;

        } else if (looksRebel) {
            agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);

            SimPvPController* pvp = new SimPvPController(agent, false);

            String zoneName = (agent->getZone() != nullptr) ? agent->getZone()->getZoneName() : "unknown";
            pvp->setCycleContext(this, tName, "pvp_solo", zoneName, "toggleBot");

            ctrl = pvp;
        } else {
             agent->setPvpStatusBitmask(0);
             ctrl = new SimMinerController(agent);
        }

        controllers.put(oid, ctrl);
        
        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }
}

void SimPlayerManager::startControllerForAgent(AiAgent* agent, Reference<SimPlayerController*> ctrl) {
    if (agent == nullptr || ctrl == nullptr)
        return;

    uint64 oid = agent->getObjectID();

    // Shared “Starting SimPlayer” flags (copied from your toggleBot start path)
    agent->setCustomAiMap(String("patrol").hashCode());
    agent->setAITemplate();

    agent->writeBlackboard("simAlwaysActive", true);
    agent->setSimAlwaysActive(true);
    agent->setSimPlayerBot(true);
    agent->setDespawnOnNoPlayerInRange(false);

    controllers.put(oid, ctrl);

    agent->activateAiBehavior(true);
    ctrl->startSimLoop();
}

static bool inferImperialFromTemplateName(const String& templateNameOrTName) {
    String lower = templateNameOrTName.toLowerCase();
    bool looksRebel = lower.beginsWith("rebel");
    bool looksImperial = lower.beginsWith("imperial") || lower.contains("stormtrooper");
    if (looksRebel) return false;
    if (looksImperial) return true;
    // default if unknown
    return true;
}

void SimPlayerManager::spawnSimPlayerWithRoute(const String& planet,
                                              const Vector3& spawn,
                                              const Vector3& hangout,
                                              const String& templateName,
                                              const String& groupType,
                                              const String& locationName) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr) return;

    Zone* zone = zoneServer->getZone(planet);
    if (zone == nullptr) {
        info("Could not find zone: " + planet, true);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr) return;

    // Compute good Z from navmesh/terrain
    float spawnZ = zone->getHeight(spawn.getX(), spawn.getY());
    if (spawnZ == 0.0f && spawn.getZ() != 0.0f) spawnZ = spawn.getZ();

    float hangoutZ = zone->getHeight(hangout.getX(), hangout.getY());
    if (hangoutZ == 0.0f && hangout.getZ() != 0.0f) hangoutZ = hangout.getZ();

    Vector3 spawnPos(spawn.getX(), spawn.getY(), spawnZ);
    Vector3 hangoutPos(hangout.getX(), hangout.getY(), hangoutZ);

    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0,
                                                              spawnPos.getX(), spawnPos.getZ(), spawnPos.getY(), 0);
    if (creature == nullptr) {
        info("Failed to spawn SimPlayer template: " + templateName, true);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr) return;

    // Reset default flags to clean slate
    agent->setCreatureBitmask(0);
    agent->setDespawnOnNoPlayerInRange(false);

    // Decide controller based on groupType first (authoritative)
    Reference<SimPlayerController*> ctrl = nullptr;

    if (groupType.beginsWith("pvp")) {
        bool imperial = inferImperialFromTemplateName(templateName);

        agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);

        SimPvPController* pvp = new SimPvPController(agent, imperial, spawnPos, hangoutPos);
        pvp->setCycleContext(this, templateName, groupType, planet, locationName);
        ctrl = pvp;
    } else {
        agent->setPvpStatusBitmask(0);
        ctrl = new SimMinerController(agent);
    }

    info("spawnSimPlayerWithRoute: spawned " + templateName +
         " at " + planet + ":" + locationName +
         " spawn=(" + String::valueOf(spawnPos.getX()) + "," + String::valueOf(spawnPos.getY()) + ")" +
         " hangout=(" + String::valueOf(hangoutPos.getX()) + "," + String::valueOf(hangoutPos.getY()) + ")",
         true);

    startControllerForAgent(agent, ctrl);
}

// -----------------------------------------------------------------------------
// Lua config glue
// -----------------------------------------------------------------------------

static Vector3 readVec3FromLua(LuaObject& arr) {
    if (!arr.isValidTable() || arr.getTableSize() < 3)
        return Vector3(0, 0, 0);

    float x = (float)arr.getFloatAt(1);
    float y = (float)arr.getFloatAt(2);
    float z = (float)arr.getFloatAt(3);
    return Vector3(x, y, z);
}

void SimPlayerManager::spawnConfiguredGroups() {
    if (!enabled)
        return;

    if (spawnGroups.size() == 0 || allShuttleports.size() == 0) {
        info("SimPlayerManager has no config to spawn from (spawnGroups/shuttleports empty).");
        return;
    }

    for (int gi = 0; gi < spawnGroups.size(); ++gi) {
        const SpawnGroup& g = spawnGroups.get(gi);
        for (int c = 0; c < g.totalCount; ++c) {
            ShuttleportLocation loc;
            if (!pickRandomShuttleport(loc))
                return;

            String tmpl = pickRandomTemplate(g);
            spawnFromConfig(g, loc, tmpl);
        }
    }
}

bool SimPlayerManager::pickRandomShuttleport(ShuttleportLocation& out) const {
    if (allShuttleports.size() == 0)
        return false;

    int idx = System::random(allShuttleports.size() - 1);
    out = allShuttleports.get(idx);
    return true;
}

String SimPlayerManager::pickRandomTemplate(const SpawnGroup& g) const {
    if (g.templates.size() == 0) {
        // Reasonable fallbacks for empty template list
        if (g.type.beginsWith("pvp"))
            return "stormtrooper";
        return "artisan";
    }

    int idx = System::random(g.templates.size() - 1);
    return g.templates.get(idx);
}

bool SimPlayerManager::isImperialForSpawn(const SpawnGroup& g, const String& templateName) const {
    // Explicit override in Lua
    if (g.faction == "imperial")
        return true;
    if (g.faction == "rebel")
        return false;
    if (g.faction == "random")
        return (System::random(1) == 1);

    // Infer from template naming
    String lower = templateName;
    lower = lower.toLowerCase();
    if (lower.beginsWith("rebel"))
        return false;
    if (lower.beginsWith("imperial") || lower.contains("stormtrooper"))
        return true;

    // Default: random
    return (System::random(1) == 1);
}

void SimPlayerManager::spawnFromConfig(const SpawnGroup& g, const ShuttleportLocation& loc, const String& templateName) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr)
        return;

    Zone* zone = zoneServer->getZone(loc.planet);
    if (zone == nullptr) {
        info("Could not find zone: " + loc.planet);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr)
        return;

    // Use nav height when available; fall back to Lua-provided Z if needed.
    float spawnZ = zone->getHeight(loc.spawn.getX(), loc.spawn.getY());
    if (spawnZ == 0.0f && loc.spawn.getZ() != 0.0f)
        spawnZ = loc.spawn.getZ();

    float hangoutZ = zone->getHeight(loc.hangout.getX(), loc.hangout.getY());
    if (hangoutZ == 0.0f && loc.hangout.getZ() != 0.0f)
        hangoutZ = loc.hangout.getZ();

    Vector3 spawnPos(loc.spawn.getX(), loc.spawn.getY(), spawnZ);
    Vector3 hangoutPos(loc.hangout.getX(), loc.hangout.getY(), hangoutZ);

    info("Spawning SimPlayer type=" + g.type + " template=" + templateName + " planet=" + loc.planet + " loc=" + loc.name, true);

    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0, spawnPos.getX(), spawnPos.getZ(), spawnPos.getY(), 0);
    if (creature == nullptr) {
        info("Failed to spawn SimPlayer template: " + templateName);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr)
        return;

    // Reset default flags to clean slate
    agent->setCreatureBitmask(0);
    agent->setDespawnOnNoPlayerInRange(false);

    // Start (non-toggle) using the same flags as toggleBot's "Starting" path
    uint64 oid = agent->getObjectID();

    agent->setCustomAiMap(String("patrol").hashCode());
    agent->setAITemplate();

    agent->writeBlackboard("simAlwaysActive", true);
    agent->setSimAlwaysActive(true);
    agent->setSimPlayerBot(true);
    agent->setDespawnOnNoPlayerInRange(false);

    Reference<SimPlayerController*> ctrl = nullptr;

    if (g.type.beginsWith("pvp")) {
        bool imperial = isImperialForSpawn(g, templateName);
        agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);

        info("spawnFromConfig: creating PvP controller with route spawn=("
            + String::valueOf(spawnPos.getX()) + "," + String::valueOf(spawnPos.getY()) + "," + String::valueOf(spawnPos.getZ())
            + ") hangout=("
            + String::valueOf(hangoutPos.getX()) + "," + String::valueOf(hangoutPos.getY()) + "," + String::valueOf(hangoutPos.getZ())
            + ")", true);

        SimPvPController* pvp = new SimPvPController(agent, imperial, spawnPos, hangoutPos);
        pvp->setCycleContext(this, templateName, g.type, loc.planet, loc.name);
        Logger::console.info(
            "SimPlayerManager: spawnFromConfig wired cycle context oid=" + String::valueOf(agent->getObjectID()) +
            " mgr=this groupType=" + g.type +
            " template=" + templateName +
            " planet=" + loc.planet +
            " location=" + loc.name,
            true
        );
        ctrl = pvp;
    } else {
        agent->setPvpStatusBitmask(0);
        ctrl = new SimMinerController(agent);
    }

        controllers.put(oid, ctrl);
        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }

void SimPlayerManager::cyclePvPBotWhenShuttleReady(uint64 oldOid,
                                                   const String& groupType,
                                                   const String& templateName,
                                                   bool imperial,
                                                   const String& fromPlanet,
                                                   const String& fromLocation,
                                                   int attempts) {
    // Safety: don’t wait forever
    if (attempts >= 24) { // 24 * 5s = ~2 minutes
        info("cyclePvPBotWhenShuttleReady: timeout waiting for shuttle; cycling anyway oldOid=" +
             String::valueOf(oldOid), true);
        cyclePvPBot(oldOid, groupType, templateName, imperial, fromPlanet, fromLocation);
        return;
    }

    // Re-acquire the old agent by OID (don’t capture oldAgent across tasks)
    ManagedReference<SceneObject*> obj = ServerCore::getZoneServer()->getObject(oldOid);
    ManagedReference<AiAgent*> oldAgent = cast<AiAgent*>(obj.get());

    if (oldAgent == nullptr) {
        info("cyclePvPBotWhenShuttleReady: oldAgent null, abort oldOid=" + String::valueOf(oldOid), true);
        return;
    }

    {
        Locker locker(oldAgent);
        // If the bot died while waiting, stop the loop and clean up.
        if (oldAgent->isDead() || oldAgent->isIncapacitated()) {
            info("cyclePvPBotWhenShuttleReady: old bot is dead/incap; cleaning up oldOid=" +
                 String::valueOf(oldOid), true);

            controllers.drop(oldOid);

            // If you want NO corpses/loot for simplayers:
            oldAgent->destroyObjectFromWorld(true);
            oldAgent->destroyObjectFromDatabase(true);

            return;
        }

        if (!isNearestShuttleBoardable(oldAgent)) {
            // Optional: make it look like it’s waiting
            oldAgent->setMovementState(AiAgent::OBLIVIOUS);
            oldAgent->activateAiBehavior(true);

            Core::getTaskManager()->scheduleTask(
                [this, oldOid, groupType, templateName, imperial, fromPlanet, fromLocation, attempts]() {
                    this->cyclePvPBotWhenShuttleReady(oldOid, groupType, templateName, imperial, fromPlanet, fromLocation, attempts + 1);
                },
                "SimPvPWaitForShuttle",
                5000
            );

            return;
        }
    }

    // Shuttle is boardable now — do the real cycle
    cyclePvPBot(oldOid, groupType, templateName, imperial, fromPlanet, fromLocation);
}

bool SimPlayerManager::isNearestShuttleBoardable(CreatureObject* creature) {
    if (creature == nullptr)
        return false;

    Zone* zone = creature->getZone();
    if (zone == nullptr)
        return false;

    PlanetManager* pm = zone->getPlanetManager();
    if (pm == nullptr)
        return false;

    Reference<PlanetTravelPoint*> ptp = pm->getNearestPlanetTravelPoint(creature, 128.f);
    if (ptp == nullptr)
        return false;

    ManagedReference<CreatureObject*> shuttle = ptp->getShuttle();
    if (shuttle == nullptr)
        return false;

    // Match BoardShuttleCommand behavior (don’t special-case Theed Spaceport unless you want to)
    if (!ptp->isPoint("naboo", "Theed Spaceport")) {
        return pm->checkShuttleStatus(creature, shuttle);
    }

    return true;
}

void SimPlayerManager::cyclePvPBot(uint64 oldOid,
                                   const String& groupType,
                                   const String& templateName,
                                   bool imperial,
                                   const String& fromPlanet,
                                   const String& fromLocation) {
    info("cyclePvPBot ENTER this=" + String::valueOf((uint64)this) +
         " oldOid=" + String::valueOf(oldOid) +
         " controllersHas=" + String::valueOf(controllers.contains(oldOid)) +
         " from " + fromPlanet + ":" + fromLocation +
         " groupType=" + groupType + " template=" + templateName +
         " shuttleports=" + String::valueOf(allShuttleports.size()) +
         " spawnGroups=" + String::valueOf(spawnGroups.size()), true);

    // Run on task thread (you already do this style elsewhere)
    Core::getTaskManager()->scheduleTask([this, oldOid, groupType, templateName, imperial, fromPlanet, fromLocation]() {
        info("cyclePvPBot TASK START this=" + String::valueOf((uint64)this) +
             " oldOid=" + String::valueOf(oldOid) +
             " enabled=" + String::valueOf(enabled) +
             " shuttleports=" + String::valueOf(allShuttleports.size()) +
             " spawnGroups=" + String::valueOf(spawnGroups.size()), true);

        if (!controllers.contains(oldOid)) {
            info("cyclePvPBot: oldOid no longer in controllers (already cleaned up?)", true);
            return;
        }

        // If config wasn't loaded (or got wiped), try loading once.
        // With the new loadLuaConfig() this won't destroy good config on failure.
        if (!enabled || allShuttleports.size() == 0 || spawnGroups.size() == 0) {
            info("BEFORE loadLuaConfig: enabled=" + String::valueOf(enabled) +
                 " shuttles=" + String::valueOf(allShuttleports.size()) +
                 " groups=" + String::valueOf(spawnGroups.size()), true);

            loadLuaConfig();

            info("AFTER  loadLuaConfig: enabled=" + String::valueOf(enabled) +
                 " shuttles=" + String::valueOf(allShuttleports.size()) +
                 " groups=" + String::valueOf(spawnGroups.size()), true);
        }

        if (!enabled || allShuttleports.size() == 0 || spawnGroups.size() == 0) {
            info("cyclePvPBot: cannot cycle because config still empty/disabled.", true);
            return;
        }

        // 1) Pick a new location (try not to repeat)
        ShuttleportLocation newLoc;
        bool pickedLoc = false;

        for (int tries = 0; tries < 10; ++tries) {
            if (!pickRandomShuttleport(newLoc))
                return;

            if (!(newLoc.planet == fromPlanet && newLoc.name == fromLocation)) {
                pickedLoc = true;
                break;
            }
        }

        if (!pickedLoc) {
            // Could not find a different one after N tries — allow same
            if (!pickRandomShuttleport(newLoc))
                return;
        }

        // 2) Find spawn group
        SpawnGroup picked;
        bool found = false;

        for (int i = 0; i < spawnGroups.size(); ++i) {
            if (spawnGroups.get(i).type == groupType) {
                picked = spawnGroups.get(i);
                found = true;
                break;
            }
        }

        if (!found) {
            info("cyclePvPBot: could not find spawnGroup type=" + groupType + ", falling back to first pvp group", true);
            for (int i = 0; i < spawnGroups.size(); ++i) {
                if (spawnGroups.get(i).type.beginsWith("pvp")) {
                    picked = spawnGroups.get(i);
                    found = true;
                    break;
                }
            }
            if (!found)
                return;
        }

        // 3) Spawn replacement
        String tmpl = templateName;
        if (tmpl.isEmpty())
            tmpl = pickRandomTemplate(picked);

        info("Cycling PvP bot " + String::valueOf(oldOid) +
             " from " + fromPlanet + ":" + fromLocation +
             " -> " + newLoc.planet + ":" + newLoc.name +
             " template=" + tmpl, true);

        spawnFromConfig(picked, newLoc, tmpl);

        // 4) Drop controller first
        controllers.drop(oldOid);

        // 5) Destroy old object via ZoneServer lookup
        ZoneServer* zoneServer = ServerCore::getZoneServer();
        if (zoneServer == nullptr)
            return;

        ManagedReference<SceneObject*> obj = zoneServer->getObject(oldOid);
        if (obj == nullptr)
            return;

        ManagedReference<AiAgent*> oldAgent = cast<AiAgent*>(obj.get());
        if (oldAgent == nullptr)
            return;

        Locker locker(oldAgent);
        if (oldAgent->isDead() || oldAgent->isIncapacitated()) {
            info("cyclePvPBot: old bot already dead/incap; destroying oldOid=" + String::valueOf(oldOid), true);
            controllers.drop(oldOid);
            oldAgent->destroyObjectFromWorld(true);
            oldAgent->destroyObjectFromDatabase(true);
            return;
        }

        info("cyclePvPBot: destroying oldOid=" + String::valueOf(oldOid), true);
        oldAgent->destroyObjectFromWorld(true);
        info("cyclePvPBot: destroyObjectFromWorld done oldOid=" + String::valueOf(oldOid), true);
        oldAgent->destroyObjectFromDatabase(true);
        info("cyclePvPBot: destroyObjectFromDatabase done oldOid=" + String::valueOf(oldOid), true);
    }, "CyclePvPBot", 0);
}