/*
 * SimPlayerController.cpp
 * Debugging Startup Hang + Robust Retry
 */

#include "SimPlayerController.h"
#include "SimPlayerManager.h"
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

//#define DEBUG_SIMPVP

// --------------------------------------------------------
// TASKS
// --------------------------------------------------------
void SimPathFindTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;
#ifdef DEBUG_SIMPVP
    // DEBUG: Trace start
    Logger::console.info("SimPlayer: [Thread] Pathfinding started...", true);
#endif
    Vector<WorldCoordinates>* path = nullptr;
    
    try {
        path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);
    } catch (...) {
        Logger::console.info("SimPlayer: [Thread] EXCEPTION in findPath!", true);
        path = nullptr;
    }
#ifdef DEBUG_SIMPVP
    // DEBUG: Trace end
    if (path != nullptr) {
        Logger::console.info("SimPlayer: [Thread] Pathfinding success. Nodes: " + String::valueOf(path->size()), true);
    }
    else {
        Logger::console.info("SimPlayer: [Thread] Pathfinding returned NULL.", true);
    }
#endif

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

    int capturedType = type;
    Core::getTaskManager()->executeTask([baseCtrl, capturedType] () {
        SimMinerController* miner = dynamic_cast<SimMinerController*>(baseCtrl.get());
        if (miner == nullptr) return;

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
    runSpeed = 3.0f; 
    setLoggingName("SimPlayerController");
    destination = Vector3(0, 0, 0);
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

void SimPlayerController::moveTo(Vector3 targetPos) {
    if (agent == nullptr) return;

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    if (!zone->isWithinBoundaries(targetPos)) {
        onPathFailed();
        return;
    }

    if (agent->isInCombat()) {
        destination = targetPos;
        state = IDLE;
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer moveTo: isInCombat", true);
#endif
        return;
    }

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH; 

    destination = targetPos;
    
    float dist = agent->getWorldPosition().distanceTo(targetPos);
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer moveTo: Requesting move to " + targetPos.toString() + " (Dist: " + String::valueOf(dist) + "m)", true);
#endif

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr);

    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord, endCoord, zone);
    
    task->schedule(100); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) { if (path) delete path; return; }
    
    if (agent->isInCombat()) {
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer onPathFound: Path found but Agent is in Combat. Holding.", true);
#endif
        if (path) delete path;
        state = IDLE;
        return;
    }

    if (path == nullptr || path->size() < 2) { 
        if (path) delete path;
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer onPathFound: Path too short. Retrying in 5s.", true);
#endif
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
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer onPathFound: Path Found (" + String::valueOf(path->size()) + " nodes). Moving...", true);
#endif
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
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer onPathFailed: Pathfinding failed/unreachable. Retrying in 5s...", true);
#endif
    state = IDLE;

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
    if (agent == nullptr || agent->getZone() == nullptr) return;

    onTick(); 
    
    Locker locker(agent);

    if (agent->isDead()) {
        // SimPvPController::onTick schedules recycle for dead bots. Do not
        // destroy the object while holding its own lock; that can deadlock
        // against world/database cleanup paths.
        state = WAITING;
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: isDead", true);
#endif
        return;
    }

    if (agent->isIncapacitated()) {
        state = WAITING;
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: isIncapacitated", true);
#endif
        locker.release();
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
        return;
    }

    if (agent->isInCombat()) {
        state = IDLE; 
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: isInCombat", true);
#endif
        locker.release();
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000); 
        return;
    }

    if (state == IDLE && destination.getX() != 0) {
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Resuming path to " + destination.toString(), true);
#endif
        Vector3 resumeDestination = destination;
        locker.release();
        moveTo(resumeDestination);
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
        return;
    }

    if (state != MOVING) {
        locker.release();
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
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Arrived at destination.", true);
#endif
        agent->clearPatrolPoints();
        state = WAITING;
        locker.release();
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
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: stuckWatchdogCount > 5.", true);
#endif
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

    locker.release();
    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(500);
}

bool SimPlayerController::pickDestinationInNavMesh(Zone* zone, const Vector3& currentPos, Vector3& out, int minSearchRadius, int maxSearchRadius) {
    if (zone == nullptr || agent == nullptr) return false;
    if (!agent->isInNavMesh()) return false;

    if (minSearchRadius < 1)
        minSearchRadius = 1;

    if (maxSearchRadius < minSearchRadius)
        maxSearchRadius = minSearchRadius;

    int distance = minSearchRadius;
    if (maxSearchRadius > minSearchRadius)
        distance += System::random(maxSearchRadius - minSearchRadius);

    Sphere area(currentPos, (float)distance);

    Vector3 result;
    if (PathFinderManager::instance()->getSpawnPointInArea(area, zone, result, true) &&
            zone->isWithinBoundaries(result)) {
        out = result;
        return true;
    }
    return false;
}

// ========================================================
// SIM MINER CONTROLLER
// ========================================================

SimMinerController::SimMinerController(AiAgent* aiAgent) : SimMinerController(aiAgent, SimMinerConfig()) {
}

SimMinerController::SimMinerController(AiAgent* aiAgent, const SimMinerConfig& minerConfig) : SimPlayerController(aiAgent) {
    retryCount = 0;
    config = minerConfig;
    setLoggingName("SimMinerController");
}

SimMinerController::~SimMinerController() {
}

void SimMinerController::startSimLoop() {
    state = DECIDING;
    String res = pickRandomResource();
    targetResource = res;
    logStateTransition("SimMiner: Loop started; selected conceptual resource [" + res + "]");
    performSurvey();
}

String SimMinerController::pickRandomResource() {
    if (config.resources.size() == 0) {
        int roll = System::random(4);
        if (roll == 0) return "iron";
        if (roll == 1) return "gas";
        if (roll == 2) return "water";
        return "copper";
    }

    if (config.resources.size() == 1)
        return config.resources.get(0);

    int index = System::random(config.resources.size() - 1);
    return config.resources.get(index);
}

void SimMinerController::performSurvey() {
    if (agent == nullptr) return;
    state = SURVEYING;
    logStateTransition("SimMiner: Survey started for [" + targetResource + "]");

    agent->setMovementState(AiAgent::OBLIVIOUS);
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    agent->doAnimation("manipulate_high"); 

    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY);
    task->schedule(config.surveyDurationMs);
}

void SimMinerController::finishSurvey() {
    logStateTransition("SimMiner: Survey finished for [" + targetResource + "]");
    goToResource(targetResource);
}

void SimMinerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();
    Vector3 targetPos;
    bool usedFallback = false;

    if (!pickDestinationInNavMesh(zone, currentPos, targetPos, config.minSearchRadius, config.maxSearchRadius)) {
        float angle = System::random(360) * (M_PI / 180.0f);
        float dist = (float)config.fallbackRadius;
        targetPos.setX(currentPos.getX() + (dist * cos(angle)));
        targetPos.setY(currentPos.getY() + (dist * sin(angle)));

        if (!zone->isWithinBoundaries(targetPos)) {
            // Near an edge, bias the fallback toward the planet center instead
            // of allowing the conceptual loop to wander beyond terrain bounds.
            float currentDistance = Math::sqrt(
                currentPos.getX() * currentPos.getX() +
                currentPos.getY() * currentPos.getY());

            if (currentDistance > 0.f) {
                targetPos.setX(currentPos.getX() -
                    currentPos.getX() / currentDistance * dist);
                targetPos.setY(currentPos.getY() -
                    currentPos.getY() / currentDistance * dist);
            }
        }

        if (!zone->isWithinBoundaries(targetPos)) {
            logStateTransition("SimMiner: No in-bounds fallback destination for [" +
                resourceName + "]; retrying loop");
            onPathFailed();
            return;
        }

        targetPos.setZ(zone->getHeight(targetPos.getX(), targetPos.getY()));
        usedFallback = true;
    }

    String destinationSource = usedFallback ? "fallback" : "navmesh";
    logStateTransition("SimMiner: Destination selected for [" + resourceName + "] using " + destinationSource + " target=" + targetPos.toString());
    moveTo(targetPos);
}

void SimMinerController::onArrived() {
    logStateTransition("SimMiner: Arrived at conceptual resource destination for [" + targetResource + "]");
    performSample();
}

void SimMinerController::onPathFailed() {
    logStateTransition("SimMiner: Path failed; retrying loop for [" + targetResource + "]");
    SimPlayerController::onPathFailed();
}

void SimMinerController::performSample() {
    state = SAMPLING;
    logStateTransition("SimMiner: Sample started for [" + targetResource + "]");

    agent->clearPatrolPoints(); 
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(config.sampleDurationMs);
}

void SimMinerController::finishSample() {
    ManagedReference<AiAgent*> strongAgent = agent;
    if (strongAgent == nullptr)
        return;

    String completedResource = targetResource;
    int yieldAmount = 0;
    bool logYield = false;
    bool recordYield = prepareConceptualYield(completedResource, yieldAmount, logYield);
    uint64 sourceObjectID = strongAgent->getObjectID();

    logStateTransition("SimMiner: Sample finished for [" + completedResource + "]");
    strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    strongAgent->doAnimation("stop_sample");
    startSimLoop();

    // Keep conceptual accounting outside the completed sample's agent work.
    if (recordYield) {
        SimPlayerManager::instance()->recordConceptualMinerYield(
            completedResource, yieldAmount, sourceObjectID, logYield);
    }
}

bool SimMinerController::prepareConceptualYield(const String& completedResource, int& amount, bool& logYield) const {
    if (!config.yieldEnabled || completedResource.isEmpty())
        return false;

    int minAmount = config.minYieldAmount;
    int maxAmount = config.maxYieldAmount;

    if (minAmount <= 0 || maxAmount <= 0)
        return false;

    if (maxAmount < minAmount)
        maxAmount = minAmount;

    amount = minAmount;
    if (maxAmount > minAmount)
        amount += System::random(maxAmount - minAmount);

    logYield = config.logYield;
    return true;
}

void SimMinerController::logStateTransition(const String& message) const {
#ifdef DEBUG_SIMPVP
    Logger::console.info(message, true);
#else
    if (config.logStateTransitions)
        Logger::console.info(message, true);
#endif
}
