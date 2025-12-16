/*
 * SimPlayerController.cpp
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

// ... (Task implementations remain the same) ...
void FindResourcePathTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    Logger::console.info("SimPlayer: [Task] Requesting Path...", true);
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
    return genericType;
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();

    agent->stopWaiting();
    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));
    
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    Logger::console.info("SimPlayer: [DEBUG] Searching for navmesh dest from: " + currentPos.toString(), true);

    Vector3 targetPos;
    if (!pickDestinationInNavMesh(zone, currentPos, targetPos)) {
        Logger::console.info("SimPlayer: [DEBUG] NavMesh pick failed. Using random fallback.", true);
        int distance = 100 + System::random(100);
        int angle = System::random(360);
        float rads = angle * (M_PI / 180.0f);

        float targetX = currentPos.getX() + (distance * cos(rads));
        float targetY = currentPos.getY() + (distance * sin(rads));
        float targetZ = zone->getHeight(targetX, targetY) + 1.0f;

        targetPos = Vector3(targetX, targetY, targetZ);
    }

    destination = targetPos;
    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr);

    Reference<FindResourcePathTask*> task = new FindResourcePathTask(this, startCoord, endCoord, zone);
    task->execute(); 
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

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) { if (path) delete path; return; }
    if (path == nullptr || path->size() == 0) { if (path) delete path; onPathFailed(); return; }

    retryCount = 0;
    state = MOVING;

    Zone* zone = agent->getZone();
    if (zone == nullptr) { delete path; return; }

    simPath.removeAll();
    simPathIndex = 0;

    for (int i = 0; i < path->size(); ++i) {
        simPath.add(path->get(i));
    }

    destination = simPath.get(simPath.size() - 1).getPoint();
    agent->setHomeLocation(destination.getX(), destination.getZ(), destination.getY(), nullptr);

    Logger::console.info("SimPlayer: [DEBUG] Path Found (" + String::valueOf(path->size()) + " nodes). Dest: " + destination.toString(), true);

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

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(1000);
}

void SimPlayerController::queueMorePathNodes() {
    if (agent == nullptr) return;

    if (simPathIndex < 0) simPathIndex = 0;

    int pathSize = simPath.size();
    if (simPathIndex >= pathSize) return;

    int currentQueued = agent->getPatrolPointSize();
    int slots = MAX_ENGINE_PATROL_POINTS - currentQueued;

    if (slots <= 0) return;

    Vector3 last = agent->getWorldPosition();
    if (simPathIndex > 0 && simPathIndex - 1 < pathSize) {
        last = simPath.get(simPathIndex - 1).getPoint();
    }

    const float minSq = MIN_NODE_SPACING * MIN_NODE_SPACING;
    int addedCount = 0; 

    while (slots > 0 && simPathIndex < pathSize) {
        Vector3 p = simPath.get(simPathIndex).getPoint();

        if (simPathIndex == 0) {
            Vector3 cur = agent->getWorldPosition();
            float dx0 = p.getX() - cur.getX();
            float dy0 = p.getY() - cur.getY();
            if ((dx0*dx0 + dy0*dy0) < 1.0f) { 
                simPathIndex++;
                last = cur;       
                continue;
            }
        }

        float dx = p.getX() - last.getX();
        float dy = p.getY() - last.getY();
        float d2 = (dx * dx) + (dy * dy);

        bool isFinal = (simPathIndex == simPath.size() - 1);
        
        if (!isFinal && d2 < minSq) {
            simPathIndex++;
            continue;
        }

        PatrolPoint pp(p.getX(), p.getZ(), p.getY(), nullptr);
        agent->addPatrolPoint(pp);
        addedCount++;

        last = p;
        simPathIndex++;
        slots--;
    }
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    Logger::console.info("SimPlayer: [ERROR] Path finding failed.", true);
    if (retryCount < 10) {
        retryCount++;
        goToResource(targetResource);
    } else {
        startSimLoop(); 
    }
}

void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->isDead() || agent->getZone() == nullptr) return;
    if (state != MOVING) return;

    // --- ENFORCE RUNNING ---
    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));
    
    if (agent->isWaiting()) {
        agent->stopWaiting();
    }

    if (agent->getPatrolPointSize() < 5 && simPathIndex < simPath.size()) {
        queueMorePathNodes();
    }

    Vector3 currentPos = agent->getWorldPosition();
    
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY();
    float distSq = (dx*dx) + (dy*dy);

    // Standard Arrival (Near Target)
    if (distSq < 16.0f) { 
        Logger::console.info("SimPlayer: ARRIVED.", true);
        performSample();
        return;
    } 

    // --- SAFETY CATCH: Queue Ran Dry ---
    if (agent->getPatrolPointSize() == 0 && simPathIndex >= simPath.size()) {
         Logger::console.info("SimPlayer: [NOTICE] Path ended before exact destination. Assuming arrival.", true);
         performSample();
         return;
    }

    float moveDx = currentPos.getX() - lastWatchdogPos.getX();
    float moveDy = currentPos.getY() - lastWatchdogPos.getY();
    float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

    // If moved less than 0.1m in 1 second
    if (movedDistSq < 0.1f) {
        stuckWatchdogCount++;
        
        if (stuckWatchdogCount > 2) { 
             StringBuffer msg;
             msg << "SimPlayer: [LAZY DETECTED] " << endl;
             msg << "   Pos: " << currentPos.toString() << endl;
             
             if (agent->getPatrolPointSize() > 0) {
                 PatrolPoint pp = agent->getNextPosition();
                 msg << "   Next Point: " << pp.getWorldPosition().toString();
             } else {
                 msg << "   Queue EMPTY";
             }
             
             Logger::console.info(msg.toString(), true);

             agent->stopWaiting();
             if (agent->getMovementState() != AiAgent::PATROLLING) {
                 agent->setMovementState(AiAgent::PATROLLING);
             }
             if (agent->getPosture() != CreaturePosture::UPRIGHT) {
                 agent->setPosture(CreaturePosture::UPRIGHT, true);
             }

             // Kick logic (0.5f tolerance)
             agent->findNextPosition(0.5f, false); 
             
             agent->activateAiBehavior(true);
        }
    } else {
        stuckWatchdogCount = 0; 
    }

    lastWatchdogPos = currentPos;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(1000);
}

void SimPlayerController::performSample() {
    state = SAMPLING;
    Logger::console.info("SimPlayer: State -> SAMPLING (15s)", true);

    agent->clearPatrolPoints(); 
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