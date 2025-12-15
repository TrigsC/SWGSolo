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
    
    // Spawn the Jedi.
    // This will now look up the correct IFF file before spawning.
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
    // The Creature Template holds a list of possible IFFs (e.g. variations). We pick the first one.
    if (tmpl->getTemplates().size() == 0) {
        error("Spawn Failed: Template '" + templateName + "' has no IFF files defined in its 'templates' list.");
        return;
    }
    
    String iffPath = tmpl->getTemplates().get(0);
    uint32 iffCRC = iffPath.hashCode();

    info("Spawn Info: Mapped '" + templateName + "' -> '" + iffPath + "'", true);

    // Find the Z (Height) at this location
    float z = zone->getHeight(x, y);

    // 3. SPAWN USING THE IFF CRC
    CreatureObject* creature = creatureManager->spawnCreature(iffCRC, x, z, y, 0);

    if (creature == nullptr) {
        error("Failed to spawn creature via CreatureManager (spawnCreature returned null).");
        return;
    }

    if (!creature->isAiAgent()) {
        error("Spawned entity is not an AiAgent.");
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    Locker lock(agent);

    // 4. APPLY THE LUA TEMPLATE STATS
    // This turns the generic "human" object into a "light_jedi_sentinel" (Level 88, Lightsaber, etc.)
    agent->loadTemplateData(tmpl);

    // 5. FORCE SIMPLAYER STATS & LOGIC
    // Force Run Speed (Player speed)
    agent->setRunSpeed(6.0f); 
    
    // Prevent Leashing
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