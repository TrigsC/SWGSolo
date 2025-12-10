#ifndef SIMPLAYERMANAGER_H_
#define SIMPLAYERMANAGER_H_

#include "engine/util/Singleton.h"
#include "system/util/SynchronizedVectorMap.h"
#include "SimPlayerController.h"
#include "engine/core/Task.h"
#include "engine/core/ManagedReference.h"
#include "engine/core/Object.h"

class SimPlayerManager : public Singleton<SimPlayerManager>, public Object, public Logger {
    // Map of Creature ObjectID -> Your Custom Controller
    SynchronizedVectorMap<uint64, Reference<SimPlayerController*> > controllers;

public:
    SimPlayerManager() {
        setLoggingName("SimPlayerManager");
    }

    // Toggle the AI on/off for a specific agent
    void toggleBot(AiAgent* agent) {
        if (agent == nullptr) return;

        uint64 oid = agent->getObjectID();

        if (controllers.contains(oid)) {
            info("Stopping SimPlayer for agent " + String::valueOf(oid), true);
            controllers.drop(oid);
            // Optional: Reset bot state here
        } else {
            info("Starting SimPlayer for agent " + String::valueOf(oid), true);
            Reference<SimPlayerController*> ctrl = new SimPlayerController(agent);
            controllers.put(oid, ctrl);
            
            // Trigger the logic immediately
            ctrl->goToResource("Iron");
        }
    }
};

#endif /* SIMPLAYERMANAGER_H_ */