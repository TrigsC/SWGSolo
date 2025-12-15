/*
 * SimPlayerManager.cpp
 */

#include "SimPlayerManager.h"
#include "server/zone/ZoneServer.h"
#include "server/ServerCore.h"
#include "server/zone/managers/creature/CreatureManager.h"
#include "server/zone/managers/creature/CreatureTemplateManager.h" 
#include "server/zone/objects/creature/ai/CreatureTemplate.h" 
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "templates/params/creature/ObjectFlag.h" 

SimPlayerManager::SimPlayerManager() {
    setLoggingName("SimPlayerManager");
}

SimPlayerManager::~SimPlayerManager() {
}

void SimPlayerManager::initialize() {
    info("Initializing SimPlayer Manager...", true);

    // ---------------------------------------------------------
    // POPULATION CONTROL
    // ---------------------------------------------------------
    
    // Spawn the Jedi (now with a working brain!)
    spawnSimPlayer("naboo", 4714.0f, -4939.0f, "light_jedi_sentinel");
}

void SimPlayerManager::spawnSimPlayer(const String& planet, float x, float y, const String& templateName) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr) return;

    Zone* zone = zoneServer->getZone(planet);
    if (zone == nullptr) {
        error("Could not find zone: " + planet);
        return;
    }

    CreatureManager* creatureManager = zone->getCreatureManager();
    if (creatureManager == nullptr) return;

    // 1. GET THE LUA TEMPLATE
    uint32 luaCRC = templateName.hashCode();
    CreatureTemplate* tmpl = CreatureTemplateManager::instance()->getTemplate(luaCRC);
    
    if (tmpl == nullptr) {
        error("Spawn Failed: Template '" + templateName + "' is not loaded in CreatureTemplateManager.");
        return;
    }

    // 2. EXTRACT THE REAL IFF PATH
    if (tmpl->getTemplates().size() == 0) {
        error("Spawn Failed: Template '" + templateName + "' has no IFF files defined.");
        return;
    }
    
    String iffPath = tmpl->getTemplates().get(0);
    uint32 iffCRC = iffPath.hashCode();

    info("Spawn Info: Mapped '" + templateName + "' -> '" + iffPath + "'", true);

    float z = zone->getHeight(x, y);

    // 3. SPAWN
    CreatureObject* creature = creatureManager->spawnCreature(iffCRC, x, z, y, 0);

    if (creature == nullptr) {
        error("Failed to spawn creature via CreatureManager.");
        return;
    }

    if (!creature->isAiAgent()) {
        error("Spawned entity is not an AiAgent.");
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    Locker lock(agent);

    // 4. APPLY TEMPLATE & STATS
    agent->loadTemplateData(tmpl);

    // Force Speed & Stats
    agent->setRunSpeed(6.0f); 
    for (int i=0; i<9; ++i) {
        agent->setMaxHAM(i, 5000, true);
        agent->setHAM(i, 5000);
    }

    // 5. PREVENT LEASHING
    agent->setHomeLocation(x, z, y, nullptr);
    agent->setCreatureBitmask(agent->getCreatureBitmask() & ~ObjectFlag::PACK & ~ObjectFlag::HERD);

    // 6. ATTACH SIMPLAYER
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
        
        // Restore original brain (Optional, but good practice)
        // If we knew the original map, we'd set it back here.
        // For now, we just reactivate behavior.
        agent->activateAiBehavior(true);
        return;
    } else {
        info("Starting SimPlayer for agent " + String::valueOf(oid), true);
        
        // --- BRAIN TRANSPLANT ---
        // Override the 'enclaveSentinel' map with 'default'.
        // This gives the bot the standard logic needed to process Patrol points.
        agent->setCustomAiMap(String("default").hashCode());
        agent->setAITemplate(); // Reloads the tree based on the new map
        
        agent->writeBlackboard("simAlwaysActive", true);
        agent->setSimAlwaysActive(true);
        agent->setSimPlayerBot(true);
        agent->setDespawnOnNoPlayerInRange(false);

        Reference<SimPlayerController*> ctrl = new SimPlayerController(agent);
        controllers.put(oid, ctrl);
        
        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }
}