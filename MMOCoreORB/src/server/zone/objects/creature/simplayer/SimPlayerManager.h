/*
 * SimPlayerManager.h
 * Updated for Lua Configuration Phase 2
 */

#ifndef SIMPLAYERMANAGER_H_
#define SIMPLAYERMANAGER_H_

#include "engine/util/Singleton.h"
#include "system/util/SynchronizedVectorMap.h"
#include "SimPlayerController.h"
#include "engine/lua/Lua.h" // Added Lua support

namespace server {
 namespace zone {
  class Zone;
 }
}

using namespace server::zone;

class SimPlayerManager : public Singleton<SimPlayerManager>, public Object, public Logger {
    // Map: ObjectID -> Controller
    SynchronizedVectorMap<uint64, Reference<SimPlayerController*> > controllers;
    
    // Lua Helper
    Lua* lua; 

public:
    SimPlayerManager();
    ~SimPlayerManager();

    void initialize();

    // Helper to read Lua config and execute spawns
    void loadLuaConfig();

    // Helper to spawn a single bot (Now public so Lua/Methods can call it)
    void spawnSimPlayer(const String& planet, float x, float y, float z, float hx, float hy, float hz, const String& templateName, int type);

    void toggleBot(AiAgent* agent);
};

#endif /* SIMPLAYERMANAGER_H_ */