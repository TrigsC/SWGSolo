
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

public:
    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start, WorldCoordinates end, Zone* z, uint64 g)
        : controller(ctrl), startCoord(start), endCoord(end), zone(z), generation(g) {
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
    uint64 workLoopGeneration;
    
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
    // P.4.2: whether a stuck miner should re-path before giving up. Re-pathing a
    // straight-line overland leg just reproduces the same line, so overland
    // assignments override this to false and escalate straight to onPathFailed().
    virtual bool shouldRepathWhenStuck() const { return true; }
    virtual void onStaleWorkLoopTaskIgnored(const String& taskType, uint64 capturedGeneration, uint64 currentGeneration);

    // --- Common Movement Logic ---
    void moveTo(Vector3 targetPos);
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
    void prepareForRelocation(const String& reason) {
        advanceWorkLoopGeneration(reason);
        state = WAITING;
        destination = Vector3(0, 0, 0);
        simPath.removeAll();
        simPathIndex = 0;
    }

    // P.6.1b: last-line defense against a stale path winning a generation
    // race — onPathFound() rejects (-> onPathFailed retry) any path whose end
    // point this returns false for. Default accepts everything (miners rely
    // on partial/exhausted paths); SimPvP leaders enforce end≈target.
    virtual bool acceptFoundPath(const Vector3& pathEnd) { return true; }
    uint64 getWorkLoopGeneration() const { return workLoopGeneration; }
    uint64 advanceWorkLoopGeneration(const String& reason);
    bool isWorkLoopGenerationCurrent(uint64 capturedGeneration, const String& taskType);
    
    void onPathFound(Vector<WorldCoordinates>* path);
    virtual void onPathFailed();

protected:
    void queueMorePathNodes();
    bool pickDestinationInNavMesh(Zone* zone, const Vector3& currentPos, Vector3& out, int minSearchRadius = 100, int maxSearchRadius = 200);
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
    String getSimStateName(SimState simState) const;
    void logIntelligentTargetActivation(const String& action, const String& reason = "") const;
    void logIntelligentTargetArrival(const String& arrivalResult) const;
};

#endif
