/*
 * SimPlayerManager.h
 * Manager for handling SimPlayer population and lifecycle.
 */

#ifndef SIMPLAYERMANAGER_H_
#define SIMPLAYERMANAGER_H_

#include "engine/util/Singleton.h"
#include "system/util/SynchronizedVectorMap.h"
#include "SimPlayerController.h"

class Zone;

class SimPlayerManager : public Singleton<SimPlayerManager>, public Object, public Logger {
    // Map of Creature ObjectID -> Your Custom Controller
    SynchronizedVectorMap<uint64, Reference<SimPlayerController*> > controllers;

public:
    SimPlayerManager();
    ~SimPlayerManager();

    // Called by ZoneServer on startup
    void initialize();

    // The main logic to spawn a specific bot
    void spawnSimPlayer(const String& planet, float x, float z, const String& templateName);

    // Toggle logic (keeps your existing examime functionality working if you still want it)
    void toggleBot(AiAgent* agent);
};

#endif /* SIMPLAYERMANAGER_H_ */