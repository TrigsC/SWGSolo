
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

public:
    SimPathFindTask(SimPlayerController* ctrl, WorldCoordinates start, WorldCoordinates end, Zone* z) 
        : controller(ctrl), startCoord(start), endCoord(end), zone(z) {
    }
    void run() override; 
};

// Generic Movement Loop Task
class ArrivalCheckTask : public Task {
    WeakReference<SimPlayerController*> controller;
public:
    ArrivalCheckTask(SimPlayerController* ctrl) : controller(ctrl) {}
    void run() override;
};

// Miner Specific: Action Task
class SimBehaviorTask : public Task {
    WeakReference<SimPlayerController*> controller;
    int type; 
public:
    static const int FINISH_SURVEY = 1;
    static const int FINISH_SAMPLE = 2;
    static const int START_STATIONED_SAMPLE = 3;

    SimBehaviorTask(SimPlayerController* ctrl, int t) : controller(ctrl), type(t) {}
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
    Vector3 destination;
    
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

    // --- Common Movement Logic ---
    void moveTo(Vector3 targetPos);
    void checkArrival();
    ManagedReference<AiAgent*> getAgent() const { return agent; }
    
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

public:
    SimMinerController(AiAgent* aiAgent);
    SimMinerController(AiAgent* aiAgent, const SimMinerConfig& minerConfig);
    virtual ~SimMinerController();

    void startSimLoop() override;
    void onArrived() override;
    void onPathFailed() override;

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

    String pickRandomResource();

private:
    void logStateTransition(const String& message) const;
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
