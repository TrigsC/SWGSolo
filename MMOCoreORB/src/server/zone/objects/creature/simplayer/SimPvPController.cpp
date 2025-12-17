/*
 * SimPvPController.cpp
 * Fixed Includes and Flags
 */

#include "SimPvPController.h"
#include "engine/core/Core.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "server/zone/managers/faction/FactionManager.h"
#include "server/zone/objects/area/ActiveArea.h"
#include "server/zone/CloseObjectsVector.h"
#include "engine/spatial/QuadTreeEntry.h" 
#include "templates/params/creature/ObjectFlag.h"

SimPvPController::SimPvPController(AiAgent* aiAgent, bool imperial) : SimPlayerController(aiAgent) {
    isImperial = imperial;
    returningToShuttle = false;
    runSpeed = 6.5f; // PvP bots run slightly faster
    setLoggingName("SimPvPController");
}

SimPvPController::~SimPvPController() {
}

void SimPvPController::startSimLoop() {
    if (agent == nullptr) return;

    // 1. Setup Faction (Make them Overt)
    agent->setFaction(isImperial ? String("imperial").hashCode() : String("rebel").hashCode());
    
    // FIX: Use ObjectFlag::OVERT
    agent->setPvpStatusBitmask(ObjectFlag::OVERT); 
    
    // 2. Define Route
    // Start: 4963, 3, -4892 (Shuttle)
    // End: 4807, 4, -4700 (Starport)
    spawnLocation = Vector3(4963.0f, -4892.0f, 3.0f);
    hangoutLocation = Vector3(4807.0f, -4700.0f, 4.0f);

    // 3. Start Patrol
    Logger::console.info("SimPvP: Spawning at Shuttle. Moving to Starport.", true);
    startPatrol();
}

void SimPvPController::startPatrol() {
    state = SimPlayerController::MOVING;
    returningToShuttle = false;
    moveTo(hangoutLocation);
}

void SimPvPController::returnToShuttle() {
    state = SimPlayerController::MOVING;
    returningToShuttle = true;
    Logger::console.info("SimPvP: Patrol done. Returning to Shuttle.", true);
    moveTo(spawnLocation);
}

void SimPvPController::onArrived() {
    if (returningToShuttle) {
        despawn();
    } else {
        startLoitering();
    }
}

void SimPvPController::startLoitering() {
    // FIX: Scope the enum correctly
    state = SimPlayerController::WAITING;
    Logger::console.info("SimPvP: Arrived at Starport. Scanning area for 30s...", true);
    
    // Look around animation
    if (agent != nullptr) agent->doAnimation("look_around");

    Reference<SimPvPBehaviorTask*> task = new SimPvPBehaviorTask(this);
    task->schedule(30000); // 30 seconds
}

void SimPvPController::finishLoitering() {
    returnToShuttle();
}

void SimPvPController::despawn() {
    Logger::console.info("SimPvP: Despawning.", true);
    if (agent != nullptr) {
        agent->destroyObjectFromWorld(true);
    }
}

// ----------------------------------------------------
// THE PVP SCANNER (Called every 500ms via checkArrival)
// ----------------------------------------------------
void SimPvPController::onTick() {
    if (agent == nullptr || agent->isDead()) return;
    if (agent->isInCombat()) return; // Already fighting

    scanForTargets();
}

void SimPvPController::scanForTargets() {
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    // Get nearby objects
    CloseObjectsVector* vec = (CloseObjectsVector*) agent->getCloseObjects();
    if (vec == nullptr) return;

    // FIX: Include QuadTreeEntry.h to make this vector valid
    Vector<QuadTreeEntry*> objects;
    vec->safeCopyReceiversTo(objects, Vector<QuadTreeEntry*>());

    for (int i = 0; i < objects.size(); ++i) {
        SceneObject* obj = static_cast<SceneObject*>(objects.get(i));
        if (obj == nullptr || !obj->isPlayerCreature()) continue;

        CreatureObject* player = obj->asCreatureObject();
        if (player == nullptr || player->isIncapacitated() || player->isDead()) continue;

        // --- INTERIOR CHECK (The "No NavMesh" Fix) ---
        // If player is inside a building (Parent != null), ignore them.
        if (player->getParent() != nullptr) continue; 

        // Check Faction
        bool playerImp = (player->getFaction() == String("imperial").hashCode());
        bool playerReb = (player->getFaction() == String("rebel").hashCode());
        
        bool isEnemy = false;
        if (isImperial && playerReb) isEnemy = true;
        if (!isImperial && playerImp) isEnemy = true;

        // Check PvP Status (Simple check for now)
        if (isEnemy && player->isAttackableBy(agent)) {
            
            float dist = agent->getDistanceTo(player);
            if (dist < 40.0f) { // Agro range
                Logger::console.info("SimPvP: ENGAGING TARGET: " + player->getFirstName(), true);
                
                // Hand over control to Core3 Combat Logic
                agent->setTargetObject(player);
                agent->addDefender(player);
                agent->setCombatState();
                
                // Stop patrol logic
                state = SimPlayerController::IDLE; 
                return; 
            }
        }
    }
}