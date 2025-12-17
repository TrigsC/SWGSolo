/*
 * SimPlayerController.cpp
 * Modular Implementation
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
#include "server/zone/objects/creature/ai/bt/BlackboardData.h"
#include "templates/params/creature/CreaturePosture.h" 

using namespace server::zone::objects::creature::ai::bt;

// --------------------------------------------------------
// TASKS
// --------------------------------------------------------
void SimPathFindTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        if (path != nullptr) strongCtrl->onPathFound(path);
        else strongCtrl->onPathFailed();
    }, "SimPlayerResultLambda");
}

void ArrivalCheckTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;
    
    Core::getTaskManager()->executeTask([strongCtrl] () {
        strongCtrl->checkArrival();
    }, "SimPlayerArrivalLambda");
}

// NOTE: We cast up to SimMinerController for specific tasks
void SimBehaviorTask::run() {
    Reference<SimPlayerController*> baseCtrl = controller.get();
    if (baseCtrl == nullptr) return;
    
    SimMinerController* miner = dynamic_cast<SimMinerController*>(baseCtrl.get());
    if (miner == nullptr) return;

    int capturedType = type;
    Core::getTaskManager()->executeTask([miner, capturedType] () {
        if (capturedType == SimBehaviorTask::FINISH_SURVEY) miner->finishSurvey();
        else if (capturedType == SimBehaviorTask::FINISH_SAMPLE) miner->finishSample();
    }, "SimPlayerBehaviorLambda");
}

// ========================================================
// BASE SIMPLAYER CONTROLLER
// ========================================================

SimPlayerController::SimPlayerController(AiAgent* aiAgent) {
    agent = aiAgent;
    state = IDLE;
    simPathIndex = 0;
    stuckWatchdogCount = 0;
    runSpeed = 5.5f; // Default
    setLoggingName("SimPlayerController");
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

void SimPlayerController::moveTo(Vector3 targetPos) {
    if (agent == nullptr) return;

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = IDLE; // Pause logic until path found

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    destination = targetPos;
    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr);

    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord, endCoord, zone);
    task->execute(); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) { if (path) delete path; return; }
    if (path == nullptr || path->size() == 0) { if (path) delete path; onPathFailed(); return; }

    state = MOVING;
    simPath.removeAll();
    simPathIndex = 0;

    for (int i = 0; i < path->size(); ++i) {
        simPath.add(path->get(i));
    }

    destination = simPath.get(simPath.size() - 1).getPoint();
    agent->setHomeLocation(destination.getX(), destination.getZ(), destination.getY(), nullptr);

    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    agent->clearPatrolPoints();
    agent->clearSavedPatrolPoints();
    agent->stopWaiting();

    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));

    queueMorePathNodes();

    // Kickstart
    if (agent->getPatrolPointSize() > 0) {
        PatrolPoint next = agent->getNextPosition();
        agent->setNextStepPosition(next.getPositionX(), next.getPositionZ(), next.getPositionY(), next.getCell());
    }

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true);

    delete path;

    // Start Drive Loop
    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(500); 
}

void SimPlayerController::onPathFailed() {
    Logger::console.info("SimPlayer: Pathfinding failed.", true);
    state = IDLE;
    // Retry or idle? Derived classes might want to handle this differently.
    // For now, simple retry logic could go here, or we just stop.
    // Let's rely on the derived class to notice state == IDLE and restart.
    startSimLoop(); 
}

void SimPlayerController::queueMorePathNodes() {
    if (agent == nullptr) return;
    if (simPathIndex < 0) simPathIndex = 0;

    int pathSize = simPath.size();
    if (simPathIndex >= pathSize) return;

    int currentQueued = agent->getPatrolPointSize();
    // Keep queue small for better corner cutting control
    int slots = 18 - currentQueued; 

    while (slots > 0 && simPathIndex < pathSize) {
        Vector3 p = simPath.get(simPathIndex).getPoint();

        if (simPathIndex == 0) {
            Vector3 cur = agent->getWorldPosition();
            float dx0 = p.getX() - cur.getX();
            float dy0 = p.getY() - cur.getY();
            if ((dx0*dx0 + dy0*dy0) < 1.0f) { 
                simPathIndex++;     
                continue;
            }
        }

        // SWG Coords: X, Z(North), Y(Height)
        PatrolPoint pp(p.getX(), p.getZ(), p.getY(), nullptr); 
        agent->addPatrolPoint(pp);

        simPathIndex++;
        slots--;
    }
}

void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->isDead() || agent->getZone() == nullptr) return;
    if (state != MOVING) return;

    // Virtual Tick for PvP Scanning
    onTick();

    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));
    if (agent->isWaiting()) agent->stopWaiting();

    if (agent->getPatrolPointSize() < 5 && simPathIndex < simPath.size()) {
        queueMorePathNodes();
    }

    Vector3 currentPos = agent->getWorldPosition();
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY();
    float distSq = (dx*dx) + (dy*dy);

    bool arrived = false;

    if (distSq < 16.0f) arrived = true;
    if (agent->getPatrolPointSize() == 0 && simPathIndex >= simPath.size()) arrived = true;

    if (arrived) {
        agent->clearPatrolPoints(); 
        onArrived(); // Virtual call to specific logic
        return;
    } 

    // Drive-by-wire
    agent->findNextPosition(2.0f, false);
    
    // Stuck Check
    float moveDx = currentPos.getX() - lastWatchdogPos.getX();
    float moveDy = currentPos.getY() - lastWatchdogPos.getY();
    float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

    if (movedDistSq < 0.05f) {
        stuckWatchdogCount++;
        if (stuckWatchdogCount > 5) { 
             // info("SimPlayer: [STUCK] Nudging...", true);
             if (agent->getPatrolPointSize() > 0) {
                 PatrolPoint next = agent->getNextPosition();
                 agent->setNextStepPosition(next.getPositionX(), next.getPositionZ(), next.getPositionY(), next.getCell());
             }
             agent->activateAiBehavior(true);
        }
    } else {
        stuckWatchdogCount = 0; 
    }

    lastWatchdogPos = currentPos;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(500);
}

bool SimPlayerController::pickDestinationInNavMesh(Zone* zone, const Vector3& currentPos, Vector3& out) {
    if (zone == nullptr || agent == nullptr) return false;
    if (!agent->isInNavMesh()) return false;

    int distance = 100 + System::random(100);
    Sphere area(currentPos, (float)distance);

    Vector3 result;
    if (PathFinderManager::instance()->getSpawnPointInArea(area, zone, result, true)) {
        out = result;
        return true;
    }
    return false;
}

// ========================================================
// SIM MINER CONTROLLER
// ========================================================

SimMinerController::SimMinerController(AiAgent* aiAgent) : SimPlayerController(aiAgent) {
    retryCount = 0;
    setLoggingName("SimMinerController");
}

SimMinerController::~SimMinerController() {
}

void SimMinerController::startSimLoop() {
    state = DECIDING;
    String res = pickRandomResource();
    targetResource = res; 
    Logger::console.info("SimMiner: Loop -> I want [" + res + "]", true);
    performSurvey();
}

String SimMinerController::pickRandomResource() {
    int roll = System::random(4);
    if (roll == 0) return "iron";
    if (roll == 1) return "gas";
    if (roll == 2) return "water";
    return "copper";
}

void SimMinerController::performSurvey() {
    if (agent == nullptr) return;
    state = PERFORMING_ACTION;

    agent->setMovementState(AiAgent::OBLIVIOUS);
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    agent->doAnimation("manipulate_high"); 

    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY);
    task->schedule(4000); 
}

void SimMinerController::finishSurvey() {
    goToResource(targetResource);
}

void SimMinerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();
    Vector3 targetPos;

    if (!pickDestinationInNavMesh(zone, currentPos, targetPos)) {
        // Fallback random
        float angle = System::random(360) * (M_PI / 180.0f);
        float dist = 100.0f;
        targetPos.setX(currentPos.getX() + (dist * cos(angle)));
        targetPos.setY(currentPos.getY() + (dist * sin(angle))); // Y is North here for calculation
        targetPos.setZ(zone->getHeight(targetPos.getX(), targetPos.getY())); 
    }

    moveTo(targetPos);
}

void SimMinerController::onArrived() {
    performSample();
}

void SimMinerController::performSample() {
    state = PERFORMING_ACTION;
    Logger::console.info("SimMiner: State -> SAMPLING (15s)", true);

    agent->clearPatrolPoints(); 
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(15000);
}

void SimMinerController::finishSample() {
    Logger::console.info("SimMiner: Done sampling.", true);
    agent->setPosture(CreaturePosture::UPRIGHT, true);
    agent->doAnimation("stop_sample"); 
    startSimLoop();
}