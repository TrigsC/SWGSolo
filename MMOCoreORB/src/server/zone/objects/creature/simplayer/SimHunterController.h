/*
 * SimHunterController.h
 * P.8.1 solo PvE hunt controller.
 */

#ifndef SIMHUNTERCONTROLLER_H_
#define SIMHUNTERCONTROLLER_H_

#include "SimPlayerManager.h"

class SimHunterActiveTickTask;

class SimHunterController : public SimPlayerController {
public:
	enum HuntPhase {
		IDLE_HOME = 0,
		ANNOUNCE_JOB,
		BUFF_UP,
		TRAVEL_OUT,
		TRAVEL_TO_TERMINAL,
		ACCEPT_MISSION,
		TRAVEL_TO_LAIR,
		AWAITING_WORLD,
		HUNTING,
		RETREATING,
		HEALING,
		TRAVEL_HOME,
		DELIVER,
		MISSION_CLEANUP,
		CLONE_HOME,
		DONE
	};

private:
	friend class SimHunterActiveTickTask;
	uint64 identityId;
	PveHuntOrder order;
	PveHuntSpecies species;
	bool orderActive;
	bool orderAbandoned;
	bool deathReported;
	HuntPhase phase;
	uint64 phaseStartedAtMs;
	uint64 huntStartedAtMs;
	uint64 activeTickGeneration;
	uint64 targetOid;
	uint64 observerTargetOid;
	Reference<Observer*> targetObserver;
	bool destructionHandled;
	bool cantinaDwellComplete;
	bool cantinaArrived;
	bool medCenterReached;
	bool medDwellComplete;
	Vector3 medCenter;
	Vector3 cantina;
	// Finding 4: don't cancel an in-flight pursuit path every tick.
	Vector3 pursuitTargetPos;
	bool pursuing;
	uint64 dwellUntilMs;
	uint64 lastPatrolMoveMs;
	uint64 lastCombatProgressMs;
	int phantomCombatTicks;
	uint64 stalemateDefenderOid;
	int stalemateSelfHam;
	int stalemateDefenderHam;
	int retreatCycles;
	bool missionHuntOrder;
	bool missionTerminalFallback;
	bool missionTerminalResolved;
	bool missionCleanupRequested;
	Vector3 missionTerminalPosition;
	Vector3 missionLairPosition;
	uint64 missionLairOid;
	int terminalResolveWaitCycles;
	int missionAddsOverCapCycles;
	int missionAddsEngaged;

	void scheduleActiveTick(int delayMs);
	void runActiveTick();
	void setPhase(HuntPhase next);
	void beginBuffUp();
	void applyHunterBuffs(bool clearWounds);
	void beginMissionTerminalLeg();
	void beginMissionFallback();
	void beginMissionAccept();
	void spawnMissionLair();
	void beginMissionCleanup(bool abandoned, const String& reason);
	void continueAfterMissionCleanup();
	bool checkMissionSocialAggro(AiAgent* hunter);
	void updateMissionAdds(int adds);
	void beginTravelHome(bool abandoned);
	void beginHunting();
	void scanForTarget();
	void selectTarget(AiAgent* target);
	void engageTarget();
	void disengageTarget(bool dropObserverHandle);
	void dropTargetObserver();
	void registerTargetObserver(uint64 target);
	void handleTargetUnavailable();
	void beginRetreat();
	void finishRetreatMove();
	void resetCombatGuard();
	void clearStaleCombat(const String& reason);
	bool isBelowRetreatThreshold(AiAgent* hunter) const;
	bool isReadyToResume(AiAgent* hunter) const;
	bool targetIsLive(uint64 oid, CreatureObject*& target,
		AiAgent*& targetAgent) const;
	bool targetMatchesSpecies(CreatureObject* target) const;
	void moveToTarget();
	void moveToPatrolPoint(uint64 nowMs);
	void completeOrder(bool abandoned, const String& reason);

public:
	SimHunterController(AiAgent* aiAgent, uint64 identity);
	virtual ~SimHunterController();

	void startSimLoop() override;
	void onArrived() override;
	void onPathFailed() override;
	void onTick() override;
	bool shouldContinueArrivalChecks() const override;

	void startOrder(const PveHuntOrder& newOrder,
		const PveHuntSpecies& newSpecies);
	void onHuntDestruction(uint64 destroyedTargetOid,
		bool participantVerified);
	void teardown(const String& reason);

	String getPhaseName() const;
	uint64 getIdentityId() const { return identityId; }
	uint64 getTargetOid() const { return targetOid; }
	HuntPhase getPhase() const { return phase; }
};

#endif /* SIMHUNTERCONTROLLER_H_ */
