/*
 * SimPlayerController.cpp
 * Phase 11: Single-Step Feeding (Forces Lazy AI to focus)
 */

#include "SimPlayerController.h"
#include "engine/core/Core.h"
#include "engine/core/TaskManager.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h"
#include "server/zone/Zone.h"
#include "server/zone/managers/resource/ResourceManager.h"
#include "server/zone/objects/resource/ResourceSpawn.h"
#include "server/ServerCore.h"
#include "server/zone/ZoneServer.h"
#include "system/lang/System.h" 

// --------------------------------------------------------
// Task Implementations
// --------------------------------------------------------
void FindResourcePathTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        if (path != nullptr) { 
            strongCtrl->onPathFound(path);
        } else {
            strongCtrl->onPathFailed();
        }
    }, "SimPlayerResultLambda");
}

void ArrivalCheckTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;
    
    Core::getTaskManager()->executeTask([strongCtrl] () {
        strongCtrl->checkArrival();
    }, "SimPlayerArrivalLambda");
}

void SimBehaviorTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    int capturedType = type;
    Core::getTaskManager()->executeTask([strongCtrl, capturedType] () {
        if (capturedType == SimBehaviorTask::FINISH_SURVEY) 
            strongCtrl->finishSurvey();
        else if (capturedType == SimBehaviorTask::FINISH_SAMPLE) 
            strongCtrl->finishSample();
    }, "SimPlayerBehaviorLambda");
}

// --------------------------------------------------------
// Controller Implementation
// --------------------------------------------------------
SimPlayerController::SimPlayerController(AiAgent* aiAgent) {
    agent = aiAgent;
    state = IDLE;
    retryCount = 0;
    stuckWatchdogCount = 0;
    simPathIndex = 0;
    setLoggingName("SimPlayerController");
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

// --------------------------------------------------------
// LOGIC
// --------------------------------------------------------
void SimPlayerController::startSimLoop() {
    state = DECIDING;
    String res = pickRandomResource();
    targetResource = res; 
    Logger::console.info("SimPlayer: Loop -> I want [" + res + "]", true);
    performSurvey();
}

String SimPlayerController::pickRandomResource() {
    int roll = System::random(4);
    if (roll == 0) return "iron";
    if (roll == 1) return "gas";
    if (roll == 2) return "water";
    return "copper";
}

void SimPlayerController::performSurvey() {
    if (agent == nullptr) return;
    state = SURVEYING;

    agent->setMovementState(AiAgent::OBLIVIOUS);
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    agent->doAnimation("manipulate_high"); 

    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY);
    task->schedule(4000); 
}

void SimPlayerController::finishSurvey() {
    goToResource(targetResource);
}

String SimPlayerController::findActualResourceSpawn(const String& genericType) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer && zoneServer->getResourceManager()) {
        return genericType + "_spawn"; 
    }
    return genericType;
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();

    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    int distance = 80 + System::random(120); 
    int angle = System::random(360);
    float rads = angle * (M_PI / 180.0f);
    
    float targetX = currentPos.getX() + (distance * cos(rads));
    float targetY = currentPos.getY() + (distance * sin(rads));
    float targetZ = zone->getHeight(targetX, targetY) + 1.0f; 

    // Debug Destination
    // Logger::console.info("SimPlayer: Dest -> X:" + String::valueOf(targetX) + " Y(North):" + String::valueOf(targetY) + " Z(Elev):" + String::valueOf(targetZ), true);

    destination.setX(targetX);
    destination.setY(targetY);
    destination.setZ(targetZ);

    Vector3 targetPos(targetX, targetY, targetZ);

    state = CALCULATING_PATH;
    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr); 

    Reference<FindResourcePathTask*> task = new FindResourcePathTask(this, startCoord, endCoord, zone);
    task->execute(); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) { if (path) delete path; return; }
    if (path == nullptr || path->size() == 0) { if (path) delete path; onPathFailed(); return; }

    retryCount = 0;
    state = MOVING;

    simPath = *path; 
    simPathIndex = 0;
    
    if (simPath.size() > 0) {
        Vector3 endPt = simPath.get(simPath.size() - 1).getPoint();
        destination = endPt;
    }

    Logger::console.info("SimPlayer: Path Loaded (" + String::valueOf(simPath.size()) + " nodes). Driving...", true);

    // AI Reset
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    agent->setMovementState(AiAgent::OBLIVIOUS);
    
    // Clear ALL internal points
    agent->clearPatrolPoints(); 

    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    
    float runSpeed = agent->getRunSpeed();
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);

    // SINGLE-STEP FEED: Only add Point 0
    if (simPath.size() > 0) {
        Vector3 firstPt = simPath.get(0).getPoint();
        
        PatrolPoint pp;
        pp.setPosition(firstPt.getX(), firstPt.getZ(), firstPt.getY());
        agent->addPatrolPoint(pp);
        
        // Force Update
        agent->setNextStepPosition(firstPt.getX(), firstPt.getZ(), firstPt.getY(), nullptr);
        agent->broadcastNextPositionUpdate(&pp);
    }

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); 
    
    delete path;

    // Fast Loop (250ms)
    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(250);
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    if (retryCount < 10) {
        retryCount++;
        goToResource(targetResource);
    } else {
        startSimLoop(); 
    }
}

// --------------------------------------------------------
// SINGLE-STEP DRIVER
// --------------------------------------------------------
void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->isDead() || agent->getZone() == nullptr) return;
    if (state != MOVING) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    // Final check
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY();
    float distSq = (dx*dx) + (dy*dy);

    if (distSq < 9.0f) { 
        Logger::console.info("SimPlayer: ARRIVED at final target.", true);
        performSample();
        return;
    } 

    if (simPath.size() == 0 || simPathIndex >= simPath.size()) {
        Logger::console.info("SimPlayer: End of path data.", true);
        performSample();
        return;
    }

    // Check Waypoint
    Vector3 targetPt = simPath.get(simPathIndex).getPoint();
    
    float wx = targetPt.getX() - currentPos.getX();
    float wy = targetPt.getY() - currentPos.getY(); 
    float waypointDistSq = (wx*wx) + (wy*wy);

    if (waypointDistSq < 16.0f) {
        // REACHED NODE -> Load Next
        simPathIndex++;
        
        if (simPathIndex >= simPath.size()) {
            performSample();
            return;
        } else {
            Vector3 nextPt = simPath.get(simPathIndex).getPoint();
            
            // LOG: Prove we are feeding (X, Elev, North)
            Logger::console.info("SimPlayer: Feed Step " + String::valueOf(simPathIndex) + 
                " -> X:" + String::valueOf(nextPt.getX()) + 
                " Z(Elev):" + String::valueOf(nextPt.getZ()) + 
                " Y(North):" + String::valueOf(nextPt.getY()), true);

            // 1. Clear old
            agent->clearPatrolPoints();
            
            // 2. Add New
            PatrolPoint pp;
            pp.setPosition(nextPt.getX(), nextPt.getZ(), nextPt.getY());
            agent->addPatrolPoint(pp);

            // 3. Force Engine
            agent->setNextStepPosition(nextPt.getX(), nextPt.getZ(), nextPt.getY(), nullptr);
            agent->broadcastNextPositionUpdate(&pp);
            agent->activateAiBehavior(true);
            
            stuckWatchdogCount = 0;
        }
    } else {
        // STUCK CHECK
        float moveDx = currentPos.getX() - lastWatchdogPos.getX();
        float moveDy = currentPos.getY() - lastWatchdogPos.getY();
        float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

        if (movedDistSq < 0.01f) {
            stuckWatchdogCount++;
            // Relaxed Tolerance (1.5 sec)
            if (stuckWatchdogCount > 6) { 
                
                // Only log sparingly
                if (stuckWatchdogCount % 10 == 0) {
                    Logger::console.info("SimPlayer: Stalled. Resending packet...", true);
                }
                
                // Re-broadcast the CURRENT target
                // We do NOT teleport here anymore to avoid the flying bug.
                // We just scream at the client "GO HERE!"
                PatrolPoint pp;
                pp.setPosition(targetPt.getX(), targetPt.getZ(), targetPt.getY());
                agent->setNextStepPosition(targetPt.getX(), targetPt.getZ(), targetPt.getY(), nullptr);
                agent->broadcastNextPositionUpdate(&pp);
                agent->activateAiBehavior(true);
                
                // Only use teleport as absolute last resort (5 seconds stuck)
                if (stuckWatchdogCount > 20) {
                     Logger::console.info("SimPlayer: HARD STUCK. Emergency Teleport.", true);
                     // Teleport to the NODE, not the nudge
                     agent->teleport(targetPt.getX(), targetPt.getZ() + 0.5f, targetPt.getY(), 0);
                     stuckWatchdogCount = 0;
                }
            }
        } else {
            stuckWatchdogCount = 0;
        }
    }

    lastWatchdogPos = currentPos;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(250);
}

void SimPlayerController::performSample() {
    state = SAMPLING;
    Logger::console.info("SimPlayer: State -> SAMPLING (15s)", true);

    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(15000);
}

void SimPlayerController::finishSample() {
    Logger::console.info("SimPlayer: Done sampling.", true);
    agent->setPosture(CreaturePosture::UPRIGHT, true);
    agent->doAnimation("stop_sample"); 
    startSimLoop();
}

Vector3 SimPlayerController::findNearestHighDensityResource(const String& resourceClass) {
    return Vector3(0,0,0);
}