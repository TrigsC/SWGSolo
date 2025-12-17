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
#include "server/zone/objects/creature/ai/PatrolPoint.h" 
#include "templates/params/creature/ObjectFlag.h" 

SimPlayerManager::SimPlayerManager() {
    setLoggingName("SimPlayerManager");
}

SimPlayerManager::~SimPlayerManager() {
}

void SimPlayerManager::initialize() {
    info("Initializing SimPlayer Manager...", true);
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

    uint32 templateCRC = templateName.hashCode();
    CreatureTemplate* tmpl = CreatureTemplateManager::instance()->getTemplate(templateCRC);
    
    if (tmpl == nullptr) {
        error("Spawn Failed: Template '" + templateName + "' is not loaded.");
        return;
    }

    if (tmpl->getTemplates().size() == 0) {
        error("Spawn Failed: Template '" + templateName + "' has no IFF files.");
        return;
    }
    
    String iffPath = tmpl->getTemplates().get(0);
    uint32 iffCRC = iffPath.hashCode();

    info("Spawn Info: Mapped '" + templateName + "' -> '" + iffPath + "'", true);

    float z = zone->getHeight(x, y);

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

    agent->loadTemplateData(tmpl);

    // 1. FORCE SPEED & STATS
    agent->setRunSpeed(5.5f); 
    agent->setWalkSpeed(5.5f);
    for (int i=0; i<9; ++i) {
        agent->setMaxHAM(i, 5000, true);
        agent->setHAM(i, 5000);
    }

    agent->setHomeLocation(x, z, y, nullptr);
    
    // 2. PACIFIST & BLIND MODE (The Fix)
    // Instead of using setters that don't exist, we just WIPE the flags.
    // Setting these to 0 removes AGGRESSIVE, ENEMY, PACK, KILLER, etc.
    // This makes the AI "Neutral" and "Oblivious" to the world.
    agent->setCreatureBitmask(0); 
    agent->setPvpStatusBitmask(0); 
    
    // Explicitly prevent any "Observer" distractions
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
        
        // Disable SimBot flag so normal physics apply if it returns to normal AI
        agent->setSimPlayerBot(false);
        return;
    } else {
        info("Starting SimPlayer for agent " + String::valueOf(oid), true);
        
        // Common Setup
        agent->setCustomAiMap(String("patrol").hashCode());
        agent->setAITemplate(); 
        
        agent->writeBlackboard("simAlwaysActive", true);
        agent->setSimAlwaysActive(true);
        agent->setSimPlayerBot(true);
        agent->setDespawnOnNoPlayerInRange(false);

        // --- FACTORY LOGIC ---
        Reference<SimPlayerController*> ctrl = nullptr;
        
        // We can check template name here to decide:
        const CreatureTemplate* tmpl = agent->getCreatureTemplate();
        if (tmpl && tmpl->getTemplateName() == "rebel_trooper") {
             //ctrl = new SimPvPController(agent);
             ctrl = new SimMinerController(agent);
        } else {
             ctrl = new SimMinerController(agent);
        }

        controllers.put(oid, ctrl);
        
        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }
}