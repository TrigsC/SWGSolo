/*
 * SimPvPController.h
 * PvP Patrol Logic + Cycle-to-next-shuttleport support
 */

#ifndef SIMPVPCONTROLLER_H_
#define SIMPVPCONTROLLER_H_

#include "SimPlayerController.h"

#include "engine/core/Task.h"
#include "system/lang/String.h"
#include "engine/util/u3d/Vector3.h"

class SimPlayerManager;
class SimPvPBehaviorTask;

class SimPvPController : public SimPlayerController {
public:
	// Backwards-compatible constructor (uses default route)
	SimPvPController(AiAgent* aiAgent, bool imperial);

	// Preferred constructor when spawning from Lua config
	SimPvPController(AiAgent* aiAgent, bool imperial, const Vector3& spawnLoc, const Vector3& hangoutLoc);

	virtual ~SimPvPController();

	// Optional: update route after construction
	void setRoute(const Vector3& spawnLoc, const Vector3& hangoutLoc);

	// Must be called by SimPlayerManager (or toggleBot) so cycling knows what to respawn
	void setCycleContext(SimPlayerManager* mgr,
	                     const String& tmpl,
	                     const String& grpType,
	                     const String& planetName,
	                     const String& locName);

	// Called when the bot is done with a stop and wants to go elsewhere
	void requestCycleToNextStop();

	// SimPlayerController interface
	void startSimLoop() override;
	void onArrived() override;
	void onTick() override;

	// PvP specific
	void scanForTargets();

private:
	friend class SimPvPBehaviorTask;

	void startPatrol();
	void returnToShuttle();

	// Loiter behavior
	void startLoitering();
	void finishLoitering();

private:
	Vector3 spawnLocation;
	Vector3 hangoutLocation;

	bool returningToShuttle = false;
	bool isImperial = false;

	// Prevent shuttle-arrival spam (only request cycle once)
	bool cycleRequested = false;

	// Cycle support
	SimPlayerManager* manager = nullptr;
	String templateName;
	String groupType;
	String planet;
	String locationName;

	int loiterMs = 30000;
};

class SimPvPBehaviorTask : public Task {
	WeakReference<SimPvPController*> controller;

public:
	SimPvPBehaviorTask(SimPvPController* ctrl);
	void run() override;
};

#endif /* SIMPVPCONTROLLER_H_ */