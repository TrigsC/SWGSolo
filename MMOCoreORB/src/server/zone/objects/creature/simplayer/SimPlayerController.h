/*
 * SimPlayerController.h
 * Author: Trigues
 * Description: High-level logic controller for SimPlayers.
 * Handles async pathfinding and resource logic.
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

class FindResourcePathTask : public Task {
    WeakReference<SimPlayerController*> controller;
    WorldCoordinates startCoord;
    WorldCoordinates endCoord;
    ManagedReference<Zone*> zone;

public:
    FindResourcePathTask(SimPlayerController* ctrl, WorldCoordinates start, WorldCoordinates end, Zone* z) 
        : controller(ctrl), startCoord(start), endCoord(end), zone(z) {
    }
    void run() override; 
};

class ArrivalCheckTask : public Task {
    WeakReference<SimPlayerController*> controller;
public:
    ArrivalCheckTask(SimPlayerController* ctrl) : controller(ctrl) {}
    void run() override;
};

class SimBehaviorTask : public Task {
    WeakReference<SimPlayerController*> controller;
    int type; 
public:
    static const int FINISH_SURVEY = 1;
    static const int FINISH_SAMPLE = 2;

    SimBehaviorTask(SimPlayerController* ctrl, int t) : controller(ctrl), type(t) {}
    void run() override;
};

class SimPlayerController : public Object, public Logger {
    ManagedReference<AiAgent*> agent;
    
    String targetResource;
    int retryCount;
    Vector3 destination;
    
    // --- WATCHDOG VARIABLES ---
    Vector3 lastWatchdogPos;
    int stuckWatchdogCount; 
    // --------------------------

    enum SimState {
        IDLE,
        DECIDING,
        SURVEYING,
        CALCULATING_PATH,
        MOVING,
        SAMPLING
    };
    SimState state;

public:
    SimPlayerController(AiAgent* aiAgent);
    virtual ~SimPlayerController();

    void startSimLoop();
    void performSurvey();
    void finishSurvey();
    void goToResource(const String& resourceName);
    
    void onPathFound(Vector<WorldCoordinates>* path);
    void onPathFailed();

    void checkArrival(); // This is now our Watchdog
    void performSample();
    void finishSample();

    String findActualResourceSpawn(const String& genericType);
    String pickRandomResource();
    Vector3 findNearestHighDensityResource(const String& resourceClass);
};

#endif