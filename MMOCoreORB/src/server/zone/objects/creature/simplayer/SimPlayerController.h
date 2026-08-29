
/*
 * SimPlayerController.h
 * Modular Controller for SimPlayers
 */

#ifndef SIMPLAYERCONTROLLER_H_
#define SIMPLAYERCONTROLLER_H_

#include <atomic>

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
class TicketArrivalRetryTask;

enum class StructureTraversalPhase {
    Idle,
    ApproachDoor,
    InteriorRoute,
    Egress,
    Reentry,
    CombatPaused,
    Resuming
};

struct StructureTraversalIntent {
    Vector3 finalTargetWorld;
    Vector3 finalTargetLocal;
    ManagedReference<CellObject*> finalTargetCell;
    ManagedReference<CellObject*> reentryCell;
    uint64 owningBuildingOid;
    // CELL-LOCAL coordinate inside reentryCell — never a world point.
    Vector3 entryReentryWaypoint;
    // WORLD coordinate of the last egress ejection point (diagnostics only).
    Vector3 egressWaypointWorld;
    int egressAttempts;
    int resumeAttempts;
    uint64 generation;
    uint64 createdAtMs;
    uint64 lastPhaseAtMs;
    bool exitIntent;
    bool active;

    StructureTraversalIntent()
        : finalTargetWorld(0, 0, 0), finalTargetLocal(0, 0, 0),
          finalTargetCell(nullptr), reentryCell(nullptr),
          owningBuildingOid(0),
          entryReentryWaypoint(0, 0, 0), egressWaypointWorld(0, 0, 0),
          egressAttempts(0),
          resumeAttempts(0), generation(0), createdAtMs(0),
          lastPhaseAtMs(0), exitIntent(false), active(false) {
    }

    void clear() {
        finalTargetWorld = Vector3(0, 0, 0);
        finalTargetLocal = Vector3(0, 0, 0);
        finalTargetCell = nullptr;
        reentryCell = nullptr;
        owningBuildingOid = 0;
        entryReentryWaypoint = Vector3(0, 0, 0);
        egressWaypointWorld = Vector3(0, 0, 0);
        egressAttempts = 0;
        resumeAttempts = 0;
        generation = 0;
        createdAtMs = 0;
        lastPhaseAtMs = 0;
        exitIntent = false;
        active = false;
    }
};

// Lua-authored, manager-owned scenario description.  These value types keep
// the harness DSL independent of live agent references; cells are resolved by
// SimPlayerManager immediately before a step is issued.
struct StructureTraversalTestPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    uint64 cellOid = 0;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct StructureTraversalTestInterrupt {
    String phase;
    int afterMs = 0;
    int durationMs = 0;
    bool hasDisplacement = false;
    StructureTraversalTestPoint displacement;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct StructureTraversalTestStep {
    String op;
    StructureTraversalTestPoint target;
    StructureTraversalTestPoint destination;
    int dwellMs = 0;
    uint64 buildingOid = 0;
    uint64 cellOid = 0;
    String cellName;
    StructureTraversalTestInterrupt interrupt;

    bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
    bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

// Phase 2 owns the monitor implementation. The generation is deliberately
// separate from SimPlayerController's work-loop generation so combat movement
// can invalidate path tasks without invalidating a traversal intent.
class StructureTraversalResumeMonitorTask : public Task {
    WeakReference<SimPlayerController*> controller;
    uint64 traversalGeneration;

public:
    StructureTraversalResumeMonitorTask(SimPlayerController* ctrl,
            uint64 generation)
        : controller(ctrl), traversalGeneration(generation) {
    }

    void run() override;
};

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

// Phase 1 exists to decide whether enforcement is safe, so a probe that could
// not finish must never be indistinguishable from one that found nothing. Any
// outcome other than Clear/WouldBlock means "no evidence", not "safe".
enum class ZeroClipOutcome {
    Clear,       // fully probed, nothing intersected
    WouldBlock,  // definite intersection with a collidable appearance mesh
    Skipped,     // not probed (no zone, degenerate or over-long segment)
    Truncated,   // candidate budget ran out with objects still unexamined
    Error        // threw while probing
};

struct ZeroClipClearanceResult {
    ZeroClipOutcome outcome;
    float hitAt;
    int hitSegment;
    String blockingTemplate;
    int candidates;
    int segments;
    // Mesh intersections the navmesh overruled as walkable (stairs, bridge
    // decks). Kept on the result so a run can show how much of the raw block
    // rate was false positive.
    int walkableReclassified;
    uint64 elapsedUs;

    ZeroClipClearanceResult()
        : outcome(ZeroClipOutcome::Skipped), hitAt(0.f), hitSegment(-1),
          blockingTemplate("none"), candidates(0), segments(0),
          walkableReclassified(0), elapsedUs(0) {
    }

    bool wouldBlock() const {
        return outcome == ZeroClipOutcome::WouldBlock;
    }

    // Evidence quality: only these two outcomes say anything about the world.
    bool isConclusive() const {
        return outcome == ZeroClipOutcome::Clear ||
            outcome == ZeroClipOutcome::WouldBlock;
    }

    static const char* outcomeName(ZeroClipOutcome value) {
        switch (value) {
        case ZeroClipOutcome::Clear:      return "clear";
        case ZeroClipOutcome::WouldBlock: return "would_block";
        case ZeroClipOutcome::Skipped:    return "skipped";
        case ZeroClipOutcome::Truncated:  return "truncated";
        case ZeroClipOutcome::Error:      return "error";
        }
        return "unknown";
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
    // Bound at CONSTRUCTION on the issuing thread, never read back off the
    // controller at probe time: two tasks can be in flight at once, and a newer
    // one's refresh would otherwise re-label this task's path.
    float probeRayHeight;
    // Identity only, never dereferenced: a Task outlives the call that
    // created it, so a raw AiAgent* here could dangle. An OID is a value.
    uint64 probeAgentOid;
    // D7 Phase 1 probes EVERY emitted path, not just the explicit overland
    // one, because PathFinderManager::findPathFromWorldToWorld independently
    // emits an unchecked 2-node fallback when it cannot evaluate a route
    // (PathFinderManager.cpp, "path could not be evaluated"). Miners are
    // non-hybrid and reach that fallback through the generic branch, so
    // instrumenting only useDirectOverlandPath would exclude the dominant
    // population from the evidence Phase 2 is decided on.
    ManagedReference<NavArea*> navArea;
    Vector3 recastStart;
    Vector3 recastEnd;
    bool allowPartial;

public:
    // Defined out-of-line: they read value snapshots off SimPlayerController,
    // which is not a complete type at this point in the header.
    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start,
            WorldCoordinates end, Zone* z, uint64 g);

    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start,
            WorldCoordinates end, Zone* z, NavArea* area,
            const Vector3& recastStartPosition,
            const Vector3& recastEndPosition, bool partial, uint64 g);

    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start,
            WorldCoordinates end, Zone* z, bool directOverland,
            bool terrainHeight, float rayHeight, uint64 rayAgentOid,
            uint64 g);

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
	enum class HollowEscalationOutcome {
		NotHandled,
		Started,
		ResumeFinalDestination,
		Failed,
		// An entry leg is already in flight. Distinct from NotHandled, whose
		// arrival tail clears the interior-approach latch and calls
		// onArrived() -- both of which would end the leg that is still
		// walking.
		InProgress
	};

    friend class TicketArrivalRetryTask;
    friend class StructureTraversalResumeMonitorTask;

    ManagedReference<AiAgent*> agent;
    // D7: value snapshots of agent identity/geometry for off-thread probing.
    uint64 probeAgentOid;
    float probeRayHeight;
    bool probePathAccepted;
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
	Vector<Vector3> cellEgressCandidates;
	Vector<Vector3> cellEgressCandidateLocals;
	Vector<int> cellEgressCandidateCellIndexes;
	Vector<int> cellEgressCandidateInHollow;
	int cellEgressCandidateIndex;
	int cellEgressCandidateAttempts;
	int cellEgressTotalAttempts;
	bool cellEgressExitSetBuilt;
	bool cellEgressBudgetExhaustedRecorded;
	int hollowEscalationAttempts;
	int hollowDoorEgressSelectedCandidateIndex;
	bool hollowEscalationActive;
	Vector3 hollowEscalationTarget;
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
    // D7 Phase 2. Conclusively-obstructed paths rejected since the last route
    // this bot actually walked. Bounded by zeroClip.rejectionCap so a
    // pathfinder that keeps returning the same clipping route cannot freeze it.
    int zeroClipRejections;

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

    StructureTraversalPhase structureTraversalPhase;
    StructureTraversalIntent structureTraversalIntent;
    uint64 traversalGeneration;
    Vector3 traversalLastAppliedWorldPosition;
    bool traversalWatchdogPositionInitialized;
    uint64 traversalPeaceSinceMs;
    std::atomic<uint64> traversalResumeMonitorGeneration;
    bool traversalResumeInProgress;
    bool combatDriverMoveActive;
    bool farSideRejectionPending;
    
    // Configurable speed/movement settings
    float runSpeed;

    // P.8.7 Phase 0: shared player-mimetic ticket-collector travel. The state
    // lives here so miners and hunters use the same proven choreography.
    bool interplanetaryTravelActive;
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
    // ArrivalCheckTask remains the single scheduler for every controller. A
    // derived controller may shorten the next interval without creating a
    // second task chain.
    virtual uint32 nextArrivalDelayMillis(uint32 defaultMs) {
        return defaultMs;
    }
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
    virtual bool canBeginInterplanetaryTravel() const { return true; }
    virtual void prepareForInterplanetaryTravelDeparture() {}
    virtual void prepareForTicketCollectorEntry(const String&) {}
    virtual void prepareForInterplanetaryTravelBoarding(const String&) {}
    // Per-controller boarding telemetry. The base journey machine is shared, but
    // the counters are not: the miner override reports into planetDispatch,
    // the hunter override into its own PvE travel counters. Base does nothing.
    virtual void onInterplanetaryTravelBoarded(const String& /*fromZone*/,
        const String& /*destZone*/, const String& /*starport*/,
        const String& /*reason*/) {}
    virtual void onInterplanetaryTravelFinished(bool, const String&,
        const String&) {}
    virtual void onStaleWorkLoopTaskIgnored(const String& taskType, uint64 capturedGeneration, uint64 currentGeneration);
    // Combat-capable controllers override this lease predicate for the
    // compound peace predicate used by the traversal resume monitor.
    virtual bool isCombatDriverActive() const { return false; }

    // --- Common Movement Logic ---
    // World-space target used by the existing callers.
    void moveTo(Vector3 targetPos);
    // P.6.5b: worldPos remains the distance/arrival target while localPos is
    // the path request coordinate when targetCell is an interior cell.
    void moveTo(Vector3 worldPos, Vector3 localPos, CellObject* targetCell);
    // Dedicated non-preempting movement entry point for combat-driver movement
    // while a formal traversal is paused.
    void moveToCombat(Vector3 targetPos);
    void moveToCombat(Vector3 worldPos, Vector3 localPos,
            CellObject* targetCell);
    // Formal structure traversal API. Gate-off callers are routed through the
    // existing movement implementation unchanged.
    void enterStructure(Vector3 worldPos, Vector3 localPos,
            CellObject* targetCell);
    void exitStructure(Vector3 outdoorDest);
    void checkArrival();
    ManagedReference<AiAgent*> getAgent() const { return agent; }
    StructureTraversalPhase getTraversalPhase() const {
        return structureTraversalPhase;
    }
    bool isTraversalActive() const {
        return structureTraversalIntent.active;
    }
    uint64 getTraversalGeneration() const { return traversalGeneration; }
    uint64 getTraversalOwningBuildingOid() const {
        return structureTraversalIntent.owningBuildingOid;
    }
    uint64 getTraversalTargetCellOid() const;
    String getTraversalPhaseName() const;
    // P.6.1a: lets the manager detect a silently-lost path request (moveTo
    // issued but neither onPathFound nor onPathFailed ever ran) and re-drive.
    bool isAwaitingPathResult() const { return state == CALCULATING_PATH; }

    // P.6.1b: invalidate ALL in-flight work-loop tasks and forget the current
    // route before a teleport/interruption. Zeroing `destination` disarms the
    // checkArrival IDLE-resume (which would otherwise re-issue moveTo() to a
    // pre-teleport target from the chain thread and race the fresh moveTo()'s
    // generation). Call before switchZone repositions.
    virtual void prepareForRelocation(const String& reason) {
        if (isTraversalActive() ||
                structureTraversalPhase != StructureTraversalPhase::Idle)
            clearStructureTraversalState(reason);
        clearCellEgressState();
        // A relocation is the "situation has actually changed" signal for the
        // D1 escalation budget, mirroring how beginCellEgressIfNeeded refreshes
        // cellEgressAttempts once the bot is outdoors. Without this the counter
        // survives every later traversal, so one failed escalation at building A
        // would permanently disable escalation at buildings B and C. Escalation
        // still cannot re-arm ITSELF -- only this external lifecycle event, and
        // a genuinely-outdoors completion, clear the budget.
        hollowEscalationAttempts = 0;
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
    virtual bool acceptFoundPath(const Vector3& pathEnd);
    uint64 getWorkLoopGeneration() const { return workLoopGeneration; }
    uint64 advanceWorkLoopGeneration(const String& reason);
    bool isWorkLoopGenerationCurrent(uint64 capturedGeneration, const String& taskType);
    
    // Virtual for the same reason onPathFailed is: the traversal harness needs
    // to substitute a path result. No production controller overrides it.
    virtual void onPathFound(Vector<WorldCoordinates>* path,
            bool pathUsesNavmesh = false, bool pathIsOverland = false);
    void onPathTaskFailed(bool pathUsesNavmesh);
    virtual void onPathFailed();

    // Segment-by-segment probe of a path that is about to be handed to the
    // mover, aggregated worst-first (WouldBlock > Error > Truncated > Skipped).
    ZeroClipClearanceResult probeEmittedPathClearance(Zone* zone,
            Vector<WorldCoordinates>* path, float rayHeight,
            uint64 ignoredAgentOid);

    // Plain-value snapshots so a task worker never touches creature state:
    // both are written on the issuing thread and read as values off-thread.
    uint64 getProbeAgentOid() const {
        return probeAgentOid;
    }
    float getProbeRayHeight() const {
        return probeRayHeight;
    }
    void refreshProbeRayHeight();

    // onPathFound can still reject an already-generation-accepted path (hybrid
    // cancellation, combat, too-short path, stale endpoint). Only a path that
    // reaches state = MOVING is one the bot will actually walk, so Phase 1
    // evidence is committed against this, not against generation acceptance.
    bool consumeProbePathAccepted() {
        bool accepted = probePathAccepted;
        probePathAccepted = false;
        return accepted;
    }

    // D7 Phase 2 enforcement. Decides whether a probed path is refused, and
    // performs the refusal. Both run on the path-delivery task thread, before
    // the path reaches the mover, and take no lock of their own — matching the
    // existing acceptFoundPath rejection they sit beside.
    bool isSegmentWalkableByNavmesh(Zone* zone, const Vector3& rayStart,
            const Vector3& rayEnd, float segmentLength);
    bool shouldRejectClippingPath(const ZeroClipClearanceResult& result,
            bool& capExhausted);
    void rejectClippingPath(Vector<WorldCoordinates>* path);

    // Shared interplanetary travel entry point. Derived controllers supply only
    // readiness and terminal policy; the journey state machine is protected.
    bool beginInterplanetaryTravel(const String& destZone,
        const Vector3& departurePos, const Vector3& destArrivalPos,
        const String& destStarportName, float boardRadius,
        String& travelResult);
    bool isInterplanetaryTravelActive() const {
        return interplanetaryTravelActive;
    }

protected:
    enum class TraversalMoveOrigin {
        External,
        Internal,
        CombatDriver
    };

    void moveToInterior(Vector3 worldPos, Vector3 localPos,
            CellObject* targetCell);
    void moveToWithOrigin(Vector3 worldPos, Vector3 localPos,
            CellObject* targetCell, TraversalMoveOrigin origin);
    bool beginCellEgressIfNeeded(Vector3 worldPos, Vector3 localPos,
            CellObject* targetCell, bool preserveTraversal = false,
            bool combatDriver = false);
    bool buildCellEgressExitSet(bool hollowDoorEgressTelemetry = false);
    bool startNextCellEgressCandidate();
    bool tryStartFarSideInteriorLeg();
    bool isStructureTraversalFeatureEnabled() const;
    uint64 advanceTraversalGeneration(const String& reason);
    void clearStructureTraversalState(const String& reason);
    void setStructureTraversalPhase(StructureTraversalPhase phase,
            const String& reason);
	HollowEscalationOutcome completeStructureTraversalIfArrived(
			const Vector3& arrivalWorld);
	HollowEscalationOutcome beginHollowEscalation(
			const Vector3& arrivalWorld);
	// Enumerate EVERY world portal in the owning building and report where each
	// doorway sits relative to the hollow. The pad doors are inside it
	// (hollowMissDistance=0), which is why walking to one does not leave. If any
	// world portal lies OUTSIDE, that is the real exit and the route is
	// pad -> door -> interior -> that cell -> out. If none does, the building has
	// no exit to the open world and the answer is elsewhere entirely.
	void observeBuildingExits(Zone* zone, BuildingObject* building,
			const Vector3& botWorld);

	void observeHollowRadialScan(Zone* zone, BuildingObject* building,
			const Vector3& originWorld);
	bool resolveHollowEscalationTarget(Zone* zone, BuildingObject* building,
			const Vector3& agentWorld, const Vector3& finalDestination,
			Vector3& target, String& source, int& candidates,
			int& nodesExamined, int& rejectedHollow, int& rejectedBounds,
			int& rejectedWater);
	float hollowEscalationSegmentGeometryHit(
			const Vector3& arrivalWorld, const Vector3& destination,
			BuildingObject* building) const;
	void recordTraversalMovementStep(const Vector3& previousPosition,
            const Vector3& currentPosition);
    bool isTraversalGenerationCurrent(uint64 generation) const {
        return generation == traversalGeneration;
    }
    void pauseStructureTraversal(const String& reason);
    void scheduleStructureTraversalResumeMonitor();
    void checkStructureTraversalResume(uint64 generation);
    bool resumeStructureTraversalFromCurrentPosition();
    bool isWithinOwningBuildingHollow() const;
	void clearCellEgressState();
	void failCellEgress();
	bool isWithinOwningBuildingHollowAt(const Vector3& worldPosition,
			BuildingObject* building) const;
	float getOwningBuildingHollowMissDistance(const Vector3& worldPosition,
			BuildingObject* building) const;
    void clearInteriorApproachLeg() { interiorApproachLeg = false; }
    bool isInteriorApproachLeg() const { return interiorApproachLeg; }
    bool isHybridMovementActive() const {
        // Every ticket-collector phase is cell-aware: the collector stands in the
        // starport's enclosed hollow, and the hybrid mover DISCARDS cell targets.
        // A hybrid controller (hunters) therefore walks to the building and then
        // stalls short of the collector forever — observed live as a relocation
        // wedged 87m out for 870s. Miners were unaffected only because they never
        // enable hybrid movement. Same reasoning as interiorApproachLeg (P.8.6).
        return usesNavmeshHybridMovement() && !interiorApproachLeg &&
            !cellEgressActive && ticketTravelPhase == TICKET_TRAVEL_NONE;
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

    bool handleInterplanetaryTravelArrival();
    bool handleInterplanetaryTravelPathFailed();
    void beginTicketCollectorDepartureApproach(const String& reason);
    void beginTicketCollectorArrivalExit(const String& reason);
    bool isAtTicketCollector() const;
    bool canRetryTicketApproach() const;
    void retryTicketApproach(const String& reason);
    void cancelTicketCollectorTravel(const String& reason);
    void completeTicketCollectorTravel();
    void boardInterplanetaryShuttle(const String& reason);
    void clearInterplanetaryTravelState();
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
// STRUCTURE-TRAVERSAL SCENARIO CONTROLLER (Harness only)
// -------------------------------------------------------
// The manager owns the scenario cursor and lifecycle.  This controller only
// translates an already-resolved step into the formal API and reports terminal
// movement callbacks back to the manager.
class SimTraversalTestController : public SimPlayerController {
public:
    SimTraversalTestController(AiAgent* aiAgent);
    virtual ~SimTraversalTestController();

    void startSimLoop() override;
    void onArrived() override;
    void onPathFailed() override;
    void onPathFound(Vector<WorldCoordinates>* path,
            bool pathUsesNavmesh = false,
            bool pathIsOverland = false) override;
    void onTick() override;
    bool shouldContinueArrivalChecks() const override { return true; }

    // Gated, default-off. A starport pad is a curved walled corridor; a
    // straight overland leg cannot follow it. Hunters already return true here
    // and are the bots observed using starports correctly.
    bool usesNavmeshHybridMovement() const override;

    // Gated, default-off. The base returns true for any endpoint because miners
    // rely on partial paths; a structure egress that stops short has FAILED.
    bool acceptFoundPath(const Vector3& pathEnd) override;

    // Scenario 19 (bounded failure) needs findPath to fail for a target the bot
    // otherwise CAN resolve and route to. Map data cannot be relied on to
    // produce that (an off-mesh cell point falls back to the nearest triangle,
    // and an out-of-bounds world point is rejected synchronously before the
    // path task is even scheduled), so the harness injects the failure at the
    // exact seam a null findPath would deliver it.
    void setHarnessForcePathFailure(bool force) {
        harnessForcePathFailure = force;
    }

    void issueResolvedStep(const StructureTraversalTestStep& step,
            Vector3 targetWorld, Vector3 targetLocal, CellObject* targetCell);
    // Dedicated deterministic combat-drift path.  It intentionally preserves
    // the traversal generation and resume monitor while invalidating only
    // movement work, then re-baselines the traversal watchdog for the expected
    // harness reposition.
    void applyHarnessCombatDisplacement(const String& zoneName,
            const StructureTraversalTestPoint& point);
    void setHarnessEgressSuppressed(bool suppressed) {
        cellEgressSuppressed = suppressed;
    }
    bool isHarnessOutdoorsClear() const;

    // The exit assertion is a conjunction of two conditions; reporting only its
    // boolean cannot distinguish "genuinely outdoors" from "standing at a door
    // that happens to sit outside the hollow AABB". Report both raw facts.
    String describeHarnessOutdoorsState() const;

    // isWithinOwningBuildingHollow() returns FALSE when the traversal intent has
    // been cleared (owningBuildingOid == 0), which makes the hollow half of the
    // exit assertion vacuous exactly when a traversal reports success. These
    // take the building from the SCENARIO CONFIG so the check cannot be
    // satisfied by the runtime simply forgetting which building it was in.
    bool isHarnessOutdoorsClearFor(uint64 buildingOid) const;
    String describeHarnessOutdoorsStateFor(uint64 buildingOid) const;

private:
    bool harnessForcePathFailure = false;
};

// -------------------------------------------------------
// MINER CONTROLLER (Resource Gathering)
// -------------------------------------------------------
class SimMinerController : public SimPlayerController {
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

    // P.4.5b: the inherited base entry point uses this readiness override so a
    // dispatch never disrupts active gathering.
    bool canBeginInterplanetaryTravel() const override {
        return isAvailableForDispatch();
    }
    void prepareForInterplanetaryTravelDeparture() override;
    void prepareForTicketCollectorEntry(const String& reason) override;
    void prepareForInterplanetaryTravelBoarding(const String&) override;
    void onInterplanetaryTravelBoarded(const String& fromZone,
        const String& destZone, const String& starport,
        const String& reason) override;
    void onInterplanetaryTravelFinished(bool success, const String&,
        const String& reason) override;

    // P.4.5b: a miner is dispatchable only when fully idle (no pending/active/
    // sampling/stationed assignment and not already traveling), so a dispatch
    // never disrupts active gathering.
    bool isAvailableForDispatch() const {
        return !(intelligentAssignmentPending || intelligentAssignmentActive ||
                 intelligentSampleActive || intelligentAssignmentStationed ||
                 interplanetaryTravelActive);
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
    String getSimStateName(SimState simState) const;
    void logIntelligentTargetActivation(const String& action, const String& reason = "") const;
    void logIntelligentTargetArrival(const String& arrivalResult) const;
};

#endif
