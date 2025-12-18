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
#include "server/zone/objects/creature/ai/CreatureTemplate.h" 
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h" 
#include "templates/params/creature/ObjectFlag.h"
#include "server/zone/managers/name/NameManager.h"

SimPlayerManager::SimPlayerManager() {
    setLoggingName("SimPlayerManager");
}

SimPlayerManager::~SimPlayerManager() {
}

void SimPlayerManager::initialize() {
    info("Initializing SimPlayer Manager...", true);
    
    // 1. Miner (Jedi Visual)
    //spawnSimPlayer("naboo", 4714.0f, -4939.0f, "light_jedi_sentinel");

    // 2. Miner (Artisan Visual)
    //spawnSimPlayer("naboo", 4720.0f, -4945.0f, "artisan");

    // 3. PvP Bot (Stormtrooper)
    spawnSimPlayer("naboo", 4963.0f, -4892.0f, "stormtrooper");
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

    float z = zone->getHeight(x, y); 

    CreatureObject* creature = creatureManager->spawnCreature(templateName.hashCode(), 0, x, z, y, 0);
    if (creature == nullptr) {
        error("Failed to spawn SimPlayer template: " + templateName);
        return;
    }

    AiAgent* agent = creature->asAiAgent();
    if (agent == nullptr) return;
    
    // 1. Generate "Real" Name
    NameManager* nm = zoneServer->getNameManager();
    if (nm != nullptr) {
        // Use type 0 (Generic) to ensure we get a First/Last name (e.g. "Gary Retski")
        // avoiding "TK-421" or droid names.
        int species = creature->getSpecies();
        String name = nm->makeCreatureName(0, species); 
        
        if (!name.isEmpty()) {
            // This overrides the default name and removes the (Template Title) suffix
            agent->setCustomObjectName(name, true);
        }
    }

    agent->setFactionRank(0);

    // Reset default flags
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
             // PvP Bot: Make Attackable + Overt
             // Note: Controller will enforce Overt, but we set Attackable here to be safe
             agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);
             ctrl = new SimPvPController(agent, true); 
        } 
        else if (tName == "rebel_trooper") {
             // PvP Bot: Make Attackable + Overt
             agent->setPvpStatusBitmask(ObjectFlag::ATTACKABLE | ObjectFlag::OVERT);
             ctrl = new SimPvPController(agent, false);
        }
        else {
             // Miner: Make Neutral/Unattackable (0 removes ATTACKABLE flag)
             agent->setPvpStatusBitmask(0); 
             ctrl = new SimMinerController(agent);
        }

        controllers.put(oid, ctrl);
        
        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }
}