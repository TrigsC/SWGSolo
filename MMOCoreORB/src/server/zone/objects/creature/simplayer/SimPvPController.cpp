/*
 * SimPvPController.cpp
 * FIXED: 
 * 1. Removed invalid setLocomotion calls.
 * 2. Fixed "Slow Boat" by equating WalkSpeed to RunSpeed during return.
 * 3. Kept anti-stutter logic.
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
#include "system/lang/System.h" 
#include "templates/params/creature/CreaturePosture.h"
#include "server/zone/Zone.h"
#include "server/zone/managers/collision/CollisionManager.h" 

SimPvPController::SimPvPController(AiAgent* aiAgent, bool imperial) : SimPlayerController(aiAgent) {
    isImperial = imperial;
    returningToShuttle = false;
    initialized = false;
    runSpeed = 6.5f; 
    setLoggingName("SimPvPController");
}

SimPvPController::~SimPvPController() {
}

Vector3 SimPvPController::getJitteredPosition(Vector3 pos) {
    float range = returningToShuttle ? 8.0f : 5.0f;
    
    float offsetX = range - System::random((int)(range * 2)); 
    float offsetY = range - System::random((int)(range * 2)); 
    
    Vector3 newPos = pos;
    newPos.setX(pos.getX() + offsetX);
    newPos.setY(pos.getY() + offsetY); 
    newPos.setZ(getWorldZ(newPos.getX(), newPos.getY()));
    
    return newPos;
}

float SimPvPController::getWorldZ(float x, float y) {
    if (agent == nullptr) return 0;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return 0;
    
    try {
        float z = CollisionManager::getWorldFloorCollision(x, y, zone, true);
        return z;
    } catch (...) {
        return zone->getHeight(x, y);
    }
}

// ---------------------------------------------------------
// MAIN LOGIC
// ---------------------------------------------------------

void SimPvPController::startSimLoop() {
    if (agent == nullptr) return;

    if (!initialized) {
        spawnTime.updateToCurrentTime(); 
        nextMoveCheckTime.updateToCurrentTime(); 

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
                float correctZ = getWorldZ(hx, hy);
                hangoutLocation = Vector3(hx, hy, correctZ);
            }
        } catch (...) {
            Logger::console.error("SimPvP: Blackboard read failed. Defaulting to spawn.");
            hangoutLocation = spawnLocation;
        }
        initialized = true;
    }

    if (returningToShuttle) {
        returnToShuttle();
    } else {
        startPatrol();
    }
}

void SimPvPController::startPatrol() {
    state = SimPlayerController::MOVING;
    returningToShuttle = false;
    
    if (agent != nullptr) {
        // Normal patrol speed
        agent->setRunSpeed(runSpeed);
    }
    
    Logger::console.info("SimPvP: Starting Patrol.", true); 
    moveTo(getJitteredPosition(hangoutLocation));
}

void SimPvPController::returnToShuttle() {
    state = SimPlayerController::MOVING;
    returningToShuttle = true; 
    
    Logger::console.info("SimPvP: Return Logic Triggered.", true);
    
    if (agent != nullptr) {
        //agent->clearCombatState(true); 
        //agent->setCreatureBitmask(0); 
        
        agent->setPosture(CreaturePosture::UPRIGHT, true);
        
        // CRITICAL FIX: Set BOTH Walk and Run speed.
        // If the AI decides to "Walk" (which it does when calm), it will now walk at RunSpeed.
        agent->setWalkSpeed(runSpeed); 
        agent->setRunSpeed(runSpeed);
    }

    Vector3 dest = getJitteredPosition(spawnLocation); 
    moveTo(dest);

    Reference<SimPvPDespawnTask*> task = new SimPvPDespawnTask(this);
    task->schedule(300000); 
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
    
    if (!returningToShuttle) {
        Time now;
        now.updateToCurrentTime();
        if ((now.getMiliTime() - spawnTime.getMiliTime()) > 600000) { 
             Logger::console.info("SimPvP: Shift over (10m limit). Forcing return.", true);
             returnToShuttle();
             return;
        }
    }

    if (agent->isInCombat()) return; 

    if (state == SimPlayerController::MOVING) {
        Time now;
        now.updateToCurrentTime();
        
        // Check every 1 second to prevent stutter
        if (now.getMiliTime() >= nextMoveCheckTime.getMiliTime()) {
            
            if (agent->getPatrolPointSize() == 0) {
                 Vector3 dest = returningToShuttle ? spawnLocation : hangoutLocation;
                 
                 float dx = agent->getWorldPosition().getX() - dest.getX();
                 float dy = agent->getWorldPosition().getY() - dest.getY(); 
                 float dist2d = sqrt((dx * dx) + (dy * dy));
                 
                 if (dist2d > 15.0f) {
                     agent->setPosture(CreaturePosture::UPRIGHT, true);
                     
                     // Re-assert speed in case something reset it
                     if (returningToShuttle) {
                        agent->setWalkSpeed(runSpeed);
                        agent->setRunSpeed(runSpeed);
                     }
                     
                     moveTo(getJitteredPosition(dest));
                     
                     nextMoveCheckTime.updateToCurrentTime();
                     nextMoveCheckTime.addMiliTime(1000); 
                 } else {
                     Logger::console.info("SimPvP: Within tolerance. Arrived.", true);
                     onArrived();
                 }
            } else {
                nextMoveCheckTime.updateToCurrentTime();
                nextMoveCheckTime.addMiliTime(1000); 
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

                Logger::console.info("SimPvP: Enemy Detected. Engaging.", true); 

                agent->setTargetObject(player);
                agent->addDefender(player);
                agent->setCombatState();
                state = SimPlayerController::IDLE; 
                return; 
            }
        }
    }
}