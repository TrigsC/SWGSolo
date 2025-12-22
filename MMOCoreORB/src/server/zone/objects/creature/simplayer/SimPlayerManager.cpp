/*
 * SimPlayerManager.cpp
 * Cleaned: Duplicate spawn function removed.
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
    
    struct LocationEntry {
        String planet;
        float x, y, z;          // Spawn Loc
        float hx, hy, hz;       // Hangout Loc
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

    std::vector<LocationEntry> locationList; 

    // --- LOAD SHUTTLEPORTS ---
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
                        LuaObject hangout = city.getObjectField("hangout");
                        
                        if (spawn.isValidTable() && hangout.isValidTable()) {
                            LocationEntry entry;
                            entry.planet = pName;
                            
                            entry.x = spawn.getFloatAt(1);
                            entry.y = spawn.getFloatAt(2); // North
                            entry.z = spawn.getFloatAt(3); // Height

                            entry.hx = hangout.getFloatAt(1);
                            entry.hy = hangout.getFloatAt(2); // North
                            entry.hz = hangout.getFloatAt(3); // Height

                            locationList.push_back(entry);
                        } 
                        spawn.pop();
                        hangout.pop();
                    }
                    city.pop();
                }
            }
            planetTable.pop();
        }
    }
    shuttles.pop();

    if (locationList.empty()) {
        error("DEBUG: ABORTING - No spawn locations found.");
        return;
    }

    // --- PROCESS GROUPS ---
    LuaObject groups = config.getObjectField("spawnGroups");
    if (groups.isValidTable()) {
        int groupCount = groups.getTableSize();
        
        for (int i = 1; i <= groupCount; ++i) {
            LuaObject group = groups.getObjectAt(i);
            String type = group.getStringField("type");
            int count = group.getIntField("totalCount");
            LuaObject templates = group.getObjectField("templates");
            
            for (int k = 0; k < count; ++k) {
                if (locationList.empty()) break;
                int locIndex = System::random(locationList.size());
                LocationEntry loc = locationList.at(locIndex);

                String tmpl = "";
                if (templates.isValidTable()) {
                    int tSize = templates.getTableSize();
                    if (tSize > 0) tmpl = templates.getStringAt(1 + System::random(tSize));
                }

                if (tmpl.isEmpty()) continue;

                if (type == "miner") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, loc.hx, loc.hy, loc.hz, tmpl, 0); 
                } 
                else if (type == "pvp_solo") {
                    spawnSimPlayer(loc.planet, loc.x, loc.y, loc.z, loc.hx, loc.hy, loc.hz, tmpl, 1); 
                }
            }
            templates.pop();
            group.pop();
        }
    }
    groups.pop();
}

void SimPlayerManager::spawnSimPlayer(const String& planet, float x, float y, float z, float hx, float hy, float hz, const String& templateName, int type) {
    try {
        ZoneServer* zoneServer = ServerCore::getZoneServer();
        if (zoneServer == nullptr) return;

        Zone* zone = zoneServer->getZone(planet);
        if (zone == nullptr) return;

        CreatureManager* creatureManager = zone->getCreatureManager();
        if (creatureManager == nullptr) return;

        uint32 templateCRC = templateName.hashCode();
        CreatureTemplate* tmpl = CreatureTemplateManager::instance()->getTemplate(templateCRC);

        if (tmpl == nullptr || tmpl->getTemplates().size() == 0) {
            error("DEBUG: Template failure: " + templateName);
            return;
        }

        String iffPath = tmpl->getTemplates().get(0);
        uint32 iffCRC = iffPath.hashCode();

        float finalZ = z; 
        if (finalZ == 0 || finalZ < -10000 || finalZ > 10000) finalZ = zone->getHeight(x, y); 

        CreatureObject* creature = creatureManager->spawnCreature(iffCRC, x, finalZ, y, 0);
        if (creature == nullptr) return;

        AiAgent* agent = creature->asAiAgent();
        if (agent == nullptr) return;

        Locker lock(agent);
        agent->loadTemplateData(tmpl);
        agent->setRunSpeed(5.0f); 
        for (int i=0; i<9; ++i) agent->setHAM(i, agent->getMaxHAM(i));
        
        agent->setHomeLocation(x, finalZ, y, nullptr);
        agent->setDespawnOnNoPlayerInRange(false);

        agent->writeBlackboard("targetX", hx);
        agent->writeBlackboard("targetY", hy); // North
        agent->writeBlackboard("targetZ", hz); // Height

        Reference<SimPlayerController*> ctrl = nullptr;

        if (type == 1) { 
            bool isImp = (templateName.contains("stormtrooper") || templateName.contains("imperial"));
            agent->setFactionStatus(FactionStatus::OVERT);
            agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE);
            ctrl = new SimPvPController(agent, isImp); 
        } else { 
            agent->setFactionStatus(FactionStatus::ONLEAVE);
            agent->setPvpStatusBitmask(0); 
            ctrl = new SimMinerController(agent);
        }

        if (controllers.contains(agent->getObjectID())) controllers.drop(agent->getObjectID());
        controllers.put(agent->getObjectID(), ctrl);
        
        agent->activateAiBehavior(true);
        
        Core::getTaskManager()->scheduleTask([ctrl] () {
            ctrl->startSimLoop();
        }, "SimStartLambda", 10000); 

    } catch (Exception& e) {
        error("DEBUG: Exception in spawnSimPlayer: " + e.getMessage());
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

        Core::getTaskManager()->scheduleTask([ctrl] () {
            ctrl->startSimLoop();
        }, "SimStartLambda", 10000); 
    }
}