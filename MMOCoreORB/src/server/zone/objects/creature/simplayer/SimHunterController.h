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
	// Last time the lair was re-pointed while holding an engagement whose follow
	// object had drifted to an attacker. Bounds the re-point to the configured
	// cadence so holding does not become a 1Hz retarget storm. Cleared wherever
	// engagedMissionLairOid is, so a fresh engagement always re-points at once.
	uint64 lastLairRetargetMs = 0;
	// Wave-break state, scoped to the lair it describes. waveStateLairOid is
	// what makes that scoping sound: the other two are discarded whenever the
	// controller is looking at a different lair, so the state can never be
	// carried onto a lair that has not advanced a wave of its own.
	//
	// lastSeenSpawnNumber tracks the LAIR's wave counter -- not the engagement,
	// which is why it survives engage and disengage. pendingWaveDisengage holds
	// a seen advance until the creatures actually materialise, because
	// spawnNumber moves 1-9s before they arrive.
	uint64 waveStateLairOid = 0;
	int lastSeenSpawnNumber = 0;
	bool pendingWaveDisengage = false;
	// De-aggro events already spent on this lair. Capped by
	// kMaxWaveDeaggroPerLair, a constant because the lair cannot produce more
	// than two wave transitions (see shedWaveAggro).
	int waveDeaggroCount = 0;
	static const int kMaxWaveDeaggroPerLair = 2;
	// When the deferred shed becomes due, or 0 when none is pending. The delay
	// must exceed LairObserver::AGGRO_TASK_DELAY (4s) or the scrub races that
	// task and is undone by it; 5s is that plus a one-second margin.
	// When the wave transition was OBSERVED. The de-aggro deadline is measured
	// from here, not from first visibility, because LairAggroTask is scheduled
	// from the transition -- anchoring later leaves the re-aggroed wave on the
	// bot for the difference.
	uint64 waveTransitionSeenAtMs = 0;
	uint64 waveDeaggroDueAtMs = 0;
	static const uint64 kWaveDeaggroDelayMs = 5000;
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
	// Consecutive PVE_LAIR_REVEAL_DEFER_NO_SPAWN_POINT results for the current
	// offer -- the PERMANENT case only (point not spawn-permitted and all 32
	// relocation probes failed). Plain PVE_LAIR_REVEAL_DEFER is transient
	// (capacity, registration race), is never counted here, and resets this,
	// because capacity is only reached once a spawn point HAS been found.
	// Measured 2026-09-21: one bot spent a full hour on 689 consecutive
	// 2-second retries against a single point.
	int missionLairRevealDeferCycles;
	// The REAL cause recorded by beginMissionCleanup, kept so completeOrder can
	// report it instead of the generic "abandoned". Without this the dashboard's
	// pveAbandonReasons shows only "abandoned" and every real cause is lost --
	// the overwrite documented at beginMissionCleanup and in roadmap 5c.4.
	String missionAbandonReason;
	// Set by scanForTarget when candidates WERE present but every one was
	// rejected by a progression guard (over the level ceiling, or a pack the bot
	// must avoid). Without this, "nothing I may fight" is indistinguishable from
	// "field clear": HUNTING would enter ENGAGING_LAIR, and the unfiltered
	// hasMissionWaveMobInRange would bounce it straight back, oscillating to the
	// mission timeout.
	// Tracked separately, not as one "ineligible" flag: the abandon reason is
	// published on the dashboard, and an in-band PACK wave reported as
	// "above level band" would send the next reader after the wrong defect.
	bool scanRejectedOverLevel = false;
	bool scanRejectedPack = false;
	// Enforcement of the level ceiling is exempt inside an accepted contract,
	// but the SIGNAL is kept: these record what the ceiling would have refused
	// so a wildly over-level contract is still visible (LAIR_FIELD
	// exemptedOverLevel=/exemptedMaxLevel=) instead of silently becoming a
	// fight to the death. Dropping the enforcement must not also drop the
	// observability -- that was the reviewable half of the objection to
	// exempting contracts wholesale.
	int scanExemptedOverLevel = 0;
	int scanExemptedMaxLevel = 0;
	// The ceiling itself, published alongside the two counters above so the
	// margin is readable without knowing the bot's tier or the Lua band.
	int scanLevelCeiling = 0;
	// The retained cause wins over any travel-epilogue reason: once a mission has
	// been abandoned for a real cause, HOW the bot then got home is not why the
	// mission was dropped. Falls back when nothing was retained.
	String epilogueAbandonReason(const String& fallback) const {
		return missionAbandonReason.isEmpty() ? fallback : missionAbandonReason;
	}
	int missionAddsEngaged;

	// P.10g: the self-motivated novice loop runs on the existing active tick.
	// These route fields are transient; counters and published state live in
	// SimPlayerManager keyed by identity.
	enum EarlyGamePhase {
		EARLY_LEAVE_CITY,
		EARLY_SELECT_TARGET,
		EARLY_ENGAGE,
		EARLY_RECOVER,
		EARLY_TRAIN
	};
	bool earlyGameActive;
	EarlyGamePhase earlyGamePhase;
	Vector3 earlyGameAnchor;
	Vector3 earlyGameRestPoint;
	int earlyGameBearingIndex;
	int earlyGameAnchorAttempts;
	bool earlyGameRingWidened;
	int earlyGameScanAttempts;
	int earlyGameScanBearingVisits;
	uint64 earlyGameScanRetryAtMs;
	// Earliest time LeaveCity may re-issue its walk after a path failure.
	uint64 earlyGamePathRetryAtMs;
	float earlyGameTargetTtkSeconds;
	// True while the current target was adopted in self-defence rather than
	// chosen by the TTK scan.
	bool earlyGameTargetDefensive;
	// The loot sub-step is transient controller state. The claim itself is a
	// value copy; no corpse or bag reference is retained between ticks.
	// lootStepActive is read by the path callbacks on other threads; they only
	// raise lootStepPathFailed and hand back to the tick, which owns the rest.
	std::atomic<bool> lootStepActive{false};
	std::atomic<bool> lootStepPathFailed{false};
	PlayerBotLootClaim lootStepClaim;
	uint64 lootStepStartedAtMs = 0;
	// True when the loot walk interrupted a travel leg, which the step must
	// put back on course when it ends (not every leg branch re-issues its move).
	bool lootStepInterruptedLeg = false;
	// Stale corpse arrivals: onArrived's loot check and the step's finish are
	// serialized by lootArrivalMutex. The finish advances the work-loop
	// generation and records [lootWalkFirstGeneration, lootWalkEndGeneration);
	// an arrival dispatched inside that range belongs to the corpse walk and is
	// never handed to the interrupted phase's arrival logic.
	Mutex lootArrivalMutex;
	uint64 lootWalkFirstGeneration = 0;
	uint64 lootWalkEndGeneration = 0;
	uint64 earlyGameRestStartedAtMs;
	std::atomic<bool> earlyGamePathFailurePending{false};
	std::atomic<bool> earlyGameDestructionPending{false};
	std::atomic<uint64> earlyGameDestroyedTargetOid{0};
	std::atomic<bool> earlyGameDestroyedParticipantVerified{false};
	// This tick's self-defence decision, for the combat telemetry sample.
	String earlyGameDefenseDecision;
	float earlyGameDefenseTtk = 0.f;
	// Last point the current recovery walk made progress from, and when.
	Vector3 earlyGameRecoverProgressPos;
	uint64 earlyGameRecoverProgressMs = 0;
	// Targets whose hunt path just failed, each skipped by the scan until its
	// own expiry (parallel vectors, same index), so unreachable creatures are
	// not reselected in a tight loop. A single slot let two unreachable
	// kreetles alternate for 70 min (TRIP-verify 20260915-185828). Bounded by
	// earlyGame.unreachableSkipMaxTargets.
	Vector<uint64> earlyGameSkipTargetOids;
	Vector<uint64> earlyGameSkipTargetUntilMs;
	// Reach of the weapon an early-game body actually fights with, recorded
	// wherever that weapon is read under the agent lock (the scan and the
	// self-defence snapshot). 0 until first read.
	float earlyGameEngageRangeMeters = 0.f;
	// Rotation position among the home city's spawner points, and the arrival
	// distance of the current anchor (0 = the ring's anchorArriveMeters).
	int earlyGameSpawnerIndex = 0;
	float earlyGameAnchorArriveMeters = 0.f;

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
	bool pveDoctorFeeReserved;
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

	// F_0.7.5: cross-building provider staging. A provider in a DIFFERENT
	// building is approached via an OUTDOOR point beside that building first,
	// and only entered from there. MEASURED 2026-08-31 (harness scenarios
	// theed_doc_A/D): entering Theed hospital cell 1697364 direct from the
	// cantina exterior FAILS deterministically -- and still fails from a fresh
	// spawn at that point, and from 15m away -- while walking to the
	// med-center anchor first and entering from there PASSES. The bot can
	// MOVE overland from that point fine (theed_doc_D's moveTo leg passed);
	// it is cell-entry pathfinding from there that has no route.
	bool pveBuffStagingActive;
	PveBuffApproachStage pveBuffStagingStage;
	// The planet/city the current provider snapshot was resolved against, so
	// the staging anchors can be looked up without re-deriving the hub.
	String pveBuffProviderPlanet;
	String pveBuffProviderCity;

	enum PveBuffFamily {
		PVE_BUFF_FAMILY_DOCTOR,
		PVE_BUFF_FAMILY_ENTERTAINER
	};

	void scheduleActiveTick(int delayMs);
	void runActiveTick();
	bool runPlayerBotLootStep(AiAgent* hunter, uint64 nowMs);
	void finishPlayerBotLootStep(const char* outcome, bool resumeLeg);
	// NOT_LOOT: the phase owns the callback. CURRENT: it belongs to the active
	// corpse walk. STALE: it belongs to movement the loot step cancelled (the
	// walk itself after a finish, or the interrupted leg once the walk began).
	enum LootWalkCallback { LOOT_CALLBACK_NOT_LOOT, LOOT_CALLBACK_CURRENT,
		LOOT_CALLBACK_STALE };
	LootWalkCallback classifyLootWalkCallback();
	// The BUFF_UP branch of runActiveTick, extracted so a single guard at the
	// call site can re-arm the heartbeat on every exit. Pure extraction: the
	// body is unchanged and no buffing behaviour is altered.
	void runBuffUpTick(AiAgent* strongAgent, uint64 nowMs);
	// Sheds the previous wave's aggro at a wave transition. Owner-scoped:
	// mission lairs only, members of that lair only, solo only, gated.
	void shedWaveAggro(AiAgent* strongAgent, int spawnNumber);
	void setPhase(HuntPhase next);
	void beginBuffUp();
	void computeBuffNeeds(bool& needDoctor, bool& needEntertainer) const;
	bool moveToNextPveBuffProvider();
	// Returns the OUTDOOR anchor beside the building housing this stage's
	// provider: the med-center anchor for the doctor, the cantina/hangout
	// anchor for the entertainers. False when the city has no resolved anchor,
	// in which case the caller keeps the pre-F_0.7.5 direct entry.
	bool resolvePveProviderStagingPoint(PveBuffApproachStage stage,
		Vector3& stagingPoint) const;
	PveBuffProviders::Provider* providerForStage(PveBuffApproachStage stage);
	// True when the agent is already standing inside the building that owns
	// this cell, i.e. the move is same-building and needs no outdoor staging.
	bool isAgentInsideBuildingOfCell(CellObject* cell) const;
	bool enterPveBuffProviderCell(PveBuffApproachStage stage);
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
	// F_0.9.8b travel-combat diagnostic rate limit (missiondiag only).
	uint64 lastTravelDisengageLogMs = 0;
	int suppressedTravelDisengages = 0;
	uint64 lastTravelEngageLogMs = 0;
	// Diagnostic only: the last onTick pick came from the threat map.
	bool travelEngageViaThreat = false;
	int suppressedTravelEngages = 0;
	// F_0.9.8c travel combat (SimBotTravelCombat::Mode). Written only by
	// runActiveTick; the mode and the fight-on-the-run flag are atomic because
	// the movement hooks read them from the arrival-check thread, and the
	// flee-leg flag because onArrived/onPathFailed set it from theirs.
	std::atomic<int> travelCombatMode{0};
	std::atomic<bool> travelFightOnTheRun{false};
	std::atomic<bool> travelFleeLegEnded{false};
	int travelCombatPhase = -1;
	int travelRetreats = 0;
	// Flee legs run since the bot last fully recovered; at
	// missionBoard.travelMaxFleeLegs it stops running and fights.
	int travelFleeLegs = 0;
	uint64 travelFleeIssuedMs = 0;
	int lairRetreatReflees = 0;
	uint64 lastLairRetreatDefendLogMs = 0;
	// Mop-up hold: in MOVE the bot does not walk while a creature is aimed at
	// it (the lair's survivors after the lair dies, or wildlife on the road).
	// Atomic for the arrival-check thread; the touch time makes a hold expire
	// by itself if the tick stops evaluating it.
	std::atomic<bool> travelHoldActive{false};
	std::atomic<uint64> travelCombatTouchedMs{0};
	Vector3 travelHomeDestination;
	bool travelHomeDestinationSet = false;
	uint64 travelHoldStartedMs = 0;
	bool travelHoldExhausted = false;
	float lastTravelHeartbeatDistance = -1.f;
	void beginHunting();
	void beginEarlyGame();
	void runEarlyGameTick(AiAgent* hunter, uint64 nowMs);
	void handleEarlyGameArrived();
	void handleEarlyGamePathFailed();
	void setEarlyGamePhase(EarlyGamePhase next);
	void publishEarlyGameMovement(const String& branch, bool tickEvent,
		bool beginEvent, bool notIdentityExit, bool moveRequested);
	String getEarlyGamePhaseName() const;
	String getEarlyGameWeaponXpType() const;
	bool chooseEarlyGameAnchor(bool widenRing);
	bool calculateEarlyGameTtk(CreatureObject* target,
		const SimBotEarlyGameConfig& config, int skillTier,
		WeaponObject* weapon, float rawAverageDamage, float attackPeriod,
		float& ttkSeconds, bool allowInCombat = false) const;
	bool isBelowEarlyGameHam(AiAgent* hunter, float fraction) const;
	void beginEarlyGameRecovery(bool fleeing);
	bool handleEarlyGameSelfDefense(AiAgent* hunter,
		const SimBotEarlyGameConfig& config);
	void handleEarlyGameTargetUnavailable();
	void publishEarlyGameCombat(AiAgent* hunter);
	bool isEarlyGameLegNearDeathSite(const Vector3& start,
		const Vector3& anchor, const String& planet);
	void capEarlyGameWounds(AiAgent* hunter, float maxFraction);
	bool isEarlyGameRecoveryWalkStalled(AiAgent* hunter, uint64 stallMs);
	float getEngageRangeMeters() const;
	bool chooseEarlyGameSpawnerAnchor(const SimBotEarlyGameConfig& config);
	void rememberEarlyGameUnreachableTarget(uint64 oid);
	bool isEarlyGameTargetSkipped(uint64 oid, uint64 nowMs) const;
	uint64 findUnwinnableEarlyGameAttacker(AiAgent* hunter,
		const SimBotEarlyGameConfig& config, uint64 excludeOid);
	float getEarlyGameArriveMeters(const SimBotEarlyGameConfig& config) const;
	bool scanForTarget();
	void selectTarget(AiAgent* target);
	void engageTarget();
	void disengageTarget(bool dropObserverHandle);
	// outViaThreat (optional): the pick came from the threat map, not the
	// defender list (F_0.9.8c attacker recognition).
	bool selectActiveCombatAttacker(AiAgent* hunter,
		ManagedReference<CreatureObject*>& outAttacker,
		ManagedReference<AiAgent*>& outAttackerAgent,
		bool* outViaThreat = nullptr);
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
	bool isTravelDiagPhase() const;
	bool isTravelCombatPhase() const;
	bool isTravelCombatGateOn() const;
	// Returns true when travel combat consumed this tick (FLEE or REST).
	bool runTravelCombat(AiAgent* hunter, const Vector3& destination);
	bool weaponFightsOnTheRun(AiAgent* hunter) const;
	void resetTravelCombat();
	void resumeTravelLeg(const Vector3& destination);
	// Travel combat lives in runActiveTick, but every overland leg used to be
	// entered WITHOUT re-arming the tick: it only woke on arrival, so on the
	// walk itself nothing evaluated HAM, the mop-up hold, or the phase TTL.
	// Called at each leg entry; a no-op with the gate off.
	void armTravelCombatTick();
	// F_0.9.8d: publish (or withdraw) the mission lair as a point area attacks
	// must not reach. Protected while the lair is revealed and the bot is not
	// deliberately attacking it.
	void updateAreaAttackGuard();
	// F_0.9.8e lair retreat: keep running while chased, fight back if attacked
	// while resting.
	bool isLairRetreatDefenseOn() const;
	void lairRetreatReflee(AiAgent* hunter, CreatureObject* attacker);
	// Threat-map attacker recognition is on when its own gate is, AND always on
	// a travel-combat leg: the mop-up hold starts right after the lair's death
	// cleared the bot's combat flag and defender list, so without it the hold
	// could not see the survivors it exists for.
	bool recognisesThreatAttackers() const;
	void logTravelDisengage(AiAgent* hunter, const char* reason);
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
	// Read-only views for the parity harness's production loot-walk scenario.
	bool isOnTravelLeg() const { return isTravelCombatPhase(); }
	bool isLootStepRunning() const { return lootStepActive.load(); }

	SimHunterController(AiAgent* aiAgent, uint64 identity);
	virtual ~SimHunterController();

	void startSimLoop() override;
	void onArrived() override;
	void onPathFailed() override;
	bool allowsMovementInCombat() const override;
	bool isMovementHeld() const override;
	bool keepsCombatOnPathAccept() const override;
	void onTick() override;
	bool shouldContinueArrivalChecks() const override;
	bool usesNavmeshHybridMovement() const override { return true; }
	bool isCombatDriverActive() const override {
		return targetOid != 0 && (earlyGameActive || pursuing || phase == HUNTING ||
			phase == ENGAGING_LAIR);
	}
	// Only resume a hybrid travel leg while the order is genuinely active and
	// not tearing down its lair. Once the order completes/abandons/times out
	// (orderActive=false) or enters cleanup, the preserved finalDestination must
	// not revive movement toward the finished target (code-review Major).
	bool shouldResumeHybridTravel() const override {
		return hasFinalDestination &&
			((earlyGameActive) || (orderActive && !missionCleanupRequested)) &&
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
