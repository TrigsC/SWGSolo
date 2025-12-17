/*
 * SimPlayerController.cpp
 * Debugging Startup Hang + Robust Retry
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

    // DEBUG: Trace start
    // Logger::console.info("SimPlayer: [Thread] Pathfinding started...", true);

    Vector<WorldCoordinates>* path = nullptr;
    
    try {
        path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);
    } catch (...) {
        Logger::console.info("SimPlayer: [Thread] EXCEPTION in findPath!", true);
        path = nullptr;
    }

    // DEBUG: Trace end
    // if (path != nullptr) Logger::console.info("SimPlayer: [Thread] Pathfinding success. Nodes: " + String::valueOf(path->size()), true);
    // else Logger::console.info("SimPlayer: [Thread] Pathfinding returned NULL.", true);

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
    runSpeed = 5.5f; 
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
    
    // FIX: Use schedule(100) instead of execute() to yield to main thread 
    // and prevent lockups during heavy startup load.
    task->schedule(100); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) { if (path) delete path; return; }
    
    if (agent->isInCombat()) {
        Logger::console.info("SimPlayer: Path found but Agent is in Combat. Holding.", true);
        if (path) delete path;
        state = IDLE;
        return;
    }

    if (path == nullptr || path->size() < 2) { 
        if (path) delete path; 
        Logger::console.info("SimPlayer: Path too short. Retrying in 5s.", true);
        onPathFailed(); 
        return; 
    }

    state = MOVING;
    simPath.removeAll();
    simPathIndex = 0;

    for (int i = 0; i < path->size(); ++i) {
        simPath.add(path->get(i));
    }

    destination = simPath.get(simPath.size() - 1).getPoint();
    
    // Logger::console.info("SimPlayer: Path Found (" + String::valueOf(path->size()) + " nodes). Moving...", true);

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

    if (agent->getPatrolPointSize() > 0) {
        PatrolPoint next = agent->getNextPosition();
        agent->setNextStepPosition(next.getPositionX(), next.getPositionZ(), next.getPositionY(), next.getCell());
    }

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true);

    delete path;

    // Ensure loop is active
    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(500); 
}

void SimPlayerController::onPathFailed() {
    Logger::console.info("SimPlayer: Pathfinding failed/unreachable. Retrying in 5s...", true);
    state = IDLE;
    
    // FIX: Don't recurse immediately. Schedule a retry.
    Reference<SimRetryTask*> task = new SimRetryTask(this);
    task->schedule(5000); // 5 seconds
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
            float dx0 = p.getX() - cur.getX();
            float dy0 = p.getY() - cur.getY();
            if ((dx0*dx0 + dy0*dy0) < 1.0f) { 
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

void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->isDead() || agent->getZone() == nullptr) return;

    onTick(); 

    if (agent->isInCombat()) {
        state = IDLE; 
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000); 
        return;
    }

    if (state == IDLE && destination.getX() != 0) {
        Logger::console.info("SimPlayer: Resuming path to " + destination.toString(), true);
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
        Logger::console.info("SimPlayer: Arrived at destination.", true);
        agent->clearPatrolPoints(); 
        onArrived(); 
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
        return;
    } 

    agent->findNextPosition(2.0f, false);
    
    float moveDx = currentPos.getX() - lastWatchdogPos.getX();
    float moveDy = currentPos.getY() - lastWatchdogPos.getY();
    float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

    if (movedDistSq < 0.05f) {
        stuckWatchdogCount++;
        if (stuckWatchdogCount > 5) { 
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