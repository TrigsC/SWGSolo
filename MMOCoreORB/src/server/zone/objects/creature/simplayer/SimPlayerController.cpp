/*
 * SimPlayerController.cpp
 * Phase 15: Native Smoothness (Back to Basics + Wake-Up Calls)
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

    // Reset Home to prevent Leashing (Important!)
    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    // Scout 100-200m
    int distance = 100 + System::random(100); 
    int angle = System::random(360);
    float rads = angle * (M_PI / 180.0f);
    
    float targetX = currentPos.getX() + (distance * cos(rads));
    float targetY = currentPos.getY() + (distance * sin(rads));
    float targetZ = zone->getHeight(targetX, targetY) + 1.0f; 

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

    if (path->size() > 0) {
        Vector3 endPt = path->get(path->size() - 1).getPoint();
        destination = endPt;
    }

    Logger::console.info("SimPlayer: Path Loaded (" + String::valueOf(path->size()) + " nodes). Using Native Engine.", true);

    // 1. BRAIN WIPE
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    agent->setMovementState(AiAgent::OBLIVIOUS);
    
    // 2. LOAD PATH (Bulk)
    agent->clearPatrolPoints(); 
    
    // We load ALL points into the agent. The engine handles the curves.
    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wc = path->get(i);
        Vector3 point = wc.getPoint();
        PatrolPoint pp;
        pp.setPosition(point.getX(), point.getZ(), point.getY());
        agent->addPatrolPoint(pp);
    }

    // 3. SET HOME TO DESTINATION (Critical for Artisan)
    // This tricks the lazy AI into thinking it's "lost" and needs to go "home" (which is our goal)
    if (path->size() > 0) {
        Vector3 endPt = path->get(path->size() - 1).getPoint();
        agent->setHomeLocation(endPt.getX(), endPt.getZ(), endPt.getY());
    }

    // 4. SPEED
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    
    float runSpeed = 5.25f; // Standard run
    agent->setRunSpeed(runSpeed);

    // 5. ACTIVATE
    // We rely on the native PATROLLING state.
    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); 
    
    delete path;

    // Check less frequently (1s) to avoid lag
    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(1000);
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
// GENTLE REMINDER (Restored Watchdog)
// --------------------------------------------------------
void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->isDead() || agent->getZone() == nullptr) return;
    if (state != MOVING) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    // Distance Check
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY();
    float distSq = (dx*dx) + (dy*dy);

    if (distSq < 16.0f) { 
        Logger::console.info("SimPlayer: ARRIVED.", true);
        performSample();
        return;
    } 

    // STALL CHECK
    float moveDx = currentPos.getX() - lastWatchdogPos.getX();
    float moveDy = currentPos.getY() - lastWatchdogPos.getY();
    float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

    // If we haven't moved in 1 second...
    if (movedDistSq < 0.1f) {
        stuckWatchdogCount++;
        
        // Give it 3 seconds to think before interfering
        if (stuckWatchdogCount > 3) { 
             // GENTLE REMINDER: Just tell it to Patrol again.
             // No teleporting. No broadcasting. Just state enforcement.
             if (stuckWatchdogCount % 5 == 0) { // Log sparingly
                 Logger::console.info("SimPlayer: Lazy Bot detected. Poking...", true);
             }
             
             agent->setMovementState(AiAgent::PATROLLING);
             agent->activateAiBehavior(true);
        }
    } else {
        stuckWatchdogCount = 0; // We are moving!
    }

    lastWatchdogPos = currentPos;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(1000);
}

void SimPlayerController::performSample() {
    state = SAMPLING;
    Logger::console.info("SimPlayer: State -> SAMPLING (15s)", true);

    agent->clearPatrolPoints(); // Stop moving
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