/*
 * SimPlayerManager.cpp
 * Fixes: Startup Deadlock & Compiler Error
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

SimPlayerManager::SimPlayerManager() {
    setLoggingName("SimPlayerManager");
}

SimPlayerManager::~SimPlayerManager() {
}

void SimPlayerManager::initialize() {
    info("Initializing SimPlayer Manager...", true);
    
    // 1. Miner (Jedi Visual)
    spawnSimPlayer("naboo", 4714.0f, -4939.0f, "light_jedi_sentinel");

    // 2. Miner (Artisan Visual)
    spawnSimPlayer("naboo", 4720.0f, -4945.0f, "artisan");

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

    // --- COSMETICS ---
    NameManager* nm = zoneServer->getNameManager();
    if (nm != nullptr) {
        // Use Type 0 to generate a Human Name (First Last) instead of TK-123
        String name = nm->makeCreatureName(0, creature->getSpecies()); 
        if (!name.isEmpty()) {
            agent->setCustomObjectName(name, true);
        }
    }

    // Rank 1 tells the client "This is a Private", enabling Faction Colors (Purple/Red)
    agent->setFactionRank(1); 

    // --- FLAGS ---
    // We set these IMMEDIATELY so the client sees them correct on spawn
    if (templateName == "stormtrooper" || templateName == "rebel_trooper") {
        // PvP Bot: OVERT (Purple/Red)
        agent->setFactionStatus(FactionStatus::OVERT);
        agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE);
    } else {
        // Miner: ONLEAVE (Blue/White)
        agent->setFactionStatus(FactionStatus::ONLEAVE);
        agent->setPvpStatusBitmask(0); 
    }

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