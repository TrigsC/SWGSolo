/*
 * SimPlayerController.cpp
 * FINAL: Hibernation-Proof, Speed-Enforced, Sanitized
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

    Vector<WorldCoordinates>* path = nullptr;
    try {
        path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);
    } catch (...) {
        path = nullptr;
    }

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

class SimRetryTask : public Task {
    WeakReference<SimPlayerController*> controller;
public:
    SimRetryTask(SimPlayerController* ctrl) : controller(ctrl) {}
    void run() override {
        Reference<SimPlayerController*> strong = controller.get();
        if (strong != nullptr) {
            Core::getTaskManager()->executeTask([strong]() {
                strong->startSimLoop();
            }, "SimRetryLambda");
        }
    }
};

// ========================================================
// BASE SIMPLAYER CONTROLLER
// ========================================================

SimPlayerController::SimPlayerController(AiAgent* aiAgent) {
    agent = aiAgent;
    state = IDLE;
    simPathIndex = 0;
    stuckWatchdogCount = 0;
    runSpeed = 6.5f; // Slightly faster to ensure 'Run' state implies movement
    setLoggingName("SimPlayerController");
    destination = Vector3(0, 0, 0);
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

void SimPlayerController::moveTo(Vector3 targetPos) {
    if (agent == nullptr) return;

    if (agent->isInCombat()) {
        destination = targetPos;
        state = IDLE; 
        return;
    }

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH; 

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    destination = targetPos;
    
    float dist = agent->getWorldPosition().distanceTo(targetPos);
    Logger::console.info("SimPlayer: Requesting move to " + targetPos.toString() + " (Dist: " + String::valueOf(dist) + "m)", true);

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr);

    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord, endCoord, zone);
    // Yield to main thread briefly to prevent startup lockups
    task->schedule(100); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) { if (path) delete path; return; }
    
    if (agent->isInCombat()) {
        if (path) delete path;
        state = IDLE;
        return;
    }

    if (path == nullptr || path->size() < 2) { 
        if (path) delete path; 
        onPathFailed(); 
        return; 
    }

    state = MOVING;
    simPath.removeAll();
    simPathIndex = 0;

    // --- PATH SANITIZATION ---
    // This logic prevents the "Spin Loop" caused by micro-nodes
    Vector3 lastAdded = agent->getWorldPosition();
    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wp = path->get(i);
        Vector3 pt = wp.getPoint();
        
        // Skip nodes closer than 1m, unless it's the final destination
        if (i < path->size() - 1) {
            if (pt.distanceTo(lastAdded) < 1.0f) continue; 
        }
        
        simPath.add(wp);
        lastAdded = pt;
    }
    
    // Safety: Ensure we have a destination
    if (simPath.size() == 0) {
        if (path->size() > 0) simPath.add(path->get(path->size()-1));
        else { delete path; onPathFailed(); return; }
    }

    Logger::console.info("SimPlayer: Path Found (" + String::valueOf(simPath.size()) + " nodes). Moving...", true);

    destination = simPath.get(simPath.size() - 1).getPoint();
    
    agent->setHomeLocation(destination.getX(), destination.getZ(), destination.getY(), nullptr);

    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    agent->clearPatrolPoints();
    agent->clearSavedPatrolPoints();
    agent->stopWaiting();

    // --- CRITICAL FIX: FORCE SPEED & POSTURE ---
    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));
    agent->setRunSpeed(runSpeed); 
    agent->setPosture(CreaturePosture::UPRIGHT, true);

    queueMorePathNodes();

    if (agent->getPatrolPointSize() > 0) {
        PatrolPoint next = agent->getNextPosition();
        agent->setNextStepPosition(next.getPositionX(), next.getPositionZ(), next.getPositionY(), next.getCell());
    }

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true);

    delete path;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(500); 
}

void SimPlayerController::onPathFailed() {
    Logger::console.info("SimPlayer: Pathfinding failed/unreachable. Retrying in 5s...", true);
    state = IDLE;
    Reference<SimRetryTask*> task = new SimRetryTask(this);
    task->schedule(5000); 
}

void SimPlayerController::queueMorePathNodes() {
    if (agent == nullptr) return;
    if (simPathIndex < 0) simPathIndex = 0;

    int pathSize = simPath.size();
    if (simPathIndex >= pathSize) return;

    int currentQueued = agent->getPatrolPointSize();
    int slots = 18 - currentQueued; 

    while (slots > 0 && simPathIndex < pathSize) {
        Vector3 p = simPath.get(simPathIndex).getPoint();

        if (simPathIndex == 0) {
            Vector3 cur = agent->getWorldPosition();
            if (p.distanceTo(cur) < 1.0f) { 
                simPathIndex++;     
                continue;
            }
        }

        PatrolPoint pp(p.getX(), p.getZ(), p.getY(), nullptr); 
        agent->addPatrolPoint(pp);

        simPathIndex++;
        slots--;
    }
}

// ----------------------------------------------------------------------
// THE IMMORTAL LOOP (Survives Hibernation)
// ----------------------------------------------------------------------
void SimPlayerController::checkArrival() {
    if (agent == nullptr) return; 
    if (agent->isDead()) return; 

    // 1. Hibernation Check: If zone is null, sleep and retry.
    if (agent->getZone() == nullptr) {
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(5000);
        return;
    }

    onTick(); 

    if (agent->isInCombat()) {
        state = IDLE; 
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000); 
        return;
    }

    if (state == IDLE && destination.getX() != 0) {
        // Resume if we were interrupted
        moveTo(destination);
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
        return;
    }

    if (state != MOVING) {
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
        return;
    }

    // 2. Stall Kickstart: Force speed if it dropped to 0
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    if (agent->getCurrentSpeed() < 0.1f) {
        agent->setRunSpeed(runSpeed);
    }

    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));
    
    if (agent->isWaiting()) agent->stopWaiting();

    if (agent->getPatrolPointSize() < 5 && simPathIndex < simPath.size()) {
        queueMorePathNodes();
    }

    Vector3 currentPos = agent->getWorldPosition();
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY(); 
    float distSq = (dx*dx) + (dy*dy);

    if (distSq < 16.0f || (agent->getPatrolPointSize() == 0 && simPathIndex >= simPath.size())) {
        Logger::console.info("SimPlayer: Arrived at destination.", true);
        agent->clearPatrolPoints(); 
        onArrived(); 
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
        return;
    } 

    // Standard Drive Logic
    agent->findNextPosition(2.0f, false);
    
    // Stuck Logic
    float moveDx = currentPos.getX() - lastWatchdogPos.getX();
    float moveDy = currentPos.getY() - lastWatchdogPos.getY();
    float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

    if (movedDistSq < 0.05f) {
        stuckWatchdogCount++;
        if (stuckWatchdogCount > 10) { 
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
    task->schedule(500); // 500ms Tick for responsiveness
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
        float angle = System::random(360) * (M_PI / 180.0f);
        float dist = 100.0f;
        targetPos.setX(currentPos.getX() + (dist * cos(angle)));
        targetPos.setY(currentPos.getY() + (dist * sin(angle))); 
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