/*
 * SimPlayerController.cpp
 * Debugging Startup Hang + Robust Retry
 */

#include "SimPlayerController.h"
#include "SimPlayerManager.h"
#include "engine/core/Core.h"
#include "engine/core/TaskManager.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "server/zone/managers/collision/CollisionManager.h"
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

    uint64 capturedGeneration = generation;

    if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "path_find"))
        return;

#ifdef DEBUG_SIMPVP
    // DEBUG: Trace start
    Logger::console.info("SimPlayer: [Thread] Pathfinding started...", true);
#endif
    Vector<WorldCoordinates>* path = nullptr;
    bool pathUsesNavmesh = useRecastPath;
    bool pathIsOverland = useDirectOverlandPath;
    
    try {
        if (useRecastPath) {
            Vector<WorldCoordinates>* recastPath =
                new Vector<WorldCoordinates>();
            float length = 0.f;

            if (navArea != nullptr &&
                    PathFinderManager::instance()->getRecastPath(
                        recastStart, recastEnd, navArea, recastPath, length,
                        allowPartial)) {
                path = recastPath;
            } else {
                delete recastPath;
            }
        } else if (useDirectOverlandPath) {
            // This is an explicit overland request, not a guess based on the
            // resulting node count. The target height is terrain-derived only
            // after the controller has confirmed the agent is off-mesh (or
            // this is the sanctioned exit egress leg).
            Vector3 start = startCoord.getWorldPosition();
            Vector3 end = endCoord.getWorldPosition();
            if (directTargetUsesTerrainHeight)
                end.setZ(zone->getHeight(end.getX(), end.getY()));

            path = new Vector<WorldCoordinates>();
            path->add(WorldCoordinates(start, nullptr));
            path->add(WorldCoordinates(end, nullptr));
        } else {
            path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);
        }
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

    Core::getTaskManager()->executeTask([strongCtrl, path, capturedGeneration, pathUsesNavmesh, pathIsOverland] () {
        if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "path_find_result")) {
            if (path != nullptr)
                delete path;
            return;
        }

        if (path != nullptr)
            strongCtrl->onPathFound(path, pathUsesNavmesh, pathIsOverland);
        else
            strongCtrl->onPathTaskFailed(pathUsesNavmesh);
    }, "SimPlayerResultLambda");
}

void ArrivalCheckTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    uint64 capturedGeneration = generation;

    if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "arrival_check"))
        return;
    
    Core::getTaskManager()->executeTask([strongCtrl, capturedGeneration] () {
        if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "arrival_check"))
            return;

        strongCtrl->checkArrival();
    }, "SimPlayerArrivalLambda");
}

void SimBehaviorTask::run() {
    Reference<SimPlayerController*> baseCtrl = controller.get();
    if (baseCtrl == nullptr) return;

    int capturedType = type;
    uint64 capturedGeneration = generation;
    String taskType = String("behavior_") + String::valueOf(capturedType);

    if (!baseCtrl->isWorkLoopGenerationCurrent(capturedGeneration, taskType))
        return;

    Core::getTaskManager()->executeTask([baseCtrl, capturedType, capturedGeneration, taskType] () {
        if (!baseCtrl->isWorkLoopGenerationCurrent(capturedGeneration, taskType))
            return;

        SimMinerController* miner = dynamic_cast<SimMinerController*>(baseCtrl.get());
        if (miner == nullptr) return;

        if (capturedType == SimBehaviorTask::FINISH_SURVEY) miner->finishSurvey();
        else if (capturedType == SimBehaviorTask::FINISH_SAMPLE) miner->finishSample();
        else if (capturedType == SimBehaviorTask::START_STATIONED_SAMPLE) miner->startStationedSample();
    }, "SimPlayerBehaviorLambda");
}

class SimRetryTask : public Task {
    WeakReference<SimPlayerController*> controller;
    uint64 generation;
public:
    SimRetryTask(SimPlayerController* ctrl, uint64 g) : controller(ctrl), generation(g) {}
    void run() override {
        Reference<SimPlayerController*> strong = controller.get();
        if (strong != nullptr) {
            uint64 capturedGeneration = generation;

            if (!strong->isWorkLoopGenerationCurrent(capturedGeneration, "retry"))
                return;

            Core::getTaskManager()->executeTask([strong, capturedGeneration]() {
                if (!strong->isWorkLoopGenerationCurrent(capturedGeneration, "retry"))
                    return;

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
    rePathAttempts = 0;
    runSpeed = 3.0f;
    workLoopGeneration = 1;
    setLoggingName("SimPlayerController");
    destination = Vector3(0, 0, 0);
    destinationLocal = Vector3(0, 0, 0);
    destinationCell = nullptr;
    finalDestination = Vector3(0, 0, 0);
    hasFinalDestination = false;
    onMeshMode = false;
    navmeshModeDebounceCounter = 0;
    navmeshRepathAttempts = 0;
    hybridLeg = HYBRID_LEG_NONE;
    hybridEgressPoint = Vector3(0, 0, 0);
    interiorApproachLeg = false;
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

void SimPlayerController::moveTo(Vector3 targetPos) {
    moveTo(targetPos, targetPos, nullptr);
}

void SimPlayerController::moveToInterior(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell) {
    interiorApproachLeg = true;
    moveTo(worldPos, localPos, targetCell);
}

void SimPlayerController::moveTo(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell) {
    if (agent == nullptr) return;

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    if (!zone->isWithinBoundaries(worldPos)) {
        onPathFailed();
        return;
    }

    if (interiorApproachLeg)
        resetHybridMovementState(true);

    if (isHybridMovementActive()) {
        finalDestination = worldPos;
        hasFinalDestination = true;
        onMeshMode = agent->isInNavMesh();
        navmeshModeDebounceCounter = 0;
        navmeshRepathAttempts = 0;
        hybridLeg = HYBRID_LEG_NONE;
        hybridEgressPoint = Vector3(0, 0, 0);
    }

    destination = worldPos;
    destinationLocal = localPos;
    destinationCell = targetCell;

    if (agent->isInCombat()) {
        state = IDLE;
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer moveTo: isInCombat", true);
#endif
        return;
    }

    if (isHybridMovementActive()) {
        requestHybridPath();
        return;
    }

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH; 
    uint64 movementGeneration = advanceWorkLoopGeneration("moveTo");

    float dist = agent->getWorldPosition().distanceTo(worldPos);
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer moveTo: Requesting move to " + worldPos.toString() + " (Dist: " + String::valueOf(dist) + "m)", true);
#endif

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(localPos, targetCell);

    Reference<SimPathFindTask*> task =
        new SimPathFindTask(this, startCoord, endCoord, zone, movementGeneration);
    
    task->schedule(100); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path,
        bool pathUsesNavmesh, bool pathIsOverland) {
    if (agent == nullptr) { if (path) delete path; return; }

    if (isHybridMovementActive() && !shouldResumeHybridTravel()) {
        // The order completed/abandoned or entered lair cleanup while this path
        // was in flight. Drop the result instead of re-entering MOVING toward a
        // finished target. (Cancellation closes the resume gate before disengage,
        // so this is the deterministic last line against a stale in-flight task.)
        if (path) delete path;
        state = IDLE;
        return;
    }

    if (isHybridMovementActive()) {
        bool expectsNavmesh = hybridLeg == HYBRID_LEG_NAVMESH_FINAL ||
            hybridLeg == HYBRID_LEG_NAVMESH_EXIT;
        if (expectsNavmesh != pathUsesNavmesh) {
            delete path;
            onPathTaskFailed(expectsNavmesh);
            return;
        }
    }

    if (isHybridMovementActive() && pathIsOverland &&
            hybridLeg == HYBRID_LEG_OVERLAND_FINAL &&
            agent->isInNavMesh()) {
        // A direct task can outlive the boundary tick that scheduled it. Do
        // not accept an overland route after the agent has entered a mesh.
        delete path;
        onMeshMode = true;
        navmeshModeDebounceCounter = 0;
        requestHybridPath();
        return;
    }
    
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
        if (isHybridMovementActive() && pathUsesNavmesh)
            onPathTaskFailed(true);
        else
            onPathFailed();
        return;
    }

    // P.6.1b: reject a path that does not end where the current destination
    // points (stale result that slipped a generation race). The retry path
    // recomputes against the correct target.
    if (!acceptFoundPath(path->get(path->size() - 1).getWorldPosition())) {
        delete path;
        onPathFailed();
        return;
    }

    state = MOVING;
    simPath.removeAll();
    simPathIndex = 0;

    for (int i = 0; i < path->size(); ++i) {
        simPath.add(path->get(i));
    }

    WorldCoordinates finalPoint = simPath.get(simPath.size() - 1);
    destination = finalPoint.getWorldPosition();
    destinationLocal = finalPoint.getPoint();
    destinationCell = finalPoint.getCell();

    if (isHybridMovementActive()) {
        onMeshMode = agent->isInNavMesh();
        navmeshModeDebounceCounter = 0;
        navmeshRepathAttempts = 0;
    }
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer onPathFound: Path Found (" + String::valueOf(path->size()) + " nodes). Moving...", true);
#endif
    agent->setHomeLocation(finalPoint.getX(), finalPoint.getZ(),
        finalPoint.getY(), finalPoint.getCell());

    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    agent->clearPatrolPoints();
    agent->clearSavedPatrolPoints();
    // P.6.1d: invalidate the AGENT's cached A* route. findNextPosition
    // (AiAgentImplementation) reuses currentFoundPath while PATROLLING WITHOUT
    // re-checking it still matches the current patrol target, so a route left
    // over from a previous leg (e.g. before a switchZone teleport) would be
    // followed toward the OLD destination even though we just queued a fresh
    // path here. Nulling it forces a re-pathfind to the new patrol[0].
    agent->clearCurrentPath();
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
    Reference<ArrivalCheckTask*> task =
        new ArrivalCheckTask(this, getWorkLoopGeneration());
    task->schedule(500); 
}

void SimPlayerController::onPathTaskFailed(bool pathUsesNavmesh) {
    if (!isHybridMovementActive() || !pathUsesNavmesh) {
        onPathFailed();
        return;
    }

    int retryBudget = SimPlayerManager::instance()->getPveNavmeshRepathTries();
    if (navmeshRepathAttempts < retryBudget) {
        navmeshRepathAttempts++;
        requestHybridPath();
        return;
    }

    navmeshRepathAttempts = 0;
    onPathFailed();
}

bool SimPlayerController::findNavAreaAt(Zone* zone, const Vector3& position,
        ManagedReference<NavArea*>& area) const {
    if (zone == nullptr)
        return false;

    SortedVector<ManagedReference<NavArea*> > areas;
    zone->getInRangeNavMeshes(position.getX(), position.getY(), &areas, false);

    for (int i = 0; i < areas.size(); ++i) {
        ManagedReference<NavArea*> candidate = areas.get(i);
        if (candidate != nullptr &&
                candidate->containsPoint(position.getX(), position.getY())) {
            area = candidate;
            return true;
        }
    }

    return false;
}

bool SimPlayerController::resolveHybridExit(Zone* zone,
        const Vector3& currentPosition, Vector3& boundary,
        Vector3& egress, ManagedReference<NavArea*>& area) const {
    if (zone == nullptr || !hasFinalDestination)
        return false;

    // getNavMeshCollisions discards rays whose origin is already inside the
    // mesh (tca < 0). Cast from the wilderness destination back toward the
    // hunter so the first collision is the reliable exit boundary.
    SortedVector<ManagedReference<NavArea*> > areas;
    zone->getInRangeNavMeshes(currentPosition.getX(), currentPosition.getY(),
        &areas, true);
    if (areas.size() == 0)
        return false;

    SortedVector<NavCollision*> collisions;
    PathFinderManager::instance()->getNavMeshCollisions(&collisions, &areas,
        finalDestination, currentPosition);

    Vector3 collisionPosition;
    NavArea* selectedArea = nullptr;
    for (int i = 0; i < collisions.size(); ++i) {
        NavCollision* collision = collisions.get(i);
        if (collision == nullptr)
            continue;

        NavArea* collisionArea = collision->getNavArea();
        if (selectedArea == nullptr || collisionArea == area.get()) {
            selectedArea = collisionArea;
            collisionPosition = collision->getPosition();
            if (collisionArea == area.get())
                break;
        }
    }

    for (int i = 0; i < collisions.size(); ++i)
        delete collisions.get(i);

    if (selectedArea == nullptr)
        return false;

    area = selectedArea;
    collisionPosition.setZ(CollisionManager::getWorldFloorCollision(
        collisionPosition.getX(), collisionPosition.getY(), zone, true));
    if (!zone->isWithinBoundaries(collisionPosition))
        return false;

    Vector3 outward = finalDestination - collisionPosition;
    outward.setZ(0.f);
    float outwardLength = outward.length2d();
    if (outwardLength < 0.001f)
        return false;
    outward.normalize();

    // NavCollision is deliberately just inside the mesh. Probe far enough to
    // clear the complete active-area radius, but keep the search bounded so a
    // malformed mesh cannot turn a movement request into an unbounded loop.
    const float probeStep = 8.f;
    const int maxProbeSteps = 128;
    for (int step = 1; step <= maxProbeSteps; ++step) {
        Vector3 candidate = collisionPosition + outward *
            (probeStep * static_cast<float>(step));
        candidate.setZ(CollisionManager::getWorldFloorCollision(
            candidate.getX(), candidate.getY(), zone, true));

        if (!zone->isWithinBoundaries(candidate))
            continue;

        SortedVector<ManagedReference<NavArea*> > candidateAreas;
        zone->getInRangeNavMeshes(candidate.getX(), candidate.getY(),
            &candidateAreas, false);

        // The candidate is valid only after both the ground/water snap and
        // the nav-region query agree that it is outside every NavArea.
        if (candidateAreas.size() == 0) {
            egress = candidate;
            boundary = collisionPosition;
            return true;
        }
    }

    return false;
}

void SimPlayerController::scheduleHybridDirectPath(const Vector3& target,
        HybridLeg leg) {
    if (agent == nullptr || agent->getZone() == nullptr ||
            !hasFinalDestination || !isHybridMovementActive())
        return;

    Zone* zone = agent->getZone();
    if (!zone->isWithinBoundaries(target)) {
        onPathTaskFailed(false);
        return;
    }

    destination = target;
    destinationLocal = target;
    destinationCell = nullptr;
    hybridLeg = leg;
    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH;
    uint64 movementGeneration = advanceWorkLoopGeneration(
        "hybridDirectOverland");

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(target, nullptr);
    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord,
        endCoord, zone, true, leg != HYBRID_LEG_EGRESS,
        movementGeneration);
    task->schedule(100);
}

void SimPlayerController::requestHybridPath() {
    if (agent == nullptr || agent->getZone() == nullptr ||
            !hasFinalDestination || !isHybridMovementActive()) {
        onPathFailed();
        return;
    }

    Zone* zone = agent->getZone();
    Vector3 currentPosition = agent->getWorldPosition();

    if (!onMeshMode) {
        scheduleHybridDirectPath(finalDestination,
            HYBRID_LEG_OVERLAND_FINAL);
        return;
    }

    ManagedReference<NavArea*> currentArea;
    if (!findNavAreaAt(zone, currentPosition, currentArea)) {
        onPathTaskFailed(true);
        return;
    }

    ManagedReference<NavArea*> targetArea;
    bool targetOnSameMesh = findNavAreaAt(zone, finalDestination, targetArea) &&
        targetArea == currentArea;

    Vector3 pathEnd = finalDestination;
    HybridLeg leg = HYBRID_LEG_NAVMESH_FINAL;
    if (!targetOnSameMesh) {
        Vector3 boundary;
        Vector3 egress;
        ManagedReference<NavArea*> exitArea;
        if (!resolveHybridExit(zone, currentPosition, boundary, egress,
                exitArea)) {
            onPathTaskFailed(true);
            return;
        }

        // The resolved area is the one used for the recast leg. This also
        // makes the area provenance explicit instead of inferring it from
        // path node count.
        currentArea = exitArea;
        pathEnd = boundary;
        hybridEgressPoint = egress;
        leg = HYBRID_LEG_NAVMESH_EXIT;
    } else {
        hybridEgressPoint = Vector3(0, 0, 0);
    }

    destination = pathEnd;
    destinationLocal = pathEnd;
    destinationCell = nullptr;
    hybridLeg = leg;
    stuckWatchdogCount = 0;
    lastWatchdogPos = currentPosition;
    state = CALCULATING_PATH;
    uint64 movementGeneration = advanceWorkLoopGeneration(
        "hybridRecastPath");

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(pathEnd, nullptr);
    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord,
        endCoord, zone, currentArea, currentPosition, pathEnd, false,
        movementGeneration);
    task->schedule(100);
}

void SimPlayerController::onPathFailed() {
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer onPathFailed: Pathfinding failed/unreachable. Retrying in 5s...", true);
#endif
    interiorApproachLeg = false;
    state = IDLE;

    Reference<SimRetryTask*> task =
        new SimRetryTask(this, getWorkLoopGeneration());
    task->schedule(5000); // 5 seconds
}

void SimPlayerController::resetHybridMovementState(bool clearFinalDestination) {
    onMeshMode = false;
    navmeshModeDebounceCounter = 0;
    navmeshRepathAttempts = 0;
    hybridLeg = HYBRID_LEG_NONE;
    hybridEgressPoint = Vector3(0, 0, 0);

    if (clearFinalDestination) {
        finalDestination = Vector3(0, 0, 0);
        hasFinalDestination = false;
    }
}

void SimPlayerController::clearHybridMovementOnCancellation() {
    bool hadHybridMovement = isHybridMovementActive() || interiorApproachLeg;
    interiorApproachLeg = false;
    if (!hadHybridMovement)
        return;

    resetHybridMovementState(true);
}

uint64 SimPlayerController::advanceWorkLoopGeneration(const String& reason) {
    (void)reason;
    workLoopGeneration++;

    if (workLoopGeneration == 0)
        workLoopGeneration = 1;

    return workLoopGeneration;
}

bool SimPlayerController::isWorkLoopGenerationCurrent(
        uint64 capturedGeneration, const String& taskType) {
    (void)taskType;
    uint64 currentGeneration = workLoopGeneration;

    if (capturedGeneration == currentGeneration)
        return true;

    return false;
}

void SimPlayerController::onStaleWorkLoopTaskIgnored(
        const String& taskType, uint64 capturedGeneration,
        uint64 currentGeneration) {
#ifdef DEBUG_SIMPVP
    Logger::console.info(
        String("SimPlayerStaleTaskIgnored taskType=") + taskType +
        " capturedGeneration=" + String::valueOf(capturedGeneration) +
        " currentGeneration=" + String::valueOf(currentGeneration),
        true);
#endif
}

void SimPlayerController::queueMorePathNodes() {
    if (agent == nullptr) return;
    if (simPathIndex < 0) simPathIndex = 0;

    int pathSize = simPath.size();
    if (simPathIndex >= pathSize) return;

    int currentQueued = agent->getPatrolPointSize();
    int slots = 18 - currentQueued; 

    while (slots > 0 && simPathIndex < pathSize) {
        WorldCoordinates node = simPath.get(simPathIndex);
        Vector3 p = node.getWorldPosition();

        if (simPathIndex == 0) {
            Vector3 cur = agent->getWorldPosition();
            float dx0 = p.getX() - cur.getX();
            float dy0 = p.getY() - cur.getY();
            if ((dx0*dx0 + dy0*dy0) < 1.0f) { 
                simPathIndex++;     
                continue;
            }
        }

        const Vector3& localPoint = node.getPoint();
        PatrolPoint pp(localPoint.getX(), localPoint.getZ(),
            localPoint.getY(), node.getCell());
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
        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
        task->schedule(1000);
        return;
    }

    if (agent->isInCombat()) {
        state = IDLE; 
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: isInCombat", true);
#endif
        locker.release();
        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
        task->schedule(1000); 
        return;
    }

    if (isHybridMovementActive() &&
            (state == MOVING || (state == IDLE && shouldResumeHybridTravel()))) {
        bool observedOnMesh = agent->isInNavMesh();
        if (observedOnMesh != onMeshMode) {
            navmeshModeDebounceCounter++;
            int debounceTicks =
                SimPlayerManager::instance()->getPveNavmeshModeDebounceTicks();
            if (debounceTicks < 1)
                debounceTicks = 1;

            if (navmeshModeDebounceCounter >= debounceTicks) {
                onMeshMode = observedOnMesh;
                navmeshModeDebounceCounter = 0;
                locker.release();
                requestHybridPath();
                return;
            }
        } else {
            navmeshModeDebounceCounter = 0;
        }
    }

    if (isHybridMovementActive() && state == IDLE &&
            shouldResumeHybridTravel()) {
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Resuming hybrid path to " +
            finalDestination.toString(), true);
#endif
        locker.release();
        requestHybridPath();
        return;
    }

    if (!isHybridMovementActive() && state == IDLE && destination.getX() != 0) {
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Resuming path to " + destination.toString(), true);
#endif
        Vector3 resumeDestination = destination;
        Vector3 resumeLocalDestination = destinationLocal;
        ManagedReference<CellObject*> resumeCell = destinationCell;
        locker.release();
        moveTo(resumeDestination, resumeLocalDestination, resumeCell.get());
        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
        task->schedule(1000);
        return;
    }

    if (state != MOVING) {
        locker.release();

        if (!shouldContinueArrivalChecks())
            return;

        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
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

    if (isHybridMovementActive() &&
            (hybridLeg == HYBRID_LEG_NAVMESH_FINAL ||
             hybridLeg == HYBRID_LEG_OVERLAND_FINAL)) {
        float finalDx = currentPos.getX() - finalDestination.getX();
        float finalDy = currentPos.getY() - finalDestination.getY();
        if ((finalDx * finalDx) + (finalDy * finalDy) >= 16.0f)
            arrived = false;
    }

    if (arrived) {
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Arrived at destination.", true);
#endif
        agent->clearPatrolPoints();
        state = WAITING;

        if (isHybridMovementActive()) {
            if (hybridLeg == HYBRID_LEG_OVERLAND_FINAL &&
                    agent->isInNavMesh()) {
                // A short wilderness leg can reach a city before two
                // arrival samples have elapsed. Never fire onArrived from a
                // direct route that ended on a navmesh.
                onMeshMode = true;
                navmeshModeDebounceCounter = 0;
                locker.release();
                requestHybridPath();
                return;
            }

            if (hybridLeg == HYBRID_LEG_NAVMESH_EXIT) {
                // The recast leg reached the resolved boundary. The logical
                // destination survives this sub-leg; first cross a bounded,
                // validated off-mesh egress waypoint.
                Vector3 egress = hybridEgressPoint;
                locker.release();
                scheduleHybridDirectPath(egress, HYBRID_LEG_EGRESS);
                return;
            }

            if (hybridLeg == HYBRID_LEG_EGRESS) {
                // This is the sanctioned transition: the egress probe was
                // validated outside every NavArea before it was scheduled.
                if (agent->isInNavMesh()) {
                    locker.release();
                    onPathFailed();
                    return;
                }
                onMeshMode = false;
                navmeshModeDebounceCounter = 0;
                locker.release();
                scheduleHybridDirectPath(finalDestination,
                    HYBRID_LEG_OVERLAND_FINAL);
                return;
            }

            // Only the logical target clears finalDestination. Boundary and
            // egress acceptance above deliberately leave it intact.
            resetHybridMovementState(true);
        }

        clearInteriorApproachLeg();

        locker.release();
        onArrived();

        if (shouldContinueArrivalChecks()) {
            Reference<ArrivalCheckTask*> task =
                new ArrivalCheckTask(this, getWorkLoopGeneration());
            task->schedule(1000);
        }

        return;
    } 

    agent->findNextPosition(2.0f, false);
    
    float moveDx = currentPos.getX() - lastWatchdogPos.getX();
    float moveDy = currentPos.getY() - lastWatchdogPos.getY();
    float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

    // --- STUCK WATCHDOG WITH BOUNDED ESCALATION ---
    // No forward progress: first soft-nudge the next step, then re-path a
    // bounded number of times, then give up via onPathFailed() so the planner
    // can reassign instead of the miner spinning in MOVING forever. Any forward
    // progress (else branch) refreshes both counters.
    static const int kStuckSoftNudgeTicks = 5;
    static const int kStuckRePathTicks = 12;
    static const int kMaxRePathAttempts = 2;

    if (movedDistSq < 0.05f) {
        stuckWatchdogCount++;

        if (stuckWatchdogCount >= kStuckRePathTicks) {
            Vector3 resumeDestination = destination;
            Vector3 resumeLocalDestination = destinationLocal;
            ManagedReference<CellObject*> resumeCell = destinationCell;
            locker.release();

            if (rePathAttempts < kMaxRePathAttempts && shouldRepathWhenStuck()) {
                rePathAttempts++;
#ifdef DEBUG_SIMPVP
                Logger::console.info("SimPlayer checkArrival: stuck; re-path attempt " + String::valueOf(rePathAttempts), true);
#endif
                // Both paths advance the work-loop generation and schedule a
                // fresh path-find + arrival loop, so do not reschedule here.
                if (isHybridMovementActive())
                    requestHybridPath();
                else
                    moveTo(resumeDestination, resumeLocalDestination,
                        resumeCell.get());
            } else {
#ifdef DEBUG_SIMPVP
                Logger::console.info("SimPlayer checkArrival: stuck; re-path budget exhausted, failing path.", true);
#endif
                onPathFailed();
            }

            return;
        }

        if (stuckWatchdogCount > kStuckSoftNudgeTicks) {
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
        rePathAttempts = 0;
    }

    lastWatchdogPos = currentPos;

    locker.release();
    Reference<ArrivalCheckTask*> task =
        new ArrivalCheckTask(this, getWorkLoopGeneration());
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
    intelligentTravelActive = false;
    travelDestinationZone = "";
    travelDeparturePosition = Vector3(0, 0, 0);
    travelDestinationArrival = Vector3(0, 0, 0);
    travelDestinationStarport = "";
    travelStartedAtMs = 0;
    travelBoardRadius = 20.f;
    intelligentFinalApproachAttempts = 0;
    intelligentLastApproachDistance = 0.f;
    setLoggingName("SimMinerController");
}

SimMinerController::~SimMinerController() {
}

void SimMinerController::startSimLoop() {
    String activationResult;

    // P.4.5b: while traveling, the run to the ticket collector is driven by
    // moveTo()/checkArrival(); the normal decision loop must not clobber it.
    if (intelligentTravelActive) {
        state = WAITING;
        logLegacyLoopSuppressed("interplanetaryTravelActive");
        return;
    }

    if (intelligentAssignmentPending && beginIntelligentTargetAssignment(activationResult)) {
        uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
        SimPlayerManager::instance()->recordIntelligentMinerLoopStarted(
            sourceObjectID, "pendingAssignment");
        return;
    }

    if (intelligentAssignmentStationed) {
        state = WAITING;
        logLegacyLoopSuppressed("stationedLifecycleActive");
        return;
    }

    if (intelligentAssignmentActive || intelligentSampleActive) {
        logLegacyLoopSuppressed("intelligentAssignmentActive");
        return;
    }

    if (SimPlayerManager::instance()->isIntelligentMinerWorkLoopOwnerEnabled() &&
            !SimPlayerManager::instance()->isLegacyConceptualMinerLoopAllowed()) {
        state = WAITING;
        logLegacyLoopSuppressed("waitingForIntelligentAssignment");
        return;
    }

    advanceWorkLoopGeneration("legacyLoopStarted");
    state = DECIDING;
    String res = pickRandomResource();
    targetResource = res;
    uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
    SimPlayerManager::instance()->recordLegacyMinerLoopStarted(
        sourceObjectID, "conceptualFallbackAllowed");
    logStateTransition("SimMinerLegacyLoopStarted: selected conceptual resource [" + res + "]");
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

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySurveyBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    state = SURVEYING;
    logStateTransition("SimMinerLegacySurveyStarted resource=" + targetResource);

    agent->setMovementState(AiAgent::OBLIVIOUS);
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    agent->doAnimation("manipulate_high"); 

    Reference<SimBehaviorTask*> task =
        new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY,
            getWorkLoopGeneration());
    task->schedule(config.surveyDurationMs);
}

void SimMinerController::finishSurvey() {
    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySurveyFinishBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    logStateTransition("SimMinerLegacySurveyFinished resource=" + targetResource);
    goToResource(targetResource);
}

// P.4.4b mounted travel. Deploy+mount a real swoop (proven P.4.4a manager
// plumbing) when the upcoming leg is long enough, and ride it at the vehicle's
// own run speed the same way a mounted player does (CreatureObject::getRunSpeed
// returns the vehicle's speed for riders; AiAgent::findNextPosition reads the
// raw member, so we copy the value explicitly and restore it on dismount).
// LOCKING: never call the manager mount/dismount functions with the agent
// locked — they take their own agent+vehicle crosslocks (see the 2026-07-02
// deadlock postmortem in docs/npc-mount-and-player-dot-plan.md).
void SimMinerController::maybeMountForTravel(const Vector3& target) {
    if (mountedForTravel)
        return;

    ManagedReference<AiAgent*> strongAgent = agent;
    if (strongAgent == nullptr)
        return;

    SimPlayerManager* manager = SimPlayerManager::instance();
    if (!manager->isMountedTravelEnabled())
        return;

    Vector3 pos;
    float baseRunSpeed = 0.f;
    {
        Locker lock(strongAgent);
        if (strongAgent->isRidingMount() || strongAgent->getParent().get() != nullptr)
            return;
        pos = strongAgent->getWorldPosition();
        baseRunSpeed = strongAgent->getRunSpeed();
    }

    float minLeg = (float)manager->getMountedTravelMinLegMeters();
    float dx = pos.getX() - target.getX();
    float dy = pos.getY() - target.getY();
    if ((dx * dx + dy * dy) < minLeg * minLeg)
        return;

    String result;
    if (!manager->deployAndMountMinerVehicle(strongAgent->getObjectID(), result)) {
        logStateTransition("SimMinerMountedTravel mountFailed result=" + result +
            "; continuing on foot");
        return;
    }

    float vehicleSpeed = 0.f;
    {
        Locker lock(strongAgent);
        ManagedReference<SceneObject*> parent = strongAgent->getParent().get();
        if (parent != nullptr && parent->isVehicleObject()) {
            CreatureObject* vehicle = parent->asCreatureObject();
            if (vehicle != nullptr)
                vehicleSpeed = vehicle->getRunSpeed();
        }
        if (vehicleSpeed > 0.f) {
            preMountRunSpeed = baseRunSpeed;
            strongAgent->setRunSpeed(vehicleSpeed);
        }
    }

    mountedForTravel = true;
    manager->recordMountedTravelLegStarted();
    logStateTransition("SimMinerMountedTravel mounted speed=" +
        String::valueOf(vehicleSpeed) + " legMeters=" +
        String::valueOf(Math::sqrt(dx * dx + dy * dy)));
}

void SimMinerController::dismountIfMounted(const String& reason) {
    if (!mountedForTravel)
        return;

    mountedForTravel = false;

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent != nullptr && preMountRunSpeed > 0.f) {
        Locker lock(strongAgent);
        strongAgent->setRunSpeed(preMountRunSpeed);
    }
    preMountRunSpeed = 0.f;

    if (strongAgent == nullptr)
        return;

    String result;
    SimPlayerManager::instance()->dismountAndStoreMinerVehicle(
        strongAgent->getObjectID(), result);
    logStateTransition("SimMinerMountedTravel dismounted reason=" + reason +
        " result=" + result);
}

void SimMinerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacyMoveBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

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
            logStateTransition("SimMinerLegacyMoveBlocked: no in-bounds fallback destination for [" +
                resourceName + "]; retrying loop");
            onPathFailed();
            return;
        }

        targetPos.setZ(zone->getHeight(targetPos.getX(), targetPos.getY()));
        usedFallback = true;
    }

    String destinationSource = usedFallback ? "fallback" : "navmesh";
    logStateTransition("SimMinerLegacyMoveStarted resource=" + resourceName + " destinationSource=" + destinationSource + " target=" + targetPos.toString());
    rePathAttempts = 0;
    maybeMountForTravel(targetPos);
    moveTo(targetPos);
}

void SimMinerController::onArrived() {
    // P.4.5b: the miner ran to the origin starport's ticket collector. Board the
    // shuttle now = teleport to the destination starport's outdoor arrival.
    if (intelligentTravelActive) {
        boardInterplanetaryShuttle("arrived");
        return;
    }

    if (intelligentAssignmentActive) {
        // P.4.5c final approach: a long off-navmesh walk can terminate short of
        // the true target (path exhausted / patrol point popped early), which
        // used to station the miner hundreds of meters away and churn it via
        // recovery. Re-path directly toward the target to close the gap -- bounded
        // by leg count and by requiring real progress each leg so a genuinely
        // unreachable target can't loop. Stationing within arrivalRadius is fine
        // (planet-wide resource; ~10-15 m short is acceptable).
        if (agent != nullptr) {
            static const int kMaxFinalApproachLegs = 8;
            static const float kMinApproachProgressMeters = 5.f;

            float arrivalRadius =
                SimPlayerManager::instance()->getMinerIntelligentArrivalRadiusMeters();
            Vector3 pos = agent->getWorldPosition();
            float dx = pos.getX() - intelligentTargetPosition.getX();
            float dy = pos.getY() - intelligentTargetPosition.getY();
            float distToTarget = Math::sqrt(dx * dx + dy * dy);

            bool madeProgress = intelligentFinalApproachAttempts == 0 ||
                distToTarget <=
                    intelligentLastApproachDistance - kMinApproachProgressMeters;

            if (distToTarget > arrivalRadius &&
                    intelligentFinalApproachAttempts < kMaxFinalApproachLegs &&
                    madeProgress) {
                intelligentFinalApproachAttempts++;
                intelligentLastApproachDistance = distToTarget;
                logIntelligentTargetArrival("final_approach");
                Logger::console.info(
                    String("SimMinerFinalApproach miner=") +
                    String::valueOf(agent->getObjectID()) +
                    " leg=" + String::valueOf(intelligentFinalApproachAttempts) +
                    " distanceToTarget=" +
                        String::valueOf(Math::getPrecision(distToTarget, 1)) +
                    " arrivalRadius=" +
                        String::valueOf(Math::getPrecision(arrivalRadius, 1)),
                    true);
                moveTo(intelligentTargetPosition);
                return;
            }
        }

        uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
        if (sourceObjectID != 0)
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                sourceObjectID, "sampleStarted");
        logIntelligentTargetArrival("sample_started");
        performIntelligentSample();
        return;
    }

    if (intelligentAssignmentPending || intelligentAssignmentStationed ||
            intelligentSampleActive) {
        logLegacyLoopSuppressed("legacyArrivalBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    logStateTransition("SimMinerLegacyMoveArrived resource=" + targetResource);
    performSample();
}

void SimMinerController::onPathFailed() {
    // P.4.5b: a traveling miner whose departure run wedged must never be left
    // standing at/near the starport. Board from where it stands (the "ticket was
    // already bought" fallback) rather than escalating to a normal path failure.
    if (intelligentTravelActive) {
        boardInterplanetaryShuttle("stuckFallback");
        return;
    }

    // P.4.4b: park the swoop before any failure handling/reassignment.
    dismountIfMounted("pathFailed");

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

bool SimMinerController::shouldContinueArrivalChecks() const {
    return !intelligentAssignmentStationed;
}

bool SimMinerController::shouldRepathWhenStuck() const {
    // P.4.5b: while traveling to a starport the path is a straight off-navmesh
    // line to the ticket collector; re-pathing reproduces it, so skip re-path and
    // let the watchdog escalate to onPathFailed() -> board-anyway fallback.
    if (intelligentTravelActive)
        return false;

    // For a directOverland assignment the path is a straight terrain-following
    // line; re-pathing reproduces the same line, so skip re-path and let the
    // watchdog give up immediately so the planner can reassign.
    if (intelligentAssignmentActive &&
            intelligentActivationPathTrustStatus == "directOverland")
        return false;

    return true;
}

void SimMinerController::resetIntelligentAssignmentForRecovery() {
    // Manager-initiated recovery (e.g. a stationed miner whose assignment was
    // reassigned far away and could not be reached). Drop all local intelligent
    // state (including stationed), tidy posture/patrol, and re-enter the work
    // loop so the planner can assign a fresh target the miner will actually
    // travel to. clearLocalIntelligentTargetAssignment advances the work-loop
    // generation, invalidating any in-flight stationed-sample/arrival tasks.
    // P.4.4b: a recovered miner must never keep (or leak) a swoop.
    dismountIfMounted("recoveryReset");
    clearLocalIntelligentTargetAssignment();

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent != nullptr) {
        Locker locker(strongAgent);
        strongAgent->clearPatrolPoints();
        if (strongAgent->getPosture() != CreaturePosture::UPRIGHT)
            strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    }

    startSimLoop();
}

bool SimMinerController::beginInterplanetaryTravel(
        const String& destZone,
        const Vector3& departurePos,
        const Vector3& destArrivalPos,
        const String& destStarportName,
        float boardRadius,
        String& travelResult) {
    travelResult = "fallback";

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        travelResult = "controllerUnavailable";
        return false;
    }

    if (destZone.isEmpty()) {
        travelResult = "invalidDestination";
        return false;
    }

    // Only ever dispatch a fully idle miner; never disrupt active gathering.
    if (!isAvailableForDispatch()) {
        travelResult = "controllerBusy";
        return false;
    }

    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();

        if (zone == nullptr) {
            travelResult = "missingZone";
            return false;
        }

        if (zone->getZoneName() == destZone) {
            travelResult = "alreadyOnPlanet";
            return false;
        }

        if (strongAgent->isDead() || strongAgent->isIncapacitated() ||
                strongAgent->isInCombat()) {
            travelResult = "controllerStateNotSafe";
            return false;
        }
    }

    intelligentTravelActive = true;
    travelDestinationZone = destZone;
    travelDeparturePosition = departurePos;
    travelDestinationArrival = destArrivalPos;
    travelDestinationStarport = destStarportName;
    travelStartedAtMs = System::getMiliTime();
    travelBoardRadius = boardRadius > 0.f ? boardRadius : 20.f;

    travelResult = "traveling";

    uint64 sourceObjectID = strongAgent->getObjectID();
    Logger::console.info(
        String("SimMinerInterplanetaryTravelStarted miner=") +
        String::valueOf(sourceObjectID) +
        " destZone=" + destZone +
        " destStarport=" +
            (destStarportName.isEmpty() ? String("none") : destStarportName) +
        " departure=(" +
            String::valueOf(Math::getPrecision(departurePos.getX(), 1)) + "," +
            String::valueOf(Math::getPrecision(departurePos.getY(), 1)) + ")",
        true);

    // Run to the origin starport's ticket collector. On arrival (or if the
    // stuck-watchdog gives up) onArrived()/onPathFailed() boards the shuttle.
    maybeMountForTravel(departurePos);
    moveTo(departurePos);
    return true;
}

void SimMinerController::boardInterplanetaryShuttle(const String& reason) {
    // P.4.4b: dismount + store the swoop at the starport before boarding —
    // player-mimetic, and the transient vehicle must never be left behind or
    // carried through switchZone.
    dismountIfMounted("boardShuttle");

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        // Nothing to board; just clear the flag so we don't wedge the state.
        clearLocalIntelligentTargetAssignment();
        return;
    }

    String destZone = travelDestinationZone;
    Vector3 arrival = travelDestinationArrival;
    String starport = travelDestinationStarport;
    uint64 minerID = strongAgent->getObjectID();

    if (destZone.isEmpty()) {
        // No valid destination recorded; cancel travel and re-acquire locally.
        resetIntelligentAssignmentForRecovery();
        return;
    }

    String fromZone = "unknown";

    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();
        if (zone != nullptr)
            fromZone = zone->getZoneName();

        // "Board the shuttle": switchZone params are (terrain, X, Z=height, Y=north,
        // parentID=0 outdoor). Same safe reposition as P.4.5a station travel; the
        // OUTDOOR arrival means we never enter the un-navmeshed starport interior.
        strongAgent->switchZone(destZone, arrival.getX(), arrival.getZ(),
            arrival.getY(), 0);
        // Anchor the leash on the new planet so a stale home location on the old
        // planet can't pull the miner. The next assignment's move resets it again.
        strongAgent->setHomeLocation(arrival.getX(), arrival.getZ(),
            arrival.getY(), nullptr);
    }

    Logger::console.info(
        String("SimMinerInterplanetaryTravelBoarded miner=") +
        String::valueOf(minerID) +
        " fromZone=" + fromZone +
        " toZone=" + destZone +
        " starport=" + (starport.isEmpty() ? String("none") : starport) +
        " reason=" + reason,
        true);

    SimPlayerManager::instance()->recordInterplanetaryTravelBoarded(
        minerID, fromZone, destZone, starport, reason);

    // clearLocalIntelligentTargetAssignment() (via reset) also clears travel state
    // and advances the work-loop generation, then startSimLoop() re-enters the
    // pipeline on the destination planet so it re-acquires a local target.
    resetIntelligentAssignmentForRecovery();
}

void SimMinerController::performSample() {
    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySampleBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    // P.4.4b: park the swoop before kneeling to sample.
    dismountIfMounted("legacySample");

    state = SAMPLING;
    logStateTransition("SimMinerLegacySampleStarted resource=" + targetResource);

    agent->clearPatrolPoints(); 
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task =
        new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE,
            getWorkLoopGeneration());
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

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySampleFinishBlockedByIntelligentLifecycle");
        strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
        strongAgent->doAnimation("stop_sample");
        startSimLoop();
        return;
    }

    String completedResource = targetResource;
    int yieldAmount = 0;
    bool logYield = false;
    bool recordYield = prepareConceptualYield(completedResource, yieldAmount, logYield);
    uint64 sourceObjectID = strongAgent->getObjectID();

    logStateTransition("SimMinerLegacySampleFinished resource=" + completedResource);
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

    // P.4.5b: a traveling miner is busy (running to the shuttle); the manager must
    // not hand it a normal same-planet target mid-trip.
    if (intelligentTravelActive) {
        activationResult = "controllerBusy";
        return false;
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
    advanceWorkLoopGeneration("intelligentAssignmentAccepted");

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

    targetResource = !intelligentResourceType.isEmpty() ?
        intelligentResourceType : intelligentResourceName;

    activationResult = "started";
    logIntelligentTargetActivation("started");
    Logger::console.info(
        String("SimMinerIntelligentMoveStarted miner=") +
        String::valueOf(sourceObjectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash) +
        " targetResource=" +
            (intelligentResourceName.isEmpty() ?
                String("none") : intelligentResourceName) +
        " targetType=" +
            (intelligentResourceType.isEmpty() ?
                String("none") : intelligentResourceType) +
        " targetZone=" +
            (intelligentTargetZone.isEmpty() ?
                String("none") : intelligentTargetZone),
        true);
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "activationStarted", activationResult);

    strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    strongAgent->doAnimation("stop_sample");
    rePathAttempts = 0;
    intelligentFinalApproachAttempts = 0;
    intelligentLastApproachDistance = 0.f;
    maybeMountForTravel(intelligentTargetPosition);
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
        new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE,
            getWorkLoopGeneration());
    task->schedule(SimPlayerManager::getGameDerivedStationedSampleResultDelayMs());
}

void SimMinerController::startStationedSample() {
    if (!intelligentAssignmentStationed)
        return;

    // P.4.4b: the ride ends where the work starts — park the swoop before the
    // first stationed sample (idempotent; later sample ticks no-op).
    dismountIfMounted("stationed");

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    intelligentAssignmentStationed = false;
    intelligentAssignmentActive = true;
    intelligentSampleActive = false;
    advanceWorkLoopGeneration("stationedSampleStarted");

    uint64 sourceObjectID = strongAgent->getObjectID();
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "sampleStarted", "stationedRepeat");
    logIntelligentTargetArrival("stationed_sample_started");
    Logger::console.info(
        String("SimMinerStationedSampleStarted miner=") +
        String::valueOf(sourceObjectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash),
        true);
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
    Logger::console.info(
        String("SimMinerStationedSampleFinished miner=") +
        String::valueOf(sourceObjectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash),
        true);
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "sampleFinished");
    strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    strongAgent->doAnimation("stop_sample");

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

    if (recordYield) {
        if (retainedStationed) {
            SimPlayerManager::instance()->recordSimulatedAcquisitionTransactionFromController(
                sourceObjectID, yieldAmount);
        }

        SimPlayerManager::instance()->recordIntelligentConceptualMinerYield(
            completedResource, yieldAmount, sourceObjectID, logYield);
    }

    if (retainedStationed) {
        intelligentAssignmentPending = false;
        intelligentAssignmentActive = false;
        intelligentSampleActive = false;
        intelligentAssignmentStationed = true;
        state = WAITING;
        advanceWorkLoopGeneration("stationed");

        if (scheduleRepeatedSample && repeatedSampleDelayMs > 0) {
            Reference<SimBehaviorTask*> task =
                new SimBehaviorTask(this,
                    SimBehaviorTask::START_STATIONED_SAMPLE,
                    getWorkLoopGeneration());
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
    advanceWorkLoopGeneration("clearIntelligentAssignment");
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
    // P.4.5b: also drop any in-flight travel so recovery that resets a traveling
    // miner cancels the trip cleanly (it re-acquires on its current planet).
    intelligentTravelActive = false;
    travelDestinationZone = "";
    travelDeparturePosition = Vector3(0, 0, 0);
    travelDestinationArrival = Vector3(0, 0, 0);
    travelDestinationStarport = "";
    travelStartedAtMs = 0;
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

    if (intelligentAssignmentActive || intelligentSampleActive ||
            intelligentAssignmentStationed) {
        amount =
            SimPlayerManager::getGameDerivedStationedSampleYield(
                intelligentTargetDensity);
        logYield = config.logYield;
        return amount > 0;
    }

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

void SimMinerController::logLegacyLoopSuppressed(const String& reason) const {
    uint64 objectID = agent != nullptr ? agent->getObjectID() : 0;

    SimPlayerManager::instance()->recordLegacyMinerLoopSuppressed(
        objectID,
        getSimStateName(state),
        intelligentAssignmentPending,
        intelligentAssignmentActive,
        intelligentAssignmentStationed,
        intelligentAssignmentGenerationId,
        intelligentTargetHash,
        reason);
}

void SimMinerController::onStaleWorkLoopTaskIgnored(
        const String& taskType, uint64 capturedGeneration,
        uint64 currentGeneration) {
    (void)taskType;
    (void)capturedGeneration;
    (void)currentGeneration;
}

void SimMinerController::logStateTransition(const String& message) const {
#ifdef DEBUG_SIMPVP
    Logger::console.info(message, true);
#else
    if (config.logStateTransitions)
        Logger::console.info(message, true);
#endif
}
