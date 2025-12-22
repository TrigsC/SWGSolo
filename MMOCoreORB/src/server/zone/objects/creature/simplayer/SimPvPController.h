/*
 * SimPvPController.h
 * Updated: Added ForceDespawnTask
 */

#ifndef SIMPVPCONTROLLER_H_
#define SIMPVPCONTROLLER_H_

#include "SimPlayerController.h"

class SimPvPController : public SimPlayerController {
    Vector3 spawnLocation;
    Vector3 hangoutLocation;
    bool returningToShuttle;
    bool isImperial;

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

// NEW: Safety Net Task
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