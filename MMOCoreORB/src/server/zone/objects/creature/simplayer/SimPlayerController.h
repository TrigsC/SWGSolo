/*
 * SimPlayerController.h
 * LOGGING BUILD: Added isMiner helper to fix compilation
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

class SimPlayerController : public Reference<SimPlayerController>, public Logger {
protected:
    ManagedReference<AiAgent*> agent;
    
public:
    enum SimState {
        IDLE, DECIDING, SURVEYING, CALCULATING_PATH, PERFORMING_ACTION, MOVING, SAMPLING, WAITING
    };
    SimState state;

public:
    SimPlayerController(AiAgent* aiAgent);
    virtual ~SimPlayerController();

    virtual void startSimLoop() = 0;
    virtual void onArrived() = 0;
    virtual void onTick() {}
    
    // FIX: Added to resolve "no member named isMiner" error
    virtual bool isMiner() { return false; }

    void moveTo(Vector3 targetPos);
    void checkArrival();
    
    void onPathFound(Vector<WorldCoordinates>* path);
    void onPathFailed();

protected:
    void queueMorePathNodes();
    bool pickDestinationInNavMesh(Zone* zone, const Vector3& currentPos, Vector3& out);
};

class SimMinerController : public SimPlayerController {
    String targetResource;
    int retryCount;

public:
    SimMinerController(AiAgent* aiAgent);
    virtual ~SimMinerController();

    // Override
    bool isMiner() override { return true; }

    void startSimLoop() override;
    void onArrived() override;

    void performSurvey();
    void finishSurvey();
    void goToResource(const String& resourceName);
    void performSample();
    void finishSample();

    String pickRandomResource();
};

#endif