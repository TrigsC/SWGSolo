/*
 * SimPlayerController.cpp
 * Robust Loop & Combat Resume
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
        // Queue the destination but don't move yet
        destination = targetPos;
        state = IDLE; 
        return;
    }

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH; // Prevent multiple requests

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    destination = targetPos;
    
    float dist = agent->getWorldPosition().distanceTo(targetPos);
    Logger::console.info("SimPlayer: Requesting move to " + targetPos.toString() + " (Dist: " + String::valueOf(dist) + "m)", true);

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr);

    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord, endCoord, zone);
    task->execute(); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) { if (path) delete path; return; }
    
    if (agent->isInCombat()) {
        Logger::console.info("SimPlayer: Path found but Agent is in Combat. Holding.", true);
        if (path) delete path;
        // We leave destination set, so checkArrival will retry later
        state = IDLE;
        return;
    }

    if (path == nullptr || path->size() < 2) { 
        if (path) delete path; 
        Logger::console.info("SimPlayer: Path too short. Retrying.", true);
        onPathFailed(); 
        return; 
    }

    state = MOVING;
    simPath.removeAll();
    simPathIndex = 0;

    for (int i = 0; i < path->size(); ++i) {
        simPath.add(path->get(i));
    }

    // Update destination to exact path end
    destination = simPath.get(simPath.size() - 1).getPoint();
    
    // Set Home to destination so we don't leash back
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

    // Ensure loop is running (it might already be, but safe to add another if needed, though we rely on the main loop)
}

void SimPlayerController::onPathFailed() {
    Logger::console.info("SimPlayer: Pathfinding failed/unreachable.", true);
    state = IDLE;
    // Let derived classes decide what to do next tick or immediately
    // For now, if we fail, we just idle. 
    // Ideally, we'd trigger startSimLoop() or similar, but let's let checkArrival handle the idle state.
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

    // --- 1. PvP Scan / Tick ---
    onTick(); 

    // --- 2. Combat / State Check ---
    if (agent->isInCombat()) {
        state = IDLE; // Force state to IDLE so we know to resume later
        // RESCHEDULE and return. Do not move.
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000); 
        return;
    }

    // --- 3. Resume Logic (Combat Just Ended) ---
    if (state == IDLE && destination.getX() != 0) {
        // We have a destination but we are idling. Resume!
        Logger::console.info("SimPlayer: Resuming path to " + destination.toString(), true);
        moveTo(destination);
        
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
        return;
    }

    // --- 4. Normal Movement Logic ---
    if (state != MOVING) {
        // If we are doing an action (SAMPLING/WAITING), just heartbeat
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

    // Check distance (16m^2 = 4m)
    if (distSq < 16.0f) arrived = true;
    if (agent->getPatrolPointSize() == 0 && simPathIndex >= simPath.size()) arrived = true;

    if (arrived) {
        Logger::console.info("SimPlayer: Arrived at destination.", true);
        agent->clearPatrolPoints(); 
        onArrived(); 
        // Note: onArrived might change state, so we reschedule to keep checking
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
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
             // Logger::console.info("SimPlayer: [STUCK] Nudging...", true);
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

    // ALWAYS RESCHEDULE
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