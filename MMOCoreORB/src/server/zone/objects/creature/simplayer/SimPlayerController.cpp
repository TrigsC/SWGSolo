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
        else if (capturedType == SimBehaviorTask::START_STATIONED_SAMPLE) miner->startStationedSample();
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
    intelligentAssignmentPending = false;
    intelligentAssignmentActive = false;
    intelligentSampleActive = false;
    intelligentAssignmentStationed = false;
    intelligentLogActivationLifecycle = true;
    intelligentQueuedDuringSample = false;
    intelligentQueuedAtMs = 0;
    intelligentAssignmentGenerationId = 0;
    intelligentActivationSnapshotId = 0;
    intelligentTargetDensity = 0.f;
    intelligentAssignmentExpiresAtMs = 0;
    setLoggingName("SimMinerController");
}

SimMinerController::~SimMinerController() {
}

void SimMinerController::startSimLoop() {
    String activationResult;

    if (intelligentAssignmentPending && beginIntelligentTargetAssignment(activationResult))
        return;

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

String SimMinerController::getSimStateName(SimState simState) const {
    switch (simState) {
    case IDLE:
        return "idle";
    case DECIDING:
        return "deciding";
    case SURVEYING:
        return "surveying";
    case CALCULATING_PATH:
        return "calculating_path";
    case PERFORMING_ACTION:
        return "performing_action";
    case MOVING:
        return "moving";
    case SAMPLING:
        return "sampling";
    case WAITING:
        return "waiting";
    default:
        return "unknown";
    }
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
    if (intelligentAssignmentActive) {
        uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
        if (sourceObjectID != 0)
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                sourceObjectID, "sampleStarted");
        logIntelligentTargetArrival("sample_started");
        performIntelligentSample();
        return;
    }

    logStateTransition("SimMiner: Arrived at conceptual resource destination for [" + targetResource + "]");
    performSample();
}

void SimMinerController::onPathFailed() {
    if (intelligentAssignmentActive || intelligentAssignmentPending) {
        logIntelligentTargetActivation("fallback", "pathFailed");
        uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
        if (sourceObjectID != 0)
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                sourceObjectID, "failed", "pathFailed");
        clearLocalIntelligentTargetAssignment();

        if (sourceObjectID != 0)
            SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(sourceObjectID, "pathFailed");
    }

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
    if (intelligentSampleActive) {
        finishIntelligentSample();
        return;
    }

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

bool SimMinerController::requestIntelligentTargetAssignment(
        const String& profileKey,
        const String& resourceName,
        const String& resourceType,
        const String& targetZone,
        const Vector3& targetPosition,
        float density,
        uint64 expiresAtMs,
        uint64 assignmentGenerationId,
        const String& targetHash,
        uint64 activationSnapshotId,
        const String& activationPathValidationStatus,
        const String& activationPathTrustStatus,
        bool logActivationLifecycle,
        String& activationResult) {
    activationResult = "fallback";

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        activationResult = "controllerUnavailable";
        return false;
    }

    if (profileKey.isEmpty() || resourceName.isEmpty() ||
            resourceType.isEmpty() || targetZone.isEmpty()) {
        activationResult = "invalidAssignment";
        return false;
    }

    uint64 now = System::getMiliTime();

    if (expiresAtMs > 0 && now > expiresAtMs) {
        activationResult = "assignmentExpired";
        return false;
    }

    String currentZoneName;

    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();

        if (zone == nullptr) {
            activationResult = "missingZone";
            return false;
        }

        currentZoneName = zone->getZoneName();

        if (currentZoneName != targetZone) {
            activationResult = "wrongPlanet";
            return false;
        }

        if (strongAgent->isDead()) {
            activationResult = "dead";
            return false;
        }

        if (strongAgent->isIncapacitated()) {
            activationResult = "incapacitated";
            return false;
        }

        if (strongAgent->isInCombat()) {
            activationResult = "combat";
            return false;
        }

        if (!zone->isWithinBoundaries(targetPosition)) {
            activationResult = "targetOutOfBounds";
            return false;
        }
    }

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        bool sameAssignment =
            intelligentAssignmentGenerationId == assignmentGenerationId &&
            !targetHash.isEmpty() &&
            intelligentTargetHash == targetHash;

        if (!sameAssignment) {
            activationResult = "controllerBusy";
            return false;
        }

        intelligentLogActivationLifecycle = logActivationLifecycle;
        activationResult = "alreadyActive";

        if (intelligentSampleActive) {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "sampleStarted", activationResult);
        } else if (intelligentAssignmentStationed) {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "stationed", activationResult);
        } else if (intelligentAssignmentActive) {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "activationStarted", activationResult);
        } else {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "queued", activationResult);
        }

        return true;
    }

    intelligentProfileKey = profileKey;
    intelligentResourceName = resourceName;
    intelligentResourceType = resourceType;
    intelligentTargetZone = targetZone;
    intelligentTargetPosition = targetPosition;
    intelligentTargetDensity = density;
    intelligentAssignmentExpiresAtMs = expiresAtMs;
    intelligentAssignmentGenerationId = assignmentGenerationId;
    intelligentTargetHash = targetHash;
    intelligentActivationSnapshotId = activationSnapshotId;
    intelligentActivationPathValidationStatus = activationPathValidationStatus;
    intelligentActivationPathTrustStatus = activationPathTrustStatus;
    intelligentLogActivationLifecycle = logActivationLifecycle;
    intelligentQueuedState = getSimStateName(state);
    intelligentQueuedDuringSample =
        state == SAMPLING || state == PERFORMING_ACTION;
    intelligentQueuedAtMs = now;
    intelligentAssignmentPending = true;

    activationResult = "queued";
    logIntelligentTargetActivation("queued");
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        strongAgent->getObjectID(), "queued", activationResult);
    return true;
}

bool SimMinerController::beginIntelligentTargetAssignment(String& activationResult) {
    activationResult = "fallback";

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        activationResult = "controllerUnavailable";
        clearLocalIntelligentTargetAssignment();
        return false;
    }

    uint64 now = System::getMiliTime();
    uint64 sourceObjectID = strongAgent->getObjectID();

    if (intelligentAssignmentExpiresAtMs > 0 &&
            now > intelligentAssignmentExpiresAtMs) {
        activationResult = "assignmentExpired";
        logIntelligentTargetActivation("fallback", activationResult);
        SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
            sourceObjectID, "failed", activationResult);
        clearLocalIntelligentTargetAssignment();
        SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(sourceObjectID, activationResult);
        return false;
    }

    bool activationSafe = true;

    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();

        if (zone == nullptr) {
            activationResult = "missingZone";
            activationSafe = false;
        } else if (zone->getZoneName() != intelligentTargetZone) {
            activationResult = "wrongPlanet";
            activationSafe = false;
        } else if (strongAgent->isDead() || strongAgent->isIncapacitated() ||
                strongAgent->isInCombat()) {
            activationResult = "controllerStateNotSafe";
            activationSafe = false;
        } else if (!zone->isWithinBoundaries(intelligentTargetPosition)) {
            activationResult = "targetOutOfBounds";
            activationSafe = false;
        }
    }

    if (!activationSafe) {
        logIntelligentTargetActivation("fallback", activationResult);
        SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
            sourceObjectID, "failed", activationResult);
        clearLocalIntelligentTargetAssignment();
        SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(sourceObjectID, activationResult);
        return false;
    }

    intelligentAssignmentPending = false;
    intelligentAssignmentActive = true;
    intelligentSampleActive = false;
    intelligentAssignmentStationed = false;

    // Keep yield conceptual and independent from the exact ResourceSpawn target.
    targetResource = pickRandomResource();

    activationResult = "started";
    logIntelligentTargetActivation("started");
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "activationStarted", activationResult);
    moveTo(intelligentTargetPosition);
    return true;
}

void SimMinerController::performIntelligentSample() {
    state = SAMPLING;
    intelligentSampleActive = true;

    if (agent == nullptr) {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    agent->clearPatrolPoints();
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample");

    Reference<SimBehaviorTask*> task =
        new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(config.sampleDurationMs);
}

void SimMinerController::startStationedSample() {
    if (!intelligentAssignmentStationed)
        return;

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    intelligentAssignmentStationed = false;
    intelligentAssignmentActive = true;
    intelligentSampleActive = false;

    uint64 sourceObjectID = strongAgent->getObjectID();
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "sampleStarted", "stationedRepeat");
    logIntelligentTargetArrival("stationed_sample_started");
    performIntelligentSample();
}

void SimMinerController::finishIntelligentSample() {
    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    String completedResource = targetResource;
    int yieldAmount = 0;
    bool logYield = false;
    bool recordYield = prepareConceptualYield(completedResource, yieldAmount, logYield);
    uint64 sourceObjectID = strongAgent->getObjectID();

    logIntelligentTargetArrival("sample_finished");
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "sampleFinished");
    strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    strongAgent->doAnimation("stop_sample");

    if (recordYield) {
        SimPlayerManager::instance()->recordIntelligentConceptualMinerYield(
            completedResource, yieldAmount, sourceObjectID, logYield);
    }

    bool scheduleRepeatedSample = false;
    int repeatedSampleDelayMs = 0;
    String stationedReason;
    bool retainedStationed =
        SimPlayerManager::instance()->transitionMinerIntelligentAssignmentToStationed(
            sourceObjectID,
            recordYield ? yieldAmount : 0,
            scheduleRepeatedSample,
            repeatedSampleDelayMs,
            stationedReason);

    if (retainedStationed) {
        intelligentAssignmentPending = false;
        intelligentAssignmentActive = false;
        intelligentSampleActive = false;
        intelligentAssignmentStationed = true;
        state = WAITING;

        if (scheduleRepeatedSample && repeatedSampleDelayMs > 0) {
            Reference<SimBehaviorTask*> task =
                new SimBehaviorTask(this, SimBehaviorTask::START_STATIONED_SAMPLE);
            task->schedule(repeatedSampleDelayMs);
        }

        return;
    }

    clearLocalIntelligentTargetAssignment();
    SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(
        sourceObjectID,
        stationedReason.isEmpty() ? String("sampleComplete") : stationedReason);
    startSimLoop();
}

void SimMinerController::clearLocalIntelligentTargetAssignment() {
    intelligentAssignmentPending = false;
    intelligentAssignmentActive = false;
    intelligentSampleActive = false;
    intelligentAssignmentStationed = false;
    intelligentProfileKey = "";
    intelligentQueuedDuringSample = false;
    intelligentQueuedAtMs = 0;
    intelligentAssignmentGenerationId = 0;
    intelligentActivationSnapshotId = 0;
    intelligentQueuedState = "";
    intelligentTargetHash = "";
    intelligentActivationPathValidationStatus = "";
    intelligentActivationPathTrustStatus = "";
    intelligentResourceName = "";
    intelligentResourceType = "";
    intelligentTargetZone = "";
    intelligentTargetPosition = Vector3(0, 0, 0);
    intelligentTargetDensity = 0.f;
    intelligentAssignmentExpiresAtMs = 0;
}

void SimMinerController::logIntelligentTargetActivation(
        const String& action, const String& reason) const {
    if (!intelligentLogActivationLifecycle)
        return;

    uint64 objectID = agent != nullptr ? agent->getObjectID() : 0;

    String line = String("MinerIntelligentTargetActivation miner=") +
        String::valueOf(objectID) +
        " action=" + action +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash) +
        " activationSnapshotId=" +
            String::valueOf(intelligentActivationSnapshotId) +
        " selectedProfile=" +
            (intelligentProfileKey.isEmpty() ?
                String("none") : intelligentProfileKey) +
        " targetResource=" +
            (intelligentResourceName.isEmpty() ?
                String("none") : intelligentResourceName) +
        " targetType=" +
            (intelligentResourceType.isEmpty() ?
                String("none") : intelligentResourceType) +
        " targetZone=" +
            (intelligentTargetZone.isEmpty() ?
                String("none") : intelligentTargetZone) +
        " x=" +
            String::valueOf(Math::getPrecision(
                intelligentTargetPosition.getX(), 1)) +
        " y=" +
            String::valueOf(Math::getPrecision(
                intelligentTargetPosition.getY(), 1)) +
        " z=" +
            String::valueOf(Math::getPrecision(
                intelligentTargetPosition.getZ(), 1)) +
        " density=" +
            String::valueOf(Math::getPrecision(intelligentTargetDensity, 3)) +
        " pathValidationStatus=" +
            (intelligentActivationPathValidationStatus.isEmpty() ?
                String("valid") : intelligentActivationPathValidationStatus) +
        " pathTrustStatus=" +
            (intelligentActivationPathTrustStatus.isEmpty() ?
                String("verifiedPath") : intelligentActivationPathTrustStatus) +
        " queuedState=" +
            (intelligentQueuedState.isEmpty() ?
                String("none") : intelligentQueuedState) +
        " queuedDuringSample=" +
            (intelligentQueuedDuringSample ?
                String("true") : String("false")) +
        " previousSampleYieldMayFollow=" +
            (intelligentQueuedDuringSample ?
                String("true") : String("false"));

    if (intelligentQueuedAtMs > 0) {
        uint64 now = System::getMiliTime();
        uint64 queuedAgeSeconds = now > intelligentQueuedAtMs ?
            (now - intelligentQueuedAtMs) / 1000 : 0;
        line += " queuedAgeSeconds=" + String::valueOf(queuedAgeSeconds);
    }

    if (!reason.isEmpty())
        line += " fallbackReason=" + reason;

    line += " mode=limited";
    Logger::console.info(line, true);
}

void SimMinerController::logIntelligentTargetArrival(
        const String& arrivalResult) const {
    if (!intelligentLogActivationLifecycle)
        return;

    uint64 objectID = agent != nullptr ? agent->getObjectID() : 0;

    Logger::console.info(
        String("MinerIntelligentTargetArrival miner=") +
        String::valueOf(objectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash) +
        " activationSnapshotId=" +
            String::valueOf(intelligentActivationSnapshotId) +
        " selectedProfile=" +
            (intelligentProfileKey.isEmpty() ?
                String("none") : intelligentProfileKey) +
        " targetResource=" +
            (intelligentResourceName.isEmpty() ?
                String("none") : intelligentResourceName) +
        " targetType=" +
            (intelligentResourceType.isEmpty() ?
                String("none") : intelligentResourceType) +
        " arrivalResult=" + arrivalResult +
        " yieldMode=conceptual" +
        " conceptualResource=" +
            (targetResource.isEmpty() ? String("none") : targetResource) +
        " mode=limited",
        true);
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
