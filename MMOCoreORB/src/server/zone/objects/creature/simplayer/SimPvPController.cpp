/*
 * SimPvPController.cpp
 * FIXED: BlackboardData type conversion error
 */

#include "SimPvPController.h"
#include "engine/core/Core.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "server/zone/managers/faction/FactionManager.h"
#include "server/zone/objects/area/ActiveArea.h"
#include "server/zone/CloseObjectsVector.h"
#include "server/zone/TreeEntry.h" 
#include "templates/params/creature/ObjectFlag.h"
#include "server/zone/objects/creature/ai/bt/BlackboardData.h"

SimPvPController::SimPvPController(AiAgent* aiAgent, bool imperial) : SimPlayerController(aiAgent) {
    isImperial = imperial;
    returningToShuttle = false;
    runSpeed = 6.5f; 
    setLoggingName("SimPvPController");
}

SimPvPController::~SimPvPController() {
}

void SimPvPController::startSimLoop() {
    if (agent == nullptr) return;

    // 1. Faction Setup
    agent->setFaction(isImperial ? String("imperial").hashCode() : String("rebel").hashCode());
    agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE); 
    
    // 2. DYNAMIC LOCATIONS
    // Spawn Location: Wherever we are right now (set by SimPlayerManager)
    spawnLocation = agent->getWorldPosition();

    // Hangout Location: Read from the AI's memory (Blackboard)
    try {
        // FIX: Explicitly call .getFloat() to extract the value from BlackboardData wrapper
        float hx = agent->readBlackboard("targetX").getFloat();
        float hy = agent->readBlackboard("targetY").getFloat(); // North
        float hz = agent->readBlackboard("targetZ").getFloat(); // Height

        // Safety Check: If Lua failed to pass coords, fallback to current spot (Bot will just loiter)
        if (hx == 0 && hy == 0) {
            Logger::console.info("SimPvP: WARNING - No Blackboard Coords found. Loitering at spawn.", true);
            hangoutLocation = spawnLocation;
        } else {
            // Construct Vector3(X, North, Height) to match your previous hardcoded format
            hangoutLocation = Vector3(hx, hy, hz);
        }
    } catch (...) {
        Logger::console.error("SimPvP: Error reading blackboard coordinates. Defaulting to spawn.");
        hangoutLocation = spawnLocation;
    }

    Logger::console.info("SimPvP: Loop Started. Spawn: " + spawnLocation.toString() + " -> Hangout: " + hangoutLocation.toString(), true);
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
    state = SimPlayerController::WAITING;
    Logger::console.info("SimPvP: Arrived at Starport. Scanning area for 30s...", true);
    
    if (agent != nullptr) agent->doAnimation("look_around");

    Reference<SimPvPBehaviorTask*> task = new SimPvPBehaviorTask(this);
    task->schedule(30000); 
}

void SimPvPController::finishLoitering() {
    // FIX: Do not leave if in combat. Delay 5s.
    if (agent != nullptr && agent->isInCombat()) {
        Logger::console.info("SimPvP: Combat in progress. Extending loiter...", true);
        Reference<SimPvPBehaviorTask*> task = new SimPvPBehaviorTask(this);
        task->schedule(5000);
        return;
    }

    returnToShuttle();
}

void SimPvPController::despawn() {
    Logger::console.info("SimPvP: Despawning.", true);
    if (agent != nullptr) {
        agent->destroyObjectFromWorld(true);
    }
}

void SimPvPController::onTick() {
    if (agent == nullptr || agent->isDead()) return;
    if (agent->isInCombat()) return; 

    scanForTargets();
}

void SimPvPController::scanForTargets() {
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    CloseObjectsVector* vec = (CloseObjectsVector*) agent->getCloseObjects();
    if (vec == nullptr) return;

    Vector<TreeEntry*> objects;
    vec->safeCopyReceiversTo(objects, CloseObjectsVector::CREOTYPE);

    for (int i = 0; i < objects.size(); ++i) {
        SceneObject* obj = static_cast<SceneObject*>(objects.get(i));
        if (obj == nullptr || !obj->isPlayerCreature()) continue;

        CreatureObject* player = obj->asCreatureObject();
        if (player == nullptr || player->isIncapacitated() || player->isDead()) continue;

        if (player->getParent() != nullptr) continue; 

        bool playerImp = (player->getFaction() == String("imperial").hashCode());
        bool playerReb = (player->getFaction() == String("rebel").hashCode());
        
        bool isEnemy = false;
        if (isImperial && playerReb) isEnemy = true;
        if (!isImperial && playerImp) isEnemy = true;

        if (isEnemy && player->isAttackableBy(agent)) {
            
            float dist = agent->getDistanceTo(player);
            if (dist < 40.0f) { 
                Logger::console.info("SimPvP: ENGAGING TARGET: " + player->getFirstName(), true);
                
                Locker locker(agent);
                Locker crossLocker(player, agent);

                agent->setTargetObject(player);
                agent->addDefender(player);
                agent->setCombatState();
                
                state = SimPlayerController::IDLE; 
                return; 
            }
        }
    }
}