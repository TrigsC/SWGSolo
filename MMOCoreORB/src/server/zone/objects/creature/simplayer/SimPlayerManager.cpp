/*
 * SimPlayerManager.cpp
 * DEBUG VERSION: Verbose Logging to diagnose startup issues
 */

#include "SimPlayerManager.h"
#include "SimPvPController.h" 
#include "server/zone/ZoneServer.h"
#include "server/ServerCore.h"
#include "server/zone/managers/creature/CreatureManager.h"
#include "server/zone/managers/creature/CreatureTemplateManager.h" 
#include "server/zone/objects/creature/ai/CreatureTemplate.h" 
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h" 
#include "templates/params/creature/ObjectFlag.h" 
#include "server/zone/managers/name/NameManager.h" 
#include "server/zone/objects/player/FactionStatus.h" 
#include "server/zone/managers/director/DirectorManager.h" 
#include <vector>

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
    loadLuaConfig();
}

void SimPlayerManager::loadLuaConfig() {
    info("DEBUG: Attempting to run Lua file: scripts/managers/sim_player_manager.lua", true);
    
    // 1. Define the struct locally so it is available
    struct LocationEntry {
        String planet;
        float x, y, z; 
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

    bool enabled = config.getBooleanField("enabled");
    if (!enabled) return;

    // 2. Load Shuttleports
    std::vector<LocationEntry> locationList; 

    LuaObject shuttles = config.getObjectField("shuttleports");
    if (shuttles.isValidTable()) {
        const char* planets[] = {"naboo", "tatooine", "corellia", "dantooine", "talus", "rori", "lok", "yavin4", "endor", "dathomir"};
        
        for (const char* pName : planets) {
            LuaObject planetTable = shuttles.getObjectField(pName);
            
            if (planetTable.isValidTable()) {
                info("DEBUG: Found shuttle entries for: " + String(pName), true);
                
                for (int j = 1; j <= planetTable.getTableSize(); ++j) {
                    LuaObject city = planetTable.getObjectAt(j);
                    if (city.isValidTable()) {
                        LuaObject spawn = city.getObjectField("spawn");
                        
                        if (spawn.isValidTable() && spawn.getTableSize() >= 3) {
                            LocationEntry entry;
                            entry.planet = pName;
                            entry.x = spawn.getFloatAt(1);
                            entry.y = spawn.getFloatAt(2);
                            entry.z = spawn.getFloatAt(3);
                            locationList.push_back(entry);
                        } else {
                            error("DEBUG: Invalid 'spawn' coord table for planet: " + String(pName));
                        }
                        spawn.pop();
                    }
                    city.pop();
                }
            }
            planetTable.pop();
        }
    } else {
        error("DEBUG: 'shuttleports' table is missing or invalid!");
    }
    shuttles.pop();

    info("DEBUG: Total Spawn Locations Loaded: " + String::valueOf(locationList.size()), true);

    if (locationList.empty()) {
        error("DEBUG: ABORTING - No spawn locations found. Check Lua 'shuttleports' structure.");
        return;
    }

    // 3. Process Spawn Groups
    LuaObject groups = config.getObjectField("spawnGroups");
    if (groups.isValidTable()) {
        int groupCount = groups.getTableSize();
        
        for (int i = 1; i <= groupCount; ++i) {
            LuaObject group = groups.getObjectAt(i);
            String type = group.getStringField("type");
            int count = group.getIntField("totalCount");
            LuaObject templates = group.getObjectField("templates");
            
            info("DEBUG: Processing Group " + String::valueOf(i) + " (" + type + ") Count: " + String::valueOf(count), true);

            for (int k = 0; k < count; ++k) {
                if (locationList.empty()) break;

                int locIndex = System::random(locationList.size());
                if (locIndex >= locationList.size()) locIndex = 0; 
                
                LocationEntry loc = locationList.at(locIndex);

                String tmpl = "";
                if (templates.isValidTable()) {
                    int tSize = templates.getTableSize();
                    if (tSize > 0) {
                        int tIdx = 1 + System::random(tSize);
                        if (tIdx > tSize) tIdx = tSize; 
                        tmpl = templates.getStringAt(tIdx);
                    }
                }

                if (tmpl.isEmpty()) {
                    error("DEBUG: Template name is empty! Skipping spawn.");
                    continue;
                }

                if (type == "miner") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, tmpl, 0); 
                } 
                else if (type == "pvp_solo") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, tmpl, 1); 
                }
            }
            templates.pop();
            group.pop();
        }
    }
    groups.pop();
}

void SimPlayerManager::spawnSimPlayer(const String& planet, float x, float y, float z, const String& templateName, int type) {
    try {
        ZoneServer* zoneServer = ServerCore::getZoneServer();
        if (zoneServer == nullptr) return;

        Zone* zone = zoneServer->getZone(planet);
        if (zone == nullptr) {
            error("DEBUG: Could not find zone object for: " + planet);
            return;
        }

        CreatureManager* creatureManager = zone->getCreatureManager();
        if (creatureManager == nullptr) return;

        String fullTemplate = templateName;
        if (!fullTemplate.contains("object/")) {
            fullTemplate = "object/mobile/" + templateName + ".iff";
        }

        // SWG Map: X=East, Y=Altitude, Z=North
        // Our Lua passed: x, y=North, z=Height
        // Arguments passed to this func: x, y(North), z(Height)
        
        // Use supplied height if valid, else calculate
        if (z == 0 || z < -10000 || z > 10000) z = zone->getHeight(x, y); 

        // Core3 spawnCreature expects (X, Z-North, Y-Height)
        // We call it with: x, y(North), z(Height)
        // Wait, verifying CreatureManager::spawnCreature signature...
        // It is: spawnCreature(uint32 templateCRC, float x, float z, float y, uint64 parentID)
        // WHERE Z is usually height in 3D engines, but in SWG Y is Height.
        // Let's rely on standard map convention: X, Z, Y (where Y is Up).
        // CreatureManager::spawnCreature(crc, type, x, z, y, parent) usually maps to world coordinates.
        // Standard call: spawnCreature(hash, 0, x, z, y, 0) -> X, Z(North), Y(Height).
        // So we should pass: x, y(North), z(Height).
        
        info("DEBUG: Spawning " + fullTemplate + " on " + planet + " @ " + String::valueOf(x) + ", " + String::valueOf(y) + " (H:" + String::valueOf(z) + ")", true);

        CreatureObject* creature = creatureManager->spawnCreature(fullTemplate.hashCode(), 0, x, y, z, 0);
        
        if (creature == nullptr) {
            error("DEBUG: Spawn Failed! Could not create object. Check template path: " + fullTemplate);
            return;
        }

        AiAgent* agent = creature->asAiAgent();
        if (agent == nullptr) {
             error("DEBUG: Spawned object is not an AiAgent. Check template type.");
             return;
        }

        try {
            NameManager* nm = zoneServer->getNameManager();
            if (nm != nullptr) {
                String name = nm->makeCreatureName(0, creature->getSpecies()); 
                if (!name.isEmpty()) agent->setCustomObjectName(name, true);
            }
        } catch (...) {
            agent->setCustomObjectName("Sim Player", true);
        }

        agent->setFactionRank(1); 
        agent->setDespawnOnNoPlayerInRange(false);

        Reference<SimPlayerController*> ctrl = nullptr;

        if (type == 1) { // PvP
            bool isImp = (fullTemplate.contains("stormtrooper"));
            agent->setFactionStatus(FactionStatus::OVERT);
            agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE);
            ctrl = new SimPvPController(agent, isImp); 
        } else { // Miner
            agent->setFactionStatus(FactionStatus::ONLEAVE);
            agent->setPvpStatusBitmask(0); 
            ctrl = new SimMinerController(agent);
        }

        if (controllers.contains(agent->getObjectID())) {
            controllers.drop(agent->getObjectID());
        }
        controllers.put(agent->getObjectID(), ctrl);
        
        agent->activateAiBehavior(true);
        
        Core::getTaskManager()->scheduleTask([ctrl] () {
            ctrl->startSimLoop();
        }, "SimStartLambda", 10000); 

    } catch (Exception& e) {
        error("DEBUG: Exception in spawnSimPlayer: " + e.getMessage());
    } catch (...) {
        error("DEBUG: Unknown Exception in spawnSimPlayer");
    }
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
        
        const CreatureTemplate* tmpl = agent->getCreatureTemplate();
        String tName = (tmpl != nullptr) ? tmpl->getTemplateName() : "";

        if (tName == "stormtrooper") {
             ctrl = new SimPvPController(agent, true); 
        } 
        else if (tName == "rebel_trooper") {
             ctrl = new SimPvPController(agent, false);
        }
        else {
             ctrl = new SimMinerController(agent);
        }

        controllers.put(oid, ctrl);
        agent->activateAiBehavior(true);

        // --- STARTUP FIX: 10 Second Warmup ---
        // Using scheduleTask (Correct API) instead of executeTask
        // This prevents the bot from asking for a path before the NavMesh is ready
        Core::getTaskManager()->scheduleTask([ctrl] () {
            ctrl->startSimLoop();
        }, "SimStartLambda", 10000); 
    }
}