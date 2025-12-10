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
#include "system/util/Vector.h"
#include "server/zone/objects/scene/WorldCoordinates.h"
#include "server/zone/objects/creature/ai/AiAgent.h"

// Forward Declaration
class SimPlayerController;

// --------------------------------------------------------
// 1. The Background Task
// --------------------------------------------------------
// This task runs on a separate thread to avoid lagging the server.
class FindResourcePathTask : public Task {
    WeakReference<SimPlayerController*> controller;
    WorldCoordinates startCoord;
    WorldCoordinates endCoord;
    ManagedReference<Zone*> zone;

public:
    FindResourcePathTask(SimPlayerController* ctrl, WorldCoordinates start, WorldCoordinates end, Zone* z) 
        : controller(ctrl), startCoord(start), endCoord(end), zone(z) {
    }

    // The code that runs on the worker thread
    void run() override; 
};

// --------------------------------------------------------
// 2. The Controller "Brain"
// --------------------------------------------------------
class SimPlayerController : public Object {
    // The physical bot we are controlling
    ManagedReference<AiAgent*> agent;
    
    // State Machine
    enum SimState {
        IDLE,
        SEARCHING_RESOURCE, // Querying PlanetManager
        CALCULATING_PATH,   // Waiting for Recast
        MOVING              // Actually walking
    };
    SimState state;

public:
    SimPlayerController(AiAgent* aiAgent);
    virtual ~SimPlayerController();

    // The main command to start the chain
    void goToResource(const String& resourceName);

    // Callbacks used by the Async Task
    void onPathFound(Vector<WorldCoordinates>* path);
    void onPathFailed();

    // Helper to check if we arrived
    bool isIdle() { return state == IDLE; }
};

#endif /* SIMPLAYERCONTROLLER_H_ */