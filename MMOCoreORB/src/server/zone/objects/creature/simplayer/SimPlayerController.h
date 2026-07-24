
/*
 * SimPlayerController.h
 * Modular Controller for SimPlayers
 */

#ifndef SIMPLAYERCONTROLLER_H_
#define SIMPLAYERCONTROLLER_H_

#include "engine/core/Task.h"
#include "engine/core/ManagedReference.h"
#include "system/lang/Object.h"
#include "engine/log/Logger.h"
#include "system/util/Vector.h"
#include "server/zone/objects/scene/WorldCoordinates.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/Zone.h"
#include "server/zone/objects/pathfinding/NavArea.h"

class SimPlayerController;

struct SimMinerConfig {
    Vector<String> resources;
    int surveyDurationMs;
    int sampleDurationMs;
    int minSearchRadius;
    int maxSearchRadius;
    int fallbackRadius;
    bool logStateTransitions;
    bool yieldEnabled;
    int minYieldAmount;
    int maxYieldAmount;
    bool logYield;
    bool summaryEnabled;
    int summaryIntervalSeconds;

    SimMinerConfig() {
        resources.add("iron");
        resources.add("gas");
        resources.add("water");
        resources.add("copper");

        surveyDurationMs = 4000;
        sampleDurationMs = 15000;
        minSearchRadius = 100;
        maxSearchRadius = 200;
        fallbackRadius = 100;
        logStateTransitions = false;
        yieldEnabled = true;
        minYieldAmount = 5;
        maxYieldAmount = 25;
        logYield = false;
        summaryEnabled = false;
        summaryIntervalSeconds = 300;
    }
};

// Generic Pathfinding Task
class SimPathFindTask : public Task {
    WeakReference<SimPlayerController*> controller;
    WorldCoordinates startCoord;
    WorldCoordinates endCoord;
    ManagedReference<Zone*> zone;
    uint64 generation;
    bool useRecastPath;
    bool useDirectOverlandPath;
    bool directTargetUsesTerrainHeight;
    ManagedReference<NavArea*> navArea;
    Vector3 recastStart;
    Vector3 recastEnd;
    bool allowPartial;

public:
    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start, WorldCoordinates end, Zone* z, uint64 g)
        : controller(ctrl), startCoord(start), endCoord(end), zone(z), generation(g),
          useRecastPath(false), useDirectOverlandPath(false),
          directTargetUsesTerrainHeight(false), navArea(nullptr), recastStart(),
          recastEnd(),
          allowPartial(true) {
    }

    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start,
            WorldCoordinates end, Zone* z, NavArea* area,
            const Vector3& recastStartPosition,
            const Vector3& recastEndPosition, bool partial, uint64 g)
        : controller(ctrl), startCoord(start), endCoord(end), zone(z), generation(g),
          useRecastPath(true), useDirectOverlandPath(false),
          directTargetUsesTerrainHeight(false), navArea(area),
          recastStart(recastStartPosition), recastEnd(recastEndPosition),
          allowPartial(partial) {
    }

    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start,
            WorldCoordinates end, Zone* z, bool directOverland,
            bool terrainHeight, uint64 g)
        : controller(ctrl), startCoord(start), endCoord(end), zone(z), generation(g),
          useRecastPath(false), useDirectOverlandPath(directOverland),
          directTargetUsesTerrainHeight(terrainHeight), navArea(nullptr),
          recastStart(), recastEnd(), allowPartial(true) {
    }
    void run() override; 
};

// Generic Movement Loop Task
class ArrivalCheckTask : public Task {
    WeakReference<SimPlayerController*> controller;
    uint64 generation;
public:
    ArrivalCheckTask(SimPlayerController* ctrl, uint64 g) : controller(ctrl), generation(g) {}
    void run() override;
};

// Miner Specific: Action Task
class SimBehaviorTask : public Task {
    WeakReference<SimPlayerController*> controller;
    int type; 
    uint64 generation;
public:
    static const int FINISH_SURVEY = 1;
    static const int FINISH_SAMPLE = 2;
    static const int START_STATIONED_SAMPLE = 3;

    SimBehaviorTask(SimPlayerController* ctrl, int t, uint64 g) : controller(ctrl), type(t), generation(g) {}
    void run() override;
};

// -------------------------------------------------------
// BASE CONTROLLER (Handles Movement & Physics)
// -------------------------------------------------------
class SimPlayerController : public Object, public Logger {
protected:
    ManagedReference<AiAgent*> agent;
    Vector<WorldCoordinates> simPath;
    int simPathIndex;
    Vector3 lastWatchdogPos;
    int stuckWatchdogCount;
    int rePathAttempts;
    Vector3 destination;
    Vector3 destinationLocal;
    ManagedReference<CellObject*> destinationCell;
    bool cellEgressActive;
    Vector3 cellEgressResumeWorld;
    Vector3 cellEgressResumeLocal;
    ManagedReference<CellObject*> cellEgressResumeCell;
    int cellEgressAttempts;
    // When set, a move to an outdoor target from inside a cell does NOT trigger the
    // generic egress; instead it does a directed findPath (routes through the portal
    // graph to an enclosed/interior-reachable point, e.g. a starport hollow collector).
    bool cellEgressSuppressed;
    // Hybrid movement keeps the logical assignment target separate from the
    // current navmesh boundary/egress sub-leg endpoint.
    Vector3 finalDestination;
    bool hasFinalDestination;
    bool onMeshMode;
    int navmeshModeDebounceCounter;
    int navmeshRepathAttempts;

    enum HybridLeg {
        HYBRID_LEG_NONE,
        HYBRID_LEG_NAVMESH_FINAL,
        HYBRID_LEG_NAVMESH_EXIT,
        HYBRID_LEG_EGRESS,
        HYBRID_LEG_OVERLAND_FINAL
    };

    HybridLeg hybridLeg;
    Vector3 hybridEgressPoint;
    uint64 workLoopGeneration;
    // A cell-aware provider approach is a complete base-path leg even for
    // hunters, whose normal wilderness legs use the hybrid mover.
    bool interiorApproachLeg;
    uint64 diagnosticLastParentCellOid;
    bool diagnosticParentCellInitialized;
    
    // Configurable speed/movement settings
    float runSpeed;

    enum SimState {
        IDLE,
        DECIDING,
        SURVEYING,
        CALCULATING_PATH,
        PERFORMING_ACTION,
        MOVING,
        SAMPLING,
        WAITING
    };
    SimState state;

public:
    SimPlayerController(AiAgent* aiAgent);
    virtual ~SimPlayerController();

    // --- Virtual Interface for Derived Bots ---
    virtual void startSimLoop() = 0;  // Start the bot's logic
    virtual void onArrived() = 0;     // Called when destination reached
    virtual void onTick() {}          // Called every 500ms (Good for PvP scanning)
    virtual bool shouldContinueArrivalChecks() const { return true; }
    // Hunter-only opt-in for navmesh/overland hybrid movement. The default
    // keeps the existing miner and PvP path behavior byte-for-byte unchanged.
    virtual bool usesNavmeshHybridMovement() const { return false; }
    // Hybrid-only: whether an IDLE agent should auto-resume travel toward the
    // preserved finalDestination on the next arrival tick. Base resumes whenever
    // a final destination is pending; SimHunterController additionally requires
    // an active, non-cleanup order so a completed/abandoned/timed-out/cleanup
    // route cannot revive movement toward a stale target.
    virtual bool shouldResumeHybridTravel() const { return hasFinalDestination; }
    // P.4.2: whether a stuck miner should re-path before giving up. Re-pathing a
    // straight-line overland leg just reproduces the same line, so overland
    // assignments override this to false and escalate straight to onPathFailed().
    virtual bool shouldRepathWhenStuck() const { return true; }
    virtual void onStaleWorkLoopTaskIgnored(const String& taskType, uint64 capturedGeneration, uint64 currentGeneration);

    // --- Common Movement Logic ---
    // World-space target used by the existing callers.
    void moveTo(Vector3 targetPos);
    // P.6.5b: worldPos remains the distance/arrival target while localPos is
    // the path request coordinate when targetCell is an interior cell.
    void moveTo(Vector3 worldPos, Vector3 localPos, CellObject* targetCell);
    void checkArrival();
    ManagedReference<AiAgent*> getAgent() const { return agent; }
    // P.6.1a: lets the manager detect a silently-lost path request (moveTo
    // issued but neither onPathFound nor onPathFailed ever ran) and re-drive.
    bool isAwaitingPathResult() const { return state == CALCULATING_PATH; }

    // P.6.1b: invalidate ALL in-flight work-loop tasks and forget the current
    // route before a teleport/interruption. Zeroing `destination` disarms the
    // checkArrival IDLE-resume (which would otherwise re-issue moveTo() to a
    // pre-teleport target from the chain thread and race the fresh moveTo()'s
    // generation). Call before switchZone repositions.
    virtual void prepareForRelocation(const String& reason) {
        clearCellEgressState();
        advanceWorkLoopGeneration(reason);
        state = WAITING;
        destination = Vector3(0, 0, 0);
        destinationLocal = Vector3(0, 0, 0);
        destinationCell = nullptr;
        simPath.removeAll();
        simPathIndex = 0;
        interiorApproachLeg = false;
        if (usesNavmeshHybridMovement())
            resetHybridMovementState(true);
    }

    // P.6.1b: last-line defense against a stale path winning a generation
    // race — onPathFound() rejects (-> onPathFailed retry) any path whose end
    // point this returns false for. Default accepts everything (miners rely
    // on partial/exhausted paths); SimPvP leaders enforce end≈target.
    virtual bool acceptFoundPath(const Vector3& pathEnd) { return true; }
    uint64 getWorkLoopGeneration() const { return workLoopGeneration; }
    uint64 advanceWorkLoopGeneration(const String& reason);
    bool isWorkLoopGenerationCurrent(uint64 capturedGeneration, const String& taskType);
    
    void onPathFound(Vector<WorldCoordinates>* path,
            bool pathUsesNavmesh = false, bool pathIsOverland = false);
    void onPathTaskFailed(bool pathUsesNavmesh);
    virtual void onPathFailed();

protected:
    void moveToInterior(Vector3 worldPos, Vector3 localPos,
            CellObject* targetCell);
    bool beginCellEgressIfNeeded(Vector3 worldPos, Vector3 localPos,
            CellObject* targetCell);
    void clearCellEgressState();
    void failCellEgress();
    void clearInteriorApproachLeg() { interiorApproachLeg = false; }
    bool isInteriorApproachLeg() const { return interiorApproachLeg; }
    bool isHybridMovementActive() const {
        return usesNavmeshHybridMovement() && !interiorApproachLeg &&
            !cellEgressActive;
    }
    void queueMorePathNodes();
    bool pickDestinationInNavMesh(Zone* zone, const Vector3& currentPos, Vector3& out, int minSearchRadius = 100, int maxSearchRadius = 200);
    void requestHybridPath();
    void scheduleHybridDirectPath(const Vector3& target, HybridLeg leg);
    void resetHybridMovementState(bool clearFinalDestination);
    void clearHybridMovementOnCancellation();
    bool findNavAreaAt(Zone* zone, const Vector3& position,
            ManagedReference<NavArea*>& area) const;
    bool resolveHybridExit(Zone* zone, const Vector3& currentPosition,
            Vector3& boundary, Vector3& egress,
            ManagedReference<NavArea*>& area) const;
    String getDiagnosticStateName() const;
};

// -------------------------------------------------------
// CELL-NAVIGATION DIAGNOSTIC CONTROLLER (One Shot)
// -------------------------------------------------------
class SimCellNavDiagController : public SimPlayerController {
    Vector3 diagnosticWorldPos;
    Vector3 diagnosticLocalPos;
    ManagedReference<CellObject*> diagnosticCell;
    bool diagnosticRouteReady;
    bool diagnosticRouteIssued;
    // Round-trip exit leg: after arriving INSIDE the cell, move back out to an
    // outdoor point to capture the cell->outdoor exit path.
    Vector3 diagnosticExitWorldPos;
    bool diagnosticExitReady;
    bool diagnosticExitIssued;
    // Leg 3 (arrival/landing): after reaching the hollow collector, route back OUT
    // through the starport interior to an outside-world point.
    Vector3 diagnosticReturnWorldPos;
    bool diagnosticReturnReady;
    bool diagnosticReturnIssued;   // leg 3a: re-enter a cell from the hollow
    bool diagnosticFinalIssued;    // leg 3b: egress out to the world

public:
    SimCellNavDiagController(AiAgent* aiAgent);
    virtual ~SimCellNavDiagController();

    void setDiagnosticRoute(const Vector3& worldPos, const Vector3& localPos,
            CellObject* cell);
    void setDiagnosticExit(const Vector3& exitWorldPos);
    void setDiagnosticReturn(const Vector3& returnWorldPos);
    void startSimLoop() override;
    void onArrived() override;
    void onPathFailed() override;
    bool shouldContinueArrivalChecks() const override { return false; }
};

// -------------------------------------------------------
// MINER CONTROLLER (Resource Gathering)
// -------------------------------------------------------
class TicketArrivalRetryTask;

class SimMinerController : public SimPlayerController {
    friend class TicketArrivalRetryTask;
    String targetResource;
    int retryCount;
    SimMinerConfig config;
    bool intelligentAssignmentPending;
    bool intelligentAssignmentActive;
    bool intelligentSampleActive;
    bool intelligentAssignmentStationed;
    bool intelligentLogActivationLifecycle;
    bool intelligentQueuedDuringSample;
    uint64 intelligentQueuedAtMs;
    uint64 intelligentAssignmentGenerationId;
    uint64 intelligentActivationSnapshotId;
    String intelligentQueuedState;
    String intelligentTargetHash;
    String intelligentActivationPathValidationStatus;
    String intelligentActivationPathTrustStatus;
    String intelligentProfileKey;
    String intelligentResourceName;
    String intelligentResourceType;
    String intelligentTargetZone;
    Vector3 intelligentTargetPosition;
    float intelligentTargetDensity;
    uint64 intelligentAssignmentExpiresAtMs;

    // P.4.5b: player-mimetic interplanetary travel. The miner runs to the origin
    // starport's ticket collector (travelDeparturePosition) and, on arrival (or
    // if it gets stuck), boards = switchZone to the destination starport's
    // OUTDOOR arrival (travelDestinationArrival on travelDestinationZone), then
    // re-acquires a target there. Orthogonal to the resource-assignment lifecycle
    // above; only ever set on an idle donor by the manager's dispatch task.
    bool intelligentTravelActive;
    String travelDestinationZone;
    Vector3 travelDeparturePosition;
    Vector3 travelDestinationArrival;
    String travelDestinationStarport;
    uint64 travelStartedAtMs;
    float travelBoardRadius;

    enum TicketTravelPhase {
        TICKET_TRAVEL_NONE,
        TICKET_DEPARTURE_RESOLVE,
        TICKET_DEPARTURE_ENTRY,
        TICKET_DEPARTURE_COLLECTOR,
        TICKET_ARRIVAL_REENTER,
        TICKET_ARRIVAL_EGRESS
    };
    TicketTravelPhase ticketTravelPhase;
    Vector3 ticketCollectorWorld;
    Vector3 ticketCollectorLocal;
    ManagedReference<CellObject*> ticketCollectorCell;
    uint64 ticketCollectorOid;
    bool ticketCollectorFound;
    bool ticketArrivalCollectorFound;
    Vector3 ticketArrivalOutdoor;
    int ticketApproachAttempts;

    // P.4.5c: bounded "final approach". Long off-navmesh walks can terminate short
    // of the true target; the miner re-paths to close the gap to within the
    // manager's arrival radius before stationing. Reset on each activation.
    int intelligentFinalApproachAttempts;
    float intelligentLastApproachDistance;

    // P.4.4b mounted travel: the miner deploys+mounts a real swoop (manager
    // plumbing) for long overland legs and rides it at vehicle speed; the swoop
    // is dismounted+stored at every leg exit (arrival/station, path failure,
    // recovery reset, shuttle boarding, sampling). preMountRunSpeed restores the
    // agent's own speed on dismount.
    bool mountedForTravel = false;
    float preMountRunSpeed = 0.f;

public:
    SimMinerController(AiAgent* aiAgent);
    SimMinerController(AiAgent* aiAgent, const SimMinerConfig& minerConfig);
    virtual ~SimMinerController();

    void startSimLoop() override;
    void onArrived() override;
    void onPathFailed() override;
    bool shouldContinueArrivalChecks() const override;
    bool shouldRepathWhenStuck() const override;

    // P.4.4b mounted travel (see member comment). Both are safe to call without
    // the agent locked; they take/release their own locks and are idempotent.
    void maybeMountForTravel(const Vector3& target);
    void dismountIfMounted(const String& reason);

    // Specific logic
    void performSurvey();
    void finishSurvey();
    void goToResource(const String& resourceName);
    void performSample();
    void finishSample();
    void startStationedSample();
    bool requestIntelligentTargetAssignment(
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
        String& activationResult);

    // P.4.3: manager-initiated recovery. Drops any local intelligent assignment
    // (including stationed) and re-enters the work loop so the planner can
    // reassign a fresh target the miner will actually travel to.
    void resetIntelligentAssignmentForRecovery();

    // P.4.5b: begin a player-mimetic shuttle trip to another planet. The miner
    // runs to departurePos (the origin starport's ticket collector) and, on
    // arrival (or if the stuck-watchdog gives up), boards -> teleports to
    // destArrivalPos on destZone, then re-acquires a target there. Returns false
    // (with a reason in travelResult) if the miner is not in a safe idle state.
    bool beginInterplanetaryTravel(
        const String& destZone,
        const Vector3& departurePos,
        const Vector3& destArrivalPos,
        const String& destStarportName,
        float boardRadius,
        String& travelResult);
    bool isInterplanetaryTravelActive() const { return intelligentTravelActive; }

    // P.4.5b: a miner is dispatchable only when fully idle (no pending/active/
    // sampling/stationed assignment and not already traveling), so a dispatch
    // never disrupts active gathering.
    bool isAvailableForDispatch() const {
        return !(intelligentAssignmentPending || intelligentAssignmentActive ||
                 intelligentSampleActive || intelligentAssignmentStationed ||
                 intelligentTravelActive);
    }

    String pickRandomResource();

private:
    void logStateTransition(const String& message) const;
    void logLegacyLoopSuppressed(const String& reason) const;
    void onStaleWorkLoopTaskIgnored(const String& taskType, uint64 capturedGeneration, uint64 currentGeneration) override;
    bool prepareConceptualYield(const String& completedResource, int& amount, bool& logYield) const;
    bool beginIntelligentTargetAssignment(String& activationResult);
    void performIntelligentSample();
    void finishIntelligentSample();
    void clearLocalIntelligentTargetAssignment();
    // P.4.5b: perform the "board the shuttle" step of interplanetary travel:
    // switchZone to the destination starport's outdoor arrival, clear travel
    // state, notify the manager, and re-enter the work loop on the new planet.
    void boardInterplanetaryShuttle(const String& reason);
    void beginTicketCollectorDepartureApproach(const String& reason);
    void beginTicketCollectorArrivalExit(const String& reason);
    bool isAtTicketCollector() const;
    bool canRetryTicketApproach() const;
    void retryTicketApproach(const String& reason);
    void cancelTicketCollectorTravel(const String& reason);
    void completeTicketCollectorTravel();
    String getSimStateName(SimState simState) const;
    void logIntelligentTargetActivation(const String& action, const String& reason = "") const;
    void logIntelligentTargetArrival(const String& arrivalResult) const;
};

#endif
