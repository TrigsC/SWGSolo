/*
 * SimPlayerManager.cpp
 */

#include "SimPlayerManager.h"
#include "server/zone/ZoneServer.h"
#include "server/ServerCore.h"
#include "server/zone/managers/creature/CreatureManager.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/region/CityRegion.h"

SimPlayerManager::SimPlayerManager() {
    setLoggingName("SimPlayerManager");
}

SimPlayerManager::~SimPlayerManager() {
}

void SimPlayerManager::initialize() {
    info("Initializing SimPlayer Manager...", true);

    // ---------------------------------------------------------
    // POPULATION CONTROL
    // Define your test spawns here.
    // ---------------------------------------------------------
    
    // Test 1: Spawn 1 Artisan on Naboo (Theed outskirts)
    // Coords roughly based on your log: x:4714, z:3.75, y:-4939
    // Note: Z is Y in SWG coords usually (X, Z, Y in code vs X, Y, Z in game). 
    // spawnSimPlayer args: Planet, X, Y (2D coordinates), Template
    
    spawnSimPlayer("naboo", 4714.0f, -4939.0f, "artisan");

    // Example: Spawn a Jedi nearby
    // spawnSimPlayer("naboo", 4720.0f, -4935.0f, "light_jedi_sentinel");
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

    // Find the Z (Height) at this location so they don't spawn underground
    float z = zone->getHeight(x, y);

    // 1. Create the Creature
    // We use a CRC hash of the template string if available, or just the string name logic
    uint32 templateCRC = templateName.hashCode();
    
    info("Attempting to spawn SimPlayer [" + templateName + "] on " + planet + " at " + String::valueOf(x) + ", " + String::valueOf(y), true);

    CreatureObject* creature = creatureManager->spawnCreature(templateCRC, x, z, y, 0);

    if (creature == nullptr) {
        error("Failed to spawn creature with template: " + templateName);
        return;
    }

    if (!creature->isAiAgent()) {
        error("Spawned entity is not an AiAgent. SimPlayer requires AiAgent.");
        return;
    }

    AiAgent* agent = creature->asAiAgent();

    // 2. FORCE STATS (Fixing the 'Artisan walk problem')
    Locker lock(agent);

    // Force Run Speed (Artisans are slow, make them efficient)
    agent->setRunSpeed(6.0f); 
    
    // Force HAM (Hitpoints) so they don't die to a stiff breeze
    for (int i=0; i<9; ++i) {
        agent->setMaxHAM(i, 2000, true);
        agent->setHAM(i, 2000);
    }

    // 3. PREVENT LEASHING & SETUP AI
    // Set Home Location to current spot (redundant but safe)
    agent->setHomeLocation(x, z, y, nullptr);
    
    // Disable standard AI packs/herds to prevent interference
    agent->setCreatureBitmask(agent->getCreatureBitmask() & ~Pack & ~Herd);

    // 4. ATTACH SIMPLAYER CONTROLLER
    toggleBot(agent);
}

void SimPlayerManager::toggleBot(AiAgent* agent) {
    if (agent == nullptr) return;

    uint64 oid = agent->getObjectID();

    if (controllers.contains(oid)) {
        info("Stopping SimPlayer for agent " + String::valueOf(oid), true);
        agent->eraseBlackboard("simAlwaysActive");
        controllers.drop(oid);
        
        // Reset to normal AI
        agent->clearPatrolPoints();
        agent->clearSavedPatrolPoints();
        agent->setMovementState(AiAgent::OBLIVIOUS);
        agent->activateAiBehavior(true);
        return;
    } else {
        info("Starting SimPlayer for agent " + String::valueOf(oid), true);
        
        // --- KEY FIX FOR LEASHING ---
        // We tell the AI logic "I am a SimPlayer, do not leash me."
        agent->writeBlackboard("simAlwaysActive", true);
        agent->setSimAlwaysActive(true);
        agent->setSimPlayerBot(true);
        
        // Don't despawn when players leave (we want persistent simulation)
        agent->setDespawnOnNoPlayerInRange(false);

        // Create and store controller
        Reference<SimPlayerController*> ctrl = new SimPlayerController(agent);
        controllers.put(oid, ctrl);
        
        // Kickoff
        agent->activateAiBehavior(true);
        ctrl->startSimLoop();
    }
}