/*
 * SimPvPController.h
 * FIXED: Added missing getJitteredPosition declaration and spawnTime
 */

#ifndef SIMPVPCONTROLLER_H_
#define SIMPVPCONTROLLER_H_

#include "SimPlayerController.h"
#include "system/lang/Time.h" // Needed for Time

class SimPvPController : public SimPlayerController {
    Vector3 spawnLocation;
    Vector3 hangoutLocation;
    bool returningToShuttle;
    bool isImperial;
    Time spawnTime; // Tracks how long bot has been alive

    // Helper to randomize destinations slightly
    Vector3 getJitteredPosition(Vector3 pos);
    // Helper to get correct ground height
    float getTerrainHeight(float x, float y);

public:
    SimPvPController(AiAgent* aiAgent, bool imperial);
    virtual ~SimPvPController();

    void startSimLoop() override;
    void onArrived() override;
    void onTick() override;

    // Specific PvP Logic
    void scanForTargets();
    void startPatrol();
    void returnToShuttle();
    void despawn();
    
    // Loiter behavior
    void startLoitering();
    void finishLoitering();
};

class SimPvPBehaviorTask : public Task {
    WeakReference<SimPvPController*> controller;
public:
    SimPvPBehaviorTask(SimPvPController* ctrl) : controller(ctrl) {}
    void run() override {
        Reference<SimPvPController*> strongRef = controller.get();
        if (strongRef != nullptr) {
            Core::getTaskManager()->executeTask([strongRef]() {
                strongRef->finishLoitering();
            }, "SimPvPLoiterLambda");
        }
    }
};

// Safety Net Task
class SimPvPDespawnTask : public Task {
    WeakReference<SimPvPController*> controller;
public:
    SimPvPDespawnTask(SimPvPController* ctrl) : controller(ctrl) {}
    void run() override {
        Reference<SimPvPController*> strongRef = controller.get();
        if (strongRef != nullptr) {
            Core::getTaskManager()->executeTask([strongRef]() {
                strongRef->despawn();
            }, "SimPvPForceDespawn");
        }
    }
};

#endif