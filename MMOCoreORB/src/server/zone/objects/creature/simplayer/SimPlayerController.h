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
#include "engine/core/Object.h"
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

class SimPlayerController : public Object, public Logger {
    ManagedReference<AiAgent*> agent;
    
    // --- NEW VARIABLES ADDED HERE ---
    String targetResource;
    int retryCount;
    // --------------------------------

    enum SimState {
        IDLE,
        SEARCHING_RESOURCE,
        CALCULATING_PATH,
        MOVING
    };
    SimState state;

public:
    SimPlayerController(AiAgent* aiAgent);
    virtual ~SimPlayerController();

    void goToResource(const String& resourceName);
    void onPathFound(Vector<WorldCoordinates>* path);
    void onPathFailed();

    Vector3 findNearestHighDensityResource(const String& resourceClass);
};

#endif