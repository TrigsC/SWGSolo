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
    
    // Using 'light_jedi_sentinel' as the test subject since we confirmed it works via commands.
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

    // 1. SAFETY CHECK: Verify template exists
    // We calculate the hash once.
    uint32 templateCRC = templateName.hashCode();
    
    // FIX: Check if the template is actually loaded in memory
    CreatureTemplate* tmpl = CreatureTemplateManager::instance()->getTemplate(templateCRC);
    if (tmpl == nullptr) {
        error("Spawn Failed: Template '" + templateName + "' is NOT loaded in CreatureTemplateManager. The server does not know this NPC exists yet.");
        return;
    }

    // Find the Z (Height) at this location
    float z = zone->getHeight(x, y);

    info("Attempting to spawn SimPlayer [" + templateName + "] on " + planet + " at " + String::valueOf(x) + ", " + String::valueOf(y), true);

    // 2. SPAWN
    CreatureObject* creature = creatureManager->spawnCreature(templateCRC, x, z, y, 0);

    if (creature == nullptr) {
        error("Failed to spawn creature via CreatureManager (spawnCreature returned null).");
        return;
    }

    if (!creature->isAiAgent()) {
        error("Spawned entity is not an AiAgent.");
        return;
    }

    AiAgent* agent = creature->asAiAgent();

    // 3. FORCE STATS
    Locker lock(agent);
    
    // Force stats so they don't walk slowly or die easily
    agent->setRunSpeed(6.0f); 
    for (int i=0; i<9; ++i) {
        agent->setMaxHAM(i, 5000, true);
        agent->setHAM(i, 5000);
    }

    // 4. PREVENT LEASHING
    agent->setHomeLocation(x, z, y, nullptr);
    
    // Remove Pack/Herd behaviors to prevent interference
    agent->setCreatureBitmask(agent->getCreatureBitmask() & ~ObjectFlag::PACK & ~ObjectFlag::HERD);

    // 5. ATTACH SIMPLAYER
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
        return;
    } else {
        info("Starting SimPlayer for agent " + String::valueOf(oid), true);
        
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