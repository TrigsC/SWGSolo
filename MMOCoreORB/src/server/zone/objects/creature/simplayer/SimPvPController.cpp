/*
 * SimPvPController.cpp
 * FIXED: Z-Correction, 2D Distance Checks, and Life Timer
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

SimPvPController::SimPvPController(AiAgent* aiAgent, bool imperial) : SimPlayerController(aiAgent) {
    isImperial = imperial;
    returningToShuttle = false;
    runSpeed = 6.5f; 
    setLoggingName("SimPvPController");
}

SimPvPController::~SimPvPController() {
}

// ---------------------------------------------------------
// HELPERS
// ---------------------------------------------------------
Vector3 SimPvPController::getJitteredPosition(Vector3 pos) {
    float offsetX = 5.0f - System::random(10); // +/- 5m
    float offsetY = 5.0f - System::random(10); 
    
    Vector3 newPos = pos;
    newPos.setX(pos.getX() + offsetX);
    newPos.setY(pos.getY() + offsetY); // Y is North/South in Vector3 logic here
    
    // Recalculate Z (Height) for the new randomized spot
    newPos.setZ(getTerrainHeight(newPos.getX(), newPos.getY()));
    
    return newPos;
}

float SimPvPController::getTerrainHeight(float x, float y) {
    if (agent == nullptr) return 0;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return 0;
    
    // Core3 Zone expects x, y (North)
    return zone->getHeight(x, y); 
}

// ---------------------------------------------------------
// MAIN LOGIC
// ---------------------------------------------------------

void SimPvPController::startSimLoop() {
    if (agent == nullptr) return;

    spawnTime.updateToCurrentTime(); // Start the clock

    agent->setFaction(isImperial ? String("imperial").hashCode() : String("rebel").hashCode());
    agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE); 
    
    spawnLocation = agent->getWorldPosition();

    try {
        String sX = agent->readBlackboard("targetX").get<String>();
        String sY = agent->readBlackboard("targetY").get<String>(); // North
        String sZ = agent->readBlackboard("targetZ").get<String>(); // Height

        float hx = Float::valueOf(sX);
        float hy = Float::valueOf(sY); 
        float hz = Float::valueOf(sZ);

        if (hx == 0 && hy == 0) {
            hangoutLocation = spawnLocation;
        } else {
            // Correct the Height (Z) from the terrain, ignore the Lua Z if it's bad
            float correctZ = getTerrainHeight(hx, hy);
            hangoutLocation = Vector3(hx, hy, correctZ);
        }
    } catch (...) {
        Logger::console.error("SimPvP: Blackboard read failed. Defaulting to spawn.");
        hangoutLocation = spawnLocation;
    }

    Logger::console.info("SimPvP: Loop Started.", true);
    startPatrol();
}

void SimPvPController::startPatrol() {
    state = SimPlayerController::MOVING;
    returningToShuttle = false;
    moveTo(getJitteredPosition(hangoutLocation));
}

void SimPvPController::returnToShuttle() {
    if (returningToShuttle) return; // Already going home

    state = SimPlayerController::MOVING;
    returningToShuttle = true;
    Logger::console.info("SimPvP: Patrol done (or Timeout). Returning to Shuttle.", true);
    
    if (agent != nullptr) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
        agent->setRunSpeed(runSpeed);
    }

    moveTo(getJitteredPosition(spawnLocation));

    // Force Despawn in 5 mins if they get stuck walking back
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
    
    // 1. LIFE TIMER CHECK (The Fail-Safe)
    // If bot has been alive > 10 minutes, force them to go home.
    // This catches bots that got stuck fighting/walking for too long.
    if (!returningToShuttle) {
        Time now;
        now.updateToCurrentTime();
        if ((now.getMiliTime() - spawnTime.getMiliTime()) > 600000) { // 10 Minutes
             Logger::console.info("SimPvP: Shift over (10m limit). Forcing return.", true);
             returnToShuttle();
             return;
        }
    }

    // 2. COMBAT CHECK
    if (agent->isInCombat()) return; 

    // 3. STUCK / MOVEMENT CHECK
    if (state == SimPlayerController::MOVING) {
        if (agent->getPatrolPointSize() == 0) {
             Vector3 dest = returningToShuttle ? spawnLocation : hangoutLocation;
             
             // 2D Distance Check (Ignore Z height differences)
             float dx = agent->getWorldPosition().getX() - dest.getX();
             float dy = agent->getWorldPosition().getY() - dest.getY(); // Y is North
             float dist2d = sqrt((dx * dx) + (dy * dy));
             
             // 15m tolerance
             if (dist2d > 15.0f) {
                 Logger::console.info("SimPvP: Stopped " + String::valueOf(dist2d) + "m from target. Re-issuing.", true);
                 agent->setPosture(CreaturePosture::UPRIGHT, true);
                 moveTo(getJitteredPosition(dest));
             } else {
                 Logger::console.info("SimPvP: Within tolerance (" + String::valueOf(dist2d) + "m). Arrived.", true);
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