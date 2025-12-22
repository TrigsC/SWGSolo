/*
 * SimPvPController.cpp
 * FIXED: Added Verbose Logging for Navigation Debugging
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
// NEW: For finding floor height inside buildings/platforms
#include "server/zone/managers/collision/CollisionManager.h" 

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
    // LOGGING START
    Logger::console.info("SimPvP: getJitteredPosition called. Base Pos: " + pos.toString() + " | ReturningToShuttle: " + String::valueOf(returningToShuttle), true);
    // LOGGING END

    // Increased spread for return trip to avoid shuttle collision
    float range = returningToShuttle ? 8.0f : 5.0f;
    
    float offsetX = range - System::random((int)(range * 2)); 
    float offsetY = range - System::random((int)(range * 2)); 
    
    Vector3 newPos = pos;
    newPos.setX(pos.getX() + offsetX);
    newPos.setY(pos.getY() + offsetY); 
    
    // Recalculate Z using Physics (Collision) not just Terrain
    newPos.setZ(getWorldZ(newPos.getX(), newPos.getY()));
    
    // LOGGING START
    Logger::console.info("SimPvP: Jitter Result -> " + newPos.toString(), true);
    // LOGGING END
    
    return newPos;
}

float SimPvPController::getWorldZ(float x, float y) {
    if (agent == nullptr) return 0;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return 0;
    
    // Attempt to find floor (building/platform)
    // We start ray from high up (200m) to find the roof/floor
    // If CollisionManager is missing in your build, revert to zone->getHeight
    try {
        float z = CollisionManager::getWorldFloorCollision(x, y, zone, true);
        // LOGGING START
        Logger::console.info("SimPvP: getWorldZ(" + String::valueOf(x) + ", " + String::valueOf(y) + ") -> Collision Z: " + String::valueOf(z), true);
        // LOGGING END
        return z;
    } catch (...) {
        float z = zone->getHeight(x, y);
        // LOGGING START
        Logger::console.info("SimPvP: getWorldZ(" + String::valueOf(x) + ", " + String::valueOf(y) + ") -> Terrain Z (Fallback): " + String::valueOf(z), true);
        // LOGGING END
        return z;
    }
}

// ---------------------------------------------------------
// MAIN LOGIC
// ---------------------------------------------------------

void SimPvPController::startSimLoop() {
    if (agent == nullptr) return;

    spawnTime.updateToCurrentTime(); 
    nextMoveCheckTime.updateToCurrentTime(); // Init timer

    agent->setFaction(isImperial ? String("imperial").hashCode() : String("rebel").hashCode());
    agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE); 
    
    spawnLocation = agent->getWorldPosition();
    Logger::console.info("SimPvP: Saved Spawn Location: " + spawnLocation.toString(), true); // LOGGING ADDED

    try {
        String sX = agent->readBlackboard("targetX").get<String>();
        String sY = agent->readBlackboard("targetY").get<String>(); 
        String sZ = agent->readBlackboard("targetZ").get<String>(); 

        float hx = Float::valueOf(sX);
        float hy = Float::valueOf(sY); 
        float hz = Float::valueOf(sZ);

        if (hx == 0 && hy == 0) {
            hangoutLocation = spawnLocation;
            Logger::console.info("SimPvP: No Blackboard Targets. Hangout = Spawn.", true); // LOGGING ADDED
        } else {
            // Recalculate Z to ensure we are on the floor
            float correctZ = getWorldZ(hx, hy);
            hangoutLocation = Vector3(hx, hy, correctZ);
            Logger::console.info("SimPvP: Calculated Hangout Location: " + hangoutLocation.toString(), true); // LOGGING ADDED
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
    Logger::console.info("SimPvP: Starting Patrol. Moving to Hangout.", true); // LOGGING ADDED
    moveTo(getJitteredPosition(hangoutLocation));
}

void SimPvPController::returnToShuttle() {
    if (returningToShuttle) {
        Logger::console.info("SimPvP: returnToShuttle ignored (Already returning).", true); // LOGGING ADDED
        return; 
    }

    state = SimPlayerController::MOVING;
    returningToShuttle = true;
    Logger::console.info("SimPvP: Patrol done. Returning to Shuttle at " + spawnLocation.toString(), true);
    
    if (agent != nullptr) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
        agent->setRunSpeed(runSpeed);
        // Wipe combat flags to stop them from turning back to fight
        agent->setCreatureBitmask(0); 
    }

    Vector3 dest = getJitteredPosition(spawnLocation); // LOGGING ADDED (Split for logging)
    Logger::console.info("SimPvP: Requesting Move to Return Destination: " + dest.toString(), true); // LOGGING ADDED
    moveTo(dest);

    Reference<SimPvPDespawnTask*> task = new SimPvPDespawnTask(this);
    task->schedule(300000); 
}

void SimPvPController::onArrived() {
    Logger::console.info("SimPvP: onArrived Triggered. ReturningToShuttle: " + String::valueOf(returningToShuttle), true); // LOGGING ADDED
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
    Logger::console.info("SimPvP: Loitering finished. Calling returnToShuttle().", true); // LOGGING ADDED
    returnToShuttle();
}

void SimPvPController::despawn() {
    if (agent == nullptr) return;
    Logger::console.info("SimPvP: Despawning Agent: " + String::valueOf(agent->getObjectID()), true);
    agent->destroyObjectFromWorld(true);
}

void SimPvPController::onTick() {
    if (agent == nullptr || agent->isDead()) return;
    
    // 1. LIFE TIMER CHECK (10 Min limit)
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

    // 2. STUCK / MOVEMENT CHECK (THROTTLED)
    if (state == SimPlayerController::MOVING) {
        
        // Only check every 3 seconds to prevent "One Step" stuttering
        Time now;
        now.updateToCurrentTime();
        
        if (now.getMiliTime() >= nextMoveCheckTime.getMiliTime()) {
            
            // Check if queue is empty (Stopped)
            if (agent->getPatrolPointSize() == 0) {
                 Vector3 dest = returningToShuttle ? spawnLocation : hangoutLocation;
                 
                 float dx = agent->getWorldPosition().getX() - dest.getX();
                 float dy = agent->getWorldPosition().getY() - dest.getY(); 
                 float dist2d = sqrt((dx * dx) + (dy * dy));
                 
                 // LOGGING START
                 if (returningToShuttle) {
                     Logger::console.info("SimPvP: Returning Tick. Current Pos: " + agent->getWorldPosition().toString() + " | Dest: " + dest.toString() + " | Dist: " + String::valueOf(dist2d), true);
                 }
                 // LOGGING END

                 if (dist2d > 15.0f) {
                     // Still far away. Re-issue move.
                     Logger::console.info("SimPvP: Stuck " + String::valueOf(dist2d) + "m from target. Re-issuing.", true);
                     agent->setPosture(CreaturePosture::UPRIGHT, true);
                     moveTo(getJitteredPosition(dest));
                     
                     // Set next check to 3 seconds from now
                     nextMoveCheckTime.updateToCurrentTime();
                     nextMoveCheckTime.addMiliTime(3000);
                 } else {
                     Logger::console.info("SimPvP: Within tolerance (" + String::valueOf(dist2d) + "m). Arrived.", true);
                     onArrived();
                 }
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

                Logger::console.info("SimPvP: Enemy Detected (" + String::valueOf(player->getObjectID()) + "). Engaging.", true); // LOGGING ADDED

                agent->setTargetObject(player);
                agent->addDefender(player);
                agent->setCombatState();
                state = SimPlayerController::IDLE; 
                return; 
            }
        }
    }
}