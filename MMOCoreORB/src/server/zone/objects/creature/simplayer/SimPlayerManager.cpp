/*
 * SimPlayerManager.cpp
 * FIXED: Random Range Logic, Safety Wrappers, and Debug Logging
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
    lua->runFile("scripts/managers/sim_player_manager.lua");

    LuaObject config = lua->getGlobalObject("SimPlayerManagerConfig");
    if (!config.isValidTable()) {
        error("Failed to load SimPlayerManagerConfig from lua!");
        return;
    }

    bool enabled = config.getBooleanField("enabled");
    if (!enabled) {
        info("SimPlayer system disabled in Lua.", true);
        return;
    }

    struct LocationEntry {
        String planet;
        float x, y, z; 
    };
    
    std::vector<LocationEntry> locationList;

    LuaObject shuttles = config.getObjectField("shuttleports");
    if (shuttles.isValidTable()) {
        const char* planets[] = {"naboo", "tatooine", "corellia", "dantooine", "talus", "rori", "lok", "yavin4", "endor", "dathomir"};
        
        for (const char* pName : planets) {
            LuaObject planetTable = shuttles.getObjectField(pName);
            if (planetTable.isValidTable()) {
                for (int j = 1; j <= planetTable.getTableSize(); ++j) {
                    LuaObject city = planetTable.getObjectAt(j);
                    if (city.isValidTable()) {
                        LuaObject spawn = city.getObjectField("spawn");
                        
                        LocationEntry entry;
                        entry.planet = pName;
                        entry.x = spawn.getFloatAt(1);
                        entry.y = spawn.getFloatAt(2); // North
                        entry.z = spawn.getFloatAt(3); // Height
                        
                        locationList.push_back(entry);
                    }
                    city.pop();
                }
            }
            planetTable.pop();
        }
    }
    shuttles.pop();

    if (locationList.empty()) {
        error("No shuttleports defined in Lua!");
        return;
    }

    LuaObject groups = config.getObjectField("spawnGroups");
    if (groups.isValidTable()) {
        for (int i = 1; i <= groups.getTableSize(); ++i) {
            LuaObject group = groups.getObjectAt(i);
            
            String type = group.getStringField("type");
            int count = group.getIntField("totalCount");
            LuaObject templates = group.getObjectField("templates");
            
            info("Spawning Group: " + type + " Count: " + String::valueOf(count), true);

            for (int k = 0; k < count; ++k) {
                // FIX: Use size(), not size()-1. Random(N) returns 0..N-1
                int locIndex = System::random(locationList.size());
                LocationEntry loc = locationList.at(locIndex);

                String tmpl = "";
                if (templates.isValidTable() && templates.getTableSize() > 0) {
                    // Lua is 1-based. Random(N) -> 0..N-1. Add 1 -> 1..N
                    int tIdx = 1 + System::random(templates.getTableSize());
                    tmpl = templates.getStringAt(tIdx);
                }

                if (type == "miner") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, tmpl, 0); 
                } 
                else if (type == "pvp_solo") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, tmpl, 1); 
                }
                else if (type == "pvp_squad") {
                    int squadSize = group.getIntField("squadSize");
                    for (int s = 0; s < squadSize; ++s) {
                        float offX = loc.x + (s * 1.5f);
                        spawnSimPlayer(loc.planet, offX, loc.y, loc.z, tmpl, 1);
                    }
                }
            }
            
            templates.pop();
            group.pop();
        }
    }
    groups.pop();
    config.pop();
}

void SimPlayerManager::spawnSimPlayer(const String& planet, float x, float y, float z, const String& templateName, int type) {
    try {
        ZoneServer* zoneServer = ServerCore::getZoneServer();
        if (zoneServer == nullptr) return;

        Zone* zone = zoneServer->getZone(planet);
        if (zone == nullptr) {
            error("Could not find zone: " + planet);
            return;
        }

        CreatureManager* creatureManager = zone->getCreatureManager();
        if (creatureManager == nullptr) return;

        // Safety Height
        if (z == 0) z = zone->getHeight(x, y); 

        // SPAWN: X, Z (North), Y (Height)
        CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0, x, y, z, 0);
        
        if (creature == nullptr) {
            error("Failed to spawn SimPlayer template: " + templateName);
            return;
        }

        AiAgent* agent = creature->asAiAgent();
        if (agent == nullptr) return;

        // --- NAME GENERATION SAFETY ---
        try {
            NameManager* nm = zoneServer->getNameManager();
            if (nm != nullptr) {
                // 0 = Generic/Human. 
                String name = nm->makeCreatureName(0, creature->getSpecies()); 
                if (!name.isEmpty()) agent->setCustomObjectName(name, true);
            }
        } catch (...) {
            error("SimPlayerManager: Name generation failed for " + templateName);
            agent->setCustomObjectName("Sim Player", true);
        }

        agent->setFactionRank(1); 
        agent->setDespawnOnNoPlayerInRange(false);

        Reference<SimPlayerController*> ctrl = nullptr;

        if (type == 1) { // PvP
            bool isImp = (templateName.contains("stormtrooper"));
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
        error("SimPlayerManager: Error spawning bot: " + e.getMessage());
    } catch (...) {
        error("SimPlayerManager: Unknown error spawning bot.");
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