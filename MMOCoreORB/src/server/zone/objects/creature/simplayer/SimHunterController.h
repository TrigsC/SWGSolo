/*
 * SimHunterController.h
 * P.8.1 solo PvE hunt controller.
 */

#ifndef SIMHUNTERCONTROLLER_H_
#define SIMHUNTERCONTROLLER_H_

#include <atomic>

#include "SimPlayerManager.h"

class SimHunterActiveTickTask;

class SimHunterController : public SimPlayerController {
public:
	enum HuntPhase {
		IDLE_HOME = 0,
	ANNOUNCE_JOB,
	BUFF_UP,
	RELOCATING,
	BUFF_TRIP,
	TRAVEL_OUT,
	TRAVEL_TO_TERMINAL,
	ACCEPT_MISSION,
	TRAVEL_TO_MISSION,
	TRAVEL_TO_LAIR,
	ENGAGING_LAIR,
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
	// C1: atomic so the read-modify-write in scheduleActiveTick and the reads in
	// SimHunterActiveTickTask/TICK_ENTRY are not a data race across task threads.
	std::atomic<uint64> activeTickGeneration;
	// C2: non-blocking single-flight guard so two runActiveTick bodies never run
	// concurrently for one controller (a loser reschedules instead of blocking).
	std::atomic<bool> tickRunning{false};
	uint64 targetOid;
	uint64 observerTargetOid;
	Reference<Observer*> targetObserver;
	// Kill-telemetry observer handles, keyed by creature OID. Written from BOTH
	// tick loops (onTick's interceptor path and runActiveTick's engage path,
	// which run on independent task-pool threads) and cleared on order
	// completion/abandon/death/teardown, so it needs its own lock. Never held
	// while taking a creature lock — see registerEngagedTelemetry.
	VectorMap<uint64, Reference<Observer*> > engagedTelemetry;
	mutable Mutex engagedTelemetryMutex;
	bool targetMissionWave;
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
	uint64 engagedMissionLairOid;
	uint64 currentMissionOfferId;
	bool missionOffersGenerated;
	// Bounded offer-generation retries at the terminal: on a partial/empty board
	// the hunter dwells and re-generates rather than abandoning the trip. Reset
	// to 0 at the start of each terminal visit (alongside missionOffersGenerated).
	int missionOfferAttempts;
	// C4: epoch captured from openTerminalVisit at the start of a terminal visit
	// (attempt 0) and reused across retries; passed into generatePveBotMissionOffers
	// so a commit that finishes after the order concluded is discarded.
	uint64 missionOfferVisitEpoch;
	Vector<uint64> missionOfferIds;
	int terminalResolveWaitCycles;
	int missionAddsOverCapCycles;
	int missionAddsEngaged;

	enum PveBuffApproachStage {
		PVE_BUFF_APPROACH_NONE,
		PVE_BUFF_APPROACH_MUSICIAN,
		PVE_BUFF_APPROACH_DANCER,
		PVE_BUFF_APPROACH_DOCTOR,
		PVE_BUFF_APPROACH_COMPLETE
	};
	PveBuffProviders pveBuffProviders;
	bool pveNeedDoctorBuff;
	bool pveNeedEntertainerBuff;
	bool pveDoctorFallbackNeeded;
	bool pveEntertainerFallbackNeeded;
	bool pveBuffProviderApproachActive;
	PveBuffApproachStage pveBuffApproachStage;
	bool pveBuffInteractionDwellActive;
	uint32 pveDoctorRequestGen;
	bool pveDoctorRequestActive;
	uint64 pveDoctorDeadlineSec;
	ManagedReference<SceneObject*> pveDoctorProviderObject;
	bool pveBuffTripAtHub;
	bool pveBuffTripReturning;
	int pveBuffTripsThisHunt;

	enum PveBuffFamily {
		PVE_BUFF_FAMILY_DOCTOR,
		PVE_BUFF_FAMILY_ENTERTAINER
	};

	void scheduleActiveTick(int delayMs);
	void runActiveTick();
	void setPhase(HuntPhase next);
	void beginBuffUp();
	void computeBuffNeeds(bool& needDoctor, bool& needEntertainer) const;
	bool moveToNextPveBuffProvider();
	void beginLegacySyntheticBuffDetour();
	bool schedulePveDoctorScreenplay(SceneObject* provider,
		const String& method, const String& args);
	void cancelPveDoctorRequest();
	void finishPveBuffProviderFlow();
	bool beginBuffTripLeg();
	void resumeAfterBuffTrip();
	void interactWithPveBuffProvider(PveBuffApproachStage stage);
	void applyHunterBuffs(bool clearWounds);
	void applyHunterBuffsForFamily(PveBuffFamily family);
	void beginMissionTerminalLeg();
	void beginMissionFallback();
	void beginMissionAccept();
	void spawnMissionLair();
	bool beginNextMissionOffer();
	bool beginMarketRelocationLeg();
	void engageMissionLair();
	void disengageMissionLair();
	void beginMissionCleanup(bool abandoned, const String& reason);
	void continueAfterMissionCleanup();
	bool checkMissionSocialAggro(AiAgent* hunter);
	void updateMissionAdds(int adds);
	void beginTravelHome(bool abandoned);
	// P.8.7 travel diagnostics: periodic stall detector for the cross-planet
	// legs. Rate-limited by travelDiag.heartbeatSeconds; no-op when the gate is
	// off (checked before any lock is taken).
	void logTravelHeartbeat(const String& phaseLabel, uint64 nowMs);
	uint64 lastTravelHeartbeatMs = 0;
	float lastTravelHeartbeatDistance = -1.f;
	void beginHunting();
	bool scanForTarget();
	void selectTarget(AiAgent* target);
	void engageTarget();
	void disengageTarget(bool dropObserverHandle);
	bool selectActiveCombatAttacker(AiAgent* hunter,
		ManagedReference<CreatureObject*>& outAttacker,
		ManagedReference<AiAgent*>& outAttackerAgent);
	ManagedReference<CreatureObject*> engageActiveAttacker(AiAgent* hunter);
	void dropTargetObserver();
	void registerTargetObserver(uint64 target);
	void clearEngagedTelemetry();
	void registerEngagedTelemetry(uint64 creatureOid);
	void handleTargetUnavailable();
	void beginRetreat();
	void finishRetreatMove();
	void resetCombatGuard();
	void shedAllDefendersBilaterally(AiAgent* hunter);
	void resetInterceptorCombat();
	void clearStaleCombat(const String& reason);
	bool defendAgainstInterceptor(AiAgent* hunter, CreatureObject* attacker);
	bool isBelowRetreatThreshold(AiAgent* hunter) const;
	bool isReadyToResume(AiAgent* hunter) const;
	bool targetIsLive(uint64 oid, CreatureObject*& target,
		AiAgent*& targetAgent) const;
	bool targetMatchesSpecies(CreatureObject* target) const;
	bool isMissionWaveMob(CreatureObject* target) const;
	bool hasMissionWaveMobInRange() const;
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
	bool usesNavmeshHybridMovement() const override { return true; }
	bool isCombatDriverActive() const override {
		return targetOid != 0 && (pursuing || phase == HUNTING ||
			phase == ENGAGING_LAIR);
	}
	// Only resume a hybrid travel leg while the order is genuinely active and
	// not tearing down its lair. Once the order completes/abandons/times out
	// (orderActive=false) or enters cleanup, the preserved finalDestination must
	// not revive movement toward the finished target (code-review Major).
	bool shouldResumeHybridTravel() const override {
		return hasFinalDestination && orderActive && !missionCleanupRequested &&
			phase != RELOCATING && phase != BUFF_TRIP;
	}

	void startOrder(const PveHuntOrder& newOrder,
		const PveHuntSpecies& newSpecies);
	void onHuntDestruction(uint64 destroyedTargetOid,
		bool participantVerified);
	void onPveBotMissionLairDestroyed(uint64 lairOid);
	void teardown(const String& reason);

	String getPhaseName() const;
	uint64 getIdentityId() const { return identityId; }
	uint64 getTargetOid() const { return targetOid; }
	HuntPhase getPhase() const { return phase; }
	bool isReadyForMarketRelocation() const;
	bool canBeginInterplanetaryTravel() const override;
	void onInterplanetaryTravelFinished(bool success, const String& destZone,
		const String& reason) override;
};

#endif /* SIMHUNTERCONTROLLER_H_ */
