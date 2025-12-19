/*
 * SimPlayerManager.cpp
 * Phase 2: Lua Configuration Reader
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
#include "server/zone/managers/director/DirectorManager.h" // For Lua access

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
    info("Initializing SimPlayer Manager via Lua...", true);
    loadLuaConfig();
}

void SimPlayerManager::loadLuaConfig() {
    // Run the script
    lua->runFile("scripts/managers/sim_player_manager.lua");

    // Get the main config table
    LuaObject config = lua->getGlobalObject("SimPlayerManagerConfig");
    if (!config.isValidTable()) {
        error("Failed to load SimPlayerManagerConfig from lua!");
        return;
    }

    // Check enabled
    bool enabled = config.getBooleanField("enabled");
    if (!enabled) {
        info("SimPlayer system disabled in Lua.", true);
        return;
    }

    // 1. Load Shuttleports into a flattened list for random picking
    // We will store them temporarily to pick from
    struct LocationEntry {
        String planet;
        float x, y, z; // Y is North in our Lua, Z is Height
        float hangX, hangY, hangZ;
    };
    Vector<LocationEntry> locationList;

    LuaObject shuttles = config.getObjectField("shuttleports");
    if (shuttles.isValidTable()) {
        // Iterate Planets (naboo, tatooine...)
        for (int i = 1; i <= shuttles.getTableSize(); ++i) { // Lua loops usually need care with iterators, strictly speaking LuaObject doesn't iterate keys easily without popping.
            // Simplified Approach: We assume specific planet keys or we iterate strictly if we write a helper.
            // For now, let's hardcode the planet lookups to keep C++ simple, or we can use the DirectorManager logic.
            // Actually, let's just look for known planets to be safe.
            const char* planets[] = {"naboo", "tatooine", "corellia", "dantooine", "talus", "rori", "lok", "yavin4", "endor", "dathomir"};
            
            for (const char* pName : planets) {
                LuaObject planetTable = shuttles.getObjectField(pName);
                if (planetTable.isValidTable()) {
                    // Iterate cities in planet
                    for (int j = 1; j <= planetTable.getTableSize(); ++j) {
                        LuaObject city = planetTable.getObjectAt(j);
                        if (city.isValidTable()) {
                            LuaObject spawn = city.getObjectField("spawn");
                            LuaObject hangout = city.getObjectField("hangout");
                            
                            LocationEntry entry;
                            entry.planet = pName;
                            entry.x = spawn.getFloatAt(1);
                            entry.y = spawn.getFloatAt(2); // In Lua we put North here
                            entry.z = spawn.getFloatAt(3); // Height
                            
                            // Store hangout if needed for PvP logic later
                            // ...
                            
                            locationList.add(entry);
                        }
                        city.pop();
                    }
                }
                planetTable.pop();
            }
        }
    }
    shuttles.pop();

    if (locationList.size() == 0) {
        error("No shuttleports defined in Lua!");
        return;
    }

    // 2. Process Spawn Groups
    LuaObject groups = config.getObjectField("spawnGroups");
    if (groups.isValidTable()) {
        for (int i = 1; i <= groups.getTableSize(); ++i) {
            LuaObject group = groups.getObjectAt(i);
            
            String type = group.getStringField("type");
            int count = group.getIntField("totalCount");
            LuaObject templates = group.getObjectField("templates");
            
            // Generate the requested number of bots
            for (int k = 0; k < count; ++k) {
                // Pick Random Location
                int locIndex = System::random(locationList.size() - 1);
                LocationEntry loc = locationList.get(locIndex);

                // Pick Random Template
                String tmpl = "";
                if (templates.isValidTable() && templates.getTableSize() > 0) {
                    int tIdx = 1 + System::random(templates.getTableSize() - 1);
                    tmpl = templates.getStringAt(tIdx);
                }

                if (type == "miner") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, tmpl, 0); // 0 = Miner
                } 
                else if (type == "pvp_solo") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, tmpl, 1); // 1 = PvP Solo
                }
                else if (type == "pvp_squad") {
                    // Phase 2.5: For now just spawn them individually at same spot
                    // Later we link them.
                    int squadSize = group.getIntField("squadSize");
                    for (int s = 0; s < squadSize; ++s) {
                        // Offset slightly so they don't stack
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
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr) return;

    Zone* zone = zoneServer->getZone(planet);
    if (zone == nullptr) {
        error("Could not find zone: " + planet);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr) return;

    // Use provided Z (height) or lookup if 0
    if (z == 0) z = zone->getHeight(x, y); 

    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0, x, z, y, 0);
    if (creature == nullptr) {
        error("Failed to spawn SimPlayer template: " + templateName);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr) return;

    // --- COSMETICS ---
    NameManager* nm = zoneServer->getNameManager();
    if (nm != nullptr) {
        String name = nm->makeCreatureName(0, creature->getSpecies()); 
        if (!name.isEmpty()) agent->setCustomObjectName(name, true);
    }
    agent->setFactionRank(1); 

    // --- SETUP CONTROLLER ---
    agent->setDespawnOnNoPlayerInRange(false);

    // Initialize the specific controller based on type INT passed from Lua
    Reference<SimPlayerController*> ctrl = nullptr;

    if (type == 1) { // PvP
        bool isImp = (templateName.contains("stormtrooper"));
        agent->setFactionStatus(FactionStatus::OVERT);
        agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE);
        ctrl = new SimPvPController(agent, isImp); 
    } else { // Miner / Default
        agent->setFactionStatus(FactionStatus::ONLEAVE);
        agent->setPvpStatusBitmask(0); 
        ctrl = new SimMinerController(agent);
    }

    // Lock and Load
    toggleBot(agent); // This registers existing controllers map
    
    // Manually inject the NEW controller if toggleBot didn't (toggleBot usually creates one)
    // Actually, let's refactor toggleBot or just register here directly.
    // For Phase 2 cleanliness, we should register here:
    
    if (controllers.contains(agent->getObjectID())) {
        controllers.drop(agent->getObjectID());
    }
    controllers.put(agent->getObjectID(), ctrl);
    
    agent->activateAiBehavior(true);
    
    // Start Loop with delay
    Core::getTaskManager()->scheduleTask([ctrl] () {
        ctrl->startSimLoop();
    }, "SimStartLambda", 10000); 
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