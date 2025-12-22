/*
 * SimPvPController.cpp
 * FIXED: Build Errors (isMoving -> PatrolPoints check, getDistanceTo -> Vector3 math)
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
#include "system/lang/Float.h" 

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

    agent->setFaction(isImperial ? String("imperial").hashCode() : String("rebel").hashCode());
    agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE); 
    
    spawnLocation = agent->getWorldPosition();

    try {
        String sX = agent->readBlackboard("targetX").get<String>();
        String sY = agent->readBlackboard("targetY").get<String>();
        String sZ = agent->readBlackboard("targetZ").get<String>();

        float hx = Float::valueOf(sX);
        float hy = Float::valueOf(sY); 
        float hz = Float::valueOf(sZ);

        if (hx == 0 && hy == 0) {
            hangoutLocation = spawnLocation;
        } else {
            hangoutLocation = Vector3(hx, hy, hz);
        }
    } catch (...) {
        Logger::console.error("SimPvP: Blackboard read failed. Defaulting to spawn.");
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

    // Safety: Force despawn if stuck for 45s
    Reference<SimPvPDespawnTask*> task = new SimPvPDespawnTask(this);
    task->schedule(45000); 
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
    if (agent != nullptr && agent->isInCombat()) {
        Logger::console.info("SimPvP: Combat in progress. Extending loiter...", true);
        Reference<SimPvPBehaviorTask*> task = new SimPvPBehaviorTask(this);
        task->schedule(5000);
        return;
    }
    returnToShuttle();
}

void SimPvPController::despawn() {
    if (agent == nullptr) return;
    Logger::console.info("SimPvP: Despawning Agent: " + String::valueOf(agent->getObjectID()), true);
    agent->destroyObjectFromWorld(true);
}

void SimPvPController::onTick() {
    if (agent == nullptr || agent->isDead()) return;
    
    // 1. COMBAT CHECK
    if (agent->isInCombat()) return; 

    // 2. STUCK / COMBAT RECOVERY CHECK
    if (state == SimPlayerController::MOVING) {
        // If state is MOVING but patrol queue is empty, combat likely wiped our path.
        if (agent->getPatrolPoints().size() == 0) {
             Vector3 dest = returningToShuttle ? spawnLocation : hangoutLocation;
             
             // FIXED: Use Vector3 math instead of SceneObject::getDistanceTo
             float dist = agent->getWorldPosition().distanceTo(dest);
             
             if (dist > 5.0f) {
                 Logger::console.info("SimPvP: Movement stopped (Queue Empty). Re-issuing move to destination.", true);
                 moveTo(dest);
             } else {
                 onArrived();
             }
        }
    }

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