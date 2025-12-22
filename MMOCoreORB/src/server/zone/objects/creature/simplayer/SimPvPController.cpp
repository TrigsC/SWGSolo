/*
 * SimPvPController.cpp
 * DEBUG VERSION: Heavy Logging in onTick to diagnose "Stutter Step"
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
#include "templates/params/creature/CreaturePosture.h"

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
    Logger::console.info("SimPvP: Starting Patrol. Destination: " + hangoutLocation.toString(), true);
    moveTo(hangoutLocation);
}

void SimPvPController::returnToShuttle() {
    state = SimPlayerController::MOVING;
    returningToShuttle = true;
    Logger::console.info("SimPvP: Returning to Shuttle. Destination: " + spawnLocation.toString(), true);
    
    moveTo(spawnLocation);

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
    
    if (agent->isInCombat()) return; 

    // LOG 3: DIAGNOSING THE STUTTER
    if (state == SimPlayerController::MOVING) {
        
        int queueSize = agent->getPatrolPointSize();
        
        if (queueSize == 0) {
             Vector3 dest = returningToShuttle ? spawnLocation : hangoutLocation;
             
             // Calculate distances manually to debug
             Vector3 current = agent->getWorldPosition();
             float dx = current.getX() - dest.getX();
             float dy = current.getY() - dest.getY();
             float dz = current.getZ() - dest.getZ();
             float dist = sqrt(dx*dx + dy*dy + dz*dz); // 3D Distance
             float dist2d = sqrt(dx*dx + dy*dy);       // 2D Distance

             Logger::console.info("SimPvP [" + String::valueOf(agent->getObjectID()) + "]: TICK DEBUG -> State=MOVING, Queue=0. Dist3D=" + String::valueOf(dist) + ", Dist2D=" + String::valueOf(dist2d), true);
             Logger::console.info("SimPvP: Current Pos: " + current.toString() + " | Target Pos: " + dest.toString(), true);

             // Original Logic (Rolled Back)
             if (dist > 5.0f) {
                 Logger::console.info("SimPvP: Distance > 5.0f -> Re-issuing MOVE command.", true);
                 moveTo(dest);
             } else {
                 Logger::console.info("SimPvP: Distance < 5.0f -> Calling onArrived.", true);
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