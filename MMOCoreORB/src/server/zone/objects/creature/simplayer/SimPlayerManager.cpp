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

        // 1. Construct Template Path
        String fullTemplate = templateName;
        if (!fullTemplate.contains("object/")) {
            fullTemplate = "object/mobile/" + templateName + ".iff";
        }

        // 2. Calculate CRC (Debug Step)
        uint32 templateCRC = fullTemplate.hashCode();
        
        if (templateCRC == 0) {
            error("DEBUG: CRITICAL - Hash for string '" + fullTemplate + "' is 0! String might be empty or corrupt.");
            return;
        }

        // 3. Resolve Height
        // SWGEmu standard: X (East/West), Z (North/South), Y (Altitude/Height)
        // Your config: x, y (North), z (Height)
        // So we pass: x, z (as height), y (as north) ? 
        // NO: The standard spawnCreature takes (CRC, X, Z, Y, Parent).
        // Where Z is usually North in 3D, but SWG uses Z as North and Y as Height.
        // Wait, 'spawnCreature(crc, x, z, y)' -> X, Z(North), Y(Height).
        
        float finalZ = z; // Height
        if (finalZ == 0 || finalZ < -10000 || finalZ > 10000) {
            finalZ = zone->getHeight(x, y); 
        }

        info("DEBUG: Spawning " + fullTemplate + " (CRC: " + String::valueOf(templateCRC) + ") @ " 
             + String::valueOf(x) + ", " + String::valueOf(y) + " (H:" + String::valueOf(finalZ) + ")", true);

        // 4. SPAWN CALL
        // STANDARD CORE3 SIGNATURE: spawnCreature(uint32 templateCRC, float x, float z, float y, uint64 parentID = 0)
        // Your previous code had 6 arguments (an extra 0 after hash). I have removed it.
        // We pass: CRC, x, y (North), finalZ (Height), 0 (Parent)
        
        CreatureObject* creature = creatureManager->spawnCreature(templateCRC, x, y, finalZ, 0);
        
        if (creature == nullptr) {
            error("DEBUG: Spawn Failed! Object is null. Template might not exist in .tre files: " + fullTemplate);
            return;
        }

        AiAgent* agent = creature->asAiAgent();
        if (agent == nullptr) {
             error("DEBUG: Spawned object is not an AiAgent.");
             return;
        }

        // 5. Setup Name and Controller
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
            // Check for stormtrooper specifically, else rebel
            bool isImp = (fullTemplate.contains("stormtrooper"));
            
            // Set Faction Status
            agent->setFactionStatus(FactionStatus::OVERT);
            agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE);
            
            // Create Controller
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
        
        // Schedule startup
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