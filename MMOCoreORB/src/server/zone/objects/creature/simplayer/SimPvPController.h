/*
 * SimPvPController.h
 * FIXED: Added CollisionManager support and Throttling timers
 */

#ifndef SIMPVPCONTROLLER_H_
#define SIMPVPCONTROLLER_H_

#include "SimPlayerController.h"
#include "system/lang/Time.h" 

class SimPvPController : public SimPlayerController {
    Vector3 spawnLocation;
    Vector3 hangoutLocation;
    bool returningToShuttle;
    bool isImperial;
    bool initialized;
    
    // TIMERS
    Time spawnTime;       // Total life duration
    Time nextMoveCheckTime; // Throttle for stuck checks

    // Helper to randomize destinations slightly
    Vector3 getJitteredPosition(Vector3 pos);
    // Helper to get correct floor height (Terrain OR Building)
    float getWorldZ(float x, float y);

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