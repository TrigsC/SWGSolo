/*
 * SimPlayerManager.h
 * Manager for handling SimPlayer population and lifecycle.
 */

#ifndef SIMPLAYERMANAGER_H_
#define SIMPLAYERMANAGER_H_

#include "engine/util/Singleton.h"
#include "system/util/SynchronizedVectorMap.h"
#include "system/util/Vector.h"
#include "engine/util/u3d/Vector3.h"
#include "engine/lua/Lua.h"

#include "SimPlayerController.h"

using namespace server::zone;

class SimPlayerManager : public Singleton<SimPlayerManager>, public Object, public Logger {
private:
	// Map of Creature ObjectID -> Controller
	SynchronizedVectorMap<uint64, Reference<SimPlayerController*> > controllers;

    Lua* lua;

public:
	struct ShuttleportLocation {
		String planet;
		String name;
		Vector3 spawn;
		Vector3 hangout;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	struct SpawnGroup {
		String type;
		int totalCount = 0;
		String behavior;
		String faction;
		Vector<String> templates;
		SimMinerConfig minerConfig;

		// Satisfy Vector/TypeInfo template instantiation
		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
	};

	void cyclePvPBotWhenShuttleReady(uint64 oldOid,
                                const String& groupType,
                                const String& templateName,
                                bool imperial,
                                const String& fromPlanet,
                                const String& fromLocation,
                                int attempts = 0);

private:
	bool enabled = true;
	Vector<ShuttleportLocation> allShuttleports;
	Vector<SpawnGroup> spawnGroups;

	// Lua config loading / spawning
	void loadLuaConfig();
	void spawnConfiguredGroups();
	void startControllerForAgent(AiAgent* agent, Reference<SimPlayerController*> ctrl);

	bool pickRandomShuttleport(ShuttleportLocation& out) const;
	bool isNearestShuttleBoardable(CreatureObject* c);
	
	String pickRandomTemplate(const SpawnGroup& g) const;
	bool isImperialForSpawn(const SpawnGroup& g, const String& templateName) const;

	void spawnFromConfig(const SpawnGroup& g, const ShuttleportLocation& loc, const String& templateName);

public:
	SimPlayerManager();
	~SimPlayerManager();

	// Called by ZoneServer on startup
	void initialize();

	// The main logic to spawn a specific bot
	void spawnSimPlayer(const String& planet, float x, float y, const String& templateName);

	// Toggle logic
	void toggleBot(AiAgent* agent);

	// Cycle logic (called by SimPvPController)
	void cyclePvPBot(uint64 oldOid,
	                 const String& groupType,
	                 const String& templateName,
	                 bool imperial,
	                 const String& fromPlanet,
	                 const String& fromLocation);

	void spawnSimPlayerWithRoute(const String& planet,
                    			const Vector3& spawn,
                    			const Vector3& hangout,
                    			const String& templateName,
                    			const String& groupType,
                    			const String& locationName);
};

#endif /* SIMPLAYERMANAGER_H_ */
