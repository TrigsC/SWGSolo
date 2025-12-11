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

// Background task to calculate the path
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

// Background task to check if we arrived at the destination
class ArrivalCheckTask : public Task {
    WeakReference<SimPlayerController*> controller;
public:
    ArrivalCheckTask(SimPlayerController* ctrl) : controller(ctrl) {}
    void run() override;
};

// Background task to handle animation delays (Surveying/Sampling)
class SimBehaviorTask : public Task {
    WeakReference<SimPlayerController*> controller;
    int type; // 1 = Finish Survey, 2 = Finish Sample
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
    Vector3 destination; // Store where we are trying to go

    enum SimState {
        IDLE,
        DECIDING,
        SURVEYING,          // Standing still, looking at tool
        CALCULATING_PATH,   // Waiting for Recast
        MOVING,             // Running
        SAMPLING            // Kneeling
    };
    SimState state;

public:
    SimPlayerController(AiAgent* aiAgent);
    virtual ~SimPlayerController();

    // The Entry Point
    void startSimLoop();

    // Behavior Chain
    void performSurvey();
    void finishSurvey();
    void goToResource(const String& resourceName);
    
    // Pathfinding Callbacks
    void onPathFound(Vector<WorldCoordinates>* path);
    void onPathFailed();

    // Arrival Logic
    void checkArrival();
    void performSample();
    void finishSample();

    // Helpers
    String findActualResourceSpawn(const String& genericType);
    String pickRandomResource();
    Vector3 findNearestHighDensityResource(const String& resourceClass);
};

#endif