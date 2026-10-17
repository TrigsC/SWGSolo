/*
 * SimHunterController.cpp
 * P.8.1 solo PvE hunt loop.
 */

#include "SimHunterController.h"
#include "TravelDiagLog.h"
#include "MissionDiagLog.h"
#include "SimHunterTravelDiag.h"
#include "SimBotTravelCombat.h"
#include "SimBotAreaAttackGuard.h"
#include "SimBotLootStep.h"
#include "server/zone/objects/auction/MarketSeederBridge.h"

#include "engine/core/Core.h"
#include "server/ServerCore.h"
#include "server/utils/LambdaObserver.h"
#include "server/utils/LambdaObserverFunction.h"
#include "server/zone/CloseObjectsVector.h"
#include "server/zone/TreeEntry.h"
#include "server/zone/ZoneServer.h"
#include "server/zone/objects/creature/ai/Creature.h"
#include "server/zone/objects/creature/buffs/Buff.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/tangible/weapon/WeaponObject.h"
#include "server/zone/objects/tangible/threat/ThreatMap.h"
#include "server/zone/managers/combat/CombatManager.h"
#include "server/zone/managers/player/PlayerManager.h"
#include "server/chat/ChatManager.h"
#include "server/zone/managers/director/ScreenPlayTask.h"
#include "templates/params/ObserverEventType.h"
#include "server/zone/objects/creature/buffs/BuffType.h"
#include "templates/params/creature/CreatureAttribute.h"

#include <cmath>
#include "server/zone/objects/tangible/spawning/SpawnEggObject.h"

class SimHunterActiveTickTask : public Task {
	WeakReference<SimHunterController*> controller;
	uint64 generation;

public:
	SimHunterActiveTickTask(SimHunterController* ctrl, uint64 capturedGeneration)
		: controller(ctrl), generation(capturedGeneration) {
	}

	void run() override {
		uint64 capturedGeneration = generation;
		Reference<SimHunterController*> strong = controller.get();
		if (strong == nullptr || strong->activeTickGeneration.load(
				std::memory_order_acquire) != capturedGeneration)
			return;

		Core::getTaskManager()->executeTask([strong, capturedGeneration]() {
			if (strong->activeTickGeneration.load(std::memory_order_acquire) !=
					capturedGeneration)
				return;
			strong->runActiveTick();
		}, "SimHunterActiveTickLambda");
	}
};

static String hunterPhaseName(SimHunterController::HuntPhase phase) {
	switch (phase) {
	case SimHunterController::IDLE_HOME: return "IDLE_HOME";
	case SimHunterController::ANNOUNCE_JOB: return "ANNOUNCE_JOB";
	case SimHunterController::BUFF_UP: return "BUFF_UP";
	case SimHunterController::RELOCATING: return "RELOCATING";
	case SimHunterController::BUFF_TRIP: return "BUFF_TRIP";
	case SimHunterController::TRAVEL_OUT: return "TRAVEL_OUT";
	case SimHunterController::TRAVEL_TO_TERMINAL: return "TRAVEL_TO_TERMINAL";
	case SimHunterController::ACCEPT_MISSION: return "ACCEPT_MISSION";
	case SimHunterController::TRAVEL_TO_MISSION: return "TRAVEL_TO_MISSION";
	case SimHunterController::TRAVEL_TO_LAIR: return "TRAVEL_TO_LAIR";
	case SimHunterController::ENGAGING_LAIR: return "ENGAGING_LAIR";
	case SimHunterController::AWAITING_WORLD: return "AWAITING_WORLD";
	case SimHunterController::HUNTING: return "HUNTING";
	case SimHunterController::RETREATING: return "RETREATING";
	case SimHunterController::HEALING: return "HEALING";
	case SimHunterController::TRAVEL_HOME: return "TRAVEL_HOME";
	case SimHunterController::DELIVER: return "DELIVER";
	case SimHunterController::MISSION_CLEANUP: return "MISSION_CLEANUP";
	case SimHunterController::CLONE_HOME: return "CLONE_HOME";
	case SimHunterController::DONE: return "DONE";
	case SimHunterController::BAZAAR_VISIT: return "BAZAAR_VISIT";
	default: return "UNKNOWN";
	}
}

SimHunterController::SimHunterController(AiAgent* aiAgent, uint64 identity)
	: SimPlayerController(aiAgent) {
	identityId = identity;
	orderActive = false;
	orderAbandoned = false;
	missionAbandonReason = "";
	deathReported = false;
	phase = IDLE_HOME;
	phaseStartedAtMs = System::getMiliTime();
	bazaarTerminal = nullptr;
	bazaarTerminalPosition = Vector3();
	bazaarVisitStartedAtMs = 0;
	huntStartedAtMs = 0;
	activeTickGeneration = 1;
	targetOid = 0;
	observerTargetOid = 0;
	targetMissionWave = false;
	destructionHandled = false;
	pursuing = false;
	cantinaArrived = false;
	cantinaDwellComplete = false;
	medCenterReached = false;
	medDwellComplete = false;
	dwellUntilMs = 0;
	lastPatrolMoveMs = 0;
	lastCombatProgressMs = 0;
	phantomCombatTicks = 0;
	stalemateDefenderOid = 0;
	stalemateSelfHam = 0;
	stalemateDefenderHam = 0;
	retreatCycles = 0;
	missionHuntOrder = false;
	missionTerminalFallback = false;
	missionTerminalResolved = false;
	missionCleanupRequested = false;
	missionTerminalPosition = Vector3();
	missionLairPosition = Vector3();
	missionLairOid = 0;
	engagedMissionLairOid = 0;
	lastLairRetargetMs = 0;
	lastSeenSpawnNumber = 0;
	pendingWaveDisengage = false;
	currentMissionOfferId = 0;
	missionOffersGenerated = false;
	missionOfferAttempts = 0;
	missionOfferVisitEpoch = 0;
	missionOfferIds.removeAll();
	terminalResolveWaitCycles = 0;
	missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
	missionAddsEngaged = 0;
	earlyGameActive = false;
	earlyGamePhase = EARLY_LEAVE_CITY;
	earlyGameAnchor = Vector3();
	earlyGameRestPoint = Vector3();
	earlyGameBearingIndex = 0;
	earlyGameAnchorAttempts = 0;
	earlyGameRingWidened = false;
	earlyGameScanAttempts = 0;
	earlyGameScanBearingVisits = 0;
	earlyGameScanRetryAtMs = 0;
	earlyGamePathRetryAtMs = 0;
	earlyGameTargetTtkSeconds = 0.f;
	earlyGameTargetDefensive = false;
	earlyGameRestStartedAtMs = 0;
	earlyGamePathFailurePending.store(false, std::memory_order_relaxed);
	earlyGameDestructionPending.store(false, std::memory_order_relaxed);
	earlyGameDestroyedTargetOid.store(0, std::memory_order_relaxed);
	earlyGameDestroyedParticipantVerified.store(false,
		std::memory_order_relaxed);
	pveNeedDoctorBuff = false;
	pveNeedEntertainerBuff = false;
	pveDoctorFeeReserved = false;
	pveDoctorFallbackNeeded = false;
	pveEntertainerFallbackNeeded = false;
	pveBuffProviderApproachActive = false;
	pveBuffApproachStage = PVE_BUFF_APPROACH_NONE;
	pveBuffInteractionDwellActive = false;
	pveDoctorRequestGen = 0;
	pveDoctorRequestActive = false;
	pveDoctorDeadlineSec = 0;
	pveDoctorProviderObject = nullptr;
	pveBuffTripAtHub = false;
	pveBuffTripReturning = false;
	pveBuffTripsThisHunt = 0;
	pveBuffStagingActive = false;
	pveBuffStagingStage = PVE_BUFF_APPROACH_NONE;
	setLoggingName("SimHunterController");
}

SimHunterController::~SimHunterController() {
	teardown("destructor");
}

bool SimHunterController::isReadyForMarketRelocation() const {
	return canBeginInterplanetaryTravel();
}

bool SimHunterController::canBeginInterplanetaryTravel() const {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr || strongAgent->isDead() ||
			strongAgent->isIncapacitated() || strongAgent->isInCombat() ||
			missionLairOid != 0 || engagedMissionLairOid != 0 ||
			isInterplanetaryTravelActive())
		return false;

	Vector<ManagedReference<CreatureObject*> > defenders;
	{
		Locker agentLock(strongAgent);
		const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
			strongAgent->getDefenderList();
		if (defenderList != nullptr) {
			for (int i = 0; i < defenderList->size(); ++i) {
				ManagedReference<SceneObject*> object = defenderList->getSafe(i);
				CreatureObject* defender = object == nullptr ? nullptr :
					object->asCreatureObject();
				if (defender != nullptr)
					defenders.add(defender);
			}
		}
	}
	for (int i = 0; i < defenders.size(); ++i) {
		CreatureObject* defender = defenders.get(i);
		if (defender != nullptr && !defender->isDead() &&
				!defender->isIncapacitated())
			return false;
	}
	return true;
}

void SimHunterController::onInterplanetaryTravelFinished(bool success,
		const String& destZone, const String& reason) {
	(void)reason;
	SimPlayerManager* manager = SimPlayerManager::instance();
	if (!orderActive)
		return;

	SimPlayerManager::PveBuffTrip buffTrip;
	if (manager->getPveHunterBuffTrip(identityId, buffTrip)) {
		if (!success) {
			manager->finishPveHunterBuffTrip(identityId, false);
			clearInterplanetaryTravelState();
			if (buffTrip.returning) {
				pveBuffTripReturning = false;
				resumeAfterBuffTrip();
			} else {
				pveBuffTripAtHub = false;
				pveBuffTripReturning = false;
				pveDoctorFallbackNeeded = pveNeedDoctorBuff;
				pveEntertainerFallbackNeeded = pveNeedEntertainerBuff;
				finishPveBuffProviderFlow();
			}
			return;
		}

		manager->advancePveHunterBuffTrip(identityId);
		setPhase(BUFF_TRIP);
		scheduleActiveTick(500);
		return;
	}

	if (!manager->isPveMarketDispatchEnabled())
		return;

	SimPlayerManager::PveRelocation relocation;
	if (!manager->getPveHunterRelocation(identityId,
			relocation))
		return;

	if (!success) {
		manager->finishPveHunterRelocation(identityId, false, destZone);
		if (!destZone.isEmpty())
			order.marketTargetPlanet = destZone;
		if (agent != nullptr && agent->getZone() != nullptr)
			order.marketTargetPlanet = agent->getZone()->getZoneName();
		setPhase(ANNOUNCE_JOB);
		scheduleActiveTick(500);
		return;
	}

	manager->advancePveHunterRelocation(identityId);
	SimPlayerManager::PveRelocation advanced;
	if (!manager->getPveHunterRelocation(identityId,
			advanced) || advanced.legIndex >= advanced.legs.size()) {
		manager->finishPveHunterRelocation(identityId, true, destZone);
		if (!destZone.isEmpty())
			order.marketTargetPlanet = destZone;
		setPhase(ANNOUNCE_JOB);
	} else {
		setPhase(RELOCATING);
	}
	scheduleActiveTick(500);
}

String SimHunterController::getPhaseName() const {
	return hunterPhaseName(phase);
}

void SimHunterController::scheduleActiveTick(int delayMs) {
	if (delayMs < 100)
		delayMs = 100;

	// C1: atomic RMW; capture the resulting value so a concurrent scheduler on
	// another thread cannot make us tag the task with a generation that a third
	// party already advanced past.
	uint64 gen = activeTickGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
	if (gen == 0)
		gen = activeTickGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;

	Reference<SimHunterActiveTickTask*> task =
		new SimHunterActiveTickTask(this, gen);
	task->schedule(delayMs);
}

void SimHunterController::setPhase(HuntPhase next) {
	if (phase == next && phaseStartedAtMs != 0)
		return;

	phase = next;
	phaseStartedAtMs = System::getMiliTime();
	if (identityId != 0)
		SimPlayerManager::instance()->recordPveHunterPhase(identityId,
			agent == nullptr ? 0 : agent->getObjectID(), getPhaseName(), targetOid);
}

bool SimHunterController::beginBazaarVisit(AiAgent* hunter, uint64 nowMs) {
	SimPlayerManager* manager = SimPlayerManager::instance();
	if (manager == nullptr || hunter == nullptr || hunter->isDead() ||
			hunter->isInCombat() || !manager->beginPlayerBotBazaarVisit(identityId))
		return false;

	Zone* zone = hunter->getZone();
	Vector3 position = hunter->getWorldPosition();
	if (zone == nullptr) {
		MissionDiagLog::event("BAZAAR_VISIT", identityId,
			"begin=1 terminal=0 distance=-1");
		manager->recordPlayerBotBazaarVisit(identityId, "noOutdoorTerminal");
		MissionDiagLog::event("BAZAAR_VISIT", identityId,
			"end=1 outcome=noOutdoorTerminal terminal=0 distance=-1");
		return false;
	}

	SceneObject* terminal = MarketSeederBridge::findBazaarTerminal(zone,
		position.getX(), position.getZ(), position.getY(),
		static_cast<float>(manager->getPlayerBotBazaarMaxDetourMeters()), true);
	bazaarVisitStartedAtMs = nowMs;
	if (terminal == nullptr) {
		MissionDiagLog::event("BAZAAR_VISIT", identityId,
			"begin=1 terminal=0 distance=-1");
		manager->recordPlayerBotBazaarVisit(identityId, "noOutdoorTerminal");
		MissionDiagLog::event("BAZAAR_VISIT", identityId,
			"end=1 outcome=noOutdoorTerminal terminal=0 distance=-1");
		bazaarVisitStartedAtMs = 0;
		return false;
	}

	bazaarTerminal = terminal;
	bazaarTerminalPosition = terminal->getWorldPosition();
	float distance = position.distanceTo2d(bazaarTerminalPosition);
	MissionDiagLog::event("BAZAAR_VISIT", identityId,
		"begin=1 terminal=" + String::valueOf(terminal->getObjectID()) +
		" distance=" + String::valueOf(distance));
	setPhase(BAZAAR_VISIT);
	const uint64 generationBefore = activeTickGeneration.load(
		std::memory_order_acquire);
	moveTo(bazaarTerminalPosition);
	if (activeTickGeneration.load(std::memory_order_acquire) == generationBefore)
		scheduleActiveTick(1000);
	return true;
}

void SimHunterController::finishBazaarVisit(const String& outcome,
		bool resumeJob) {
	SimPlayerManager* manager = SimPlayerManager::instance();
	uint64 terminalOid = bazaarTerminal == nullptr ? 0 :
		bazaarTerminal->getObjectID();
	float distance = -1.f;
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent != nullptr)
		distance = strongAgent->getWorldPosition().distanceTo2d(
			bazaarTerminalPosition);
	if (manager != nullptr)
		manager->recordPlayerBotBazaarVisit(identityId, outcome);
	MissionDiagLog::event("BAZAAR_VISIT", identityId,
		"end=1 outcome=" + outcome + " terminal=" +
		String::valueOf(terminalOid) + " distance=" + String::valueOf(distance));
	bazaarTerminal = nullptr;
	bazaarTerminalPosition = Vector3();
	bazaarVisitStartedAtMs = 0;
	if (!resumeJob)
		return;
	setPhase(ANNOUNCE_JOB);
	const uint64 generationBefore = activeTickGeneration.load(
		std::memory_order_acquire);
	beginBuffUp();
	if (activeTickGeneration.load(std::memory_order_acquire) == generationBefore)
		scheduleActiveTick(500);
}

String SimHunterController::getEarlyGamePhaseName() const {
	switch (earlyGamePhase) {
	case EARLY_LEAVE_CITY: return "LeaveCity";
	case EARLY_SELECT_TARGET: return "SelectTarget";
	case EARLY_ENGAGE: return "Engage";
	case EARLY_RECOVER: return "Recover";
	case EARLY_TRAIN: return "Train";
	default: return "Unknown";
	}
}

String SimHunterController::getEarlyGameWeaponXpType() const {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return "";

	String xpType;
	Locker agentLock(strongAgent);
	WeaponObject* weapon = strongAgent->getWeapon();
	if (weapon != nullptr)
		xpType = weapon->getXpType();
	return xpType;
}

void SimHunterController::setEarlyGamePhase(EarlyGamePhase next) {
	earlyGamePhase = next;
	if (identityId != 0)
		SimPlayerManager::instance()->setPlayerBotEarlyGamePhase(identityId,
			getEarlyGamePhaseName(), targetOid, earlyGameTargetTtkSeconds,
			getEarlyGameWeaponXpType(), earlyGameTargetDefensive);
}

void SimHunterController::publishEarlyGameMovement(const String& branch,
		bool tickEvent, bool beginEvent, bool notIdentityExit,
		bool moveRequested) {
	// Default-off diagnostic (earlyGame.movementTelemetry): while it is off the
	// gate read is the only cost.
	SimPlayerManager* manager = SimPlayerManager::instance();
	if (identityId == 0 ||
			!manager->isPlayerBotEarlyGameMovementTelemetryEnabled()) {
		setHybridFailureProbeEnabled(false);
		return;
	}
	setHybridFailureProbeEnabled(true);
	ManagedReference<AiAgent*> strongAgent = agent;
	Vector3 position = strongAgent == nullptr ? Vector3() :
		strongAgent->getWorldPosition();
	manager->recordPlayerBotEarlyGameMovement(identityId, branch, tickEvent,
		beginEvent, notIdentityExit, moveRequested, getDiagnosticStateName(),
		getWorkLoopGeneration(), getLastWorkLoopAdvanceReason(), position,
		earlyGameAnchor, getHybridPathDiagLine());
}

void SimHunterController::startSimLoop() {
	if (agent == nullptr)
		return;

	if (SimPlayerManager::instance()->isPlayerBotEarlyGameIdentity(identityId)) {
		beginEarlyGame();
		return;
	}

	if (!orderActive) {
		setPhase(IDLE_HOME);
		scheduleActiveTick(30000);
		return;
	}

	setPhase(ANNOUNCE_JOB);
	scheduleActiveTick(100);
}

void SimHunterController::startOrder(const PveHuntOrder& newOrder,
		const PveHuntSpecies& newSpecies) {
	if (newOrder.identityId != identityId || newOrder.bodyOid == 0)
		return;

	// The manager uses the same scorer while building matchmaker candidates.
	// Recheck at dispatch as well: an order can race a training tick and a
	// mid-grind novice must decline it rather than let demand take over.
	SimBotIdentity identity;
	SimBotActivityDecision activity;
	if (SimPlayerManager::instance()->snapshotPlayerBotIdentity(identityId,
			identity)) {
		SimPlayerManager::instance()->scorePlayerBotActivity(identity, activity);
		if (activity.activity == "grind") {
			SimPlayerManager::instance()->declinePveHunterOrder(identityId,
				newOrder.bodyOid, "EARLY_GAME_GRIND");
			if (!earlyGameActive)
				startSimLoop();
			else
				scheduleActiveTick(100);
			return;
		}
	}

	if (orderActive && order.issuedAtMs == newOrder.issuedAtMs)
		return;

	if (lootStepActive) {
		SimPlayerManager::instance()->returnPlayerBotLootClaim(lootStepClaim);
		finishPlayerBotLootStep("newOrder", false);
	}
	dropTargetObserver();
	disengageTarget(false);
	clearEngagedTelemetry();
	cancelPveDoctorRequest();
	SimPlayerManager::instance()->releasePlayerBotDoctorBuffFee(identityId);
	pveDoctorFeeReserved = false;
	advanceWorkLoopGeneration("hunterStartOrder");
	clearInteriorApproachLeg();
	resetHybridMovementState(true);
	order = newOrder;
	species = newSpecies;
	orderActive = true;
	// One contract, one travel-combat tally (diagnostic).
	SimHunterTravelDiag::reset(newOrder.bodyOid);
	resetTravelCombat();
	missionHuntOrder = SimPlayerManager::instance()->isPveMissionHuntEnabled();
	orderAbandoned = false;
	missionAbandonReason = "";
	deathReported = false;
	phase = IDLE_HOME;
	phaseStartedAtMs = 0;
	huntStartedAtMs = 0;
	targetOid = 0;
	targetMissionWave = false;
	retreatCycles = 0;
	missionTerminalFallback = false;
	missionTerminalResolved = false;
	missionCleanupRequested = false;
	missionTerminalPosition = Vector3();
	missionLairPosition = Vector3();
	missionLairOid = 0;
	engagedMissionLairOid = 0;
	lastLairRetargetMs = 0;
	lastSeenSpawnNumber = 0;
	pendingWaveDisengage = false;
	currentMissionOfferId = 0;
	missionOffersGenerated = false;
	missionOfferAttempts = 0;
	missionOfferVisitEpoch = 0;
	missionOfferIds.removeAll();
	terminalResolveWaitCycles = 0;
	missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
	missionAddsEngaged = 0;
	pveBuffProviders = PveBuffProviders();
	pveNeedDoctorBuff = false;
	pveNeedEntertainerBuff = false;
	pveDoctorFeeReserved = false;
	pveDoctorFallbackNeeded = false;
	pveEntertainerFallbackNeeded = false;
	pveBuffProviderApproachActive = false;
	pveBuffApproachStage = PVE_BUFF_APPROACH_NONE;
	pveBuffInteractionDwellActive = false;
	pveDoctorRequestActive = false;
	pveDoctorDeadlineSec = 0;
	pveDoctorProviderObject = nullptr;
	pveBuffTripAtHub = false;
	pveBuffTripReturning = false;
	pveBuffTripsThisHunt = 0;
	pveBuffStagingActive = false;
	pveBuffStagingStage = PVE_BUFF_APPROACH_NONE;
	cantinaDwellComplete = false;
	medDwellComplete = false;
	dwellUntilMs = 0;
	resetCombatGuard();
	setPhase(ANNOUNCE_JOB);
	SimPlayerManager::instance()->announcePveHunterEvent(
		agent == nullptr ? 0 : agent->getObjectID(), species.key,
		"Heading out for a " + species.harvestKind + " contract.");
	scheduleActiveTick(100);
}

// Lowest of the three HAM pools as a fraction, the same measure LAIR_FIELD logs.
static float simHunterMinHamFraction(AiAgent* hunter) {
	float hamFraction = 1.f;
	if (hunter == nullptr)
		return hamFraction;
	const uint8 hamPools[] = {CreatureAttribute::HEALTH,
		CreatureAttribute::ACTION, CreatureAttribute::MIND};
	Locker agentLock(hunter);
	for (uint8 i = 0; i < 3; ++i) {
		int maxHam = hunter->getMaxHAM(hamPools[i]);
		if (maxHam > 0)
			hamFraction = Math::min(hamFraction, static_cast<float>(
				hunter->getHAM(hamPools[i])) / static_cast<float>(maxHam));
	}
	return hamFraction;
}

// F_0.9.8b diagnostic (missiondiag only): the interceptor path is about to shed
// every defender bilaterally and clear combat. Logged BEFORE the shed so the
// defender count and nearest distance describe what was dropped. Rate-limited
// to one line per 5s per bot; the tally counts every occurrence.
bool SimHunterController::isTravelDiagPhase() const {
	// defendAgainstInterceptor is shared with the HUNTING handler; only the
	// movement legs belong in the travel tally.
	return phase != HUNTING && phase != ENGAGING_LAIR &&
		phase != RETREATING && phase != HEALING;
}

void SimHunterController::logTravelDisengage(AiAgent* hunter,
		const char* reason) {
	if (hunter == nullptr || !MissionDiagLog::isLoggingEnabled() ||
			!isTravelDiagPhase())
		return;
	float hamFraction = simHunterMinHamFraction(hunter);
	SimHunterTravelDiag::noteDisengage(hunter->getObjectID(), hamFraction);

	uint64 nowMs = System::getMiliTime();
	if (lastTravelDisengageLogMs != 0 &&
			nowMs - lastTravelDisengageLogMs < 5000) {
		++suppressedTravelDisengages;
		return;
	}
	int defenders = 0;
	float nearest = -1.f;
	{
		Locker agentLock(hunter);
		const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
			hunter->getDefenderList();
		if (defenderList != nullptr) {
			for (int i = 0; i < defenderList->size(); ++i) {
				ManagedReference<SceneObject*> defender = defenderList->getSafe(i);
				if (defender == nullptr)
					continue;
				++defenders;
				float distance = hunter->getWorldPosition().distanceTo(
					defender->getWorldPosition());
				if (nearest < 0.f || distance < nearest)
					nearest = distance;
			}
		}
	}
	MissionDiagLog::event("TRAVEL_DISENGAGE", identityId,
		"phase=" + getPhaseName() + " reason=" + String(reason) +
		" defenders=" + String::valueOf(defenders) +
		" nearestDefenderM=" + String::valueOf(static_cast<int>(nearest)) +
		" selectRadiusM=" + String::valueOf(static_cast<int>(
			SimPlayerManager::instance()->getPveHunterScanRadiusMeters() + 24.f)) +
		" botHamFraction=" + String::valueOf(hamFraction) +
		" suppressedSinceLast=" + String::valueOf(suppressedTravelDisengages));
	lastTravelDisengageLogMs = nowMs;
	suppressedTravelDisengages = 0;
}

// ---- F_0.9.8c travel combat ------------------------------------------------
// The two overland mission legs. The base mover already holds a bot in place
// while it is in combat and the AI fights; what this adds is the retreat (low
// HAM, toward the mission) and the recovery before walking on.

bool SimHunterController::isTravelCombatPhase() const {
	// Every overland walk of a mission order: out to the mission and the lair,
	// and -- owner 2026-10-10 -- back to the terminal and home. The return legs
	// start AT the lair, with its survivors still on the bot.
	return missionHuntOrder &&
		(phase == TRAVEL_TO_MISSION || phase == TRAVEL_TO_LAIR ||
			phase == TRAVEL_TO_TERMINAL || phase == TRAVEL_HOME);
}

bool SimHunterController::isTravelCombatGateOn() const {
	return SimPlayerManager::instance()->getPveMissionBoardTravelCombatEnabled();
}

bool SimHunterController::isLairRetreatDefenseOn() const {
	return SimPlayerManager::instance()->getPveMissionBoardLairRetreatDefense();
}

void SimHunterController::lairRetreatReflee(AiAgent* hunter,
		CreatureObject* attacker) {
	if (hunter == nullptr)
		return;
	++lairRetreatReflees;
	Vector3 position = hunter->getWorldPosition();
	Vector3 direction = attacker == nullptr ? Vector3::UNIT_X :
		position - attacker->getWorldPosition();
	direction.setZ(0.f);
	if (direction.length2d() < 0.01f)
		direction = Vector3::UNIT_X;
	else
		direction.normalize();
	Vector3 away = position + direction *
		SimPlayerManager::instance()->getPveHunterRetreatRangeMeters();
	if (hunter->getZone() != nullptr)
		away.setZ(hunter->getZone()->getHeight(away.getX(), away.getY()));
	MissionDiagLog::event("LAIR_RETREAT", identityId,
		"event=reflee leg=" + String::valueOf(lairRetreatReflees) +
		" botHamFraction=" + String::valueOf(simHunterMinHamFraction(hunter)) +
		" attackerLevel=" + String::valueOf(attacker == nullptr ? 0 :
			attacker->getLevel()) +
		" lairOid=" + String::valueOf(missionLairOid));
	// Drop the fight so the bot runs instead of auto-attacking; the hunt target
	// is preserved for the resume.
	resetInterceptorCombat();
	moveTo(away);
	scheduleActiveTick(1000);
}

bool SimHunterController::allowsMovementInCombat() const {
	// A lair retreat is a run; the base in-combat hold used to pin the bot where
	// it stood for as long as anything kept hitting it.
	if (phase == RETREATING && isLairRetreatDefenseOn())
		return true;
	if (!isTravelCombatGateOn() || !isTravelCombatPhase())
		return false;
	int mode = travelCombatMode.load(std::memory_order_acquire);
	return mode == SimBotTravelCombat::FLEE ||
		(mode == SimBotTravelCombat::MOVE &&
			travelFightOnTheRun.load(std::memory_order_acquire));
}

bool SimHunterController::keepsCombatOnPathAccept() const {
	// Only fight on the run keeps its fight across a re-path. A FLEE leg wants
	// the fight dropped -- it shed combat itself before moving.
	return isTravelCombatGateOn() && isTravelCombatPhase() &&
		travelCombatMode.load(std::memory_order_acquire) ==
			SimBotTravelCombat::MOVE &&
		travelFightOnTheRun.load(std::memory_order_acquire);
}

bool SimHunterController::isMovementHeld() const {
	if (!isTravelCombatGateOn() || !isTravelCombatPhase())
		return false;
	if (travelCombatMode.load(std::memory_order_acquire) ==
			SimBotTravelCombat::REST)
		return true;
	// The mop-up hold is only honoured while the tick keeps refreshing it, so
	// it can never outlive the code that decides it.
	return travelHoldActive.load(std::memory_order_acquire) &&
		System::getMiliTime() < travelCombatTouchedMs.load(
			std::memory_order_acquire) + 5000;
}

bool SimHunterController::recognisesThreatAttackers() const {
	return SimPlayerManager::instance()->getPveMissionBoardAttackerRecognition() ||
		(isTravelCombatGateOn() && isTravelCombatPhase()) ||
		(isLairRetreatDefenseOn() &&
			(phase == RETREATING || phase == HEALING));
}

void SimHunterController::updateAreaAttackGuard() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;
	const uint64 bodyOid = strongAgent->getObjectID();
	// engagedMissionLairOid != 0 means the bot is hitting the lair on purpose:
	// an area attack there is wanted, and the wave tactics pace it.
	if (orderActive && missionHuntOrder && missionLairOid != 0 &&
			engagedMissionLairOid == 0 && !strongAgent->isDead())
		SimBotAreaAttackGuard::protect(bodyOid, missionLairPosition.getX(),
			missionLairPosition.getY());
	else
		SimBotAreaAttackGuard::release(bodyOid);
}

void SimHunterController::armTravelCombatTick() {
	if (isTravelCombatGateOn())
		scheduleActiveTick(1000);
}

void SimHunterController::resumeTravelLeg(const Vector3& destination) {
	// The terminal leg re-resolves its terminal (it may be indoors); every
	// other leg walks to a plain world point.
	if (phase == TRAVEL_TO_TERMINAL)
		beginMissionTerminalLeg();
	else
		moveTo(destination);
}

void SimHunterController::resetTravelCombat() {
	travelCombatMode.store(SimBotTravelCombat::MOVE, std::memory_order_release);
	travelFleeLegEnded.store(false, std::memory_order_release);
	travelFightOnTheRun.store(false, std::memory_order_release);
	travelHoldActive.store(false, std::memory_order_release);
	travelHoldStartedMs = 0;
	travelHoldExhausted = false;
	travelHomeDestinationSet = false;
	travelCombatPhase = -1;
	travelRetreats = 0;
	travelFleeLegs = 0;
	travelFleeIssuedMs = 0;
}

bool SimHunterController::weaponFightsOnTheRun(AiAgent* hunter) const {
	if (hunter == nullptr)
		return false;
	Locker agentLock(hunter);
	WeaponObject* weapon = hunter->getWeapon();
	if (weapon == nullptr)
		return false;
	String weaponName;
	if (weapon->isRifleWeapon())
		weaponName = "rifle";
	else if (weapon->isCarbineWeapon())
		weaponName = "carbine";
	else if (weapon->isPistolWeapon())
		weaponName = "pistol";
	// Mirrors CombatManager's moving-attack offset, which exists only for
	// these three; anything else (melee included) stands and fights.
	int hitWhileMoving = weaponName.isEmpty() ? 0 :
		hunter->getSkillMod(weaponName + "_hit_while_moving");
	return SimBotTravelCombat::weaponFightsOnTheRun(weapon->isMeleeWeapon(),
		hitWhileMoving);
}

bool SimHunterController::runTravelCombat(AiAgent* hunter,
		const Vector3& destination) {
	if (hunter == nullptr)
		return false;
	const int current = travelCombatMode.load(std::memory_order_acquire);

	if (!isTravelCombatGateOn()) {
		// Switched off mid-flee, mid-rest or mid-hold: release it and walk on.
		const bool wasHolding = travelHoldActive.load(std::memory_order_acquire);
		if (current != SimBotTravelCombat::MOVE || wasHolding) {
			resetTravelCombat();
			resumeTravelLeg(destination);
		}
		return false;
	}
	travelCombatTouchedMs.store(System::getMiliTime(),
		std::memory_order_release);

	// A mode never carries across legs: each leg starts walking.
	if (travelCombatPhase != static_cast<int>(phase)) {
		travelCombatPhase = static_cast<int>(phase);
		travelHoldActive.store(false, std::memory_order_release);
		if (current != SimBotTravelCombat::MOVE) {
			travelCombatMode.store(SimBotTravelCombat::MOVE,
				std::memory_order_release);
			travelFleeLegEnded.store(false, std::memory_order_release);
			resumeTravelLeg(destination);
			return false;
		}
	}

	travelFightOnTheRun.store(weaponFightsOnTheRun(hunter),
		std::memory_order_release);

	ManagedReference<CreatureObject*> attacker;
	ManagedReference<AiAgent*> attackerAgent;
	// With recognition on, a creature hitting the bot counts even before the
	// bot's own combat state says so.
	const bool attacked = (hunter->isInCombat() || recognisesThreatAttackers()) &&
		selectActiveCombatAttacker(hunter, attacker, attackerAgent);
	const float hamFraction = simHunterMinHamFraction(hunter);
	const int restPct = SimPlayerManager::instance()->
		getPveMissionBoardTravelRestHamPct();

	SimBotTravelCombat::Inputs in;
	in.attacked = attacked;
	in.belowRetreat = isBelowRetreatThreshold(hunter);
	in.belowRest = restPct > 0 && hamFraction < restPct / 100.f;
	in.readyToResume = isReadyToResume(hunter);
	// An encounter ends when the bot is clear and recovered; only then does it
	// earn its flee legs back. Until then a bot that has run them and is caught
	// again fights instead of bouncing between FLEE and REST.
	if (!attacked && in.readyToResume)
		travelFleeLegs = 0;
	in.standAndFight = SimBotTravelCombat::fleeLegsSpent(travelFleeLegs,
		SimPlayerManager::instance()->getPveMissionBoardTravelMaxFleeLegs());
	// A flee leg ends on arrival or path failure (the guards in onArrived and
	// onPathFailed), or if it is not moving 3s after it was issued -- a moveTo
	// refused outright gets no callback, and must not pin the bot in FLEE
	// until the phase TTL.
	in.fleeArrived = current == SimBotTravelCombat::FLEE &&
		(travelFleeLegEnded.load(std::memory_order_acquire) ||
			(System::getMiliTime() > travelFleeIssuedMs + 3000 &&
				state != SimPlayerController::MOVING &&
				state != SimPlayerController::CALCULATING_PATH));

	const SimBotTravelCombat::Mode next = SimBotTravelCombat::nextMode(
		static_cast<SimBotTravelCombat::Mode>(current), in);
	const bool reflee = current == SimBotTravelCombat::FLEE &&
		next == SimBotTravelCombat::FLEE && in.fleeArrived;

	Vector3 fleeTarget;
	if (next == SimBotTravelCombat::FLEE &&
			(current != SimBotTravelCombat::FLEE || reflee)) {
		Vector3 position = hunter->getWorldPosition();
		Vector3 attackerPosition = attacker == nullptr ? position :
			attacker->getWorldPosition();
		float fleeX = 0.f;
		float fleeY = 0.f;
		SimBotTravelCombat::fleePoint(position.getX(), position.getY(),
			destination.getX(), destination.getY(), attacker != nullptr,
			attackerPosition.getX(), attackerPosition.getY(),
			SimPlayerManager::instance()->getPveHunterRetreatRangeMeters(),
			fleeX, fleeY);
		// Engine Vector3 is (x, y_north, z_height); Z from terrain, as
		// moveToPatrolPoint does.
		fleeTarget = Vector3(fleeX, fleeY, position.getZ());
		if (hunter->getZone() != nullptr)
			fleeTarget.setZ(hunter->getZone()->getHeight(fleeX, fleeY));
	}

	if (next != SimBotTravelCombat::MOVE)
		travelHoldActive.store(false, std::memory_order_release);

	if (next != current || reflee) {
		if (next == SimBotTravelCombat::FLEE && current != SimBotTravelCombat::FLEE)
			++travelRetreats;
		String attackerName = "none";
		int attackerLevel = 0;
		if (attackerAgent != nullptr) {
			const CreatureTemplate* attackerTemplate =
				attackerAgent->getCreatureTemplate();
			if (attackerTemplate != nullptr)
				attackerName = attackerTemplate->getTemplateName();
			attackerLevel = attacker->getLevel();
		}
		SimHunterTravelDiag::noteTravelMode(hunter->getObjectID(),
			SimBotTravelCombat::modeName(next), travelRetreats);
		MissionDiagLog::event("TRAVEL_MODE", identityId,
			"phase=" + getPhaseName() +
			" from=" + String(SimBotTravelCombat::modeName(current)) +
			" to=" + String(SimBotTravelCombat::modeName(next)) +
			" reflee=" + String::valueOf(reflee) +
			" botHamFraction=" + String::valueOf(hamFraction) +
			" attacker=" + attackerName +
			" attackerLevel=" + String::valueOf(attackerLevel) +
			" destDistM=" + String::valueOf(static_cast<int>(
				hunter->getWorldPosition().distanceTo2d(destination))) +
			" retreats=" + String::valueOf(travelRetreats) +
			" fleeLegs=" + String::valueOf(travelFleeLegs) +
			" stand=" + String::valueOf(in.standAndFight) +
			(next == SimBotTravelCombat::FLEE ?
				" fleeX=" + String::valueOf(static_cast<int>(fleeTarget.getX())) +
				" fleeY=" + String::valueOf(static_cast<int>(fleeTarget.getY())) :
				String("")));
	}

	switch (next) {
	case SimBotTravelCombat::FLEE:
		if (current != SimBotTravelCombat::FLEE || reflee) {
			// Mode first: the movement hooks must already allow walking in
			// combat when the path request lands.
			travelCombatMode.store(SimBotTravelCombat::FLEE,
				std::memory_order_release);
			travelFleeLegEnded.store(false, std::memory_order_release);
			// Drop the fight so the bot is not auto-attacking as it runs.
			resetInterceptorCombat();
			travelFleeIssuedMs = System::getMiliTime();
			++travelFleeLegs;
			moveTo(fleeTarget);
		}
		scheduleActiveTick(1000);
		return true;
	case SimBotTravelCombat::REST:
		if (current != SimBotTravelCombat::REST) {
			travelCombatMode.store(SimBotTravelCombat::REST,
				std::memory_order_release);
			travelFleeLegEnded.store(false, std::memory_order_release);
			state = SimPlayerController::IDLE;
		}
		scheduleActiveTick(2000);
		return true;
	default: {
		if (current != SimBotTravelCombat::MOVE) {
			travelCombatMode.store(SimBotTravelCombat::MOVE,
				std::memory_order_release);
			travelFleeLegEnded.store(false, std::memory_order_release);
		}
		// Mop-up hold (owner 2026-10-10). A bot does not walk away while a
		// creature is aimed at it: when the lair dies its survivors are still on
		// the bot, and leaving for the terminal dragged them along until they
		// killed it. Standing lets the fight finish -- onTick fights back, FLEE
		// still takes over below the retreat threshold -- and the walk resumes
		// once nothing is aimed at the bot. A fight-on-the-run weapon keeps
		// walking instead.
		static const uint64 MAX_HOLD_MS = 180000;
		const bool wasHolding = travelHoldActive.load(std::memory_order_acquire);
		const uint64 holdNowMs = System::getMiliTime();
		travelHoldExhausted = SimBotTravelCombat::mopUpExhausted(attacked,
			travelHoldExhausted, wasHolding && travelHoldStartedMs != 0 ?
				holdNowMs - travelHoldStartedMs : 0, MAX_HOLD_MS);
		const bool hold = SimBotTravelCombat::mopUpHold(attacked,
			travelFightOnTheRun.load(std::memory_order_acquire),
			travelHoldExhausted);
		if (hold != wasHolding) {
			travelHoldStartedMs = hold ? holdNowMs : 0;
			travelHoldActive.store(hold, std::memory_order_release);
			String attackerName = "none";
			if (attackerAgent != nullptr &&
					attackerAgent->getCreatureTemplate() != nullptr)
				attackerName =
					attackerAgent->getCreatureTemplate()->getTemplateName();
			MissionDiagLog::event("TRAVEL_HOLD", identityId,
				"phase=" + getPhaseName() +
				" state=" + String(hold ? "begin" : "end") +
				" exhausted=" + String::valueOf(travelHoldExhausted) +
				" botHamFraction=" + String::valueOf(hamFraction) +
				" attacker=" + attackerName +
				" attackerLevel=" + String::valueOf(attacker == nullptr ? 0 :
					attacker->getLevel()));
		}
		if (hold) {
			if (!wasHolding)
				state = SimPlayerController::IDLE;
			scheduleActiveTick(1000);
			return true;
		}
		if (wasHolding || current != SimBotTravelCombat::MOVE)
			resumeTravelLeg(destination);
		return false;
	}
	}
}

void SimHunterController::onTick() {
	// Self-defense hook, run on the arrival cadence (checkArrival calls onTick),
	// which stays live through the travel legs even when the active tick is idle.
	// It fights back at WHATEVER is attacking the hunter on a pure movement leg
	// (e.g. an interceptor between the city and the lair — TRAVEL_TO_LAIR can
	// leave the active tick sparse). To keep the two independently-scheduled loops
	// from racing the mission-target state, onTick does NOT own targetOid: in
	// HUNTING it defers entirely to runActiveTick (which handles the lair stand,
	// including a creature that aggros before a target is acquired), and elsewhere
	// it only self-defends via the interceptor path. It never moves the bot.
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	// RETREATING flees and HEALING recovers; both own their combat/movement.
	if (phase == RETREATING || phase == HEALING)
		return;

	// F_0.9.8c: a bot fleeing on a travel leg runs; it does not turn and fight.
	if (isTravelCombatPhase() &&
			travelCombatMode.load(std::memory_order_acquire) ==
				SimBotTravelCombat::FLEE)
		return;

	// Attacker recognition looks past the bot's own combat state: a creature
	// can be hitting a bot whose combat flag has already been cleared.
	if (!strongAgent->isInCombat() && !recognisesThreatAttackers())
		return;

	// Concurrency: onTick (ArrivalCheckTask) and runActiveTick
	// (SimHunterActiveTickTask) are independently scheduled and can run on
	// different task-pool threads. runActiveTick is the SOLE writer of targetOid
	// and the mission observer (both are HUNTING-scoped: targetOid is set only by
	// selectTarget during HUNTING and cleared before the phase is left). To avoid
	// racing that state, onTick never promotes a mission target — it only
	// self-defends, and only in the travel/movement legs where runActiveTick's
	// combat handler does not run. In HUNTING, runActiveTick owns combat.
	if (phase == HUNTING || phase == ENGAGING_LAIR)
		return;

	// Interceptor-only self-defense: fight whatever is actually attacking, via
	// startCombat/setTargetObject/activateAiBehavior, without ever touching
	// targetOid or the observer. In the phases this code runs (travel/movement
	// legs — RETREATING/HEALING and HUNTING have already returned above, and
	// only those plus HUNTING ever hold a non-zero targetOid), targetOid is 0,
	// so resetInterceptorCombat preserves it and cannot abandon a mission target.
	ManagedReference<CreatureObject*> attacker;
	ManagedReference<AiAgent*> attackerAgent;
	bool viaThreat = false;
	if (selectActiveCombatAttacker(strongAgent, attacker, attackerAgent,
			&viaThreat)) {
		travelEngageViaThreat = viaThreat;
		defendAgainstInterceptor(strongAgent, attacker.get());
	} else if (strongAgent->isInCombat()) {
		logTravelDisengage(strongAgent, "no_selectable_attacker");
		resetInterceptorCombat();
	}
}

bool SimHunterController::shouldContinueArrivalChecks() const {
	return earlyGameActive || orderActive || state == MOVING ||
		state == CALCULATING_PATH;
}

void SimHunterController::computeBuffNeeds(bool& needDoctor,
		bool& needEntertainer) const {
	needDoctor = false;
	needEntertainer = false;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr) {
		needDoctor = true;
		needEntertainer = true;
		return;
	}

	SimPlayerManager* manager = SimPlayerManager::instance();
	Vector<uint32> trackedBuffCrcs;
	manager->getPveTrackedBuffCrcs(trackedBuffCrcs);
	int thresholdSeconds = manager->getPveRealBuffReapplyThresholdSeconds();

	// Snapshot all buff state while holding the agent lock. Do not query other
	// manager state while this lock is held; the controller's provider work will
	// use the same release-before-manager-query choreography.
	Locker agentLock(strongAgent);
	for (int i = 0; i < trackedBuffCrcs.size(); ++i) {
		Buff* buff = strongAgent->getBuff(trackedBuffCrcs.get(i));
		bool needsRefresh = buff == nullptr ||
			buff->getTimeLeft() < thresholdSeconds;
		if (!needsRefresh)
			continue;

		if (i < 6)
			needDoctor = true;
		else
			needEntertainer = true;

		if (needDoctor && needEntertainer)
			break;
	}
}

bool SimHunterController::isAgentInsideBuildingOfCell(CellObject* cell) const {
	if (cell == nullptr)
		return false;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return false;

	ManagedReference<SceneObject*> agentParent = strongAgent->getParent().get();
	if (agentParent == nullptr || !agentParent->isCellObject())
		return false;

	ManagedReference<SceneObject*> agentBuilding = agentParent->getParent().get();
	ManagedReference<SceneObject*> providerBuilding = cell->getParent().get();
	if (agentBuilding == nullptr || providerBuilding == nullptr)
		return false;

	return agentBuilding->getObjectID() == providerBuilding->getObjectID();
}

PveBuffProviders::Provider* SimHunterController::providerForStage(
		PveBuffApproachStage stage) {
	if (stage == PVE_BUFF_APPROACH_MUSICIAN)
		return &pveBuffProviders.musician;
	if (stage == PVE_BUFF_APPROACH_DANCER)
		return &pveBuffProviders.dancer;
	if (stage == PVE_BUFF_APPROACH_DOCTOR)
		return &pveBuffProviders.doctor;
	return nullptr;
}

bool SimHunterController::resolvePveProviderStagingPoint(
		PveBuffApproachStage stage, Vector3& stagingPoint) const {
	if (pveBuffProviderPlanet.isEmpty() || pveBuffProviderCity.isEmpty())
		return false;

	Vector3 cantina;
	Vector3 medCenter;
	Vector3 home;
	if (!SimPlayerManager::instance()->getPveHomeLocations(
			pveBuffProviderPlanet, pveBuffProviderCity, cantina, medCenter,
			home))
		return false;

	// The doctor lives in the med center and the entertainers in the cantina,
	// so each stage stages beside its OWN building. Both anchors are the
	// resolver's exterior points (building position pushed out past the
	// bounding radius and height-snapped), which is exactly the point
	// theed_doc_C/D entered from successfully.
	stagingPoint = stage == PVE_BUFF_APPROACH_DOCTOR ? medCenter : cantina;

	// KNOWN LIMITATION: getPveHomeLocations collapses an UNRESOLVED med centre
	// to the shuttle pad, so this cannot distinguish "anchor beside the
	// hospital" from "no hospital in this city". Staging on the pad instead is
	// a benign degradation rather than a regression -- entry from the pad is
	// the one approach already proven to work (harness theed_doc_B PASS) -- and
	// after the F_0.7.3 dual-anchor fix every city that actually has a hospital
	// resolves a real anchor. Surfacing medCenterResolved through this accessor
	// would let the check be exact.
	return !(stagingPoint.getX() == 0.f && stagingPoint.getY() == 0.f);
}

bool SimHunterController::enterPveBuffProviderCell(
		PveBuffApproachStage stage) {
	PveBuffProviders::Provider* provider = providerForStage(stage);
	if (provider == nullptr || !provider->found || provider->cell == nullptr)
		return false;

	// The snapshot was refreshed moments ago in approach(); these providers are
	// stationary screenplay NPCs, so re-querying the world here would add a
	// lock round-trip for no benefit.
	enterStructure(provider->worldPos, provider->localPos,
		provider->cell.get());
	return true;
}

bool SimHunterController::moveToNextPveBuffProvider() {
	ZoneServer* zoneServer = ServerCore::getZoneServer();
	if (zoneServer == nullptr)
		return false;

	// The resolver's OID/cell snapshot is deliberately revalidated immediately
	// before each movement leg. The world query happens before the provider lock;
	// no manager or agent lock is held across that query.
	auto refreshProvider = [&](PveBuffProviders::Provider& provider,
			bool requireDancing, bool requirePlayingMusic) {
		if (!provider.found || provider.oid == 0)
			return false;

		ManagedReference<SceneObject*> object =
			zoneServer->getObject(provider.oid);
		if (object == nullptr || !object->isCreatureObject())
			return false;

		CreatureObject* creature = object->asCreatureObject();
		if (creature == nullptr)
			return false;

		Locker providerLock(creature);
		if (requireDancing && !creature->isDancing())
			return false;
		if (requirePlayingMusic && !creature->isPlayingMusic())
			return false;

		provider.worldPos = creature->getWorldPosition();
		provider.cell = creature->getParent().get().castTo<CellObject*>();
		provider.cellId = provider.cell == nullptr ? 0 :
			provider.cell->getObjectID();
		provider.localPos = provider.cell == nullptr ? provider.worldPos :
			creature->getPosition();
		return true;
	};

	auto approach = [&](PveBuffProviders::Provider& provider,
			PveBuffApproachStage stage, bool requireDancing,
			bool requirePlayingMusic) {
		if (!refreshProvider(provider, requireDancing, requirePlayingMusic)) {
			if (stage == PVE_BUFF_APPROACH_DOCTOR)
				pveDoctorFallbackNeeded = true;
			else if (stage == PVE_BUFF_APPROACH_MUSICIAN ||
					stage == PVE_BUFF_APPROACH_DANCER)
				pveEntertainerFallbackNeeded = true;
			return false;
		}

		pveBuffApproachStage = stage;
		pveBuffProviderApproachActive = true;

		// F_0.8.1 stage 1, RE-APPLIED 2026-08-30. A provider inside a cell is a
		// real BUILDING ENTRY, so drive it through the traversal state machine.
		// moveToInterior only sets interiorApproachLeg and never creates a
		// StructureTraversalIntent, so none of the F_0.8.0 machinery was
		// reachable from here.
		//
		// The first attempt (run 20260829-194002) produced 747 ST_FAILs: a
		// provider in a DIFFERENT building routes through the traversal Egress
		// phase, and cross-building egress failed. That was D2 -- and D2 turned
		// out to be GATED OFF rather than unsolved: the portal-derived exit-set
		// ladder in failCellEgress() sits behind zeroClip.exitSetEnabled, which
		// was false in every run to that point. With it true the matrix went
		// 22 -> 23 PASS and cantina_to_corellia_hospital passed for the first
		// time (run 20260829-215252).
		//
		// Guarded on a non-null cell: an outdoor provider stays an outdoor
		// approach, and a starport hollow is frequently a destination in its own
		// right (owner constraint, 2026-08-27).
		// F_0.7.5: a provider in a DIFFERENT building than the one the bot is
		// standing in is reached by walking OUTDOORS to that building's
		// exterior anchor first, then entering from the doorstep. Measured
		// 2026-08-31: direct cell entry across buildings fails
		// deterministically at Theed (theed_doc_A/E FAIL) while the staged
		// route from the identical start point passes (theed_doc_D PASS).
		// Same-building moves and outdoor providers are untouched.
		bool crossBuilding = provider.cell != nullptr &&
			!isAgentInsideBuildingOfCell(provider.cell.get());
		Vector3 stagingPoint;
		// Gated on traversal too: with the traversal feature off, enterStructure
		// degrades to legacy moveToInterior, and inserting a staging leg in
		// front of THAT would change movement in a configuration this fix was
		// never measured against.
		if (crossBuilding && isStructureTraversalFeatureEnabled() &&
				SimPlayerManager::instance()->
					isPveBuffCrossBuildingStagingEnabled() &&
				resolvePveProviderStagingPoint(stage, stagingPoint)) {
			pveBuffStagingActive = true;
			pveBuffStagingStage = stage;
			moveTo(stagingPoint);
			return true;
		}

		if (provider.cell != nullptr)
			enterStructure(provider.worldPos, provider.localPos,
				provider.cell.get());
		else
			moveToInterior(provider.worldPos, provider.localPos, nullptr);

		return true;
	};

	// Each provider is gated by its OWN stage so approaching it (which sets the
	// stage to that provider) advances past it on the next call. Guarding by the
	// next stage would re-select the same provider forever.
	if (pveNeedEntertainerBuff &&
			pveBuffApproachStage < PVE_BUFF_APPROACH_MUSICIAN &&
			approach(pveBuffProviders.musician,
				PVE_BUFF_APPROACH_MUSICIAN, false, true))
		return true;

	if (pveNeedEntertainerBuff &&
			pveBuffApproachStage < PVE_BUFF_APPROACH_DANCER &&
			approach(pveBuffProviders.dancer,
				PVE_BUFF_APPROACH_DANCER, true, false))
		return true;

	if (pveNeedDoctorBuff &&
			pveBuffApproachStage < PVE_BUFF_APPROACH_DOCTOR &&
			approach(pveBuffProviders.doctor,
				PVE_BUFF_APPROACH_DOCTOR, false, false))
		return true;

	pveBuffApproachStage = PVE_BUFF_APPROACH_COMPLETE;
	pveBuffProviderApproachActive = false;
	return false;
}

void SimHunterController::beginLegacySyntheticBuffDetour() {
	Vector3 cantina;
	Vector3 medCenter;
	Vector3 home;
	if (!SimPlayerManager::instance()->getPveHomeLocations(
		order.homePlanet, order.homeCity, cantina, medCenter, home)) {
		scheduleActiveTick(5000);
		return;
	}

	cantinaArrived = false;
	cantinaDwellComplete = false;
	medCenterReached = false;
	medDwellComplete = false;
	this->cantina = cantina;
	this->medCenter = medCenter;
	dwellUntilMs = 0;
	setPhase(BUFF_UP);
	moveTo(cantina);
}

bool SimHunterController::schedulePveDoctorScreenplay(SceneObject* provider,
		const String& method, const String& args) {
	if (provider == nullptr || method.isEmpty())
		return false;

	Reference<ScreenPlayTask*> task = new ScreenPlayTask(provider, method,
		"SmartDoctorBuffer", args);
	task->schedule(1);
	return true;
}

void SimHunterController::cancelPveDoctorRequest() {
	if (!pveDoctorRequestActive)
		return;

	ManagedReference<AiAgent*> strongHunter = agent;
	uint64 bodyOid = order.bodyOid;
	if (strongHunter != nullptr) {
		Locker agentLock(strongHunter);
		bodyOid = strongHunter->getObjectID();
	}

	String cancelArgs = String::valueOf(bodyOid) + ":" +
		String::valueOf(pveDoctorRequestGen);
	if (pveDoctorProviderObject != nullptr)
		schedulePveDoctorScreenplay(pveDoctorProviderObject.get(), "botCancel",
			cancelArgs);

	pveDoctorRequestActive = false;
	pveDoctorDeadlineSec = 0;
}

void SimHunterController::applyHunterBuffsForFamily(PveBuffFamily family) {
	SimPlayerManager* manager = SimPlayerManager::instance();
	if (!manager->isPveRealBuffsEnabled() ||
			!manager->isPveRealBuffsFallbackSyntheticEnabled())
		return;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	Vector<PveBuffSpec> specs;
	manager->getPveRealBuffFallbackSpecs(specs);
	int thresholdSeconds = manager->getPveRealBuffReapplyThresholdSeconds();
	bool appliedAny = false;
	uint64 bodyOid = 0;

	{
		Locker agentLock(strongAgent);
		bodyOid = strongAgent->getObjectID();
		for (int i = 0; i < specs.size(); ++i) {
			const PveBuffSpec& spec = specs.get(i);
			bool medical = spec.attribute <= CreatureAttribute::STAMINA;
			if ((family == PVE_BUFF_FAMILY_DOCTOR) != medical || spec.crc == 0)
				continue;

			Buff* existing = strongAgent->getBuff(spec.crc);
			if (existing != nullptr &&
					existing->getTimeLeft() >= thresholdSeconds)
				continue;

			strongAgent->removeBuff(spec.crc);
			Reference<Buff*> buff = new Buff(strongAgent.get(), spec.crc,
				spec.durationSeconds, spec.buffType);
			Locker buffLock(buff);
			buff->setAttributeModifier(spec.attribute, spec.modifier);
			buff->setFillAttributesOnBuff(true);
			strongAgent->addBuff(buff);
			appliedAny = true;
		}
	}

	if (appliedAny) {
		manager->recordPveSyntheticFallback(bodyOid);
		manager->recordPveBuffSource(bodyOid,
			"synthetic-fallback");
		if (family == PVE_BUFF_FAMILY_DOCTOR && pveDoctorFeeReserved) {
			manager->commitPlayerBotDoctorBuffFee(identityId);
			pveDoctorFeeReserved = false;
		}
	}
}

void SimHunterController::finishPveBuffProviderFlow() {
	cancelPveDoctorRequest();
	pveBuffProviderApproachActive = false;
	pveBuffInteractionDwellActive = false;
	pveBuffStagingActive = false;
	pveDoctorProviderObject = nullptr;
	pveBuffApproachStage = PVE_BUFF_APPROACH_COMPLETE;
	dwellUntilMs = 0;

	if (pveEntertainerFallbackNeeded)
		applyHunterBuffsForFamily(PVE_BUFF_FAMILY_ENTERTAINER);
	if (pveDoctorFallbackNeeded && pveNeedDoctorBuff)
		applyHunterBuffsForFamily(PVE_BUFF_FAMILY_DOCTOR);

	// The heal-up stop is complete (real providers and/or synthetic fallback).
	// Clear wounds + shock so clone/respawn wounds don't permanently reduce HAM.
	// The legacy synthetic applyHunterBuffs(clearWounds=true) did this each cycle;
	// the no-strip sim-bot doctor path skips the wipe that used to heal them, and
	// the family fallback only replaces buffs (code-review round 1).
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent != nullptr) {
		Locker agentLock(strongAgent);
		for (uint8 pool = CreatureAttribute::HEALTH;
				pool <= CreatureAttribute::WILLPOWER; ++pool)
			strongAgent->setWounds(pool, 0);
		float shock = strongAgent->getShockWounds();
		if (shock > 0)
			strongAgent->addShockWounds(-shock, true, false);
	}
	if (pveDoctorFeeReserved) {
		SimPlayerManager::instance()->releasePlayerBotDoctorBuffFee(identityId);
		pveDoctorFeeReserved = false;
	}

	// A hub provider flow is complete only after the hunter has a route back to
	// the hunt planet. Keep this handoff outside the provider/agent locks; the
	// manager owns the route record and the controller owns the next travel leg.
	if (SimPlayerManager::instance()->isPveBuffHubsEnabled() &&
			pveBuffTripAtHub) {
		if (SimPlayerManager::instance()->beginPveHunterBuffReturn(identityId)) {
			pveBuffTripAtHub = false;
			pveBuffTripReturning = true;
			setPhase(BUFF_TRIP);
			scheduleActiveTick(500);
			return;
		}

		SimPlayerManager::instance()->finishPveHunterBuffTrip(identityId,
			false);
		pveBuffTripAtHub = false;
		pveBuffTripReturning = false;
	}

	if (missionHuntOrder)
		beginMissionTerminalLeg();
	else {
		setPhase(TRAVEL_OUT);
		moveTo(species.huntGround);
	}
}

void SimHunterController::interactWithPveBuffProvider(
		PveBuffApproachStage stage) {
	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<AiAgent*> strongHunter = agent;
	if (zoneServer == nullptr || strongHunter == nullptr) {
		if (stage == PVE_BUFF_APPROACH_DOCTOR)
			pveDoctorFallbackNeeded = true;
		else
			pveEntertainerFallbackNeeded = true;
		finishPveBuffProviderFlow();
		return;
	}

	uint64 providerOid = 0;
	if (stage == PVE_BUFF_APPROACH_MUSICIAN)
		providerOid = pveBuffProviders.musician.oid;
	else if (stage == PVE_BUFF_APPROACH_DANCER)
		providerOid = pveBuffProviders.dancer.oid;
	else if (stage == PVE_BUFF_APPROACH_DOCTOR)
		providerOid = pveBuffProviders.doctor.oid;

	ManagedReference<SceneObject*> providerObject =
		zoneServer->getObject(providerOid);
	if (providerObject == nullptr || !providerObject->isCreatureObject()) {
		if (stage == PVE_BUFF_APPROACH_DOCTOR)
			pveDoctorFallbackNeeded = true;
		else
			pveEntertainerFallbackNeeded = true;
		if (moveToNextPveBuffProvider())
			return;
		finishPveBuffProviderFlow();
		return;
	}

	float approachRange = SimPlayerManager::instance()->
		getPveProviderApproachRangeMeters();
	Vector3 providerPosition;
	bool providerDancing = false;
	bool providerPlayingMusic = false;
	CreatureObject* providerCreature = providerObject->asCreatureObject();
	if (providerCreature == nullptr) {
		if (stage == PVE_BUFF_APPROACH_DOCTOR)
			pveDoctorFallbackNeeded = true;
		else
			pveEntertainerFallbackNeeded = true;
		if (moveToNextPveBuffProvider())
			return;
		finishPveBuffProviderFlow();
		return;
	}
	{
		Locker providerLock(providerCreature);
		providerPosition = providerCreature->getWorldPosition();
		providerDancing = providerCreature->isDancing();
		providerPlayingMusic = providerCreature->isPlayingMusic();
	}

	Vector3 hunterPosition;
	uint64 bodyOid = 0;
	{
		Locker hunterLock(strongHunter);
		hunterPosition = strongHunter->getWorldPosition();
		bodyOid = strongHunter->getObjectID();
	}
	float dx = hunterPosition.getX() - providerPosition.getX();
	float dy = hunterPosition.getY() - providerPosition.getY();
	float dz = hunterPosition.getZ() - providerPosition.getZ();
	float distanceSquared = dx * dx + dy * dy + dz * dz;
	if (distanceSquared > approachRange * approachRange ||
			(stage == PVE_BUFF_APPROACH_MUSICIAN && !providerPlayingMusic) ||
			(stage == PVE_BUFF_APPROACH_DANCER && !providerDancing)) {
		if (stage == PVE_BUFF_APPROACH_DOCTOR)
			pveDoctorFallbackNeeded = true;
		else
			pveEntertainerFallbackNeeded = true;
		if (moveToNextPveBuffProvider())
			return;
		finishPveBuffProviderFlow();
		return;
	}

	// onArrived holds no agent lock across this point. Keep strong references
	// alive while PlayerManager takes its own creature/entertainer locks.
	if (stage == PVE_BUFF_APPROACH_MUSICIAN) {
		PlayerManager* playerManager = zoneServer->getPlayerManager();
		if (playerManager == nullptr) {
			pveEntertainerFallbackNeeded = true;
		} else {
			playerManager->startListen(strongHunter.get(), providerOid);
			SimPlayerManager::instance()->recordPveMusicianListen();
		}
		pveBuffInteractionDwellActive = true;
		dwellUntilMs = System::getMiliTime() +
			static_cast<uint64>(SimPlayerManager::instance()->
				getPveEntertainerDwellMs());
		scheduleActiveTick(SimPlayerManager::instance()->
			getPveEntertainerDwellMs());
		return;
	}

	if (stage == PVE_BUFF_APPROACH_DANCER) {
		PlayerManager* playerManager = zoneServer->getPlayerManager();
		if (playerManager == nullptr) {
			pveEntertainerFallbackNeeded = true;
		} else {
			playerManager->startWatch(strongHunter.get(), providerOid);
			SimPlayerManager::instance()->recordPveDancerWatch();
		}
		pveBuffInteractionDwellActive = true;
		dwellUntilMs = System::getMiliTime() +
			static_cast<uint64>(SimPlayerManager::instance()->
				getPveEntertainerDwellMs());
		scheduleActiveTick(SimPlayerManager::instance()->
			getPveEntertainerDwellMs());
		return;
	}

	if (stage != PVE_BUFF_APPROACH_DOCTOR) {
		finishPveBuffProviderFlow();
		return;
	}

	pveDoctorProviderObject = providerObject;

	ChatManager* chatManager = zoneServer->getChatManager();
	if (chatManager != nullptr)
		chatManager->broadcastChatMessage(strongHunter.get(),
			UnicodeString("I need a buff."));

	++pveDoctorRequestGen;
	if (pveDoctorRequestGen == 0)
		pveDoctorRequestGen = 1;

	uint64 deadlineSec = System::getMiliTime() / 1000;
	uint64 timeoutSec = Math::max(1,
		SimPlayerManager::instance()->getPveDoctorInteractionTimeoutMs() / 1000);
	deadlineSec += timeoutSec;
	pveDoctorDeadlineSec = deadlineSec;
	pveDoctorRequestActive = true;

	String args = String::valueOf(bodyOid) + ":" +
		String::valueOf(pveDoctorRequestGen) + ":" +
		String::valueOf(deadlineSec);
	if (!schedulePveDoctorScreenplay(providerObject.get(), "botBuffRequest",
			args)) {
		pveDoctorRequestActive = false;
		pveDoctorFallbackNeeded = pveNeedDoctorBuff;
		if (moveToNextPveBuffProvider())
			return;
		finishPveBuffProviderFlow();
		return;
	}

	SimPlayerManager::instance()->recordPveDoctorInteraction();
	scheduleActiveTick(1000);
}

bool SimHunterController::beginMarketRelocationLeg() {
	if (!orderActive || order.marketTargetPlanet.isEmpty() || agent == nullptr)
		return false;
	Zone* zone = agent->getZone();
	if (zone == nullptr || zone->getZoneName() == order.marketTargetPlanet)
		return false;

	SimPlayerManager::PveRelocation relocation;
	if (!SimPlayerManager::instance()->getPveHunterRelocation(identityId,
			relocation) || relocation.legIndex < 0 || relocation.legIndex >=
			relocation.legs.size())
		return false;
	if (isInterplanetaryTravelActive())
		return true;

	const SimPlayerManager::SimTravelLeg& leg = relocation.legs.get(
		relocation.legIndex);
	String travelResult;
	if (!beginInterplanetaryTravel(leg.destPlanet, leg.departurePos,
			leg.arrivalPos, leg.destCity, 0.f, travelResult)) {
		SimPlayerManager::instance()->finishPveHunterRelocation(identityId,
			false, zone->getZoneName());
		return false;
	}
	setPhase(RELOCATING);
	return true;
}

bool SimHunterController::beginBuffTripLeg() {
	if (!orderActive || agent == nullptr)
		return false;

	SimPlayerManager* manager = SimPlayerManager::instance();
	SimPlayerManager::PveBuffTrip trip;
	if (!manager->getPveHunterBuffTrip(identityId, trip))
		return false;

	if (isInterplanetaryTravelActive())
		return true;

	if (trip.legIndex < 0 || trip.legIndex >= trip.legs.size()) {
		if (!trip.returning) {
			pveBuffTripAtHub = true;
			pveBuffTripReturning = false;
			setPhase(BUFF_UP);
			beginBuffUp();
			return true;
		}

		manager->finishPveHunterBuffTrip(identityId, true);
		pveBuffTripAtHub = false;
		pveBuffTripReturning = false;
		resumeAfterBuffTrip();
		return true;
	}

	if (!canBeginInterplanetaryTravel()) {
		manager->finishPveHunterBuffTrip(identityId, false);
		if (trip.returning) {
			pveBuffTripReturning = false;
			resumeAfterBuffTrip();
		} else {
			pveBuffTripAtHub = false;
			pveBuffTripReturning = false;
			pveDoctorFallbackNeeded = pveNeedDoctorBuff;
			pveEntertainerFallbackNeeded = pveNeedEntertainerBuff;
			finishPveBuffProviderFlow();
		}
		return false;
	}

	const SimPlayerManager::SimTravelLeg& leg = trip.legs.get(
		trip.legIndex);
	String travelResult;
	if (!beginInterplanetaryTravel(leg.destPlanet, leg.departurePos,
			leg.arrivalPos, leg.destCity, 0.f, travelResult)) {
		manager->finishPveHunterBuffTrip(identityId, false);
		if (trip.returning) {
			pveBuffTripReturning = false;
			resumeAfterBuffTrip();
		} else {
			pveBuffTripAtHub = false;
			pveBuffTripReturning = false;
			pveDoctorFallbackNeeded = pveNeedDoctorBuff;
			pveEntertainerFallbackNeeded = pveNeedEntertainerBuff;
			finishPveBuffProviderFlow();
		}
		return false;
	}

	setPhase(BUFF_TRIP);
	return true;
}

void SimHunterController::resumeAfterBuffTrip() {
	if (!orderActive)
		return;

	if (missionHuntOrder)
		beginMissionTerminalLeg();
	else {
		setPhase(TRAVEL_OUT);
		moveTo(species.huntGround);
	}
}

void SimHunterController::beginBuffUp() {
	if (SimPlayerManager::instance()->isPveMarketDispatchEnabled() &&
			!order.marketTargetPlanet.isEmpty() &&
			beginMarketRelocationLeg())
		return;

	if (SimPlayerManager::instance()->isPveRealBuffsEnabled()) {
		bool needDoctor = false;
		bool needEntertainer = false;
		computeBuffNeeds(needDoctor, needEntertainer);
		pveDoctorFeeReserved = false;
		if (needDoctor) {
			PlayerBotBuffFee feeState = SimPlayerManager::instance()->
				reservePlayerBotDoctorBuffFee(identityId);
			if (feeState == PLAYER_BOT_BUFF_FEE_UNAFFORDABLE)
				needDoctor = false;
			else if (feeState == PLAYER_BOT_BUFF_FEE_RESERVED)
				pveDoctorFeeReserved = true;
		}
		if (!needDoctor && !needEntertainer) {
			SimPlayerManager::instance()->recordPveBuffDetourSkipped();
			if (missionHuntOrder)
				beginMissionTerminalLeg();
			else {
				setPhase(TRAVEL_OUT);
				moveTo(species.huntGround);
			}
			return;
		}

		pveNeedDoctorBuff = needDoctor;
		pveNeedEntertainerBuff = needEntertainer;
		pveDoctorFallbackNeeded = false;
		pveEntertainerFallbackNeeded = false;
		pveBuffProviders = PveBuffProviders();
		pveBuffApproachStage = PVE_BUFF_APPROACH_NONE;
		pveBuffStagingActive = false;
		pveBuffStagingStage = PVE_BUFF_APPROACH_NONE;
		// Default to the home city; the buff-hub branch below overwrites this
		// with the hub it actually resolved providers against.
		pveBuffProviderPlanet = order.homePlanet;
		pveBuffProviderCity = order.homeCity;
		pveBuffProviderApproachActive = false;
		pveBuffInteractionDwellActive = false;
		pveDoctorRequestActive = false;
		pveDoctorDeadlineSec = 0;

		SimPlayerManager* manager = SimPlayerManager::instance();
		PveBuffProviders providers;
		if (manager->isPveBuffHubsEnabled()) {
			String providerPlanet;
			String providerCity;
			bool providerLocationResolved = false;
			if (pveBuffTripAtHub) {
				SimPlayerManager::PveBuffTrip trip;
				if (manager->getPveHunterBuffTrip(identityId, trip)) {
					providerPlanet = trip.hubPlanet;
					providerCity = trip.hubCity;
					providerLocationResolved = true;
				}
			} else {
				SimPlayerManager::ShuttleportLocation currentCity;
				providerLocationResolved =
					manager->getPveHunterCurrentRoutableCity(
						agent == nullptr ? 0 : agent->getObjectID(),
						providerPlanet, currentCity);
				if (providerLocationResolved)
					providerCity = currentCity.name;
			}

			if (!providerLocationResolved) {
				pveDoctorFallbackNeeded = needDoctor;
				pveEntertainerFallbackNeeded = needEntertainer;
				finishPveBuffProviderFlow();
				return;
			}

			pveBuffProviderPlanet = providerPlanet;
			pveBuffProviderCity = providerCity;
			if (!manager->resolvePveBuffProviders(providerPlanet, providerCity,
					providers)) {
				if (providers.pending) {
					scheduleActiveTick(5000);
					return;
				}
				pveDoctorFallbackNeeded = needDoctor;
				pveEntertainerFallbackNeeded = needEntertainer;
				finishPveBuffProviderFlow();
				return;
			}

			bool hasDoctor = !needDoctor || providers.doctor.found;
			bool hasEntertainer = !needEntertainer ||
				(providers.musician.found && providers.dancer.found);
			if (!pveBuffTripAtHub && (!hasDoctor || !hasEntertainer) &&
					pveBuffTripsThisHunt < manager->getPveMaxBuffTripsPerHunt() &&
					canBeginInterplanetaryTravel()) {
				SimPlayerManager::PveBuffTrip trip;
				if (manager->planPveHunterBuffTrip(identityId,
						agent == nullptr ? 0 : agent->getObjectID(), needDoctor,
						needEntertainer, trip)) {
					++pveBuffTripsThisHunt;
					setPhase(BUFF_TRIP);
					scheduleActiveTick(100);
					return;
				}
			}
		} else if (!manager->resolvePveBuffProviders(
				order.homePlanet, order.homeCity, providers)) {
			if (providers.pending) {
				scheduleActiveTick(5000);
				return;
			}
			pveDoctorFallbackNeeded = needDoctor;
			pveEntertainerFallbackNeeded = needEntertainer;
			finishPveBuffProviderFlow();
			return;
		}

		pveBuffProviders = providers;
		pveDoctorFallbackNeeded = needDoctor && !providers.doctor.found;
		pveEntertainerFallbackNeeded = needEntertainer &&
			(!providers.musician.found || !providers.dancer.found);
		setPhase(BUFF_UP);
		if (moveToNextPveBuffProvider())
			return;

		finishPveBuffProviderFlow();
		return;
	}

	Vector3 cantina;
	Vector3 medCenter;
	Vector3 home;
	if (!SimPlayerManager::instance()->getPveHomeLocations(
		order.homePlanet, order.homeCity, cantina, medCenter, home)) {
		// The identity's city was validated when the assignment was made. A
		// transient city scan miss should retry, not strand the contract.
		scheduleActiveTick(5000);
		return;
	}

	cantinaArrived = false;
	cantinaDwellComplete = false;
	medCenterReached = false;
	medDwellComplete = false;
	this->cantina = cantina;
	this->medCenter = medCenter;
	dwellUntilMs = 0;
	setPhase(BUFF_UP);
	moveTo(cantina);
}

void SimHunterController::applyHunterBuffs(bool clearWounds) {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	Vector<PveBuffSpec> buffs;
	SimPlayerManager::instance()->getPveHunterBuffs(buffs);

	Locker agentLock(strongAgent);
	if (clearWounds) {
		for (uint8 pool = CreatureAttribute::HEALTH;
				pool <= CreatureAttribute::WILLPOWER; ++pool)
			strongAgent->setWounds(pool, 0);
	}

	for (int i = 0; i < buffs.size(); ++i) {
		const PveBuffSpec& spec = buffs.get(i);
		if (spec.crc == 0)
			continue;

		// Refreshes replace the prior effect before applying the new modifier;
		// this prevents repeated mission ticks from stacking the same buff.
		strongAgent->removeBuff(spec.crc);
		Reference<Buff*> buff = new Buff(strongAgent.get(), spec.crc,
			spec.durationSeconds, spec.buffType);
		// @preLocked Buff contract: construct under the agent lock, lock the
		// Buff before touching modifiers, and retain that lock through addBuff.
		Locker buffLock(buff);
		buff->setAttributeModifier(spec.attribute, spec.modifier);
		buff->setFillAttributesOnBuff(true);
		strongAgent->addBuff(buff);
	}
}

void SimHunterController::beginMissionTerminalLeg() {
	if (!orderActive || !missionHuntOrder || agent == nullptr)
		return;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;
	Vector3 currentPosition;
	uint64 bodyOid = 0;
	{
		Locker agentLock(strongAgent);
		currentPosition = strongAgent->getWorldPosition();
		bodyOid = strongAgent->getObjectID();
	}

	PveMissionTerminalLocation terminal;
	int cityState = PVE_MISSION_TERMINAL_PENDING;
	SimPlayerManager* manager = SimPlayerManager::instance();
	bool boardEnabled = manager->isPveMissionBoardEnabled();
	String currentPlanet;
	if (boardEnabled) {
		Zone* currentZone = strongAgent->getZone();
		currentPlanet = currentZone == nullptr ? String() :
			currentZone->getZoneName();
	}
	bool resolved = boardEnabled ? manager->getNearestGeneralMissionTerminal(
		currentPlanet, currentPosition, terminal, cityState) :
		manager->getNearestMissionTerminal(order.homePlanet, order.homeCity,
			currentPosition, terminal, cityState);

	if (resolved) {
		missionTerminalPosition = terminal.position;
		missionTerminalResolved = true;
		missionTerminalFallback = false;
		manager->recordPveHunterMissionTerminal(
			identityId, bodyOid, terminal.planet, terminal.city,
			terminal.position, terminal.terminalType, terminal.terminalOid);
		// Publish the resolved terminal onto the ORDER. beginMissionAccept()
		// rebuilds its PveMissionTerminalLocation from these fields, and nothing
		// else ever wrote them -- they sat at their defaults (oid 0, empty
		// strings, zero position), so generatePveBotMissionOffers() rejected every
		// single contract on its `terminal.terminalOid == 0` entry gate. The
		// hunter resolved a terminal, walked to it and arrived, and the board was
		// then asked about a terminal that did not exist. Position matters too:
		// choosePveBotMissionPosition() takes the terminal->city-centre bearing
		// from it, so a zero position would aim the spawn arc at the map origin.
		order.missionTerminalPlanet = terminal.planet;
		order.missionTerminalCity = terminal.city;
		order.missionTerminalType = terminal.terminalType;
		order.missionTerminalOid = terminal.terminalOid;
		order.missionTerminalPosition = terminal.position;

		setPhase(TRAVEL_TO_TERMINAL);
		// NOTE the guard: if the controller is still MOVING/CALCULATING_PATH from
		// a previous leg (e.g. the ticket-collector arrival egress that just ran),
		// the terminal move is SKIPPED and this phase then rides on whatever the
		// stale movement does next. movedIssued distinguishes the two cases.
		bool moveIssued = state != MOVING && state != CALCULATING_PATH;
		MissionDiagLog::event("TERM_RESOLVED", identityId, "planet=" +
			terminal.planet + " city=" + terminal.city +
			" type=" + terminal.terminalType +
			" terminalOid=" + String::valueOf(terminal.terminalOid) +
			" dist=" + String::valueOf(
				currentPosition.distanceTo2d(terminal.position)) +
			" state=" + getDiagnosticStateName() +
			" moveIssued=" + String::valueOf(moveIssued) +
			" waitCycles=" + String::valueOf(terminalResolveWaitCycles));
		// Arm the recast failure probe so TERM_PATH_FAILED can report which
		// polygon lookup failed. Default-off with mission diagnostics; the probe
		// only records, it never changes a route.
		setHybridFailureProbeEnabled(MissionDiagLog::isLoggingEnabled());
		if (moveIssued)
			moveTo(missionTerminalPosition);
		armTravelCombatTick();
		return;
	}

	if (cityState == PVE_MISSION_TERMINAL_ABSENT ||
			terminalResolveWaitCycles >= SimPlayerManager::instance()->
			getPveMissionTerminalResolveWaitCycles()) {
		MissionDiagLog::event("TERM_GIVEUP", identityId, "planet=" +
			(boardEnabled ? currentPlanet : order.homePlanet) +
			" cityState=" + String::valueOf(cityState) +
			" waitCycles=" + String::valueOf(terminalResolveWaitCycles) +
			" maxWaitCycles=" + String::valueOf(SimPlayerManager::instance()->
				getPveMissionTerminalResolveWaitCycles()));
		beginMissionFallback();
		return;
	}

	MissionDiagLog::event("TERM_PENDING", identityId, "planet=" +
		(boardEnabled ? currentPlanet : order.homePlanet) +
		" cityState=" + String::valueOf(cityState) +
		" waitCycles=" + String::valueOf(terminalResolveWaitCycles));

	missionTerminalResolved = false;
	missionTerminalFallback = false;
	++terminalResolveWaitCycles;
	manager->recordPveHunterMissionTerminal(identityId, bodyOid,
			boardEnabled ? currentPlanet : order.homePlanet,
			boardEnabled ? String() : order.homeCity, Vector3(), "", 0);
	setPhase(TRAVEL_TO_TERMINAL);
	scheduleActiveTick(2000);
}

void SimHunterController::beginMissionFallback() {
	MissionDiagLog::event("TERM_FALLBACK", identityId, "phase=" +
		getPhaseName() + " boardEnabled=" + String::valueOf(
			SimPlayerManager::instance()->isPveMissionBoardEnabled()));
	missionTerminalFallback = true;
	missionTerminalResolved = false;
	SimPlayerManager::instance()->recordPveHunterMissionTerminal(
		identityId, agent == nullptr ? 0 : agent->getObjectID(),
		order.homePlanet, order.homeCity, Vector3());
	if (SimPlayerManager::instance()->isPveMissionBoardEnabled()) {
		beginMissionCleanup(true, "mission_terminal_unavailable");
		return;
	}
	spawnMissionLair();
}

void SimHunterController::beginMissionAccept() {
	if (!orderActive || !missionHuntOrder)
		return;

	SimPlayerManager* manager = SimPlayerManager::instance();
	if (manager->isPveMissionBoardEnabled() && !missionOffersGenerated) {
		ManagedReference<AiAgent*> strongAgent = agent;
		if (strongAgent == nullptr)
			return;
		// C4: fresh terminal visit (attempt 0) opens a visit epoch under pveMutex,
		// which also verifies an active order for this body and drops any stale
		// board. Epoch 0 => no active order (concluded under us) -> abandon.
		// Retries (attempts > 0) reuse the captured epoch, never re-reading it.
		if (missionOfferAttempts == 0) {
			missionOfferVisitEpoch = manager->openTerminalVisit(
				identityId, strongAgent->getObjectID());
			if (missionOfferVisitEpoch == 0) {
				beginMissionCleanup(true, "mission_offers_unavailable");
				return;
			}
		}
		Vector<PveBotMissionOffer> offers;
		PveMissionTerminalLocation terminal;
		terminal.planet = order.missionTerminalPlanet;
		terminal.city = order.missionTerminalCity;
		terminal.terminalType = order.missionTerminalType;
		terminal.terminalOid = order.missionTerminalOid;
		terminal.position = order.missionTerminalPosition;
		// generatePveBotMissionOffers is no longer all-or-nothing: it commits and
		// returns however many offers it could place (0, 1, or the full 2), and
		// on a zero result it preserves any partial already committed this visit.
		// It commits ONLY if the visit epoch still matches (order still active).
		manager->generatePveBotMissionOffers(identityId,
			strongAgent->getObjectID(), terminal,
			manager->getPveBotHunterLevel(strongAgent), offers,
			missionOfferVisitEpoch);
		int have = offers.size();
		int want = manager->getPveMissionBoardMaxHeldMissions();
		++missionOfferAttempts;
		int maxAttempts = manager->getPveMissionBoardOfferMaxAttempts();

		if (have < want && missionOfferAttempts < maxAttempts) {
			// Partial or empty board with retries left: dwell at the terminal and
			// re-generate, rather than abandoning the whole trip. The ACCEPT_MISSION
			// tick re-enters beginMissionAccept() while missionOffersGenerated is
			// still false. This is the fix for the dominant mission_offers_unavailable
			// churn -- ~55% of empty boards already had one valid offer that the old
			// all-or-nothing rule discarded.
			MissionDiagLog::event("OFFERS_RETRY", identityId, "have=" +
				String::valueOf(have) + " want=" + String::valueOf(want) +
				" attempt=" + String::valueOf(missionOfferAttempts) +
				" maxAttempts=" + String::valueOf(maxAttempts));
			setPhase(ACCEPT_MISSION);
			dwellUntilMs = System::getMiliTime() +
				static_cast<uint64>(manager->
					getPveMissionBoardOfferRetrySeconds()) * 1000;
			scheduleActiveTick((int)Math::max(100,
				(int)(dwellUntilMs - System::getMiliTime())));
			return;
		}

		if (have == 0) {
			// Retry budget exhausted and still nothing placeable here.
			beginMissionCleanup(true, "mission_offers_unavailable");
			return;
		}

		// Accept what we have -- a full board (2) or a single offer after the
		// retries could not fill the second slot.
		MissionDiagLog::event("OFFERS_ACCEPT", identityId, "have=" +
			String::valueOf(have) + " want=" + String::valueOf(want) +
			" attempts=" + String::valueOf(missionOfferAttempts));
		missionOffersGenerated = true;
		missionOfferIds.removeAll();
		for (int i = 0; i < offers.size(); ++i) {
			missionOfferIds.add(offers.get(i).offerId);
			manager->announcePveHunterEvent(strongAgent->getObjectID(),
				species.key, "Offer " + String::valueOf(i + 1) + " is ready.");
		}
	}

	setPhase(ACCEPT_MISSION);
	SimPlayerManager::instance()->announcePveHunterEvent(
		agent == nullptr ? 0 : agent->getObjectID(), species.key,
		"Signed on for a " + species.harvestKind + " hunting contract.");
	dwellUntilMs = System::getMiliTime() +
		static_cast<uint64>(SimPlayerManager::instance()->
			getPveMissionTerminalDwellSeconds()) * 1000;
	scheduleActiveTick(1000);
}

bool SimHunterController::beginNextMissionOffer() {
	if (!orderActive || !missionHuntOrder || !missionOffersGenerated)
		return false;

	Vector<PveBotMissionOffer> pending;
	for (int offerIndex = 0; offerIndex < missionOfferIds.size();
			++offerIndex) {
		uint64 offerId = missionOfferIds.get(offerIndex);
		PveBotMissionOffer offer;
		if (!SimPlayerManager::instance()->getPveBotMissionOffer(identityId,
				offerId, offer))
			continue;
		if (!offer.completed && !offer.revealed) {
			pending.add(offer);
			if (pending.size() >= SimPlayerManager::instance()->
					getPveMissionBoardMaxHeldMissions())
				break;
		}
	}
	if (pending.size() == 0)
		return false;

	currentMissionOfferId = pending.get(0).offerId;
	missionLairPosition = pending.get(0).advertisedPos;
	missionLairOid = 0;
	missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
	missionAbandonReason = "";
	updateMissionAdds(0);
	setPhase(TRAVEL_TO_MISSION);
	setHybridFailureProbeEnabled(MissionDiagLog::isLoggingEnabled());
	moveTo(missionLairPosition);
	armTravelCombatTick();
	return true;
}

void SimHunterController::spawnMissionLair() {
	if (!orderActive || !missionHuntOrder || agent == nullptr)
		return;

	dwellUntilMs = 0;
	Vector3 missionAnchor = missionTerminalFallback ? species.huntGround :
		missionTerminalPosition;
	if (!SimPlayerManager::instance()->spawnPveHuntLair(
			agent->getObjectID(), species, missionAnchor)) {
		beginMissionCleanup(true, "lair_spawn_failed");
		return;
	}

	PveHuntLair lair;
	if (!SimPlayerManager::instance()->getPveHuntLair(
			agent->getObjectID(), lair) || lair.lairOid == 0) {
		beginMissionCleanup(true, "lair_record_missing");
		return;
	}

	missionLairOid = lair.lairOid;
	missionLairPosition = Vector3(lair.x, lair.y, lair.z);
	// Publish at once: the arrival loop can self-defend before the next tick.
	updateAreaAttackGuard();
	missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
	updateMissionAdds(0);
	setPhase(TRAVEL_TO_LAIR);
	SimPlayerManager::instance()->announcePveHunterEvent(
		agent->getObjectID(), species.key,
		"Moving out to the " + species.harvestKind + " lair.");
	moveTo(missionLairPosition);
	armTravelCombatTick();
}

void SimHunterController::beginMissionCleanup(bool abandoned,
		const String& reason) {
	if (!orderActive || !missionHuntOrder)
		return;

	if (missionLairOid != 0)
		SimPlayerManager::instance()->recordPveHunterMissionEnd(
			agent == nullptr ? 0 : agent->getObjectID(), missionLairOid, reason);

	// The REAL abandon cause. It used to be lost downstream: completeOrder
	// overwrote it with the generic "abandoned" (reached home) or
	// "path_failed_TRAVEL_HOME" (didn't), so the dashboard abandonReasons never
	// showed WHY the mission was dropped. It is now retained in
	// missionAbandonReason and passed through, so this log and the dashboard
	// agree.
	MissionDiagLog::event("MISSION_CLEANUP", identityId, "abandoned=" +
		String::valueOf(abandoned) + " reason=" + reason +
		" phase=" + getPhaseName() +
		" lairAlive=" + String::valueOf(missionLairOid != 0) +
		" planet=" + (agent == nullptr || agent->getZone() == nullptr ?
			String("?") : agent->getZone()->getZoneName()));

	orderAbandoned = abandoned;
	if (abandoned)
		missionAbandonReason = reason;
	// Close the resume gate BEFORE disengaging so an arrival tick that acquires
	// the agent lock right after disengage cannot revive movement toward the
	// lair being torn down (code-review round 2).
	missionCleanupRequested = true;
	dropTargetObserver();
	disengageMissionLair();
	disengageTarget(false);
	targetOid = 0;
	targetMissionWave = false;
	missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
	updateMissionAdds(0);
	setPhase(MISSION_CLEANUP);
	SimPlayerManager::instance()->requestPveHuntLairCleanup(
		agent == nullptr ? 0 : agent->getObjectID(), reason);
	scheduleActiveTick(250);
}

void SimHunterController::continueAfterMissionCleanup() {
	if (!orderActive || !missionHuntOrder)
		return;

	PveHuntLair lair;
	if (SimPlayerManager::instance()->getPveHuntLair(
			agent == nullptr ? 0 : agent->getObjectID(), lair)) {
		scheduleActiveTick(500);
		return;
	}

	missionLairOid = 0;
	missionCleanupRequested = false;
	beginTravelHome(orderAbandoned);
}

bool SimHunterController::checkMissionSocialAggro(AiAgent* hunter) {
	if (!missionHuntOrder || hunter == nullptr || targetOid == 0)
		return false;

	CreatureObject* target = nullptr;
	AiAgent* targetAgent = nullptr;
	if (!targetIsLive(targetOid, target, targetAgent)) {
		missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
		return false;
	}

	const CreatureTemplate* targetTemplate = targetAgent->getCreatureTemplate();
	bool socialTarget = !targetAgent->getSocialGroup().isEmpty() ||
		(targetTemplate != nullptr && targetTemplate->isHerd());
	// Snapshot the defender OIDs under the hunter lock and count outside it:
	// add/removeDefender are @preLocked, so iterating the live DeltaVector
	// unlocked races a concurrent mutation. Same choreography as
	// findUnwinnableEarlyGameAttacker (:2141) and :4181. The lock is released
	// before lairObserverSnapshot, which takes pveMutex -- that mutex is never
	// nested inside an agent lock.
	Vector<uint64> defenderOids;
	{
		Locker agentLock(hunter);
		const DeltaVector<ManagedReference<SceneObject*> >* defenders =
			hunter->getDefenderList();
		if (defenders != nullptr) {
			for (int i = 0; i < defenders->size(); ++i) {
				ManagedReference<SceneObject*> defender = defenders->getSafe(i);
				if (defender != nullptr)
					defenderOids.add(defender->getObjectID());
			}
		}
	}

	SimPlayerManager* manager = SimPlayerManager::instance();
	PveHuntLair lair;
	bool missionLairLive = manager->getPveHuntLair(hunter->getObjectID(), lair) &&
		lair.alive && !lair.cleanupQueued && lair.lairOid == missionLairOid;
	Vector<ManagedReference<CreatureObject*> > lairMembers;
	if (missionLairLive)
		lairMembers = manager->lairObserverSnapshot(hunter->getObjectID(),
			missionLairOid);

	// While a mission lair is live, lair members are excluded from the cap
	// entirely: a wave is 2-5 creatures against a cap of 3, so counting them at
	// all reproduces the abandonment this fix exists to remove. The cap's job on
	// a mission is to notice UNRELATED attackers piling on.
	int adds = 0;
	for (int i = 0; i < defenderOids.size(); ++i) {
		uint64 defenderOid = defenderOids.get(i);
		bool isLairMember = false;
		if (missionLairLive) {
			for (int j = 0; j < lairMembers.size(); ++j) {
				if (lairMembers.get(j) != nullptr &&
						lairMembers.get(j)->getObjectID() == defenderOid) {
					isLairMember = true;
					break;
				}
			}
		}
		if (!isLairMember)
			++adds;
	}
	updateMissionAdds(adds);

	if (missionLairLive && manager->isPveMissionLairTacticsEnabled()) {
		missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
		return false;
	}

	if (!socialTarget || adds <= manager->
		getPveMissionMaxSimultaneousAdds()) {
		missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
		return false;
	}

	++missionAddsOverCapCycles;
	if (missionAddsOverCapCycles >= SimPlayerManager::instance()->
		getPveMissionAddsAbandonCycles()) {
		beginMissionCleanup(true, "social_adds_above_cap");
		return true;
	}

	if (SimPlayerManager::instance()->isPveMissionBoardEnabled()) {
		// A mission wave boundary is a break-off, not a retreat-cycle
		// abandonment. Keep the same target/observer so the next HUNTING pass
		// can resume this wave, but do not let the generic retreat cap fire
		// before addsAbandonCycles has elapsed.
		MissionDiagLog::event("RETREATING", identityId,
			"reason=social_adds_cap lairOid=" +
			String::valueOf(missionLairOid) + " adds=" +
			String::valueOf(adds));
		setPhase(RETREATING);
		// A new root retreat: it gets its own budget of re-flee legs.
		lairRetreatReflees = 0;
		Vector3 current = hunter->getWorldPosition();
		Vector3 away = current;
		Vector3 direction = current - target->getWorldPosition();
		direction.setZ(0.f);
		if (direction.length2d() < 0.01f)
			direction = Vector3::UNIT_X;
		else
			direction.normalize();
		away = current + direction * SimPlayerManager::instance()->
			getPveHunterRetreatRangeMeters();
		disengageTarget(false);
		updateMissionAdds(0);
		moveTo(away);
		// Same as beginRetreat: without this the tick sleeps until an arrival
		// that a bot held in combat never reaches.
		if (isLairRetreatDefenseOn())
			scheduleActiveTick(1000);
	} else {
		beginRetreat();
	}
	return true;
}

void SimHunterController::updateMissionAdds(int adds) {
	adds = Math::max(0, adds);
	if (adds == missionAddsEngaged)
		return;

	missionAddsEngaged = adds;
	SimPlayerManager::instance()->recordPveHunterMissionAdds(
		identityId, agent == nullptr ? 0 : agent->getObjectID(), adds);
}

SimHunterController::LootWalkCallback
SimHunterController::classifyLootWalkCallback() {
	// Serialized with finishPlayerBotLootStep: either the step is still active,
	// or the callback was dispatched under a generation the finished corpse walk
	// used. Either way it belongs to the loot walk, not to the phase.
	Locker lootArrivalLock(&lootArrivalMutex);
	const uint64 dispatched = getCallbackDispatchGeneration();
	// While the step is active the corpse walk owns movement: a callback from
	// before it began refers to movement the walk already replaced.
	if (lootStepActive.load(std::memory_order_acquire))
		return dispatched >= lootWalkFirstGeneration ? LOOT_CALLBACK_CURRENT :
			LOOT_CALLBACK_STALE;
	// After a finish, everything dispatched before its generation advance refers
	// to movement the loot step superseded: the corpse walk and the leg it
	// interrupted alike. The resumed leg and later moves are all >= end.
	if (lootWalkEndGeneration != 0 && dispatched < lootWalkEndGeneration)
		return LOOT_CALLBACK_STALE;
	return LOOT_CALLBACK_NOT_LOOT;
}

void SimHunterController::onArrived() {
	if (agent == nullptr)
		return;

	// F_0.9.3: a corpse-walk arrival ends the loot walk only; checked before
	// every other branch (flee legs included). The tick loots and resumes.
	if (classifyLootWalkCallback() != LOOT_CALLBACK_NOT_LOOT) {
		scheduleActiveTick(0);
		return;
	}

	// Cross-planet legs (RELOCATING / BUFF_TRIP) run on the SHARED ticket-collector
	// state machine, and this callback is its ONLY advance point: the 2->3
	// (TICKET_DEPARTURE_ENTRY -> TICKET_DEPARTURE_COLLECTOR) transition and every
	// later one live inside handleInterplanetaryTravelArrival(). Without this
	// dispatch the hunter walked into the starport cell, went MOVING -> WAITING,
	// and sat in TICKET_DEPARTURE_ENTRY until the 900s phase timeout while
	// runActiveTick() spun on isInterplanetaryTravelActive() forever (observed
	// live: no ev=ARRIVED in traveldiag.log across 250+ ticks). SimMinerController
	// makes the identical call as its first statement; the handler is a no-op
	// returning false whenever no travel is in flight, so the phase branches below
	// are unaffected.
	if (handleInterplanetaryTravelArrival())
		return;

	// F_0.9.8c: arriving at a FLEE point is the end of the flee leg, not the
	// mission point and never the lair (TRAVEL_TO_LAIR's arrival starts the
	// hunt). Hand it back to the tick.
	if (isTravelCombatPhase() &&
			travelCombatMode.load(std::memory_order_acquire) !=
				SimBotTravelCombat::MOVE) {
		travelFleeLegEnded.store(true, std::memory_order_release);
		scheduleActiveTick(0);
		return;
	}

	// F_0.9.3: arriving at a corpse ends the loot walk only. The tick loots and
	// resumes the interrupted phase; none of that phase's arrival logic runs.
	uint64 nowMs = System::getMiliTime();
	if (earlyGameActive) {
		handleEarlyGameArrived();
		return;
	}
	if (phase == BAZAAR_VISIT) {
		// The phase tick owns the distance check and publication.  Keeping the
		// callback as a hand-off prevents an arrival thread from taking the
		// progression/object locks used by the listing path.
		scheduleActiveTick(0);
		return;
	}
	if (phase == BUFF_UP) {
		// F_0.7.5: arrival at the outdoor staging anchor. The provider approach
		// is still active, so entering here hands the normal arrival path the
		// same state it would have had from a direct entry.
		if (pveBuffStagingActive) {
			pveBuffStagingActive = false;
			if (enterPveBuffProviderCell(pveBuffStagingStage))
				return;

			// The provider vanished between staging and entry.
			if (pveBuffStagingStage == PVE_BUFF_APPROACH_DOCTOR)
				pveDoctorFallbackNeeded = true;
			else
				pveEntertainerFallbackNeeded = true;
			pveBuffProviderApproachActive = false;
			if (moveToNextPveBuffProvider())
				return;
			finishPveBuffProviderFlow();
			return;
		}

		if (pveBuffInteractionDwellActive) {
			pveBuffInteractionDwellActive = false;
			bool needDoctor = false;
			bool needEntertainer = false;
			computeBuffNeeds(needDoctor, needEntertainer);
			if (needEntertainer) {
				pveEntertainerFallbackNeeded = true;
			} else {
				ManagedReference<AiAgent*> strongAgent = agent;
				uint64 bodyOid = 0;
				if (strongAgent != nullptr) {
					Locker agentLock(strongAgent);
					bodyOid = strongAgent->getObjectID();
				}
				SimPlayerManager::instance()->recordPveBuffSource(
					bodyOid, "real-entertainer");
			}
			if (moveToNextPveBuffProvider())
				return;
			finishPveBuffProviderFlow();
			return;
		}

		if (pveBuffProviderApproachActive) {
			PveBuffApproachStage providerStage = pveBuffApproachStage;
			pveBuffProviderApproachActive = false;
			interactWithPveBuffProvider(providerStage);
			return;
		}

		if (SimPlayerManager::instance()->isPveRealBuffsEnabled())
			return;

		if (!cantinaArrived) {
			// Cantina dwell (entertainer theater; real doctor/entertainer BOT
			// providers are P.8.3). Loiter, then head to the med center.
			cantinaArrived = true;
			dwellUntilMs = nowMs + 3000;
			scheduleActiveTick(3000);
			return;
		}
		if (!cantinaDwellComplete) {
			cantinaDwellComplete = true;
			moveTo(medCenter);
			return;
		}

		if (!medCenterReached) {
			medCenterReached = true;
			dwellUntilMs = nowMs + 3000;
			scheduleActiveTick(3000);
			return;
		}

		if (!medDwellComplete) {
			medDwellComplete = true;
			applyHunterBuffs(true);
			if (missionHuntOrder)
				beginMissionTerminalLeg();
			else {
				setPhase(TRAVEL_OUT);
				moveTo(species.huntGround);
			}
			return;
		}
	}

	if (phase == TRAVEL_TO_TERMINAL) {
		MissionDiagLog::event("TERM_ARRIVED", identityId, "resolved=" +
			String::valueOf(missionTerminalResolved) +
			" offersGenerated=" + String::valueOf(missionOffersGenerated));
		// C3: do NOT generate here (onArrived runs on its own callback thread).
		// Hand off to the single-flight runActiveTick lane, which owns all offer
		// generation/retry (the ACCEPT_MISSION tick calls beginMissionAccept while
		// missionOffersGenerated is still false). Leaving TRAVEL_TO_TERMINAL now
		// also stops a concurrent tick from re-issuing terminal movement.
		setPhase(ACCEPT_MISSION);
		dwellUntilMs = 0;
		scheduleActiveTick(100);
		return;
	}

	if (phase == TRAVEL_TO_MISSION) {
		scheduleActiveTick(0);
		return;
	}

	if (phase == TRAVEL_TO_LAIR) {
		// DIAGNOSTIC: proves the engine reported arrival at the (relocated,
		// wilderness) lair -- i.e. the hunter physically reached it and is about
		// to start the fight. Absence of this line for a revealed lair means the
		// stall is upstream in movement, not in combat.
		MissionDiagLog::event("LAIR_REACHED", identityId,
			MissionDiagLog::fmtVec("lair", missionLairPosition) +
			" dist=" + String::valueOf(agent == nullptr ? -1.f :
				agent->getWorldPosition().distanceTo2d(missionLairPosition)));
		beginHunting();
		return;
	}

	if (phase == TRAVEL_OUT) {
		setPhase(AWAITING_WORLD);
		dwellUntilMs = nowMs + 5000;
		scheduleActiveTick(5000);
		return;
	}

	if (phase == AWAITING_WORLD) {
		beginHunting();
		return;
	}

	if (phase == HUNTING && targetOid != 0) {
		CreatureObject* target = nullptr;
		AiAgent* targetAgent = nullptr;
		if (targetIsLive(targetOid, target, targetAgent)) {
			if (agent->getDistanceTo(target) <= getEngageRangeMeters())
				engageTarget();
			else
				moveToTarget();
		} else {
			handleTargetUnavailable();
		}
		return;
	}

	if (phase == RETREATING) {
		// F_0.9.8e: the tick decides whether the bot is clear or must run again.
		if (isLairRetreatDefenseOn())
			scheduleActiveTick(0);
		else
			finishRetreatMove();
		return;
	}

	if (phase == TRAVEL_HOME) {
		if (orderAbandoned) {
			completeOrder(true, missionAbandonReason.isEmpty() ?
				String("abandoned") : missionAbandonReason);
		} else {
			setPhase(DELIVER);
			SimPlayerManager::instance()->announcePveHunterEvent(
				agent->getObjectID(), species.key,
				"Back with a full pack of " + species.harvestKind + ".");
			completeOrder(false, "quota");
		}
	}

	if (phase == MISSION_CLEANUP) {
		continueAfterMissionCleanup();
		return;
	}
}

void SimHunterController::onPathFailed() {
	// F_0.9.3: an unreachable corpse abandons the loot walk, never the mission or
	// the early-game anchor; a stale corpse-walk failure is ignored entirely.
	const LootWalkCallback lootCallback = classifyLootWalkCallback();
	if (lootCallback != LOOT_CALLBACK_NOT_LOOT) {
		if (lootCallback == LOOT_CALLBACK_CURRENT)
			lootStepPathFailed.store(true, std::memory_order_release);
		scheduleActiveTick(0);
		return;
	}

	if (isStructureTraversalFeatureEnabled() && isTraversalActive()) {
		SimPlayerController::onPathFailed();
		return;
	}

	// The base leaves the agent in CALCULATING_PATH; reset it or a failed
	// retreat/patrol path wedges the controller until the phase TTL fires.
	clearHybridMovementOnCancellation();
	state = IDLE;

	// F_0.9.8c: an unreachable FLEE point ends the flee leg; it is not a
	// mission path failure. The tick picks the next point or rests.
	if (isTravelCombatPhase() &&
			travelCombatMode.load(std::memory_order_acquire) !=
				SimBotTravelCombat::MOVE) {
		travelFleeLegEnded.store(true, std::memory_order_release);
		scheduleActiveTick(0);
		return;
	}

	// Same reason as onArrived(): while a cross-planet leg is in flight the shared
	// ticket-collector machine owns failure handling (bounded retryTicketApproach,
	// then cancelTicketCollectorTravel -> onInterplanetaryTravelFinished(false),
	// which the hunter already implements for both the relocation and buff-trip
	// records). It must run BEFORE the phase branches below: RELOCATING has no
	// branch at all (a failed leg just fell through to scheduleActiveTick and
	// waited out the phase TTL), and BUFF_TRIP's branch killed the whole trip on
	// the first failure instead of letting the approach retry. Deliberately ahead
	// of the !orderActive guard so an abandoned order can still tear travel down.
	if (handleInterplanetaryTravelPathFailed())
		return;

	if (earlyGameActive) {
		earlyGamePathFailurePending.store(true, std::memory_order_release);
		scheduleActiveTick(100);
		return;
	}
	if (phase == BAZAAR_VISIT) {
		finishBazaarVisit("pathFailed");
		return;
	}

	if (!orderActive)
		return;

	if (phase == BUFF_UP && pveBuffProviderApproachActive) {
		PveBuffApproachStage providerStage = pveBuffApproachStage;
		pveBuffProviderApproachActive = false;
		pveBuffStagingActive = false;
		if (providerStage == PVE_BUFF_APPROACH_DOCTOR)
			pveDoctorFallbackNeeded = true;
		else
			pveEntertainerFallbackNeeded = true;
		if (moveToNextPveBuffProvider())
			return;
		finishPveBuffProviderFlow();
		return;
	}

	if (phase == BUFF_TRIP) {
		SimPlayerManager::PveBuffTrip trip;
		bool haveTrip = SimPlayerManager::instance()->getPveHunterBuffTrip(
			identityId, trip);
		SimPlayerManager::instance()->finishPveHunterBuffTrip(identityId,
			false);
		clearInterplanetaryTravelState();
		if (haveTrip && trip.returning) {
			pveBuffTripReturning = false;
			resumeAfterBuffTrip();
		} else {
			pveBuffTripAtHub = false;
			pveBuffTripReturning = false;
			pveDoctorFallbackNeeded = pveNeedDoctorBuff;
			pveEntertainerFallbackNeeded = pveNeedEntertainerBuff;
			finishPveBuffProviderFlow();
		}
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_TERMINAL) {
		// The hybrid mover ALREADY has an off-mesh fallback: requestHybridPath
		// routes to a resolved mesh exit and finishes overland
		// (HYBRID_LEG_OVERLAND_FINAL). So a terminal failure means one of its
		// branches gave up, and getHybridPathDiagLine() names which -- was the
		// target on the same mesh, did resolveHybridExit succeed, did recast find
		// start/end polygons. Without this the only honest next step would be to
		// guess at a fallback that may already exist.
		MissionDiagLog::event("TERM_PATH_FAILED", identityId, "resolved=" +
			String::valueOf(missionTerminalResolved) +
			" " + MissionDiagLog::fmtVec("terminal", missionTerminalPosition) +
			" " + MissionDiagLog::fmtVec("pos", agent == nullptr ? Vector3() :
				agent->getWorldPosition()) +
			" hybrid[" + getHybridPathDiagLine() + "]");
		beginMissionFallback();
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_MISSION) {
		// This site had NO diagnostics, which is why the second-largest mission
		// failure was invisible. Same evidence the terminal leg now carries: the
		// hybrid line says whether the wilderness target was off-mesh (and so
		// whether resolveHybridExit was even reached), and whether recast found
		// start/end polygons.
		//
		// The CELL is recorded because it is what separates two fixes with
		// nothing else to tell them apart. The overnight failures all showed a
		// stalled leg rather than a failed path-find, and a bot standing
		// OUTDOORS that cannot move is a navmesh/collision problem the overland
		// demotion answers, while one standing INSIDE a cell is the known
		// cell->outdoor egress defect and belongs to that work instead. Read
		// the same way the buff-provider check reads it (:559).
		uint64 parentOid = 0;
		uint64 buildingOid = 0;
		bool inCell = false;
		{
			ManagedReference<AiAgent*> strongAgent = agent;
			ManagedReference<SceneObject*> agentParent = strongAgent == nullptr ?
				nullptr : strongAgent->getParent().get();
			if (agentParent != nullptr) {
				parentOid = agentParent->getObjectID();
				inCell = agentParent->isCellObject();
				if (inCell) {
					ManagedReference<SceneObject*> agentBuilding =
						agentParent->getParent().get();
					if (agentBuilding != nullptr)
						buildingOid = agentBuilding->getObjectID();
				}
			}
		}
		MissionDiagLog::event("MISSION_PATH_FAILED", identityId,
			MissionDiagLog::fmtVec("target", missionLairPosition) +
			" " + MissionDiagLog::fmtVec("pos", agent == nullptr ? Vector3() :
				agent->getWorldPosition()) +
			" dist=" + String::valueOf(agent == nullptr ? -1.f :
				agent->getWorldPosition().distanceTo2d(missionLairPosition)) +
			" offerId=" + String::valueOf(currentMissionOfferId) +
			" pathFailedSource=" + String(getPathFailedSource()) +
			" inCell=" + String::valueOf(inCell) +
			" parentOid=" + String::valueOf(parentOid) +
			" buildingOid=" + String::valueOf(buildingOid) +
			" hybrid[" + getHybridPathDiagLine() + "]");
		beginMissionCleanup(true, "mission_offer_path_failed");
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_LAIR) {
		// DIAGNOSTIC: the stuck-watchdog gave up pathing to the lair. If this
		// fires, the lair is genuinely unreachable overland (unlike miners); if
		// neither LAIR_REACHED nor LAIR_PATHFAIL ever fires, the movement loop is
		// wedged without resolving either way.
		MissionDiagLog::event("LAIR_PATHFAIL", identityId,
			MissionDiagLog::fmtVec("lair", missionLairPosition) +
			" dist=" + String::valueOf(agent == nullptr ? -1.f :
				agent->getWorldPosition().distanceTo2d(missionLairPosition)));
		beginMissionCleanup(true, "lair_path_failed");
		return;
	}

	if (missionHuntOrder && phase == BUFF_UP) {
		beginMissionCleanup(true, "buff_path_failed");
		return;
	}

	// TRAVEL_HOME is an EPILOGUE: the hunt's verdict was already decided and
	// stored in orderAbandoned by beginTravelHome(). Failing the walk back must
	// not overturn it — forcing abandoned=true here re-labelled successful hunts
	// as abandonments and was the single dominant churn source observed live
	// (34 of 34 abandons were path_failed_TRAVEL_HOME). Complete with the real
	// outcome and let the idle loop re-acquire; the hunter does not need to be
	// physically home to take its next contract.
	if (phase == TRAVEL_HOME) {
		completeOrder(orderAbandoned, orderAbandoned ?
			epilogueAbandonReason("path_failed_TRAVEL_HOME") :
			String("travel_home_unreachable_after_success"));
		return;
	}

	// TRAVEL_OUT and BUFF_UP are prologues — the hunt never happened, so a path
	// failure there is a genuine abandonment.
	if (phase == TRAVEL_OUT || phase == BUFF_UP) {
		completeOrder(true, "path_failed_" + getPhaseName());
		return;
	}

	if (phase == HUNTING && targetOid != 0) {
		clearStaleCombat("target_path_failed");
		scheduleActiveTick(1000);
		return;
	}

	scheduleActiveTick(1000);
}

void SimHunterController::beginHunting() {
	if (!orderActive)
		return;
	if (missionHuntOrder && missionLairOid == 0) {
		beginMissionCleanup(true, "lair_missing_before_hunt");
		return;
	}

	if (huntStartedAtMs == 0)
		huntStartedAtMs = System::getMiliTime();
	// Deliberately NOT huntStartedAtMs: that is order-level and survives
	// beginNextMissionOffer(), so the second held offer would inherit the
	// first's start and report a cumulative timeInHuntingMs. The lair record is
	// created fresh per offer and recordPveHunterMissionHuntStarted ignores a
	// second call for the same lair, so a current timestamp is the per-lair
	// start. The order-level value stays as it is for the phase timeout.
	if (missionHuntOrder)
		SimPlayerManager::instance()->recordPveHunterMissionHuntStarted(
			identityId, agent == nullptr ? 0 : agent->getObjectID(),
			System::getMiliTime());

	setPhase(HUNTING);
	lastPatrolMoveMs = 0;
	scheduleActiveTick(100);
}

float SimHunterController::getEarlyGameArriveMeters(
		const SimBotEarlyGameConfig& config) const {
	return earlyGameAnchorArriveMeters > 0.f ? earlyGameAnchorArriveMeters :
		config.anchorArriveMeters;
}

uint64 SimHunterController::findUnwinnableEarlyGameAttacker(AiAgent* hunter,
		const SimBotEarlyGameConfig& config, uint64 excludeOid) {
	// Every creature attacking the novice must be one it could beat (upper level
	// band + TTK, in-combat rules), not only the one it is fighting. Returns the
	// first that is not, or 0. Checked on entering a fight and on every Engage
	// tick, because pack allies join asynchronously after the fight has started
	// (AiAgent::notifyPackMobs). Caller holds no agent lock.
	if (hunter == nullptr)
		return 0;

	Vector<ManagedReference<CreatureObject*> > otherAttackers;
	ManagedReference<WeaponObject*> weapon;
	float rawDamage = 0.f;
	float attackPeriod = 0.f;
	{
		Locker agentLock(hunter);
		const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
			hunter->getDefenderList();
		if (defenderList != nullptr) {
			for (int i = 0; i < defenderList->size(); ++i) {
				ManagedReference<SceneObject*> defender = defenderList->getSafe(i);
				CreatureObject* creature = defender == nullptr ? nullptr :
					defender->asCreatureObject();
				if (creature != nullptr && creature != hunter &&
						creature->getObjectID() != excludeOid)
					otherAttackers.add(creature);
			}
		}
		// A one-on-one fight - the common case - costs only this snapshot.
		if (otherAttackers.size() == 0)
			return 0;
		weapon = hunter->getWeapon();
		if (weapon != nullptr) {
			rawDamage = (weapon->getMinDamage() + weapon->getMaxDamage()) * 0.5f;
			attackPeriod = weapon->getAttackSpeed();
		}
	}
	SimBotIdentity identity;
	if (weapon == nullptr || !SimPlayerManager::instance()->
			snapshotPlayerBotIdentity(identityId, identity))
		return 0;

	float attackerReach = SimPlayerManager::instance()->
		getPveHunterScanRadiusMeters() + 24.f;
	for (int i = 0; i < otherAttackers.size(); ++i) {
		CreatureObject* other = otherAttackers.get(i).get();
		AiAgent* otherAgent = other->asAiAgent();
		if (otherAgent == nullptr || otherAgent->getSimPlayerBot() ||
				other->isDead() || other->isIncapacitated() ||
				hunter->getDistanceTo(other) > attackerReach)
			continue;
		float otherTtk = 0.f;
		if (!calculateEarlyGameTtk(other, config, identity.skillTier, weapon,
				rawDamage, attackPeriod, otherTtk, true))
			return other->getObjectID();
	}
	return 0;
}

bool SimHunterController::chooseEarlyGameSpawnerAnchor(
		const SimBotEarlyGameConfig& config) {
	// A novice grinds the static spawner points around its city before taking
	// missions, as a new player does (owner rule, 2026-09-15). The city spawner
	// screenplays place a SpawnEggObject at each point and respawn its creature
	// table there, so the points are found by zone query: no location is
	// hard-coded, and whatever a spawner is producing now is what the scan finds.
	SimBotIdentity identity;
	if (!SimPlayerManager::instance()->snapshotPlayerBotIdentity(identityId,
			identity))
		return false;
	Vector3 cantina;
	Vector3 medCenter;
	Vector3 cityCenter;
	String homePlanet;
	String homeCity;
	SimPlayerManager::instance()->resolvePlayerBotHomeCity(identity, homePlanet,
		homeCity);
	if (!SimPlayerManager::instance()->getPveHomeLocations(homePlanet, homeCity,
			cantina, medCenter, cityCenter))
		return false;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return false;
	Zone* zone = nullptr;
	{
		Locker agentLock(strongAgent);
		zone = strongAgent->getZone();
	}
	if (zone == nullptr || zone->getZoneName() != homePlanet)
		return false;

	SortedVector<TreeEntry*> nearby;
	zone->getInRangeObjects(cityCenter.getX(), cityCenter.getZ(),
		cityCenter.getY(), config.spawnerSearchRadiusMeters, &nearby, true, true);

	// Ordered by object id, so the rotation is stable between scans.
	Vector<uint64> spawnerIds;
	Vector<Vector3> spawnerPoints;
	for (int i = 0; i < nearby.size(); ++i) {
		SceneObject* object = static_cast<SceneObject*>(nearby.get(i));
		if (object == nullptr || !object->isSpawnEggObject())
			continue;
		SpawnEggObject* egg = dynamic_cast<SpawnEggObject*>(object);
		if (egg == nullptr || (config.skipInactiveSpawners && !egg->isActive()))
			continue;
		uint64 oid = object->getObjectID();
		int insertAt = spawnerIds.size();
		while (insertAt > 0 && spawnerIds.get(insertAt - 1) > oid)
			--insertAt;
		spawnerIds.add(insertAt, oid);
		spawnerPoints.add(insertAt, object->getWorldPosition());
	}
	if (spawnerIds.size() == 0)
		return false;

	Vector3 start = strongAgent->getWorldPosition();
	for (int tried = 0; tried < spawnerIds.size(); ++tried) {
		int index = earlyGameSpawnerIndex % spawnerIds.size();
		if (index < 0)
			index += spawnerIds.size();
		earlyGameSpawnerIndex = index + 1;
		Vector3 point = spawnerPoints.get(index);
		point.setZ(0.f);
		if (!zone->isWithinBoundaries(point) ||
				SimPlayerManager::instance()->isStructureTraversalPointInWater(zone,
					point))
			continue;
		// The spawner the bot is standing at has just been scanned; move on.
		if (start.distanceTo2d(point) <= config.spawnerArriveMeters)
			continue;
		if (isEarlyGameLegNearDeathSite(start, point, homePlanet)) {
			SimPlayerManager::instance()->recordPlayerBotEarlyGameDeathZoneReject(
				identityId);
			continue;
		}
		point.setZ(zone->getHeight(point.getX(), point.getY()));
		earlyGameAnchor = point;
		earlyGameAnchorArriveMeters = config.spawnerArriveMeters;
		return true;
	}
	return false;
}

float SimHunterController::getEngageRangeMeters() const {
	// Legacy and mission hunters carry rifles and keep the configured engage
	// range. An early-game body fights with whatever its training plan equipped
	// (unarmed reaches 5 m), so it must close to its own weapon's reach: at the
	// 48 m rifle range an unarmed novice stood 27-38 m from its target in combat,
	// never landed a hit and fled (quick check 20260915-012448).
	float configured = SimPlayerManager::instance()->
		getPveHunterWeaponRangeMeters();
	if (!earlyGameActive || earlyGameEngageRangeMeters <= 0.f)
		return configured;
	return Math::min(configured, earlyGameEngageRangeMeters);
}

bool SimHunterController::isEarlyGameRecoveryWalkStalled(AiAgent* hunter,
		uint64 stallMs) {
	// A recovery walk normally outruns the "never re-flee while walking" rule,
	// but an attacker can pin it in place (verification 20260914-205211: a giant
	// worrt stopped the flee ~14 m short and killed the novice standing still).
	// Progress is measured from the last point the walk moved on from, so a walk
	// that keeps moving is never judged stalled however long it runs.
	const float progressMeters = 4.f;
	if (hunter == nullptr)
		return false;
	Vector3 position = hunter->getWorldPosition();
	uint64 nowMs = System::getMiliTime();
	if (earlyGameRecoverProgressMs == 0 ||
			position.distanceTo2d(earlyGameRecoverProgressPos) >= progressMeters) {
		earlyGameRecoverProgressPos = position;
		earlyGameRecoverProgressMs = nowMs;
		return false;
	}
	return nowMs - earlyGameRecoverProgressMs >= stallMs;
}

void SimHunterController::capEarlyGameWounds(AiAgent* hunter,
		float maxFraction) {
	// The same per-pool cap spawnPveIdentityBody applies to a novice body. It is
	// also needed for a live body: one respawned while the early-game gate was
	// off still carries the flat hunter clone wounds, and wounds picked up later
	// can exceed the cap too. Either way HAM regeneration stops below the rest
	// line and Recover would wait forever. Caller holds no agent lock.
	if (hunter == nullptr)
		return;
	Locker agentLock(hunter);
	for (uint8 pool = CreatureAttribute::HEALTH;
			pool <= CreatureAttribute::WILLPOWER; ++pool) {
		int cap = static_cast<int>(hunter->getMaxHAM(pool) * maxFraction);
		if (hunter->getWounds(pool) > cap)
			hunter->setWounds(pool, cap);
	}
}

bool SimHunterController::isEarlyGameLegNearDeathSite(const Vector3& start,
		const Vector3& anchor, const String& planet) {
	Vector<SimBotEarlyGameDeathZone> zones;
	float radius = 0.f;
	SimPlayerManager::instance()->getPlayerBotEarlyGameDeathZones(identityId,
		planet, zones, radius);
	if (radius <= 0.f)
		return false;

	float legX = anchor.getX() - start.getX();
	float legY = anchor.getY() - start.getY();
	float legLengthSq = legX * legX + legY * legY;
	float radiusSq = radius * radius;
	for (int i = 0; i < zones.size(); ++i) {
		const SimBotEarlyGameDeathZone& zone = zones.get(i);
		float fromStartX = zone.x - start.getX();
		float fromStartY = zone.y - start.getY();
		// A site around the start (a death beside the cloner) cannot be walked
		// around; judging it would reject every bearing and pin the bot in town.
		if (fromStartX * fromStartX + fromStartY * fromStartY <= radiusSq)
			continue;
		float along = legLengthSq <= 0.f ? 0.f :
			Math::max(0.f, Math::min(1.f,
				(fromStartX * legX + fromStartY * legY) / legLengthSq));
		float offX = legX * along - fromStartX;
		float offY = legY * along - fromStartY;
		if (offX * offX + offY * offY <= radiusSq)
			return true;
	}
	return false;
}

bool SimHunterController::chooseEarlyGameAnchor(bool widenRing) {
	SimBotIdentity identity;
	if (!SimPlayerManager::instance()->snapshotPlayerBotIdentity(identityId,
			identity))
		return false;

	SimBotEarlyGameConfig config;
	SimPlayerManager::instance()->getPlayerBotEarlyGameConfig(config);
	Vector3 cantina;
	Vector3 medCenter;
	Vector3 cityCenter;
	String homePlanet;
	String homeCity;
	SimPlayerManager::instance()->resolvePlayerBotHomeCity(identity, homePlanet,
		homeCity);
	if (!SimPlayerManager::instance()->getPveHomeLocations(
			homePlanet, homeCity, cantina, medCenter, cityCenter))
		return false;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return false;
	Zone* zone = nullptr;
	{
		Locker agentLock(strongAgent);
		zone = strongAgent->getZone();
	}
	if (zone == nullptr || zone->getZoneName() != homePlanet)
		return false;

	// Eight fixed bearings are enough to spread identities around the city while
	// keeping the result stable across restarts. The identity seeds the first
	// bearing; each rejected candidate advances monotonically through the ring.
	const float bearingStep = 0.7853981633974483f;
	const float ringSpan = Math::max(0.f,
		config.grindAnchorMaxMeters - config.grindAnchorMinMeters);
	float radius = widenRing ? config.grindAnchorMaxMeters + ringSpan :
		config.grindAnchorMinMeters +
		(static_cast<float>(identity.id % 1000) / 1000.f) * ringSpan;
	int bearing = earlyGameBearingIndex % 8;
	if (bearing < 0)
		bearing += 8;
	float angle = static_cast<float>(bearing) * bearingStep;
	Vector3 candidate(cityCenter.getX() + std::cos(angle) * radius,
		cityCenter.getY() + std::sin(angle) * radius, 0.f);
	earlyGameBearingIndex = (bearing + 1) % 8;
	++earlyGameAnchorAttempts;

	// A grind anchor must be OUTDOORS: outside every NavArea, by the same test
	// resolveHybridExit uses for its egress point. The ring is measured from the
	// shuttle pad, so in a large city it lands inside the urban navmesh. The
	// hybrid mover then treats it as an on-mesh destination, and recast cannot
	// route to an arbitrary interior point: in verification run 20260914 every
	// path from Keren failed and the novice never moved. Step outward along the
	// same bearing to the first outdoor point, which gives the walk the proven
	// navmesh-exit-then-overland leg. Bounded, so a malformed mesh cannot loop.
	const float outwardStepMeters = 64.f;
	const int maxOutwardSteps = 48;
	const float stepX = std::cos(angle) * outwardStepMeters;
	const float stepY = std::sin(angle) * outwardStepMeters;
	bool outdoor = false;
	for (int step = 0; step <= maxOutwardSteps; ++step) {
		if (step > 0)
			candidate = Vector3(candidate.getX() + stepX,
				candidate.getY() + stepY, 0.f);
		if (!zone->isWithinBoundaries(candidate))
			return false;
		SortedVector<ManagedReference<NavArea*> > areas;
		zone->getInRangeNavMeshes(candidate.getX(), candidate.getY(), &areas,
			false);
		if (areas.size() == 0) {
			outdoor = true;
			break;
		}
	}
	if (!outdoor || SimPlayerManager::instance()->
			isStructureTraversalPointInWater(zone, candidate))
		return false;

	// Never walk a novice back through a place it recently died: in
	// verification 20260914-173645 it was killed twice on one deterministic
	// anchor route. Reject an anchor whose overland leg from here passes within
	// the avoid radius; the ring then moves on to the next bearing.
	if (isEarlyGameLegNearDeathSite(strongAgent->getWorldPosition(), candidate,
			homePlanet)) {
		SimPlayerManager::instance()->recordPlayerBotEarlyGameDeathZoneReject(
			identityId);
		return false;
	}

	candidate.setZ(zone->getHeight(candidate.getX(), candidate.getY()));
	earlyGameAnchor = candidate;
	earlyGameAnchorArriveMeters = 0.f;
	return true;
}

void SimHunterController::beginEarlyGame() {
	if (agent == nullptr || !SimPlayerManager::instance()->
			isPlayerBotEarlyGameIdentity(identityId))
		return;

	SimBotIdentity identity;
	if (!SimPlayerManager::instance()->snapshotPlayerBotIdentity(identityId,
			identity))
		return;

	SimBotEarlyGameConfig config;
	SimPlayerManager::instance()->getPlayerBotEarlyGameConfig(config);
	Vector3 cantina;
	Vector3 medCenter;
	Vector3 cityCenter;
	String homePlanet;
	String homeCity;
	SimPlayerManager::instance()->resolvePlayerBotHomeCity(identity, homePlanet,
		homeCity);
	if (SimPlayerManager::instance()->getPveHomeLocations(
			homePlanet, homeCity, cantina, medCenter, cityCenter))
		earlyGameRestPoint = cantina;
	else
		earlyGameRestPoint = agent->getWorldPosition();

	// Entering grind mode PREEMPTS the legacy order branch in runActiveTick, so
	// an order that was already active would never be completed or abandoned:
	// it would sit in the manager reserving inbound supply forever and leave
	// this controller wedged after graduation. startOrder's decline only covers
	// orders that arrive AFTER the switch, so release the in-flight one here.
	if (orderActive) {
		SimPlayerManager::instance()->declinePveHunterOrder(identityId,
			agent == nullptr ? 0 : agent->getObjectID(),
			"EARLY_GAME_GRIND_PREEMPT");
		orderActive = false;
		missionHuntOrder = false;
		missionCleanupRequested = false;
		disengageMissionLair();
		// disengageMissionLair only clears engagedMissionLairOid. The order's
		// own mission identifiers must be cleared too, or a stale lair/offer id
		// survives into grind mode. The manager destroys the lair itself: that
		// is queued from declinePveHunterOrder above, since releasing an order
		// must release the resources the order acquired.
		missionTerminalResolved = false;
		missionTerminalFallback = false;
		missionTerminalPosition = Vector3();
		missionLairPosition = Vector3();
		missionLairOid = 0;
		currentMissionOfferId = 0;
		missionOffersGenerated = false;
		missionOfferAttempts = 0;
		missionOfferVisitEpoch = 0;
		cancelPveDoctorRequest();
		SimPlayerManager::instance()->releasePlayerBotDoctorBuffFee(identityId);
		pveDoctorFeeReserved = false;
		advanceWorkLoopGeneration("hunterEnterEarlyGame");
		clearInteriorApproachLeg();
		resetHybridMovementState(true);
		if (SimPlayerManager::instance()->isPveMarketDispatchEnabled())
			clearInterplanetaryTravelState();
	}

	earlyGameActive = true;
	earlyGamePhase = EARLY_LEAVE_CITY;
	earlyGameBearingIndex = static_cast<int>(identity.id % 8);
	earlyGameSpawnerIndex = static_cast<int>(identity.id % 64);
	earlyGameAnchorAttempts = 0;
	earlyGameRingWidened = false;
	earlyGameScanAttempts = 0;
	earlyGameScanBearingVisits = 0;
	earlyGameScanRetryAtMs = 0;
	earlyGamePathRetryAtMs = 0;
	earlyGameTargetTtkSeconds = 0.f;
	earlyGameTargetDefensive = false;
	earlyGameRestStartedAtMs = 0;
	earlyGamePathFailurePending.store(false, std::memory_order_relaxed);
	earlyGameDestructionPending.store(false, std::memory_order_relaxed);
	earlyGameDestroyedTargetOid.store(0, std::memory_order_relaxed);
	earlyGameDestroyedParticipantVerified.store(false,
		std::memory_order_relaxed);
	dropTargetObserver();
	disengageTarget(false);
	targetOid = 0;
	targetMissionWave = false;
	destructionHandled = false;
	clearHybridMovementOnCancellation();
	resetCombatGuard();

	bool found = config.roamCitySpawners &&
		chooseEarlyGameSpawnerAnchor(config);
	for (int attempt = 0; attempt < config.anchorMaxAttempts && !found;
			++attempt)
		found = chooseEarlyGameAnchor(false);
	if (!found) {
		earlyGameRingWidened = true;
		earlyGameAnchorAttempts = 0;
		for (int attempt = 0; attempt < config.anchorMaxAttempts && !found;
				++attempt)
			found = chooseEarlyGameAnchor(true);
	}
	if (!found) {
		earlyGameAnchor = earlyGameRestPoint;
		earlyGameAnchorArriveMeters = 0.f;
		SimPlayerManager::instance()->recordPlayerBotEarlyGameAnchorFallback(
			identityId);
	}

	// The legacy HuntPhase stays HUNTING for the whole novice mode so onTick's
	// interceptor path cannot become a second writer of targetOid/observer state.
	setPhase(HUNTING);
	setEarlyGamePhase(EARLY_LEAVE_CITY);
	publishEarlyGameMovement("begin", false, true, false, false);
	// A body that starts hurt (clone wounds, a respawn mid-fight) heals at its
	// rest point before walking out, as a player does after cloning. Walking
	// out at 1 HP made the first hit of any creature fatal.
	ManagedReference<AiAgent*> strongAgent = agent;
	// Entering the loop heals to the clone-wound cap (0 heals every wound, the
	// owner's heal-on-death rule), covering a body respawned with the gate off.
	capEarlyGameWounds(strongAgent.get(), config.cloneWoundMaxFraction);
	if (strongAgent != nullptr &&
			isBelowEarlyGameHam(strongAgent.get(), config.restHamFraction)) {
		beginEarlyGameRecovery(false);
		return;
	}
	scheduleActiveTick(100);
}

bool SimHunterController::isBelowEarlyGameHam(AiAgent* hunter,
		float fraction) const {
	if (hunter == nullptr)
		return false;
	float minimum = 1.f;
	const uint8 hamPools[] = {CreatureAttribute::HEALTH,
		CreatureAttribute::ACTION, CreatureAttribute::MIND};
	for (uint8 i = 0; i < 3; ++i) {
		int maximum = hunter->getMaxHAM(hamPools[i]);
		if (maximum > 0)
			minimum = Math::min(minimum, static_cast<float>(hunter->getHAM(
				hamPools[i])) / static_cast<float>(maximum));
	}
	return minimum < fraction;
}

bool SimHunterController::calculateEarlyGameTtk(CreatureObject* target,
		const SimBotEarlyGameConfig& config, int skillTier,
		WeaponObject* weapon, float rawAverageDamage, float attackPeriod,
		float& ttkSeconds, bool allowInCombat) const {
	// Config, identity and weapon are IDENTICAL for every candidate in one
	// scan, so they are snapshotted once by the caller. Taking the two manager
	// mutexes and the agent lock per candidate made target selection O(n) in
	// lock acquisitions across a populated wilderness cell - the cost pattern
	// ARCHI's performance note calls out. Only the per-target read below is
	// unavoidably per candidate.
	ttkSeconds = 0.f;
	if (target == nullptr || agent == nullptr || weapon == nullptr)
		return false;
	AiAgent* targetAgent = target->asAiAgent();
	if (targetAgent == nullptr || target->isPlayerCreature() ||
		target->isDead() || target->isIncapacitated() ||
		target->getParent() != nullptr ||
		(!allowInCombat && target->isInCombat()))
		return false;

	float effectiveHam = 0.f;
	float averageDamage = 0.f;
	int targetLevel = 0;
	{
		Locker targetLock(targetAgent);
		int health = target->getHAM(CreatureAttribute::HEALTH);
		int action = target->getHAM(CreatureAttribute::ACTION);
		int mind = target->getHAM(CreatureAttribute::MIND);
		targetLevel = targetAgent->getLevel();
		// A creature dies when ANY primary pool is depleted - only secondaries
		// are clamped at zero (CreatureObjectImplementation.cpp:1100-1101) - so
		// the minimum primary pool is the fight-ending budget. Deliberately
		// conservative: it can only make a target look easier, which is why the
		// level ceiling below is the hard guard rather than the TTK band.
		effectiveHam = static_cast<float>(Math::min(health,
			Math::min(action, mind)));
		averageDamage = CombatManager::instance()->
			estimateDamageAfterMitigation(targetAgent, weapon,
				rawAverageDamage);
	}

	// A level band around the novice's tier that rises as it trains. Chosen
	// targets must sit inside tier - levelBandBelow .. tier + levelBandAbove;
	// self-defence (allowInCombat) keeps only the upper bound, so a weaker
	// attacker is fought, never fled from. A one-sided ceiling with a level-10
	// floor let a tier-2 novice pick armed level 9-10 NPCs that beat it in
	// seconds (quick check 20260915-002501).
	if (effectiveHam <= 0.f || averageDamage <= 0.f || attackPeriod <= 0.f ||
			config.assumedHitRate <= 0.f ||
			targetLevel > skillTier + config.levelBandAbove ||
			(!allowInCombat && targetLevel < skillTier - config.levelBandBelow))
		return false;
	ttkSeconds = effectiveHam * attackPeriod /
		(averageDamage * config.assumedHitRate);
	// minTtkSeconds keeps proactive selection off trivially weak targets. It
	// must not apply to self-defence: an attacker that dies quickly is the
	// easiest fight there is, and the owner rule is to fight weaker attackers,
	// never flee them. The upper TTK bound still applies to both.
	return (allowInCombat || ttkSeconds >= config.minTtkSeconds) &&
		ttkSeconds <= config.maxTtkSeconds;
}

void SimHunterController::publishEarlyGameCombat(AiAgent* hunter) {
	// Default-off diagnostic (earlyGame.combatTelemetry): while it is off the
	// lock-free gate read is the only cost.
	SimPlayerManager* manager = SimPlayerManager::instance();
	if (hunter == nullptr || identityId == 0 ||
			!manager->isPlayerBotEarlyGameCombatTelemetryEnabled())
		return;

	SimBotEarlyGameCombatSample sample;
	sample.decision = earlyGameDefenseDecision;
	sample.decisionTtk = earlyGameDefenseTtk;
	Vector<ManagedReference<SceneObject*> > defenders;
	{
		// Same snapshot choreography as selectActiveCombatAttacker: copy under the
		// hunter lock, inspect the retained references after releasing it.
		Locker agentLock(hunter);
		sample.inCombat = hunter->isInCombat();
		sample.health = hunter->getHAM(CreatureAttribute::HEALTH);
		sample.healthMax = hunter->getMaxHAM(CreatureAttribute::HEALTH);
		sample.action = hunter->getHAM(CreatureAttribute::ACTION);
		sample.actionMax = hunter->getMaxHAM(CreatureAttribute::ACTION);
		sample.mind = hunter->getHAM(CreatureAttribute::MIND);
		sample.mindMax = hunter->getMaxHAM(CreatureAttribute::MIND);
		const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
			hunter->getDefenderList();
		if (defenderList != nullptr) {
			sample.defenderCount = defenderList->size();
			for (int i = 0; i < defenderList->size() && defenders.size() < 3; ++i) {
				ManagedReference<SceneObject*> defender = defenderList->getSafe(i);
				if (defender != nullptr)
					defenders.add(defender);
			}
		}
	}

	float reachabilityRadius = manager->getPveHunterScanRadiusMeters() + 24.f;
	StringBuffer described;
	for (int i = 0; i < defenders.size(); ++i) {
		SceneObject* defender = defenders.get(i).get();
		CreatureObject* creature = defender->asCreatureObject();
		AiAgent* creatureAgent = creature == nullptr ? nullptr :
			creature->asAiAgent();
		float distance = hunter->getDistanceTo(defender);
		// The first selectActiveCombatAttacker filter this defender fails.
		const char* verdict = "eligible";
		if (creature == nullptr)
			verdict = "not_creature";
		else if (creature == hunter)
			verdict = "self";
		else if (creature->isPlayerCreature())
			verdict = "player";
		else if (creatureAgent == nullptr)
			verdict = "not_ai";
		else if (manager->isSimPresenceCreature(creature))
			verdict = "presence";
		else if (creatureAgent->getSimPlayerBot())
			verdict = "sim_bot";
		else if (creature->isDead())
			verdict = "dead";
		else if (creature->isIncapacitated())
			verdict = "incapacitated";
		else if (creature->getParent() != nullptr)
			verdict = "in_cell";
		else if (!creature->isAttackableBy(hunter))
			verdict = "not_attackable";
		else if (distance > reachabilityRadius)
			verdict = "out_of_range";
		if (i > 0)
			described << ";";
		described << defender->getObjectID() << ":" <<
			(defender->getObjectTemplate() == nullptr ? String("none") :
				defender->getObjectTemplate()->getFullTemplateString()) << ":lvl" <<
			(creature == nullptr ? 0 : creature->getLevel()) << ":" <<
			static_cast<int>(distance) << "m:" << verdict;
	}
	sample.defenders = described.toString();
	manager->recordPlayerBotEarlyGameCombatTick(identityId, sample);
}

bool SimHunterController::handleEarlyGameSelfDefense(AiAgent* hunter,
		const SimBotEarlyGameConfig& config) {
	// Grind mode runs under the legacy HUNTING phase, where onTick's interceptor
	// defence deliberately stands down, and scanForTarget ignores a bot that is
	// already in combat. So a novice attacked on the way to its anchor neither
	// fought back nor fled (live 20260914: killed en route with no target).
	// Engage already owns its own fight and flee check.
	if (hunter == nullptr)
		return false;
	if (earlyGamePhase == EARLY_ENGAGE) {
		earlyGameDefenseDecision = "engage_phase";
		return false;
	}
	if (!hunter->isInCombat()) {
		phantomCombatTicks = 0;
		earlyGameDefenseDecision = "not_in_combat";
		return false;
	}

	ManagedReference<CreatureObject*> attacker;
	ManagedReference<AiAgent*> attackerAgent;
	if (!selectActiveCombatAttacker(hunter, attacker, attackerAgent)) {
		// In combat with no eligible attacker: dead, despawned, out of range, or
		// one target selection refuses (in a cell, not attackable, not a
		// creature). scanForTarget and moveTo both refuse while the flag stands.
		// A recovery walk already under way is left alone - clearing would drop
		// its path.
		if (earlyGamePhase == EARLY_RECOVER &&
				(state == MOVING || state == CALCULATING_PATH)) {
			if (!isEarlyGameRecoveryWalkStalled(hunter, 4000)) {
				earlyGameDefenseDecision = "no_attacker_recovering";
				return false;
			}
			// Pinned while something we cannot target keeps us in combat:
			// re-issue the walk to the rest point.
			earlyGameDefenseDecision = "no_attacker_flee_stalled";
			SimPlayerManager::instance()->recordPlayerBotEarlyGameDefense(
				identityId, false);
			targetOid = 0;
			beginEarlyGameRecovery(true);
			return true;
		}
		// Something we cannot fight is wearing us down: run for the rest point
		// instead of waiting out the phantom guard. Recover is already at or
		// heading to safety, so it never re-flees from there.
		if (earlyGamePhase != EARLY_RECOVER &&
				isBelowEarlyGameHam(hunter, config.fleeHamFraction)) {
			earlyGameDefenseDecision = "no_attacker_flee";
			SimPlayerManager::instance()->recordPlayerBotEarlyGameDefense(
				identityId, false);
			targetOid = 0;
			beginEarlyGameRecovery(true);
			return true;
		}
		earlyGameDefenseDecision = "no_attacker";
		// Otherwise age the flag out like runActiveTick's phantom-combat guard.
		if (++phantomCombatTicks >= 6) {
			earlyGameDefenseDecision = "no_attacker_cleared";
			clearStaleCombat("phantom_combat");
		}
		return false;
	}
	phantomCombatTicks = 0;

	// The same estimate a player makes by eye, and the same no-cheat rule as
	// target selection: fight what the TTK window and level ceiling say is
	// winnable, otherwise run. Already below the flee line means run regardless.
	bool winnable = false;
	float ttkSeconds = 0.f;
	uint64 unwinnableAttackerOid = 0;
	SimBotIdentity identity;
	if (!isBelowEarlyGameHam(hunter, config.fleeHamFraction) &&
			SimPlayerManager::instance()->snapshotPlayerBotIdentity(identityId,
				identity)) {
		ManagedReference<WeaponObject*> weapon;
		float rawDamage = 0.f;
		float attackPeriod = 0.f;
		{
			Locker agentLock(hunter);
			weapon = hunter->getWeapon();
			if (weapon != nullptr) {
				rawDamage = (weapon->getMinDamage() + weapon->getMaxDamage()) *
					0.5f;
				attackPeriod = weapon->getAttackSpeed();
				earlyGameEngageRangeMeters = Math::max(2.f,
					static_cast<float>(weapon->getMaxRange()) - 1.f);
			}
		}
		winnable = weapon != nullptr && calculateEarlyGameTtk(attacker.get(),
			config, identity.skillTier, weapon, rawDamage, attackPeriod,
			ttkSeconds, true);

		// Judge every attacker, not only the one selected: a novice engaged a
		// winnable creature while a level-22 womp rat beside it did the damage
		// (quick check 20260915-143615). Fight only if all of them are winnable;
		// otherwise run from the one that is not.
		if (winnable) {
			unwinnableAttackerOid = findUnwinnableEarlyGameAttacker(hunter, config,
				attacker->getObjectID());
			if (unwinnableAttackerOid != 0)
				winnable = false;
		}
	}

	earlyGameDefenseTtk = ttkSeconds;
	if (!winnable) {
		// Already walking away: let the recovery leg run instead of re-issuing a
		// flee (and a path request) every tick while the attacker chases.
		if (earlyGamePhase == EARLY_RECOVER &&
				(state == MOVING || state == CALCULATING_PATH) &&
				!isEarlyGameRecoveryWalkStalled(hunter, 4000)) {
			earlyGameDefenseDecision = "flee_recovering";
			return false;
		}
		// A fresh flee, or a flee walk the attacker has pinned in place: run
		// directly away from the attacker again.
		earlyGameDefenseDecision = earlyGamePhase == EARLY_RECOVER &&
			(state == MOVING || state == CALCULATING_PATH) ?
			String("flee_stalled") : String("flee");
		if (unwinnableAttackerOid != 0)
			earlyGameDefenseDecision = "flee_other_attacker";
		SimPlayerManager::instance()->recordPlayerBotEarlyGameDefense(identityId,
			false);
		// beginEarlyGameRecovery flees directly away from targetOid: the
		// attacker that made the fight unwinnable.
		targetOid = unwinnableAttackerOid != 0 ? unwinnableAttackerOid :
			attacker->getObjectID();
		beginEarlyGameRecovery(true);
		return true;
	}

	earlyGameDefenseDecision = "engage";
	SimPlayerManager::instance()->recordPlayerBotEarlyGameDefense(identityId,
		true);
	earlyGameTargetTtkSeconds = ttkSeconds;
	earlyGameTargetDefensive = true;
	selectTarget(attackerAgent);
	if (targetOid != attacker->getObjectID()) {
		// selectTarget refused the attacker; do not stand in a fight we cannot
		// take.
		earlyGameDefenseDecision = "engage_refused_flee";
		earlyGameTargetDefensive = false;
		targetOid = attacker->getObjectID();
		beginEarlyGameRecovery(true);
		return true;
	}
	if (earlyGamePhase != EARLY_ENGAGE)
		setEarlyGamePhase(EARLY_ENGAGE);
	scheduleActiveTick(100);
	return true;
}

void SimHunterController::beginEarlyGameRecovery(bool fleeing) {
	if (!earlyGameActive || agent == nullptr)
		return;

	SimBotEarlyGameConfig config;
	SimPlayerManager::instance()->getPlayerBotEarlyGameConfig(config);
	Vector3 current = agent->getWorldPosition();
	Vector3 recoveryPoint = earlyGameRestPoint;
	CreatureObject* target = nullptr;
	AiAgent* targetAgent = nullptr;
	if (fleeing && targetOid != 0 && targetIsLive(targetOid, target,
			targetAgent)) {
		Vector3 direction = current - target->getWorldPosition();
		direction.setZ(0.f);
		if (direction.length2d() < 0.01f)
			direction = Vector3::UNIT_X;
		else
			direction.normalize();
		recoveryPoint = current + direction * 80.f;
	}

	dropTargetObserver();
	disengageTarget(false);
	targetOid = 0;
	targetMissionWave = false;
	destructionHandled = false;
	clearHybridMovementOnCancellation();
	resetCombatGuard();
	earlyGameTargetDefensive = false;
	if (fleeing)
		SimPlayerManager::instance()->recordPlayerBotEarlyGameFlee(identityId);
	earlyGameRestStartedAtMs = System::getMiliTime();
	setEarlyGamePhase(EARLY_RECOVER);
	if (current.distanceTo2d(recoveryPoint) > config.anchorArriveMeters) {
		state = SimPlayerController::IDLE;
		earlyGameRecoverProgressPos = current;
		earlyGameRecoverProgressMs = System::getMiliTime();
		moveTo(recoveryPoint);
		// Like every other walking branch, Recover keeps its own 1 s tick. It
		// used to rely on onArrived/onPathFailed alone, and a walk an attacker
		// pinned in place fired neither: the loop froze and the novice stood
		// still until killed (verification 20260914-205211). The Recover branch
		// re-ticks every second while MOVING, so self-defence keeps running.
		scheduleActiveTick(1000);
	} else {
		state = SimPlayerController::IDLE;
		scheduleActiveTick(100);
	}
}

void SimHunterController::rememberEarlyGameUnreachableTarget(uint64 oid) {
	if (oid == 0)
		return;
	SimBotEarlyGameConfig config;
	SimPlayerManager::instance()->getPlayerBotEarlyGameConfig(config);
	uint64 nowMs = System::getMiliTime();

	// Drop expired entries and any earlier entry for this target.
	for (int i = earlyGameSkipTargetOids.size() - 1; i >= 0; --i) {
		if (earlyGameSkipTargetOids.get(i) == oid ||
				earlyGameSkipTargetUntilMs.get(i) <= nowMs) {
			earlyGameSkipTargetOids.remove(i);
			earlyGameSkipTargetUntilMs.remove(i);
		}
	}
	// Full: replace the entry that would expire soonest.
	while (earlyGameSkipTargetOids.size() > 0 &&
			earlyGameSkipTargetOids.size() >= config.unreachableSkipMaxTargets) {
		int soonest = 0;
		for (int i = 1; i < earlyGameSkipTargetUntilMs.size(); ++i) {
			if (earlyGameSkipTargetUntilMs.get(i) <
					earlyGameSkipTargetUntilMs.get(soonest))
				soonest = i;
		}
		earlyGameSkipTargetOids.remove(soonest);
		earlyGameSkipTargetUntilMs.remove(soonest);
	}
	earlyGameSkipTargetOids.add(oid);
	earlyGameSkipTargetUntilMs.add(nowMs +
		static_cast<uint64>(config.unreachableSkipSeconds) * 1000);
}

bool SimHunterController::isEarlyGameTargetSkipped(uint64 oid,
		uint64 nowMs) const {
	for (int i = 0; i < earlyGameSkipTargetOids.size(); ++i) {
		if (earlyGameSkipTargetOids.get(i) == oid &&
				nowMs < earlyGameSkipTargetUntilMs.get(i))
			return true;
	}
	return false;
}

void SimHunterController::handleEarlyGameTargetUnavailable() {
	dropTargetObserver();
	disengageTarget(false);
	targetOid = 0;
	targetMissionWave = false;
	destructionHandled = false;
	clearHybridMovementOnCancellation();
	resetCombatGuard();
	earlyGameTargetTtkSeconds = 0.f;
	earlyGameTargetDefensive = false;
	earlyGameScanAttempts = 0;
	earlyGameScanRetryAtMs = 0;
	setEarlyGamePhase(EARLY_SELECT_TARGET);
	scheduleActiveTick(100);
}

void SimHunterController::handleEarlyGameArrived() {
	if (!earlyGameActive)
		return;
	state = SimPlayerController::IDLE;
	if (earlyGamePhase == EARLY_LEAVE_CITY) {
		earlyGameScanAttempts = 0;
		earlyGameScanRetryAtMs = 0;
		setEarlyGamePhase(EARLY_SELECT_TARGET);
	} else if (earlyGamePhase == EARLY_RECOVER) {
		earlyGameRestStartedAtMs = System::getMiliTime();
	}
	scheduleActiveTick(100);
}

void SimHunterController::handleEarlyGamePathFailed() {
	if (!earlyGameActive)
		return;

	if (earlyGamePhase == EARLY_ENGAGE) {
		// Skip this target for earlyGame.unreachableSkipSeconds: the next scan
		// found the same creature and re-failed its path several times a second
		// (quick check 20260915-002501). Skips accumulate, so two unreachable
		// creatures can no longer alternate (TRIP-verify 20260915-185828).
		rememberEarlyGameUnreachableTarget(targetOid);
		handleEarlyGameTargetUnavailable();
		return;
	}
	if (earlyGamePhase == EARLY_RECOVER) {
		earlyGameRestPoint = agent->getWorldPosition();
		scheduleActiveTick(1000);
		return;
	}

	SimBotEarlyGameConfig config;
	SimPlayerManager::instance()->getPlayerBotEarlyGameConfig(config);
	earlyGameAnchorAttempts = 0;
	bool found = config.roamCitySpawners &&
		chooseEarlyGameSpawnerAnchor(config);
	for (int attempt = 0; attempt < config.anchorMaxAttempts && !found;
			++attempt)
		found = chooseEarlyGameAnchor(earlyGameRingWidened);
	if (!found && !earlyGameRingWidened) {
		earlyGameRingWidened = true;
		earlyGameAnchorAttempts = 0;
		for (int attempt = 0; attempt < config.anchorMaxAttempts && !found;
				++attempt)
			found = chooseEarlyGameAnchor(true);
	}
	if (!found) {
		earlyGameAnchor = earlyGameRestPoint;
		earlyGameAnchorArriveMeters = 0.f;
		SimPlayerManager::instance()->recordPlayerBotEarlyGameAnchorFallback(
			identityId);
	}
	setEarlyGamePhase(EARLY_LEAVE_CITY);
	publishEarlyGameMovement("path_failed", false, false, false, false);
	if (agent->getWorldPosition().distanceTo2d(earlyGameAnchor) <=
			getEarlyGameArriveMeters(config)) {
		handleEarlyGameArrived();
	} else {
		// Back off before the next walk. Re-issuing immediately turned a failing
		// route into a tight path-request loop (~8 work-loop generations a
		// second in run D); the LeaveCity tick re-issues the walk once the retry
		// time has passed.
		uint64 retryDelayMs = static_cast<uint64>(
			Math::max(1, config.scanRetrySeconds)) * 1000;
		earlyGamePathRetryAtMs = System::getMiliTime() + retryDelayMs;
		scheduleActiveTick(static_cast<int>(retryDelayMs));
	}
}

void SimHunterController::finishPlayerBotLootStep(const char* outcome,
		bool resumeLeg) {
	const bool interruptedLeg = lootStepInterruptedLeg;
	MissionDiagLog::event("LOOT_STEP", identityId,
		String("end outcome=") + outcome + " corpse=" +
		String::valueOf(lootStepClaim.corpseOid) + " phase=" + getPhaseName() +
		" resumeLeg=" + String::valueOf(resumeLeg && interruptedLeg));
	{
		// Serialized with onArrived's check. Always advance the generation (an
		// arrival may already have set WAITING and be on its way to onArrived),
		// stop the corpse walk, and mark every generation the walk used as
		// stale BEFORE dropping the guard.
		Locker lootArrivalLock(&lootArrivalMutex);
		lootWalkEndGeneration = advanceWorkLoopGeneration("lootStepEnd");
		if (state == MOVING || state == CALCULATING_PATH || state == WAITING)
			state = IDLE;
		lootStepActive = false;
	}
	lootStepClaim = PlayerBotLootClaim();
	lootStepStartedAtMs = 0;
	lootStepInterruptedLeg = false;
	if (!resumeLeg || !interruptedLeg || !isTravelCombatPhase())
		return;

	// Put the interrupted leg back on course. TRAVEL_TO_MISSION re-issues its
	// own move when idle; the other legs do not, so they are resumed here
	// through the same helper travel combat uses after a flee.
	if (phase == TRAVEL_TO_TERMINAL)
		resumeTravelLeg(missionTerminalPosition);
	else if (phase == TRAVEL_TO_LAIR)
		resumeTravelLeg(missionLairPosition);
	else if (phase == TRAVEL_HOME && travelHomeDestinationSet)
		resumeTravelLeg(travelHomeDestination);
}

bool SimHunterController::runPlayerBotLootStep(AiAgent* hunter,
		uint64 nowMs) {
	SimPlayerManager* manager = SimPlayerManager::instance();
	if (manager == nullptr || hunter == nullptr)
		return false;

	if (lootStepActive) {
		bool newAttacker = targetOid != 0;
		ManagedReference<CreatureObject*> attacker;
		ManagedReference<AiAgent*> attackerAgent;
		if (!newAttacker && (hunter->isInCombat() || recognisesThreatAttackers()))
			newAttacker = selectActiveCombatAttacker(hunter, attacker,
				attackerAgent);

		ZoneServer* zoneServer = ServerCore::getZoneServer();
		bool corpsePresent = zoneServer != nullptr &&
			zoneServer->getObject(lootStepClaim.corpseOid) != nullptr;
		const uint64 timeoutSeconds = static_cast<uint64>(Math::max(0,
			manager->getPlayerBotLootStepTimeoutSeconds()));
		const uint64 deadline = timeoutSeconds == 0 || lootStepStartedAtMs == 0 ?
			0 : lootStepStartedAtMs + timeoutSeconds * 1000;
		SimBotLootStep::ExitReason exit = SimBotLootStep::decideExit(
			!hunter->isDead(), newAttacker, corpsePresent, nowMs,
			lootStepClaim.expiresAtMs, deadline);
		if (exit == SimBotLootStep::EXIT_NONE &&
				lootStepPathFailed.exchange(false, std::memory_order_acq_rel)) {
			manager->recordPlayerBotLootAbandoned("pathFailed");
			finishPlayerBotLootStep("pathFailed", true);
			scheduleActiveTick(100);
			return true;
		}
		if (exit != SimBotLootStep::EXIT_NONE) {
			const char* outcome = "none";
			switch (exit) {
			case SimBotLootStep::EXIT_NEW_ATTACKER: outcome = "newAttacker"; break;
			case SimBotLootStep::EXIT_CLAIM_EXPIRED: outcome = "expired"; break;
			case SimBotLootStep::EXIT_CORPSE_GONE: outcome = "corpseGone"; break;
			case SimBotLootStep::EXIT_TIMEOUT: outcome = "timeout"; break;
			case SimBotLootStep::EXIT_BOT_DEAD: outcome = "botDeath"; break;
			default: break;
			}
			if (exit == SimBotLootStep::EXIT_NEW_ATTACKER)
				manager->returnPlayerBotLootClaim(lootStepClaim);
			manager->recordPlayerBotLootAbandoned(outcome);
			// Every exit but death puts the leg back at once, a new attacker
			// included: a fight-on-the-run bot never holds, so travel combat's
			// hold-end resume would never fire for it.
			finishPlayerBotLootStep(outcome,
				exit != SimBotLootStep::EXIT_BOT_DEAD);
			if (exit == SimBotLootStep::EXIT_NEW_ATTACKER)
				return false;
			if (exit != SimBotLootStep::EXIT_BOT_DEAD)
				scheduleActiveTick(100);
			return true;
		}

		if (isTravelCombatPhase() && travelCombatMode.load(
				std::memory_order_acquire) != SimBotTravelCombat::MOVE) {
			scheduleActiveTick(1000);
			return true;
		}

		Vector3 position = hunter->getWorldPosition();
		if (position.distanceTo2d(lootStepClaim.position) <=
				static_cast<float>(manager->getPlayerBotLootArriveMeters())) {
			PlayerBotLootDisposition result = manager->lootPlayerBotCorpse(
				identityId, hunter, lootStepClaim.corpseOid);
			manager->recordPlayerBotLootResult(identityId,
				lootStepClaim.corpseOid, result);
			// A corpse just out of reach is retried while the claim lives. Every
			// other result is final: a transfer or bag failure is counted and the
			// corpse stays lootable until it despawns, with no retry loop.
			if (result == PLAYER_BOT_LOOT_TOO_FAR)
				manager->returnPlayerBotLootClaim(lootStepClaim);
			finishPlayerBotLootStep("looted", true);
			scheduleActiveTick(100);
			return true;
		}

		if (state == MOVING || state == CALCULATING_PATH ||
				isTraversalActive()) {
			scheduleActiveTick(1000);
			return true;
		}

		const uint64 generationBefore = activeTickGeneration.load(
			std::memory_order_acquire);
		moveTo(lootStepClaim.position);
		if (activeTickGeneration.load(std::memory_order_acquire) ==
				generationBefore)
			scheduleActiveTick(1000);
		return true;
	}

	// On a travel leg the bot is normally walking, so a walking bot may take a
	// nearby claim there; elsewhere it must be standing idle.
	const bool travelLeg = isTravelCombatPhase();
	if (hunter->isDead() || hunter->isInCombat() || targetOid != 0 ||
			isTraversalActive() ||
			(!travelLeg && (state == MOVING || state == CALCULATING_PATH)))
		return false;
	if (travelLeg &&
			travelCombatMode.load(std::memory_order_acquire) !=
				SimBotTravelCombat::MOVE)
		return false;
	if (travelLeg && travelHoldActive.load(std::memory_order_acquire))
		return false;
	if (!travelLeg && isMovementHeld())
		return false;

	ManagedReference<CreatureObject*> attacker;
	ManagedReference<AiAgent*> attackerAgent;
	if ((hunter->isInCombat() || recognisesThreatAttackers()) &&
			selectActiveCombatAttacker(hunter, attacker, attackerAgent))
		return false;

	Zone* zone = hunter->getZone();
	if (zone == nullptr)
		return false;
	Vector3 position = hunter->getWorldPosition();
	PlayerBotLootClaim claim;
	if (!manager->takeNearestPlayerBotLootClaim(identityId,
			zone->getZoneName(), position,
			static_cast<float>(manager->getPlayerBotLootMaxDetourMeters()),
			claim))
		return false;

	lootStepPathFailed.store(false, std::memory_order_release);
	lootStepClaim = claim;
	lootStepStartedAtMs = nowMs;
	lootStepInterruptedLeg = travelLeg;
	{
		// Range and activation are published together, so a callback can never
		// see the step active with the previous walk's range. moveTo below
		// advances the generation; everything from here until the finish
		// belongs to this corpse walk.
		Locker lootArrivalLock(&lootArrivalMutex);
		lootWalkFirstGeneration = getWorkLoopGeneration() + 1;
		lootWalkEndGeneration = 0;
		lootStepActive = true;
	}
	MissionDiagLog::event("LOOT_STEP", identityId,
		"begin corpse=" + String::valueOf(claim.corpseOid) + " phase=" +
		getPhaseName() + " dist=" + String::valueOf(static_cast<int>(
			position.distanceTo2d(claim.position))) + " walking=" +
		String::valueOf(state == MOVING || state == CALCULATING_PATH));
	const uint64 generationBefore = activeTickGeneration.load(
		std::memory_order_acquire);
	moveTo(claim.position);
	if (activeTickGeneration.load(std::memory_order_acquire) ==
			generationBefore)
		scheduleActiveTick(1000);
	return true;
}

void SimHunterController::runEarlyGameTick(AiAgent* hunter, uint64 nowMs) {
	SimPlayerManager* manager = SimPlayerManager::instance();
	if (!manager->isPlayerBotEarlyGameIdentity(identityId)) {
		publishEarlyGameMovement("not_identity_exit", false, false, true, false);
		earlyGameActive = false;
		dropTargetObserver();
		disengageTarget(false);
		targetOid = 0;
		clearHybridMovementOnCancellation();
		earlyGamePathFailurePending.store(false, std::memory_order_release);
		earlyGameDestructionPending.store(false, std::memory_order_release);
		setPhase(IDLE_HOME);
		scheduleActiveTick(30000);
		return;
	}

	SimBotEarlyGameConfig config;
	manager->getPlayerBotEarlyGameConfig(config);
	publishEarlyGameMovement("tick", true, false, false, false);
	if (earlyGameDestructionPending.exchange(false,
			std::memory_order_acq_rel)) {
		uint64 destroyedTargetOid = earlyGameDestroyedTargetOid.load(
			std::memory_order_acquire);
		bool participantVerified = earlyGameDestroyedParticipantVerified.load(
			std::memory_order_acquire);
		earlyGamePathFailurePending.store(false, std::memory_order_release);
		if (destroyedTargetOid == targetOid) {
			destructionHandled = true;
			dropTargetObserver();
			if (!participantVerified)
				handleEarlyGameTargetUnavailable();
			else {
				manager->recordPlayerBotEarlyGameKill(identityId);
				beginEarlyGameRecovery(false);
			}
			return;
		}
	}
	if (earlyGamePathFailurePending.exchange(false,
			std::memory_order_acq_rel)) {
		handleEarlyGamePathFailed();
		return;
	}
	earlyGameDefenseDecision = "";
	earlyGameDefenseTtk = 0.f;
	bool defended = handleEarlyGameSelfDefense(hunter, config);
	// The all-attackers rule holds for the whole fight, not just its start:
	// pack allies and passing aggressors join after Engage began. Decided before
	// the combat sample so the sample carries this tick's decision.
	uint64 unwinnableAttackerOid = 0;
	if (!defended && earlyGamePhase == EARLY_ENGAGE) {
		unwinnableAttackerOid = findUnwinnableEarlyGameAttacker(hunter, config,
			targetOid);
		if (unwinnableAttackerOid != 0)
			earlyGameDefenseDecision = "flee_other_attacker";
	}
	publishEarlyGameCombat(hunter);
	if (defended)
		return;
	if (unwinnableAttackerOid != 0) {
		// Leave as soon as any attacker besides the target is one the novice
		// could not beat; beginEarlyGameRecovery flees directly away from
		// targetOid.
		SimPlayerManager::instance()->recordPlayerBotEarlyGameDefense(identityId,
			false);
		targetOid = unwinnableAttackerOid;
		beginEarlyGameRecovery(true);
		return;
	}
	if (earlyGamePhase == EARLY_ENGAGE &&
			isBelowEarlyGameHam(hunter, config.fleeHamFraction)) {
		beginEarlyGameRecovery(true);
		return;
	}
	if (earlyGamePhase == EARLY_SELECT_TARGET &&
		runPlayerBotLootStep(hunter, nowMs))
		return;

	if (earlyGamePhase == EARLY_RECOVER) {
		if (state == MOVING || state == CALCULATING_PATH) {
			// Self-defence above re-flees a walk pinned while in combat. A walk that
			// stops making progress with no combat to explain it (hits that never
			// restore the combat state after the flee's disengage, a wedged mover)
			// is re-issued toward the rest point instead of waited on forever.
			// Bounded: the restart resets progress, so at most once per 10 s.
			if (isEarlyGameRecoveryWalkStalled(hunter, 10000)) {
				beginEarlyGameRecovery(false);
				return;
			}
			scheduleActiveTick(1000);
			return;
		}
		if (isBelowEarlyGameHam(hunter, config.restHamFraction)) {
			// Keep the rest line reachable while waiting (see capEarlyGameWounds).
			// Only as far as keeps the rest line reachable: resting does not wipe
			// wounds taken mid-life, it just must not wait forever.
			capEarlyGameWounds(hunter,
				Math::max(0.f, 1.f - config.restHamFraction));
			scheduleActiveTick(1000);
			return;
		}
		manager->recordPlayerBotEarlyGameRest(identityId);
		setEarlyGamePhase(EARLY_TRAIN);
		scheduleActiveTick(100);
		return;
	}

	if (earlyGamePhase == EARLY_TRAIN) {
		// Training itself remains on the existing progression maintenance lane;
		// this phase only gives that lane a quiet transition point before the bot
		// starts the next deterministic outdoor leg.
		earlyGameScanAttempts = 0;
		earlyGameScanRetryAtMs = 0;
		earlyGameAnchorAttempts = 0;
		setEarlyGamePhase(EARLY_LEAVE_CITY);
		if (agent->getWorldPosition().distanceTo2d(earlyGameAnchor) <=
				getEarlyGameArriveMeters(config))
			setEarlyGamePhase(EARLY_SELECT_TARGET);
		else if (state != MOVING && state != CALCULATING_PATH)
			moveTo(earlyGameAnchor);
		scheduleActiveTick(1000);
		return;
	}

	if (earlyGamePhase == EARLY_ENGAGE) {
		if (targetOid == 0) {
			setEarlyGamePhase(EARLY_SELECT_TARGET);
			scheduleActiveTick(100);
			return;
		}
		CreatureObject* target = nullptr;
		AiAgent* targetAgent = nullptr;
		if (!targetIsLive(targetOid, target, targetAgent)) {
			handleEarlyGameTargetUnavailable();
			return;
		}
		if (hunter->getDistanceTo(target) <= getEngageRangeMeters())
			engageTarget();
		else
			moveToTarget();
		scheduleActiveTick(1000);
		return;
	}

	if (earlyGamePhase == EARLY_SELECT_TARGET) {
		if (earlyGameScanRetryAtMs != 0 && nowMs < earlyGameScanRetryAtMs) {
			scheduleActiveTick(static_cast<int>(Math::max(100,
				static_cast<int>(earlyGameScanRetryAtMs - nowMs))));
			return;
		}
		earlyGameScanRetryAtMs = 0;
		if (scanForTarget()) {
			// The pursuit keeps its own 1 s tick like every other walking
			// branch; it used to wait on arrival alone, leaving 20-40 s with no
			// tick while closing in (quick check 20260915-002501).
			scheduleActiveTick(1000);
			return;
		}
		++earlyGameScanAttempts;
		if (earlyGameScanAttempts < config.scanMaxAttempts) {
			earlyGameScanRetryAtMs = nowMs +
				static_cast<uint64>(config.scanRetrySeconds) * 1000;
			scheduleActiveTick(config.scanRetrySeconds * 1000);
			return;
		}

		bool completedBearingRotation = ++earlyGameScanBearingVisits >= 8;
		if (completedBearingRotation) {
			earlyGameScanBearingVisits = 0;
			manager->recordPlayerBotEarlyGameEmptyScanRotation(identityId);
		}
		earlyGameScanAttempts = 0;
		earlyGameAnchorAttempts = 0;
		if (completedBearingRotation)
			earlyGameRingWidened = true;
		bool found = config.roamCitySpawners &&
			chooseEarlyGameSpawnerAnchor(config);
		for (int attempt = 0; attempt < config.anchorMaxAttempts && !found;
				++attempt)
			found = chooseEarlyGameAnchor(earlyGameRingWidened);
		if (!found && !earlyGameRingWidened) {
			earlyGameRingWidened = true;
			earlyGameAnchorAttempts = 0;
			for (int attempt = 0; attempt < config.anchorMaxAttempts && !found;
					++attempt)
				found = chooseEarlyGameAnchor(true);
		}
		if (!found) {
			earlyGameAnchor = earlyGameRestPoint;
			earlyGameAnchorArriveMeters = 0.f;
			manager->recordPlayerBotEarlyGameAnchorFallback(identityId);
		}
		setEarlyGamePhase(EARLY_LEAVE_CITY);
		if (hunter->getWorldPosition().distanceTo2d(earlyGameAnchor) <=
				getEarlyGameArriveMeters(config))
			handleEarlyGameArrived();
		else
			moveTo(earlyGameAnchor);
		scheduleActiveTick(1000);
		return;
	}

	// LeaveCity scans while walking, so an eligible creature encountered en route
	// wins over the arbitrary anchor destination.
	if (scanForTarget()) {
		publishEarlyGameMovement("leave_city_target_found", false, false,
			false, false);
		scheduleActiveTick(1000);
		return;
	}
	if (hunter->getWorldPosition().distanceTo2d(earlyGameAnchor) <=
			getEarlyGameArriveMeters(config)) {
		publishEarlyGameMovement("leave_city_arrived", false, false, false,
			false);
		setEarlyGamePhase(EARLY_SELECT_TARGET);
		earlyGameScanAttempts = 0;
		earlyGameScanRetryAtMs = 0;
		scheduleActiveTick(100);
		return;
	}
	if (earlyGamePathRetryAtMs != 0) {
		if (nowMs < earlyGamePathRetryAtMs) {
			uint64 waitMs = earlyGamePathRetryAtMs - nowMs;
			scheduleActiveTick(waitMs < 100 ? 100 : static_cast<int>(waitMs));
			return;
		}
		earlyGamePathRetryAtMs = 0;
	}
	bool moveRequested = state != MOVING && state != CALCULATING_PATH;
	if (moveRequested)
		moveTo(earlyGameAnchor);
	publishEarlyGameMovement("leave_city_walk", false, false, false,
		moveRequested);
	scheduleActiveTick(1000);
}

void SimHunterController::runBuffUpTick(AiAgent* strongAgent,
		uint64 nowMs) {
	if (pveBuffTripAtHub) {
		SimPlayerManager::PveBuffTrip trip;
		if (SimPlayerManager::instance()->getPveHunterBuffTrip(identityId,
				trip) && trip.deadlineAtMs != 0 &&
				nowMs >= trip.deadlineAtMs) {
			cancelPveDoctorRequest();
			pveDoctorFallbackNeeded = pveNeedDoctorBuff;
			pveEntertainerFallbackNeeded = pveNeedEntertainerBuff;
			finishPveBuffProviderFlow();
			return;
		}
	}

	if (pveDoctorRequestActive) {
		bool needDoctor = false;
		bool needEntertainer = false;
		computeBuffNeeds(needDoctor, needEntertainer);
		if (!needDoctor) {
			uint64 bodyOid = 0;
			{
				Locker agentLock(strongAgent);
				bodyOid = strongAgent->getObjectID();
			}
			pveDoctorRequestActive = false;
			pveDoctorDeadlineSec = 0;
			pveDoctorProviderObject = nullptr;
			if (pveDoctorFeeReserved) {
				SimPlayerManager::instance()->commitPlayerBotDoctorBuffFee(
					identityId);
				pveDoctorFeeReserved = false;
			}
			SimPlayerManager::instance()->recordPveBuffSource(
				bodyOid, "real-doctor");
			if (moveToNextPveBuffProvider())
				return;
			finishPveBuffProviderFlow();
			return;
		}

		if (nowMs / 1000 >= pveDoctorDeadlineSec) {
			cancelPveDoctorRequest();
			pveDoctorFallbackNeeded = pveNeedDoctorBuff;
			if (moveToNextPveBuffProvider())
				return;
			finishPveBuffProviderFlow();
			return;
		}

		scheduleActiveTick(1000);
		return;
	}

	if (dwellUntilMs != 0 && nowMs >= dwellUntilMs) {
		dwellUntilMs = 0;
		onArrived();
	} else {
		scheduleActiveTick(dwellUntilMs == 0 ? 1000 :
			(int)Math::max(100, (int)(dwellUntilMs - nowMs)));
	}
	return;
}

void SimHunterController::runActiveTick() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr) {
		TravelDiagLog::event("TICK_ABORT", 0, "reason=noAgent");
		return;
	}

	// C2: non-blocking single-flight guard. If another runActiveTick body is
	// executing for this controller (they can be dispatched on different task
	// threads), reschedule and bail rather than run concurrently. NEVER blocks,
	// so it adds no lock-order/deadlock surface. RAII clears it on every exit.
	bool expected = false;
	if (!tickRunning.compare_exchange_strong(expected, true,
			std::memory_order_acq_rel)) {
		scheduleActiveTick(100);
		return;
	}
	struct TickGuard {
		std::atomic<bool>& flag;
		~TickGuard() { flag.store(false, std::memory_order_release); }
	} tickGuard{tickRunning};

	updateAreaAttackGuard();
	uint64 nowMs = System::getMiliTime();
	// Entry probe: distinguishes "the controller stopped ticking entirely" from
	// "it ticks but never reaches the phase branch". Rate-limited by the same
	// heartbeat interval and only while on a cross-planet leg, so it cannot spam.
	if (phase == RELOCATING || phase == BUFF_TRIP)
		TravelDiagLog::event("TICK_ENTRY", strongAgent->getObjectID(),
			"phase=" + getPhaseName() +
			" orderActive=" + String::valueOf(orderActive) +
			" gen=" + String::valueOf(activeTickGeneration.load(
				std::memory_order_relaxed)));
	if (strongAgent->isDead()) {
		const bool bazaarVisitAtDeath = phase == BAZAAR_VISIT;
		const String phaseAtDeath = getPhaseName();
		if (bazaarVisitAtDeath)
			finishBazaarVisit("botDeath", false);
		// Every queued corpse belongs to this body's OID; the respawned body gets
		// a new one and could never loot them, so all of them are dropped.
		SimPlayerManager* lootManager = SimPlayerManager::instance();
		if (lootManager != nullptr)
			lootManager->dropPlayerBotLootClaims(identityId);
		if (lootStepActive) {
			if (lootManager != nullptr)
				lootManager->recordPlayerBotLootAbandoned("botDeath");
			finishPlayerBotLootStep("botDeath", false);
		}
		if (isStructureTraversalFeatureEnabled() && isTraversalActive())
			clearStructureTraversalState("hunter_death");
		cancelPveDoctorRequest();
		SimPlayerManager::instance()->releasePlayerBotDoctorBuffFee(identityId);
		pveDoctorFeeReserved = false;
		if (!deathReported) {
			deathReported = true;
			// Captured first: beginMissionCleanup below re-records the phase as
			// MISSION_CLEANUP before the death is finalized (diagnostic).
			SimHunterTravelDiag::notePhaseAtDeath(strongAgent->getObjectID(),
				phaseAtDeath);
			SimPlayerManager::instance()->finishPveHunterBuffTrip(identityId,
				false);
			pveBuffTripAtHub = false;
			pveBuffTripReturning = false;
			if (SimPlayerManager::instance()->isPveMarketDispatchEnabled()) {
				String actualPlanet;
				if (strongAgent->getZone() != nullptr)
					actualPlanet = strongAgent->getZone()->getZoneName();
				SimPlayerManager::instance()->finishPveHunterRelocation(
					identityId, false, actualPlanet);
				clearInterplanetaryTravelState();
			}
			dropTargetObserver();
			disengageTarget(false);
			clearEngagedTelemetry();
			if (missionHuntOrder)
				beginMissionCleanup(true, "hunter_died");
			else
				setPhase(CLONE_HOME);
			SimPlayerManager::instance()->onPveHunterDied(identityId,
				strongAgent->getObjectID(), "controller", phaseAtDeath);
		}
		return;
	}

	// Planned novice bodies own the existing active tick while the early-game
	// gate is on. This branch is deliberately before the legacy order idle path:
	// no demand order, second task, or second target writer is introduced.
	if (earlyGameActive || SimPlayerManager::instance()->
			isPlayerBotEarlyGameIdentity(identityId)) {
		if (!earlyGameActive)
			beginEarlyGame();
		else
			runEarlyGameTick(strongAgent, nowMs);
		return;
	}

	if (!orderActive) {
		setPhase(IDLE_HOME);
		scheduleActiveTick(30000);
		return;
	}

	// Every active phase has a bounded lifetime. Path callbacks and world
	// readiness can be lost independently of the controller task; a phase TTL
	// prevents a hunter from retaining an assignment or observer forever.
	uint64 phaseTimeoutMs = 0;
	switch (phase) {
	case ANNOUNCE_JOB: phaseTimeoutMs = 120000; break;
	case BUFF_UP: phaseTimeoutMs = 600000; break;
	case TRAVEL_OUT: phaseTimeoutMs = 900000; break;
	case TRAVEL_TO_TERMINAL: phaseTimeoutMs = 900000; break;
	case ACCEPT_MISSION: phaseTimeoutMs = 600000; break;
	case TRAVEL_TO_MISSION: phaseTimeoutMs = 900000; break;
	case TRAVEL_TO_LAIR: phaseTimeoutMs = 900000; break;
	case ENGAGING_LAIR: phaseTimeoutMs = 900000; break;
	case AWAITING_WORLD: phaseTimeoutMs = 300000; break;
	case HUNTING: phaseTimeoutMs = missionHuntOrder ?
		SimPlayerManager::instance()->getPveMissionHuntTimeoutMs() : 0; break;
	case RETREATING: phaseTimeoutMs = 180000; break;
	case HEALING: phaseTimeoutMs = 900000; break;
	case TRAVEL_HOME: phaseTimeoutMs = 900000; break;
	case MISSION_CLEANUP: phaseTimeoutMs = 120000; break;
	// Cross-planet legs had NO timeout, so a wedged ticket-collector approach
	// pinned a hunter indefinitely (observed live: 870s stuck at leg 0 of 1 with
	// nothing to recover it). Generous, because a real multi-leg journey is slow,
	// but bounded so a stuck leg always resolves.
	case RELOCATING: phaseTimeoutMs = 900000; break;
	case BUFF_TRIP: phaseTimeoutMs = 900000; break;
	case BAZAAR_VISIT: phaseTimeoutMs = static_cast<uint64>(
		SimPlayerManager::instance()->getPlayerBotBazaarVisitTimeoutSeconds()) *
		1000; break;
	default: break;
	}
	// With travel combat the tick now runs DURING these walks, so their TTL is
	// enforced for the first time, and a walk can legitimately include holds
	// and rests. Give the covered legs twice the budget.
	if (isTravelCombatGateOn() && isTravelCombatPhase() &&
			phaseTimeoutMs == 900000)
		phaseTimeoutMs = 1800000;
	if (phaseTimeoutMs != 0 && phaseStartedAtMs != 0 &&
			nowMs - phaseStartedAtMs >= phaseTimeoutMs) {
		if (phase == BAZAAR_VISIT) {
			finishBazaarVisit("timeout");
			return;
		} else if (phase == RELOCATING || phase == BUFF_TRIP) {
			// Abandon the trip, not the hunter: cancel travel, drop the
			// relocation/trip record, and let the matchmaker re-dispatch it on
			// whatever planet it is actually standing on.
			cancelTicketCollectorTravel("phase_timeout_" + getPhaseName());
			completeOrder(true, "phase_timeout_" + getPhaseName());
		} else if (phase == TRAVEL_HOME)
			completeOrder(true, epilogueAbandonReason(
				"phase_timeout_" + getPhaseName()));
		else if (missionHuntOrder && phase == MISSION_CLEANUP)
			completeOrder(true, "cleanup_timeout");
		else if (missionHuntOrder)
			beginMissionCleanup(true, "phase_timeout_" + getPhaseName());
		else
			beginTravelHome(true);
		return;
	}

	if (isTravelCombatPhase() && travelCombatMode.load(
			std::memory_order_acquire) == SimBotTravelCombat::MOVE &&
			runPlayerBotLootStep(strongAgent, nowMs))
		return;

	if (phase == ANNOUNCE_JOB) {
		if (!strongAgent->isInCombat() && SimPlayerManager::instance()->
				shouldPlayerBotVisitBazaar(identityId)) {
			if (beginBazaarVisit(strongAgent, nowMs))
				return;
		}
		beginBuffUp();
		scheduleActiveTick(500);
		return;
	}

	if (phase == BAZAAR_VISIT) {
		ManagedReference<CreatureObject*> attacker;
		ManagedReference<AiAgent*> attackerAgent;
		if ((strongAgent->isInCombat() || recognisesThreatAttackers()) &&
				selectActiveCombatAttacker(strongAgent, attacker, attackerAgent)) {
			finishBazaarVisit("newAttacker");
			return;
		}
		if (bazaarTerminal == nullptr) {
			finishBazaarVisit("pathFailed");
			return;
		}
		Vector3 position = strongAgent->getWorldPosition();
		if (position.distanceTo2d(bazaarTerminalPosition) <=
				static_cast<float>(SimPlayerManager::instance()->
					getPlayerBotBazaarArriveMeters())) {
			SimPlayerManager* bazaarManager = SimPlayerManager::instance();
			bool listed = bazaarManager->listPlayerBotItemsAtTerminal(
				identityId, strongAgent->asCreatureObject(), bazaarTerminal);
			bool bought = bazaarManager->buyPlayerBotItemsAtTerminal(identityId,
				bazaarTerminal);
			String outcome = listed && bought ? String("listedAndBought") :
				listed ? String("listed") : bought ? String("bought") :
				String("none");
			finishBazaarVisit(outcome);
			return;
		}
		if (state == MOVING || state == CALCULATING_PATH ||
				isTraversalActive()) {
			scheduleActiveTick(1000);
			return;
		}
		const uint64 generationBefore = activeTickGeneration.load(
			std::memory_order_acquire);
		moveTo(bazaarTerminalPosition);
		if (activeTickGeneration.load(std::memory_order_acquire) ==
				generationBefore)
			scheduleActiveTick(1000);
		return;
	}

	if (phase == RELOCATING) {
		// STALL HEARTBEAT. A wedged leg produces no transitions at all, so this
		// is the only line that can catch it: it reports which travel phase the
		// bot is parked in, whether it is actually moving (distance to the
		// departure target, sampled over time), and the mover mode.
		logTravelHeartbeat("RELOCATING", nowMs);
		if (isInterplanetaryTravelActive()) {
			scheduleActiveTick(2000);
			return;
		}
		if (beginMarketRelocationLeg()) {
			scheduleActiveTick(500);
			return;
		}
		Zone* currentZone = strongAgent->getZone();
		if (currentZone != nullptr)
			order.marketTargetPlanet = currentZone->getZoneName();
		setPhase(ANNOUNCE_JOB);
		scheduleActiveTick(500);
		return;
	}

	if (phase == BUFF_TRIP) {
		logTravelHeartbeat("BUFF_TRIP", nowMs);
		SimPlayerManager* manager = SimPlayerManager::instance();
		SimPlayerManager::PveBuffTrip trip;
		if (!manager->getPveHunterBuffTrip(identityId, trip)) {
			pveBuffTripAtHub = false;
			pveBuffTripReturning = false;
			setPhase(ANNOUNCE_JOB);
			scheduleActiveTick(500);
			return;
		}

		if (trip.deadlineAtMs != 0 && nowMs >= trip.deadlineAtMs &&
				!trip.returning) {
			manager->finishPveHunterBuffTrip(identityId, false);
			clearInterplanetaryTravelState();
			pveBuffTripAtHub = false;
			pveBuffTripReturning = false;
			pveDoctorFallbackNeeded = pveNeedDoctorBuff;
			pveEntertainerFallbackNeeded = pveNeedEntertainerBuff;
			finishPveBuffProviderFlow();
			return;
		}

		if (isInterplanetaryTravelActive()) {
			scheduleActiveTick(2000);
			return;
		}
		if (beginBuffTripLeg()) {
			scheduleActiveTick(500);
			return;
		}
		return;
	}

	if (phase == BUFF_UP) {
		// Extracted verbatim so ONE guard covers every exit. The buff-provider
		// paths issue a move (moveTo / enterStructure) and return relying on an
		// arrival callback to restart the heartbeat; if that callback is ever
		// lost the controller goes silent and the BUFF_UP phase TTL, which would
		// otherwise recover it, never runs -- because the TTL lives in this same
		// heartbeat. That is the identical shape as the ENGAGING_LAIR orphan that
		// stranded a bot for 14 hours, found by auditing for the same pattern.
		// Latent rather than observed: no bot has been seen stuck in BUFF_UP, so
		// this is insurance, and it deliberately changes NOTHING about buffing --
		// not provider choice, order, interaction or thresholds.
		//
		// The generation check keeps an inner scheduleActiveTick authoritative,
		// so the existing cadences (the 1000ms doctor poll, the dwell timer) are
		// untouched and this only fires when nothing else booked a wake-up.
		// Deliberately NOT conditioned on the phase still being BUFF_UP.
		// finishPveBuffProviderFlow() can transition to TRAVEL_OUT or
		// TRAVEL_TO_TERMINAL and issue a move that books no wake-up of its own,
		// so a phase-scoped guard would hand the silence straight to the next
		// phase and disable ITS timeout instead -- the same bug one step later.
		// The generation is the only thing worth testing: unchanged means
		// nothing anywhere booked a wake-up, and whatever phase we are now in
		// still needs one. No phase reachable from here is terminal, and the
		// phases that do own a cadence (the doctor poll, the dwell timer) have
		// already bumped the generation, so this cannot shorten them.
		const uint64 tickGenerationBefore =
			activeTickGeneration.load(std::memory_order_acquire);
		runBuffUpTick(strongAgent, nowMs);
		if (activeTickGeneration.load(std::memory_order_acquire) ==
				tickGenerationBefore)
			scheduleActiveTick(1000);
		return;
	}

	if (phase == AWAITING_WORLD) {
		if (dwellUntilMs == 0 || nowMs >= dwellUntilMs) {
			dwellUntilMs = 0;
			beginHunting();
		} else {
			scheduleActiveTick((int)Math::max(100,
				(int)(dwellUntilMs - nowMs)));
		}
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_TERMINAL) {
		// F_0.9.8c: this leg starts at the lair that just died. Hold, flee or
		// rest owns the tick; it needs a resolved terminal to aim a flee at.
		if (missionTerminalResolved &&
				runTravelCombat(strongAgent, missionTerminalPosition))
			return;
		if (missionTerminalResolved &&
				(state == MOVING || state == CALCULATING_PATH))
			scheduleActiveTick(isTravelCombatGateOn() ? 1000 : 2000);
		else
			beginMissionTerminalLeg();
		return;
	}

	if (missionHuntOrder && phase == ACCEPT_MISSION) {
		if (dwellUntilMs == 0 || nowMs >= dwellUntilMs) {
			dwellUntilMs = 0;
			if (SimPlayerManager::instance()->isPveMissionBoardEnabled()) {
				if (!missionOffersGenerated) {
					// Still generating: the dwell was an offer-retry wait, not the
					// board-reading dwell. Re-run generation for another pass.
					beginMissionAccept();
				} else if (!beginNextMissionOffer()) {
					beginMissionCleanup(true, "mission_offers_expired");
				}
			} else {
				spawnMissionLair();
			}
		}
		else
			scheduleActiveTick((int)Math::max(100,
				(int)(dwellUntilMs - nowMs)));
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_MISSION) {
		PveBotMissionOffer offer;
		if (!SimPlayerManager::instance()->getPveBotMissionOffer(identityId,
				currentMissionOfferId, offer)) {
			beginMissionCleanup(true, "mission_offer_unavailable");
			return;
		}
		if (offer.revealed) {
			missionLairOid = offer.lairOid;
			missionLairPosition = offer.revealedPos;
			updateAreaAttackGuard();
			setPhase(TRAVEL_TO_LAIR);
			moveTo(missionLairPosition);
			armTravelCombatTick();
			return;
		}
		// F_0.9.8c: flee or rest owns the tick; the reveal below only runs
		// while the bot is walking.
		if (runTravelCombat(strongAgent, offer.advertisedPos))
			return;
		if (strongAgent->getWorldPosition().distanceTo2d(offer.advertisedPos) <=
				SimPlayerManager::instance()->
				getPveMissionBoardLairRevealRadiusMeters()) {
			SimPlayerManager::PveLairRevealResult revealResult =
				SimPlayerManager::instance()->revealPveBotMissionLair(
					strongAgent->getObjectID(), currentMissionOfferId);
			if (revealResult ==
					SimPlayerManager::PVE_LAIR_REVEAL_DEFER) {
				// TRANSIENT only. Deliberately NOT counted: a lair slot can be
				// held for lairTimeoutSeconds (1800s), far beyond the bound
				// below, so counting capacity here would abandon missions that
				// would have succeeded. It also RESETS the no-spawn counter:
				// capacity is only tested after a spawn point was successfully
				// found, so reaching here proves the no-spawn condition cleared,
				// and separate failure runs must not accumulate.
				missionLairRevealDeferCycles = 0;
				scheduleActiveTick(2000);
				return;
			}
			if (revealResult ==
					SimPlayerManager::PVE_LAIR_REVEAL_DEFER_NO_SPAWN_POINT) {
				// PERMANENT: the advertised point is not spawn-permitted and all
				// 32 relocation probes failed (rings 16/32/48/64m x 8 bearings).
				// Nothing about that clears, so every retry is wasted.
				// Measured 2026-09-21: one bot burned a full hour on 689
				// consecutive retries against a single point, ~17% of the roster
				// doing nothing, and only escaped via the 900s phase timeout.
				// Bounding it costs nothing the transient case needs -- capacity
				// frees within a couple of lair lifetimes -- and releases the bot
				// ~14 minutes earlier in the permanent case. The mission is lost
				// either way; only the latency differs.
				int deferMax = SimPlayerManager::instance()->
					getPveMissionLairRevealDeferMaxCycles();
				if (deferMax > 0 &&
						++missionLairRevealDeferCycles >= deferMax) {
					MissionDiagLog::event("REVEAL_DEFER_EXHAUSTED", identityId,
						MissionDiagLog::fmtVec("advertised",
							offer.advertisedPos) +
						" cycles=" + String::valueOf(
							missionLairRevealDeferCycles) +
						" offerId=" + String::valueOf(currentMissionOfferId));
					beginMissionCleanup(true, "lair_reveal_defer_exhausted");
					return;
				}
				scheduleActiveTick(2000);
				return;
			}
			missionLairRevealDeferCycles = 0;
			if (revealResult != SimPlayerManager::PVE_LAIR_REVEAL_OK) {
				beginMissionCleanup(true, "mission_lair_reveal_failed");
				return;
			}
			PveBotMissionOffer revealed;
			if (!SimPlayerManager::instance()->getPveBotMissionOffer(identityId,
					currentMissionOfferId, revealed) || !revealed.revealed) {
				beginMissionCleanup(true, "mission_lair_record_missing");
				return;
			}
			missionLairOid = revealed.lairOid;
			missionLairPosition = revealed.revealedPos;
			updateAreaAttackGuard();
			// An AiAgent has no client, so sendSystemMessage() would be a no-op.
			// Broadcast through the same spatial channel every other hunter
			// phase uses, so the reveal is actually observable in-world.
			SimPlayerManager::instance()->announcePveHunterEvent(
				strongAgent->getObjectID(), species.key,
				"Transmission Received: Mission Target has been located.  Mission waypoint has been updated to exact location");
			setPhase(TRAVEL_TO_LAIR);
			moveTo(missionLairPosition);
			armTravelCombatTick();
			return;
		}
		if (state != MOVING && state != CALCULATING_PATH)
			moveTo(offer.advertisedPos);
		scheduleActiveTick(1000);
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_LAIR) {
		if (runTravelCombat(strongAgent, missionLairPosition))
			return;
		// DIAGNOSTIC heartbeat: a decreasing dist across ticks = the hunter is
		// walking overland to the lair (reach is fine, look downstream); a flat
		// dist with state=MOVING/CALCULATING = wedged mover; state=IDLE with the
		// engine reporting neither arrival nor failure = the 2-node off-navmesh
		// fallback trap. state ints: 0 IDLE, 3 CALCULATING_PATH, 5 MOVING.
		MissionDiagLog::event("LAIR_TRAVEL", identityId,
			MissionDiagLog::fmtVec("lair", missionLairPosition) +
			" dist=" + String::valueOf(strongAgent->getWorldPosition().
				distanceTo2d(missionLairPosition)) +
			" state=" + String::valueOf((int)state) +
			" lairOid=" + String::valueOf(missionLairOid));
		// Travel combat checks HAM at the same 1s cadence as TRAVEL_TO_MISSION.
		scheduleActiveTick(isTravelCombatGateOn() ? 1000 : 2000);
		return;
	}

	if (missionHuntOrder && phase == MISSION_CLEANUP) {
		continueAfterMissionCleanup();
		return;
	}

	if (missionHuntOrder && phase == ENGAGING_LAIR &&
			SimPlayerManager::instance()->isPveMissionBoardEnabled()) {
		// The lair record can be cleared underneath a controller still standing
		// in ENGAGING_LAIR -- a mission that ended (timeout, completion) drops
		// it manager-side. Every path in engageMissionLair() below returns
		// immediately on a zero oid, so without this the phase can only be left
		// by the 900s TTL. Observed live 2026-09-25 on identities 84 and 851:
		// both sat in ENGAGING_LAIR with lairOid=0.
		// A zero lair oid is NOT actionable here, and deliberately does not
		// abandon. The destruction callback clears missionLairOid AND
		// currentMissionOfferId, in that order, from a different task-pool
		// thread; a tick landing between those writes sees no lair and no offer
		// to prove completion by, so ANY abandon decision made from this state
		// can discard a mission that actually just succeeded -- and an offer-id
		// lookup cannot close that window because the id is already gone.
		// Rescheduling instead keeps the controller alive and leaves the
		// outcome to whichever writer owns it: the completion transition if the
		// mission succeeded, or the ENGAGING_LAIR TTL, which now runs because
		// the tick is rearmed below. Bounded, and it cannot destroy a success.
		if (missionLairOid == 0) {
			scheduleActiveTick(1000);
			return;
		}
		// COMMITMENT: a wave mob in range preempts the lair only BEFORE combat
		// with the lair has started. Once engaged, the bot stays on the lair.
		//
		// This restores behaviour that was lost by accident. Before the tick was
		// rearmed in this branch, the controller went silent the moment it
		// engaged, and that silence was doing real work: the bot could not be
		// pulled off the lair and simply hammered it until it died. Making the
		// tick live re-enabled this check at 1Hz, and because damaging a lair
		// spawns more waves there is almost always a mob in range, so the bot
		// thrashed between the two and finished neither. Measured per mission:
		//
		//   lair hits (LAIR_DAMAGE source=combat)   13.8  ->  4.0
		//   lair_destroyed                          28%   ->  0%
		//   retreats per mission                    1.66  ->  2.54
		//
		// Both post-rearm windows were equally bad regardless of the engage
		// gate, which is what identifies the rearm rather than the gate as the
		// cause. Owner rule 2026-09-22: a bot that can spin a lair down while
		// taking all the aggro SHOULD, because that is the fastest XP and
		// credits -- which is exactly the committed behaviour.
		//
		// NOT paired with a retreat check here, deliberately. Retreat is only
		// evaluated in HUNTING (guarded at :3907), so pre-rearm a committed bot
		// never retreated either -- it won or died, and that is the state that
		// produced the 28% kill rate. Adding a survival path here would be new,
		// unmeasured behaviour introduced while fixing a regression that was
		// itself caused by unmeasured behaviour. If the death rate proves too
		// high, that becomes its own change with its own measurement.
		// SPAWN-TRIGGERED DISENGAGE. Owner game knowledge 2026-09-29: attacking
		// a lair past a wave threshold makes the whole camp fight you AND heal
		// the lair, so a player breaks off, clears the wave, and returns. The
		// bot was doing neither of the two sane things -- before commitment it
		// flipped to HUNTING on ANY mob in range, every tick, which was the 1Hz
		// thrash; with commitment it never broke off at all and tanked waves 2
		// and 3 while they repaired the lair.
		//
		// Confirmed in the log, not inferred: lair condition damage RECOVERS
		// while the bot is committed -- 3840->1965, 2224->353, 3382->2059 --
		// so the wave was undoing the bot's damage faster than it could deal
		// it, while the bot sat at 5-6% HAM with three attackers on it.
		//
		// The trigger is the spawnNumber TRANSITION, not "a mob is in range".
		// That is what makes this different from the thrash: it fires once per
		// wave, on the event the owner described, instead of on every tick.
		SimPlayerManager* waveManager = SimPlayerManager::instance();
		PveHuntLair waveLair;
		int waveSpawnNumber = -1;
		if (waveManager->getPveHuntLair(strongAgent->getObjectID(), waveLair) &&
				waveLair.lairOid == missionLairOid &&
				waveLair.waveObserver != nullptr)
			waveSpawnNumber = waveLair.waveObserver->getSpawnNumber();

		// The wave state belongs to a SPECIFIC lair, so it carries that lair's
		// oid and is discarded the moment the controller is looking at a
		// different one. Resetting at each rebind site instead would have
		// leaked across lairs: when lair A dies inside the 1-9s pending window,
		// the completion path binds the second held offer immediately, and a
		// member appearing at lair B would have triggered a break-off on A's
		// latch without B ever advancing a wave. Self-scoping cannot miss a
		// rebind site that gets added later.
		if (waveStateLairOid != missionLairOid) {
			waveStateLairOid = missionLairOid;
			lastSeenSpawnNumber = 0;
			pendingWaveDisengage = false;
			waveTransitionSeenAtMs = 0;
			waveDeaggroCount = 0;
			waveDeaggroDueAtMs = 0;
		}

		const bool committedToLair = engagedMissionLairOid != 0;
		if (waveSpawnNumber > 0) {
			if (lastSeenSpawnNumber > 0 &&
					waveSpawnNumber > lastSeenSpawnNumber) {
				pendingWaveDisengage = true;
				// Anchored at the TRANSITION, not at first visibility. The
				// LairAggroTask that re-aggros the wave is scheduled from the
				// transition, so anchoring the scrub anywhere later leaves the
				// re-aggroed wave hitting the bot for the difference -- which is
				// precisely the burst window this feature exists to close.
				waveTransitionSeenAtMs = nowMs;
			}
			lastSeenSpawnNumber = waveSpawnNumber;
		}

		// LATCHED, not consumed on the transition. spawnNumber advances when the
		// lair DECIDES to spawn, but the creatures arrive on tasks delayed by
		// 1-9s (LairObserverImplementation), so breaking off the instant the
		// counter moves would drop combat and hand the bot an empty field --
		// stranding it in HUNTING with nothing to scan. The latch holds until a
		// wave member is actually visible; until then the bot keeps hitting the
		// lair, which is also what a player does while the wave walks in.
		if (committedToLair && pendingWaveDisengage &&
				waveManager->getPveMissionBoardWaveDisengageEnabled()) {
			if (hasMissionWaveMobInRange()) {
				pendingWaveDisengage = false;
				// DIAGNOSTIC: waves 2 and 3 were previously invisible --
				// LAIR_FIELD only emits from the HUNTING branch, so every
				// sample ever taken read spawnNumber=1.
				MissionDiagLog::event("LAIR_WAVE", identityId, "event=disengage"
					" lairOid=" + String::valueOf(missionLairOid) +
					" spawnNumber=" + String::valueOf(waveSpawnNumber));
				disengageMissionLair();
				// The shed is DEFERRED, not run here. LairObserver schedules
				// LairAggroTask AGGRO_TASK_DELAY (4s) after the transition, and
				// wave mobs become visible from about 1s -- so a shed fired the
				// moment a mob appears is undone three seconds later when that
				// task re-aggros the whole wave, having spent one of the two
				// allowed events on nothing. Breaking off still happens now;
				// only the scrub waits until after the aggro task has run.
				//
				// Due time is measured from the TRANSITION, so an early-visible
				// mob does not push the scrub past the re-aggro and leave the
				// bot taking that wave for the difference. If visibility arrives
				// after the deadline has already passed, the next tick sheds
				// immediately, which is correct -- the aggro task has run.
				waveDeaggroDueAtMs = (waveTransitionSeenAtMs != 0 ?
					waveTransitionSeenAtMs : nowMs) + kWaveDeaggroDelayMs;
				setPhase(HUNTING);
				// Hand off through the tick rather than calling scanForTarget
				// here: the HUNTING branch owns the no-target case, and a direct
				// call would return without rearming -- the orphan pattern this
				// chunk has already fixed twice.
				scheduleActiveTick(100);
				return;
			}
			MissionDiagLog::event("LAIR_WAVE", identityId, "event=pending"
				" lairOid=" + String::valueOf(missionLairOid) +
				" spawnNumber=" + String::valueOf(waveSpawnNumber));
		}

		if (hasMissionWaveMobInRange() && !committedToLair) {
			disengageMissionLair();
			setPhase(HUNTING);
			scanForTarget();
		} else {
			// engageMissionLair() has several early returns -- out of weapon
			// range, lair despawned, already following the lair -- and only ONE
			// of them rearmed the tick. Taking any other left the controller
			// with no scheduled task at all, which silently disables everything
			// downstream of this branch: the ENGAGING_LAIR TTL, the
			// !orderActive reset, the whole phase machine. Nordihi Dunaco spent
			// 14 hours in ENGAGING_LAIR that way, stopped 2.47m outside weapon
			// range of a lair, drifting on leftover movement with a dead
			// controller (the mission itself had already ended: MISSION_SUMMARY
			// endReason=timeout).
			//
			// Rearmed HERE rather than at each return so a future early return
			// cannot reintroduce the same orphan. The generation check keeps an
			// inner scheduleActiveTick() authoritative: scheduleActiveTick
			// bumps activeTickGeneration, so an unchanged generation means
			// nothing inside scheduled anything and this is the only rearm.
			const uint64 tickGenerationBefore =
				activeTickGeneration.load(std::memory_order_acquire);
			engageMissionLair();
			if (phase == ENGAGING_LAIR &&
					activeTickGeneration.load(std::memory_order_acquire) ==
						tickGenerationBefore)
				scheduleActiveTick(1000);
		}
		return;
	}

	if (phase == TRAVEL_HOME || phase == DELIVER || phase == DONE) {
		if (phase == TRAVEL_HOME && missionHuntOrder &&
				travelHomeDestinationSet &&
				runTravelCombat(strongAgent, travelHomeDestination))
			return;
		scheduleActiveTick(1000);
		return;
	}

	if (missionHuntOrder && missionLairOid != 0) {
		PveHuntLair lair;
		if (!SimPlayerManager::instance()->getPveHuntLair(
				agent == nullptr ? 0 : agent->getObjectID(), lair) ||
				!lair.alive || lair.cleanupQueued) {
			if (SimPlayerManager::instance()->isPveMissionBoardEnabled()) {
				PveBotMissionOffer offer;
				if (SimPlayerManager::instance()->getPveBotMissionOffer(
						identityId, currentMissionOfferId, offer) && offer.completed) {
					scheduleActiveTick(100);
					return;
				}
			}
			beginMissionCleanup(true, "lair_unavailable");
			return;
		}
	}

	// Deferred wave de-aggro: fires on the first tick at or after the due time,
	// which is set when a wave transition breaks the bot off the lair. Checked
	// before the phase branches because the break-off moves the controller to
	// HUNTING, so an ENGAGING_LAIR-scoped check would never run.
	if (waveDeaggroDueAtMs != 0 && nowMs >= waveDeaggroDueAtMs) {
		waveDeaggroDueAtMs = 0;
		shedWaveAggro(strongAgent, lastSeenSpawnNumber);
	}

	if (huntStartedAtMs != 0 && nowMs - huntStartedAtMs >=
			(uint64)SimPlayerManager::instance()->getPveHunterTimeoutSeconds() * 1000) {
		dropTargetObserver();
		disengageTarget(false);
		beginTravelHome(true);
		return;
	}

	if (phase == TRAVEL_OUT) {
		scheduleActiveTick(2000);
		return;
	}

	static const int MAX_LAIR_RETREAT_REFLEES = 5;
	if (phase == RETREATING) {
		const bool retreatDefense = isLairRetreatDefenseOn();
		if (state == MOVING || state == CALCULATING_PATH) {
			scheduleActiveTick(retreatDefense ? 1000 : 2000);
			return;
		}
		if (retreatDefense) {
			ManagedReference<CreatureObject*> chaser;
			ManagedReference<AiAgent*> chaserAgent;
			const bool chased = selectActiveCombatAttacker(strongAgent, chaser,
				chaserAgent);
			if (SimBotTravelCombat::retreatLegEnded(chased, lairRetreatReflees,
					MAX_LAIR_RETREAT_REFLEES) == SimBotTravelCombat::RETREAT_REFLEE) {
				lairRetreatReflee(strongAgent, chaser.get());
				return;
			}
		}
		finishRetreatMove();
		return;
	}

	if (phase == HEALING) {
		if (isLairRetreatDefenseOn()) {
			ManagedReference<CreatureObject*> attacker;
			ManagedReference<AiAgent*> attackerAgent;
			const bool attacked = selectActiveCombatAttacker(strongAgent, attacker,
				attackerAgent);
			const SimBotTravelCombat::RetreatStep step =
				SimBotTravelCombat::whileResting(attacked,
					isBelowRetreatThreshold(strongAgent), lairRetreatReflees,
					MAX_LAIR_RETREAT_REFLEES);
			if (step == SimBotTravelCombat::RETREAT_REFLEE) {
				setPhase(RETREATING);
				lairRetreatReflee(strongAgent, attacker.get());
				return;
			}
			// Fight back rather than stand and be killed. Once HAM is back the
			// normal resume below hands the fight to HUNTING.
			// Honoured even at full HAM: the hunt resumes only once the bot is
			// undisturbed, so it never turns back to its old target with an
			// interceptor still on it.
			if (step == SimBotTravelCombat::RETREAT_DEFEND) {
				const bool defending = defendAgainstInterceptor(strongAgent,
					attacker.get());
				if (defending && (lastLairRetreatDefendLogMs == 0 ||
						nowMs - lastLairRetreatDefendLogMs >= 5000)) {
					lastLairRetreatDefendLogMs = nowMs;
					MissionDiagLog::event("LAIR_RETREAT", identityId,
						"event=defend botHamFraction=" + String::valueOf(
							simHunterMinHamFraction(strongAgent)) +
						" attackerLevel=" + String::valueOf(attacker->getLevel()) +
						" reflees=" + String::valueOf(lairRetreatReflees) +
						" lairOid=" + String::valueOf(missionLairOid));
				}
				scheduleActiveTick(1000);
				return;
			}
		}
		if (isReadyToResume(strongAgent)) {
			// Neither resume call is guaranteed to book a wake-up: scanForTarget
			// returns false with nothing scheduled when the field is empty or
			// the bot is still in combat, and moveToTarget returns silently on
			// an in-flight pursuit. Since F_0.9.8e the bot fights back while it
			// retreats, so it routinely finishes the last wave member here and
			// resumes onto an EMPTY field -- five of six bots went silent in
			// HUNTING that way on 2026-10-11/12, outlived the mission timeout
			// and stood in the wild for hours. Same orphan as ENGAGING_LAIR and
			// BUFF_UP; same generation guard.
			const uint64 tickGenerationBefore =
				activeTickGeneration.load(std::memory_order_acquire);
			setPhase(HUNTING);
			if (targetOid != 0)
				moveToTarget();
			else
				scanForTarget();
			if (activeTickGeneration.load(std::memory_order_acquire) ==
					tickGenerationBefore)
				scheduleActiveTick(1000);
		} else {
			scheduleActiveTick(SimPlayerManager::instance()->
				getPveHunterActiveTickSeconds() * 1000);
		}
		return;
	}

	if (phase != HUNTING) {
		scheduleActiveTick(2000);
		return;
	}

	if (isBelowRetreatThreshold(strongAgent)) {
		beginRetreat();
		return;
	}

	if (missionHuntOrder && checkMissionSocialAggro(strongAgent))
		return;

	if (runPlayerBotLootStep(strongAgent, nowMs))
		return;

	if (strongAgent->isInCombat()) {
		// Select and engage the actual attacker before any reachability or
		// stalemate bookkeeping. The defender list can contain a stale first
		// entry while a nearer creature is the one still attacking. runActiveTick
		// is the sole owner of targetOid / the mission observer among the tick
		// loops; onTick only self-defends during travel legs (see onTick).
		ManagedReference<CreatureObject*> selectedAttacker =
			engageActiveAttacker(strongAgent);
		if (selectedAttacker == nullptr) {
			scheduleActiveTick(2000);
			return;
		}

		CreatureObject* defender = selectedAttacker.get();
		AiAgent* defenderAgent = defender == nullptr ? nullptr :
			defender->asAiAgent();
		bool reachable = defender != nullptr && defenderAgent != nullptr &&
			!defender->isDead() && !defender->isIncapacitated() &&
			defender->getParent() == nullptr &&
			strongAgent->getDistanceTo(defender) <=
				SimPlayerManager::instance()->getPveHunterScanRadiusMeters() + 24.f;

		if (!reachable) {
			if (++phantomCombatTicks >= 6) {
				if (defender != nullptr && targetOid != 0 &&
						targetOid == defender->getObjectID() &&
						targetMatchesSpecies(defender)) {
					clearStaleCombat("phantom_combat");
				} else {
					CreatureObject* missionTarget = nullptr;
					AiAgent* missionTargetAgent = nullptr;
					if (targetOid != 0 && targetIsLive(targetOid, missionTarget,
							missionTargetAgent))
						resetInterceptorCombat();
					else
						clearStaleCombat("phantom_combat");
				}
			}
			scheduleActiveTick(2000);
			return;
		}

		phantomCombatTicks = 0;
		int selfHam = strongAgent->getHAM(CreatureAttribute::HEALTH) +
			strongAgent->getHAM(CreatureAttribute::ACTION) +
			strongAgent->getHAM(CreatureAttribute::MIND);
		int defenderHam = defender->getHAM(CreatureAttribute::HEALTH) +
			defender->getHAM(CreatureAttribute::ACTION) +
			defender->getHAM(CreatureAttribute::MIND);
		if (stalemateDefenderOid != defender->getObjectID()) {
			stalemateDefenderOid = defender->getObjectID();
			stalemateSelfHam = selfHam;
			stalemateDefenderHam = defenderHam;
			lastCombatProgressMs = nowMs;
		} else if (selfHam != stalemateSelfHam || defenderHam != stalemateDefenderHam) {
			stalemateSelfHam = selfHam;
			stalemateDefenderHam = defenderHam;
			lastCombatProgressMs = nowMs;
		} else if (lastCombatProgressMs != 0 &&
				nowMs - lastCombatProgressMs > 45000) {
			bool missionTargetEngaged = targetOid != 0 &&
				targetOid == defender->getObjectID() &&
				targetMatchesSpecies(defender);
			if (missionTargetEngaged) {
				clearStaleCombat("combat_stalemate");
			} else {
				CreatureObject* missionTarget = nullptr;
				AiAgent* missionTargetAgent = nullptr;
				if (targetOid != 0 && targetIsLive(targetOid, missionTarget,
						missionTargetAgent))
					resetInterceptorCombat();
				else
					clearStaleCombat("combat_stalemate");
			}
			scheduleActiveTick(SimPlayerManager::instance()->
				getPveHunterActiveTickSeconds() * 1000);
			return;
		}

		scheduleActiveTick(SimPlayerManager::instance()->
			getPveHunterActiveTickSeconds() * 1000);
		return;
	}

	resetCombatGuard();
	if (targetOid != 0) {
		CreatureObject* target = nullptr;
		AiAgent* targetAgent = nullptr;
		if (!targetIsLive(targetOid, target, targetAgent))
			handleTargetUnavailable();
		else if (strongAgent->getDistanceTo(target) <= getEngageRangeMeters())
			engageTarget();
		else
			moveToTarget();
	} else {
		bool foundTarget = scanForTarget();
		if (missionHuntOrder && missionLairOid != 0) {
			SimPlayerManager* manager = SimPlayerManager::instance();
			PveHuntLair lair;
			Vector<ManagedReference<CreatureObject*> > lairMembers =
				manager->lairObserverSnapshot(strongAgent->getObjectID(),
					missionLairOid);
			manager->getPveHuntLair(strongAgent->getObjectID(), lair);
			uint64 targetHomeOid = 0;
			ZoneServer* zoneServer = ServerCore::getZoneServer();
			ManagedReference<SceneObject*> targetObject = zoneServer == nullptr ?
				nullptr : zoneServer->getObject(targetOid);
			CreatureObject* target = targetObject == nullptr ? nullptr :
				targetObject->asCreatureObject();
			if (target != nullptr) {
				AiAgent* targetAgent = target->asAiAgent();
				ManagedReference<SceneObject*> targetHome = targetAgent == nullptr ?
					nullptr : targetAgent->getHomeObject().get();
				if (targetHome != nullptr)
					targetHomeOid = targetHome->getObjectID();
			}
			// Indices 0,1,2 are HEALTH, STRENGTH, CONSTITUTION -- not the primary
			// pools. The primaries are 0, 3, 6, so a raw 0..2 loop reports a
			// healthy bot while ACTION or MIND is empty, which is exactly the
			// "bot too weak" case this diagnostic exists to catch. Same pool
			// list isBelowRetreatThreshold uses (:2597).
			float hamFraction = 1.f;
			{
				const uint8 hamPools[] = {CreatureAttribute::HEALTH,
					CreatureAttribute::ACTION, CreatureAttribute::MIND};
				Locker agentLock(strongAgent);
				for (uint8 i = 0; i < 3; ++i) {
					int maxHam = strongAgent->getMaxHAM(hamPools[i]);
					int ham = strongAgent->getHAM(hamPools[i]);
					if (maxHam > 0)
						hamFraction = Math::min(hamFraction,
							static_cast<float>(ham) / static_cast<float>(maxHam));
				}
			}
			// DIAGNOSTIC: fires each HUNTING scan on a mission. foundTarget=1
			// repeatedly => the field remains occupied or is reacquired;
			// foundTarget=0 with the engage gate on => it transitions to
			// ENGAGING_LAIR (see LAIR_ENGAGE next).
			MissionDiagLog::event("LAIR_FIELD", identityId,
				"foundTarget=" + String::valueOf(foundTarget) +
				" engageGate=" + String::valueOf(SimPlayerManager::instance()->
					getPveMissionBoardLairEngageAfterFieldClear()) +
				" lairOid=" + String::valueOf(missionLairOid) +
				" targetOid=" + String::valueOf(targetOid) +
				" targetHomeOid=" + String::valueOf(targetHomeOid) +
				" lairMembersAlive=" + String::valueOf(lairMembers.size()) +
				" spawnNumber=" + String::valueOf(lair.waveObserver == nullptr ?
					-1 : lair.waveObserver->getSpawnNumber()) +
				" botHamFraction=" + String::valueOf(hamFraction) +
				" exemptedOverLevel=" +
					String::valueOf(scanExemptedOverLevel) +
				" exemptedMaxLevel=" +
					String::valueOf(scanExemptedMaxLevel) +
				" levelCeiling=" + String::valueOf(scanLevelCeiling));
		}
		// "Nothing I am allowed to fight" is NOT "field clear". If every wave
		// mob was rejected by a progression guard, entering ENGAGING_LAIR would
		// be undone next tick by hasMissionWaveMobInRange, which does not apply
		// those guards -- an oscillation that runs to the mission timeout.
		// Abandon instead, and say so.
		//
		// scanRejectedOverLevel can no longer fire here: the ceiling is exempt
		// once the lair is revealed, which is exactly this branch's condition.
		// It is kept in the test rather than deleted so that narrowing that
		// exemption later cannot silently reintroduce the oscillation -- and
		// because a wave_above_level_band abandon reappearing on the dashboard
		// is then a precise signal that the exemption stopped covering a case
		// it used to. An earlier revision of this comment called an over-level
		// wave proof of a DISPATCH defect; that was wrong, and the wrong
		// conclusion is what this fix corrects. Dispatch was issuing correctly
		// bounded contracts (difficulty 8-9 to a level-3 body) and the ceiling
		// was rejecting their level-10 mobs on an unrelated scale.
		if (!foundTarget && (scanRejectedOverLevel || scanRejectedPack) &&
				missionHuntOrder && missionLairOid != 0) {
			// Kept for the defensive case above; pack is the reachable reason.
			const String ineligibleReason = scanRejectedOverLevel ?
				String("wave_above_level_band") : String("wave_pack_avoided");
			int ineligibleTier = 0;
			SimPlayerManager::instance()->isPlayerBotProgressionIdentity(
				identityId, ineligibleTier);
			MissionDiagLog::event("WAVE_INELIGIBLE", identityId,
				"tier=" + String::valueOf(ineligibleTier) +
				" lairOid=" + String::valueOf(missionLairOid) +
				" offerId=" + String::valueOf(currentMissionOfferId) +
				" overLevel=" + String::valueOf(scanRejectedOverLevel) +
				" pack=" + String::valueOf(scanRejectedPack) +
				" reason=" + ineligibleReason);
			beginMissionCleanup(true, ineligibleReason);
			return;
		}
		if (!foundTarget && missionHuntOrder &&
				SimPlayerManager::instance()->isPveMissionBoardEnabled() &&
				missionLairOid != 0 && SimPlayerManager::instance()->
				getPveMissionBoardLairEngageAfterFieldClear()) {
			MissionDiagLog::event("LAIR_ENGAGE", identityId,
				MissionDiagLog::fmtVec("lair", missionLairPosition));
			setPhase(ENGAGING_LAIR);
			// Deliberately NOT a direct engageMissionLair() call. This is the
			// FIRST engagement attempt, and calling it here returned without
			// rearming the tick on every one of its early-return paths -- the
			// exact orphan the ENGAGING_LAIR branch was just fixed to prevent,
			// reachable before that branch ever runs. Handing off through the
			// tick makes the guarded branch the single owner of every
			// engagement attempt, first and subsequent alike.
			scheduleActiveTick(100);
			return;
		}
		moveToPatrolPoint(nowMs);
	}

	scheduleActiveTick(SimPlayerManager::instance()->
		getPveHunterActiveTickSeconds() * 1000);
}

bool SimHunterController::isBelowRetreatThreshold(AiAgent* hunter) const {
	if (hunter == nullptr)
		return false;

	float minimum = 1.f;
	const uint8 hamPools[] = {CreatureAttribute::HEALTH,
		CreatureAttribute::ACTION, CreatureAttribute::MIND};
	for (uint8 i = 0; i < 3; ++i) {
		uint8 pool = hamPools[i];
		int maximum = hunter->getMaxHAM(pool);
		if (maximum > 0)
			minimum = Math::min(minimum,
				(float)hunter->getHAM(pool) / (float)maximum);
	}
	return minimum < SimPlayerManager::instance()->getPveHunterRetreatHamPct() / 100.f;
}

bool SimHunterController::isReadyToResume(AiAgent* hunter) const {
	if (hunter == nullptr)
		return false;

	float minimum = 1.f;
	const uint8 hamPools[] = {CreatureAttribute::HEALTH,
		CreatureAttribute::ACTION, CreatureAttribute::MIND};
	for (uint8 i = 0; i < 3; ++i) {
		uint8 pool = hamPools[i];
		int maximum = hunter->getMaxHAM(pool);
		if (maximum > 0)
			minimum = Math::min(minimum,
				(float)hunter->getHAM(pool) / (float)maximum);
	}
	return minimum >= SimPlayerManager::instance()->getPveHunterResumeHamPct() / 100.f;
}

bool SimHunterController::targetIsLive(uint64 oid, CreatureObject*& target,
		AiAgent*& targetAgent) const {
	target = nullptr;
	targetAgent = nullptr;
	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<SceneObject*> object = zoneServer == nullptr ? nullptr :
		zoneServer->getObject(oid);
	target = object == nullptr ? nullptr : object->asCreatureObject();
	targetAgent = target == nullptr ? nullptr : target->asAiAgent();
	if (target == nullptr || targetAgent == nullptr || target->isDead() ||
			target->isIncapacitated() || target->getParent() != nullptr)
		return false;
	return targetMatchesSpecies(target);
}

bool SimHunterController::targetMatchesSpecies(CreatureObject* target) const {
	if (target == nullptr || target->isPlayerCreature() ||
		SimPlayerManager::instance()->isSimPresenceCreature(target))
		return false;

	AiAgent* targetAgent = target->asAiAgent();
	if (targetAgent == nullptr || targetAgent->getSimPlayerBot())
		return false;
	if (earlyGameActive)
		return true;

	if (missionHuntOrder && SimPlayerManager::instance()->
			isPveMissionBoardEnabled()) {
		if (missionLairOid == 0)
			return false;
		ManagedReference<SceneObject*> homeObject =
			targetAgent->getHomeObject().get();
		return homeObject != nullptr && homeObject->getObjectID() ==
			missionLairOid;
	}

	const CreatureTemplate* targetTemplate = targetAgent->getCreatureTemplate();
	if (targetTemplate == nullptr)
		return false;

	String targetName = targetTemplate->getTemplateName().toLowerCase();
	return species.templateFilter.isEmpty() ||
		targetName.contains(species.templateFilter.toLowerCase());
}

bool SimHunterController::scanForTarget() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr || strongAgent->isInCombat())
		return false;

	// Query the zone's QuadTree directly (like the proven spike scan at
	// SimPlayerManager :9662). getCloseObjects() is NOT maintained for these
	// spawned/moved bots (same class of gap as NPC updateActiveAreas), so it
	// returns an empty/stale set even when creatures are right next to the
	// hunter (lair packs) — the cause of hunters never acquiring a target.
	scanRejectedOverLevel = false;
	scanRejectedPack = false;
	scanExemptedOverLevel = 0;
	scanExemptedMaxLevel = 0;
	scanLevelCeiling = 0;
	Zone* zone = nullptr;
	Vector3 hunterPosition;
	// Snapshotted with the position under the SAME agent lock: group
	// membership lifts pack avoidance (owner rule), and reading it per
	// candidate would take the agent lock inside the scan loop -- the O(n)
	// lock pattern ARCHI's performance note calls out.
	bool inGroup = false;
	{
		Locker agentLock(strongAgent);
		zone = strongAgent->getZone();
		if (zone == nullptr)
			return false;
		hunterPosition = strongAgent->getWorldPosition();
		inGroup = strongAgent->getGroup() != nullptr;
	}

	float scanRadius =
		SimPlayerManager::instance()->getPveHunterScanRadiusMeters();
	SortedVector<TreeEntry*> closeObjects;
	zone->getInRangeObjects(hunterPosition.getX(), hunterPosition.getZ(),
		hunterPosition.getY(), scanRadius, &closeObjects, true, true);

	// ONE pack policy for BOTH branches. It used to be derived separately: the
	// early-game loop read the config flag alone, so a grouped or elite-capable
	// bot still dodged packs there while the generic loop correctly let it
	// engage. Snapshotted once, before either branch.
	bool progressionBot = false;
	int progressionTier = 0;
	bool avoidPacks = false;
	SimBotEarlyGameConfig progressionConfig;
	if (identityId != 0) {
		SimPlayerManager* manager = SimPlayerManager::instance();
		progressionBot = manager->isPlayerBotProgressionIdentity(
			identityId, progressionTier);
		if (progressionBot) {
			manager->getPlayerBotEarlyGameConfig(progressionConfig);
			// Owner rule: pack avoidance is a novice crutch. It lifts once the
			// bot is past a novice elite profession ("xx1x Teras Kasi") or is in
			// a group -- such a bot SHOULD take a whole lair's aggro, which is
			// the fastest XP and credits.
			avoidPacks = progressionConfig.avoidGroupAggroTargets &&
				!manager->isPlayerBotEliteCapable(identityId) &&
				!inGroup;
			scanLevelCeiling =
				progressionTier + progressionConfig.levelBandAbove;
		}
	}

	// The level ceiling is a TARGET-SELECTION guard: it stops a bot picking a
	// fight it cannot win out in the world. It must NOT apply to the mobs of a
	// contract the bot has already accepted and walked to. Those were bounded
	// when the mission was issued -- selectPveBotMissionLairSpawn keeps the lair
	// inside hunterLevel +/- 5 -- so a second ceiling on a DIFFERENT scale is
	// pure harm: the ceiling is measured in progression TIER (6 for a graduated
	// 4xxx brawler) while a wave mob is measured in creature LEVEL. A correctly
	// issued difficulty-8 dantooine_bol_pigmy_neutral_small spawns
	// lesser_plains_bol at level 10 -- ONE above the tier-6 ceiling of 9 -- so
	// every wave was refused and every mission abandoned wave_above_level_band.
	// Measured over ~2h on 2026-09-22 after the ceiling began following the bot:
	// 0 kills, 14 deaths, 5 of 5 missions abandoned. The bot stood in its own
	// lair refusing to fight back until the wave killed it.
	//
	// The ceiling still applies while TRAVELLING to the contract, which is the
	// case the owner actually reported (a tier-6 bot engaging five huurtons in
	// open country on the way to a lair). It lifts only once the lair is
	// revealed. Pack avoidance is deliberately NOT lifted here: the owner's rule
	// is that it stays for a solo sub-elite bot, and no observed abandon has
	// been attributed to it (abandonReasons carried wave_above_level_band only).
	const bool onAcceptedContract = missionHuntOrder && missionLairOid != 0;

	if (earlyGameActive) {
		// Hoisted out of the candidate loop: one config read, one identity
		// snapshot, one agent lock for the weapon numbers.
		SimBotEarlyGameConfig earlyConfig;
		SimPlayerManager::instance()->getPlayerBotEarlyGameConfig(earlyConfig);
		SimBotIdentity earlyIdentity;
		if (!SimPlayerManager::instance()->snapshotPlayerBotIdentity(
				identityId, earlyIdentity))
			return false;
		ManagedReference<WeaponObject*> earlyWeapon;
		float earlyRawDamage = 0.f;
		float earlyAttackPeriod = 0.f;
		{
			Locker agentLock(strongAgent);
			earlyWeapon = strongAgent->getWeapon();
			if (earlyWeapon == nullptr)
				return false;
			earlyRawDamage = (earlyWeapon->getMinDamage() +
				earlyWeapon->getMaxDamage()) * 0.5f;
			earlyAttackPeriod = earlyWeapon->getAttackSpeed();
			// A metre inside the weapon's maximum so attacks actually connect.
			earlyGameEngageRangeMeters = Math::max(2.f,
				static_cast<float>(earlyWeapon->getMaxRange()) - 1.f);
		}

		ManagedReference<AiAgent*> bestTarget;
		float bestDistance = 0.f;
		float bestTtk = 0.f;
		int bestLevelDiff = 0;
		uint64 scanNowMs = System::getMiliTime();
		for (int i = 0; i < closeObjects.size(); ++i) {
			SceneObject* object = static_cast<SceneObject*>(closeObjects.get(i));
			CreatureObject* candidate = object == nullptr ? nullptr :
				object->asCreatureObject();
			AiAgent* candidateAgent = candidate == nullptr ? nullptr :
				candidate->asAiAgent();
			if (candidate == nullptr || candidateAgent == nullptr ||
					candidate == strongAgent.get() || candidate->isInCombat() ||
					!targetMatchesSpecies(candidate) ||
					!candidate->isAttackableBy(strongAgent.get()))
				continue;

			// hunterPosition is the snapshot taken under the agent lock above;
			// re-reading the live world position per candidate cost an extra
			// read for no added accuracy within a single scan.
			float distance = hunterPosition.distanceTo(
				candidate->getWorldPosition());
			if (distance > scanRadius)
				continue;
			// Novices seek creatures that do not pull their group into the fight
			// (owner rule). Only PACK creatures assist their social group
			// (AiAgent::notifyPackMobs); herds such as kreetles and minor worrts
			// fight alone and are fair game.
			if (avoidPacks) {
				const CreatureTemplate* candidateTemplate =
					candidateAgent->getCreatureTemplate();
				if (candidateTemplate != nullptr && candidateTemplate->isPack())
					continue;
			}
			if (isEarlyGameTargetSkipped(candidate->getObjectID(), scanNowMs))
				continue;
			// Prefer the creature closest to the novice's own level, then the
			// nearest: the band edges are taken only when nothing closer is here.
			int candidateLevel = candidate->getLevel();
			int levelDiff = candidateLevel >= earlyIdentity.skillTier ?
				candidateLevel - earlyIdentity.skillTier :
				earlyIdentity.skillTier - candidateLevel;
			if (bestTarget != nullptr && (levelDiff > bestLevelDiff ||
					(levelDiff == bestLevelDiff && distance >= bestDistance)))
				continue;

			float candidateTtk = 0.f;
			if (!calculateEarlyGameTtk(candidate, earlyConfig,
					earlyIdentity.skillTier, earlyWeapon, earlyRawDamage,
					earlyAttackPeriod, candidateTtk))
				continue;
			bestTarget = candidateAgent;
			bestDistance = distance;
			bestLevelDiff = levelDiff;
			bestTtk = candidateTtk;
		}
		if (bestTarget == nullptr)
			return false;
		earlyGameTargetTtkSeconds = bestTtk;
		earlyGameTargetDefensive = false;
		selectTarget(bestTarget);
		return true;
	}

	// A progression character keeps its novice guards AFTER graduating. They
	// used to live only in the early-game branch above, so the moment a novice
	// graduated to missions it silently lost both and picked fights it could not
	// win. Observed in game 2026-09-22: a tier-6 4xxx brawler walked onto
	// Dantooine and engaged five huurtons -- a PACK creature well above its
	// band -- and died. Legacy planless hunters are unaffected.
	//
	// The level CEILING is the hard guard (the same reasoning as
	// calculateEarlyGameTtk): it is the one that stops a bot fighting something
	// far above it. The floor is deliberately not applied here -- a graduated
	// bot on a contract should still be allowed to clear weak wave mobs -- and
	// the ceiling itself is skipped once the bot is standing at an accepted
	// contract's lair (onAcceptedContract, see the reasoning at its definition).
	for (int i = 0; i < closeObjects.size(); ++i) {
		SceneObject* object = static_cast<SceneObject*>(closeObjects.get(i));
		CreatureObject* candidate = object == nullptr ? nullptr :
			object->asCreatureObject();
		AiAgent* candidateAgent = candidate == nullptr ? nullptr :
			candidate->asAiAgent();
		if (candidate == nullptr || candidateAgent == nullptr ||
				candidate == strongAgent.get())
			continue;
		if (!targetMatchesSpecies(candidate) ||
				!candidate->isAttackableBy(strongAgent.get()) ||
				strongAgent->getWorldPosition().distanceTo(
					candidate->getWorldPosition()) > scanRadius)
			continue;
		if (progressionBot) {
			const CreatureTemplate* candidateTemplate =
				candidateAgent->getCreatureTemplate();
			// BOTH predicates are evaluated before skipping. Short-circuiting on
			// the pack branch hid the over-level condition for a mob that is
			// both -- exactly the observed huurton case -- so an over-level pack
			// wave would have reported only wave_pack_avoided and buried the
			// mis-issued contract that the level ceiling is there to expose.
			const bool packRejected = avoidPacks &&
				candidateTemplate != nullptr && candidateTemplate->isPack();
			const int candidateLevel = candidateAgent->getLevel();
			const bool aboveBand = candidateLevel >
				progressionTier + progressionConfig.levelBandAbove;
			// Exempt, but counted: the ceiling does not block a mob of a
			// contract the bot accepted, and the margin by which it would have
			// is still published so a genuinely mis-sized contract stays
			// diagnosable.
			if (aboveBand && onAcceptedContract) {
				++scanExemptedOverLevel;
				scanExemptedMaxLevel = Math::max(scanExemptedMaxLevel,
					candidateLevel);
			}
			const bool overLevel = aboveBand && !onAcceptedContract;
			if (packRejected)
				scanRejectedPack = true;
			if (overLevel)
				scanRejectedOverLevel = true;
			if (packRejected || overLevel)
				continue;
		}

		selectTarget(candidateAgent);
		return true;
	}
	return false;
}

bool SimHunterController::isMissionWaveMob(CreatureObject* target) const {
	return targetMatchesSpecies(target);
}

bool SimHunterController::hasMissionWaveMobInRange() const {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return false;
	Zone* zone = nullptr;
	Vector3 hunterPosition;
	{
		Locker agentLock(strongAgent);
		zone = strongAgent->getZone();
		if (zone == nullptr)
			return false;
		hunterPosition = strongAgent->getWorldPosition();
	}
	float scanRadius = SimPlayerManager::instance()->
		getPveHunterScanRadiusMeters();
	SortedVector<TreeEntry*> closeObjects;
	zone->getInRangeObjects(hunterPosition.getX(), hunterPosition.getZ(),
		hunterPosition.getY(), scanRadius, &closeObjects, true, true);
	for (int i = 0; i < closeObjects.size(); ++i) {
		SceneObject* object = static_cast<SceneObject*>(closeObjects.get(i));
		CreatureObject* candidate = object == nullptr ? nullptr :
			object->asCreatureObject();
		if (candidate != nullptr && isMissionWaveMob(candidate) &&
				candidate->isAttackableBy(strongAgent.get()) &&
				hunterPosition.distanceTo(candidate->getWorldPosition()) <=
				scanRadius)
			return true;
	}
	return false;
}

bool SimHunterController::selectActiveCombatAttacker(AiAgent* hunter,
		ManagedReference<CreatureObject*>& outAttacker,
		ManagedReference<AiAgent*>& outAttackerAgent, bool* outViaThreat) {
	outAttacker = nullptr;
	outAttackerAgent = nullptr;
	if (outViaThreat != nullptr)
		*outViaThreat = false;

	if (hunter == nullptr)
		return false;

	ManagedReference<CreatureObject*> nearestAttacker;
	ManagedReference<AiAgent*> nearestAttackerAgent;
	float nearestDistance = 0.f;

	ManagedReference<CreatureObject*> followedAttacker;
	ManagedReference<AiAgent*> followedAttackerAgent;
	float followedDistance = 0.f;
	bool hasFollowedAttacker = false;

	// Snapshot the defender list and follow target under the hunter lock so a
	// concurrent (@preLocked) removeDefender cannot shrink the vector between the
	// size() read and the element access — getSafe locks its own DeltaVector
	// mutex but does not bounds-check a stale index. Filtering and distance math
	// then run on the retained strong references outside the lock.
	Vector<ManagedReference<CreatureObject*> > candidates;
	uint64 followedOid = 0;
	{
		Locker agentLock(hunter);
		const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
			hunter->getDefenderList();
		if (defenderList != nullptr) {
			for (int i = 0; i < defenderList->size(); ++i) {
				ManagedReference<SceneObject*> defender = defenderList->getSafe(i);
				CreatureObject* candidate = defender == nullptr ? nullptr :
					defender->asCreatureObject();
				if (candidate != nullptr)
					candidates.add(candidate);
			}
		}
		ManagedReference<SceneObject*> followObject = hunter->getFollowObject();
		followedOid = followObject == nullptr ? 0 : followObject->getObjectID();
	}

	// F_0.9.8c attacker recognition. CombatManager::startCombat puts the BOT on
	// the attacking creature's defender list and the creature on the bot's
	// THREAT map -- never on the bot's own defender list. Read only the defender
	// list and a creature can hit a walking bot to death unseen: in 37 of 57
	// travel deaths the bot first registered a fight with HAM already under 30
	// percent. Threat entries outlive a fight (clearCombatState leaves the map),
	// so one only counts while that creature is in combat AND aimed at this bot
	// -- following it or holding it as a defender -- read under the creature's
	// own lock with the hunter's released.
	Vector<ManagedReference<CreatureObject*> > threatCandidates;
	if (recognisesThreatAttackers()) {
		{
			Locker agentLock(hunter);
			ThreatMap* threatMap = hunter->getThreatMap();
			if (threatMap != nullptr) {
				for (int i = 0; i < threatMap->size(); ++i) {
					ManagedReference<TangibleObject*> entry =
						threatMap->elementAt(i).getKey();
					CreatureObject* creature = entry == nullptr ? nullptr :
						entry->asCreatureObject();
					if (creature != nullptr && !candidates.contains(creature))
						threatCandidates.add(creature);
				}
			}
		}
		const uint64 hunterOid = hunter->getObjectID();
		for (int i = 0; i < threatCandidates.size(); ++i) {
			ManagedReference<CreatureObject*> creature = threatCandidates.get(i);
			AiAgent* creatureAgent = creature == nullptr ? nullptr :
				creature->asAiAgent();
			if (creatureAgent == nullptr || creature == hunter)
				continue;
			bool aimedAtHunter = false;
			{
				Locker creatureLock(creature);
				if (creature->isInCombat() && !creature->isDead()) {
					ManagedReference<SceneObject*> follow =
						creatureAgent->getFollowObject();
					aimedAtHunter = (follow != nullptr &&
						follow->getObjectID() == hunterOid) ||
						creature->hasDefender(hunter);
				}
			}
			if (aimedAtHunter)
				candidates.add(creature);
		}
	}

	if (candidates.size() == 0)
		return false;

	float reachabilityRadius = SimPlayerManager::instance()->
		getPveHunterScanRadiusMeters() + 24.f;

	for (int i = 0; i < candidates.size(); ++i) {
		ManagedReference<CreatureObject*> candidateReference = candidates.get(i);
		CreatureObject* candidate = candidateReference.get();
		AiAgent* candidateAgent = candidate == nullptr ? nullptr :
			candidate->asAiAgent();

		if (candidate == nullptr || candidateAgent == nullptr ||
				candidate == hunter || candidate->isPlayerCreature() ||
				SimPlayerManager::instance()->isSimPresenceCreature(candidate) ||
				candidateAgent->getSimPlayerBot() || candidate->isDead() ||
				candidate->isIncapacitated() || candidate->getParent() != nullptr ||
				!candidate->isAttackableBy(hunter))
			continue;

		float distance = hunter->getDistanceTo(candidate);
		if (distance > reachabilityRadius)
			continue;

		ManagedReference<AiAgent*> candidateAgentReference = candidateAgent;
		if (nearestAttacker == nullptr || distance < nearestDistance) {
			nearestAttacker = candidateReference;
			nearestAttackerAgent = candidateAgentReference;
			nearestDistance = distance;
		}

		if (followedOid != 0 && candidate->getObjectID() == followedOid) {
			followedAttacker = candidateReference;
			followedAttackerAgent = candidateAgentReference;
			followedDistance = distance;
			hasFollowedAttacker = true;
		}
	}

	if (nearestAttacker == nullptr)
		return false;

	float weaponRange = getEngageRangeMeters();
	float hysteresis = weaponRange * 0.5f;
	if (hasFollowedAttacker && followedDistance <= weaponRange &&
			!(nearestDistance + hysteresis < followedDistance)) {
		outAttacker = followedAttacker;
		outAttackerAgent = followedAttackerAgent;
	} else {
		outAttacker = nearestAttacker;
		outAttackerAgent = nearestAttackerAgent;
	}

	if (outViaThreat != nullptr && outAttacker != nullptr)
		*outViaThreat = threatCandidates.contains(outAttacker);
	return outAttacker != nullptr && outAttackerAgent != nullptr;
}

ManagedReference<CreatureObject*> SimHunterController::engageActiveAttacker(
		AiAgent* hunter) {
	// If the current mission target just died, let its queued destruction handoff
	// (onHuntDestruction) credit the kill before we retarget. That handoff is
	// rejected once targetOid advances (targetOid != destroyedTargetOid), so
	// proactively promoting a new target here would silently drop a legitimate
	// kill. The handoff runs promptly for a real kill and clears targetOid /
	// destructionHandled, after which retargeting resumes normally next tick.
	//
	// Only defer when an observer is actually installed on this target
	// (observerTargetOid == targetOid && targetObserver != nullptr). targetOid is
	// assigned while approaching but the observer is registered only once in
	// weapon range (engageTarget); if the target dies externally before that, no
	// handoff will ever come, so deferring would suppress combat forever. Without
	// an observer we fall through and retarget/clean up the dead target normally.
	if (targetOid != 0 && !destructionHandled &&
			observerTargetOid == targetOid && targetObserver != nullptr) {
		ZoneServer* zoneServer = ServerCore::getZoneServer();
		ManagedReference<SceneObject*> current = zoneServer == nullptr ?
			nullptr : zoneServer->getObject(targetOid);
		CreatureObject* currentCreature = current == nullptr ? nullptr :
			current->asCreatureObject();
		if (currentCreature != nullptr && currentCreature->isDead())
			return nullptr;
	}

	ManagedReference<CreatureObject*> attacker;
	ManagedReference<AiAgent*> attackerAgent;
	if (!selectActiveCombatAttacker(hunter, attacker, attackerAgent)) {
		CreatureObject* missionTarget = nullptr;
		AiAgent* missionTargetAgent = nullptr;
		if (targetOid != 0 && targetIsLive(targetOid, missionTarget,
				missionTargetAgent)) {
			resetInterceptorCombat();
		} else {
			if (++phantomCombatTicks >= 6)
				clearStaleCombat("phantom_combat");
		}
		return nullptr;
	}

	ManagedReference<SceneObject*> currentFollow;
	if (hunter != nullptr)
		currentFollow = hunter->getFollowObject();
	if (currentFollow != nullptr &&
			currentFollow->getObjectID() == attacker->getObjectID()) {
		if (targetOid != 0 && targetOid == attacker->getObjectID() &&
				targetMatchesSpecies(attacker.get()) &&
				hunter->getDistanceTo(attacker) > getEngageRangeMeters())
			moveToTarget();
		return attacker;
	}

	if (targetMatchesSpecies(attacker.get())) {
		selectTarget(attackerAgent.get());
		return attacker;
	}

	if (!defendAgainstInterceptor(hunter, attacker.get()))
		return nullptr;

	return attacker;
}

void SimHunterController::selectTarget(AiAgent* target) {
	if (target == nullptr || target->getObjectID() == 0 ||
		!targetMatchesSpecies(target))
		return;

	uint64 selectedOid = target->getObjectID();
	if (targetOid != selectedOid) {
		dropTargetObserver();
		targetOid = selectedOid;
		targetMissionWave = missionHuntOrder && SimPlayerManager::instance()->
			isPveMissionBoardEnabled() && isMissionWaveMob(target);
		destructionHandled = false;
		if (earlyGameActive) {
			earlyGameScanBearingVisits = 0;
			setEarlyGamePhase(EARLY_ENGAGE);
		} else
			SimPlayerManager::instance()->recordPveHunterPhase(identityId,
				agent == nullptr ? 0 : agent->getObjectID(), getPhaseName(), targetOid);
	}

	moveToTarget();
}

void SimHunterController::moveToTarget() {
	if (targetOid == 0 || agent == nullptr)
		return;

	CreatureObject* target = nullptr;
	AiAgent* targetAgent = nullptr;
	if (!targetIsLive(targetOid, target, targetAgent)) {
		handleTargetUnavailable();
		return;
	}

	if (agent->getDistanceTo(target) <= getEngageRangeMeters()) {
		pursuing = false;
		engageTarget();
		return;
	}

	// Finding 4: do NOT re-issue the pursuit path every tick - moveTo()
	// advances the generation and discards the in-flight path, so under
	// pathfinder latency the hunter never arrives. Keep the current path
	// unless we are not yet pursuing OR the target has moved materially
	// (half weapon range) since we last pathed to it. The stuck-watchdog +
	// onPathFailed handle a genuinely stalled path.
	Vector3 targetPos = target->getWorldPosition();
	bool alreadyPursuing = pursuing &&
		(state == MOVING || state == CALCULATING_PATH);
	float moveThresh = SimPlayerManager::instance()->
		getPveHunterWeaponRangeMeters() * 0.5f;
	if (alreadyPursuing &&
			pursuitTargetPos.distanceTo(targetPos) < moveThresh)
		return;

	// Movement and combat are mutually exclusive on the shared base. Clear
	// both sides first; moveTo() otherwise refuses to enqueue a path.
	disengageTarget(false);
	pursuitTargetPos = targetPos;
	pursuing = true;
	moveTo(targetPos);
}

void SimHunterController::engageMissionLair() {
	if (!orderActive || !missionHuntOrder || missionLairOid == 0 ||
			agent == nullptr)
		return;
	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<SceneObject*> object = zoneServer == nullptr ? nullptr :
		zoneServer->getObject(missionLairOid);
	TangibleObject* lair = object == nullptr ? nullptr :
		object->asTangibleObject();
	if (lair == nullptr || lair->getZone() == nullptr) {
		PveBotMissionOffer offer;
		if (SimPlayerManager::instance()->getPveBotMissionOffer(identityId,
				currentMissionOfferId, offer) && offer.completed) {
			scheduleActiveTick(100);
			return;
		}
		beginMissionCleanup(true, "mission_lair_unavailable");
		return;
	}

	const float weaponRange = SimPlayerManager::instance()->
		getPveHunterWeaponRangeMeters();
	// ENGAGE AS SOON AS THE LAIR IS IN WEAPON RANGE. An earlier revision gated
	// this on a 24m standoff instead, and that was a measured regression:
	//
	//   outcome            before (118 missions)   with the 24m gate (45)
	//   lair_destroyed     33  (28%)               2  (4%)
	//   max_retreat_cycles  4  (3%)                22 (49%)
	//
	// The cause is an interaction with retreat, not with the standoff distance.
	// retreatRangeMeters is 40, so a bot that retreats lands OUTSIDE a 28m gate
	// and is forced into a fresh approach -- and movement and combat are
	// mutually exclusive on the shared base, so it absorbs wave damage the whole
	// way back without fighting, drops below the retreat threshold again, and
	// burns all three cycles. At a 48m gate the same bot is already in range
	// when it finishes retreating and simply resumes firing.
	//
	// INVARIANT for anyone tightening this again: the engage gate must stay
	// ABOVE retreatRangeMeters, or every retreat becomes a defenceless walk.
	//
	// The distance band was never the problem. Bots already engaged at an
	// average of 19.7m before the change (max 47.7) and 21.6m after (max 27.8);
	// capping the tail bought nothing and cost the retreat interaction. The
	// justification for the 24m gate came from reading the distances of
	// SUCCESSFUL kills only, which is selection bias -- the failures' distances
	// were never in the sample.
	const float standoff = weaponRange * 0.5f;
	if (agent->getDistanceTo(lair) > weaponRange) {
		// DIAGNOSTIC: hunter decided to attack the lair but is out of weapon
		// range and is closing. A dist that never falls below range across ticks
		// = it cannot close on the lair structure (final-approach stall).
		// Walk to a STANDOFF point on the approach bearing, not to the lair's
		// own position. A lair is a solid structure, so its world position is
		// inside its collision volume: a path to that point does not resolve,
		// no arrival callback fires, and the bot parks just outside weapon
		// range forever. Measured 2026-09-25: dist=50.4725 against range=48 --
		// short by 2.47m, and never closed again.
		//
		// The standoff is only the MOVE TARGET, never a gate: it exists so the
		// path has a reachable destination on open ground. The bot will engage
		// as soon as it crosses weaponRange on the way in, so it normally never
		// reaches this point -- which is correct, and is what keeps engagement
		// timing identical to the pre-standoff behaviour.
		const Vector3 lairPosition = lair->getWorldPosition();
		const Vector3 agentPosition = agent->getWorldPosition();
		const float bearingX = agentPosition.getX() - lairPosition.getX();
		const float bearingY = agentPosition.getY() - lairPosition.getY();
		const float bearingLength =
			Math::sqrt(bearingX * bearingX + bearingY * bearingY);
		Vector3 approachPoint = lairPosition;
		if (bearingLength > 0.001f && bearingLength > standoff) {
			const float scale = standoff / bearingLength;
			approachPoint = Vector3(
				lairPosition.getX() + bearingX * scale,
				lairPosition.getY() + bearingY * scale,
				lairPosition.getZ());
		}
		// Re-issuing moveTo() every tick would starve the approach it is meant
		// to drive: moveTo() advances the work-loop generation, so a 1s rearm
		// against a multi-second path calculation cancels each path just as it
		// resolves and the bot never arrives -- the same starvation moveToTarget
		// guards against at :4678. Keep the current leg unless it is not
		// running, or the standoff point has moved materially since we pathed
		// to it. A genuinely stalled leg is still handled by the stuck watchdog
		// and onPathFailed.
		const bool approachLegRunning = pursuing &&
			(state == MOVING || state == CALCULATING_PATH);
		const bool approachPointMoved =
			pursuitTargetPos.distanceTo(approachPoint) >= standoff * 0.5f;
		MissionDiagLog::event("LAIR_ATTACK", identityId, "phase=approach"
			" dist=" + String::valueOf(agent->getDistanceTo(lair)) +
			" range=" + String::valueOf(weaponRange) +
			" standoff=" + String::valueOf(standoff) +
			" legRunning=" + String::valueOf(approachLegRunning) +
			" reissue=" + String::valueOf(
				!approachLegRunning || approachPointMoved) +
			" " + MissionDiagLog::fmtVec("approach", approachPoint));
		if (approachLegRunning && !approachPointMoved)
			return;
		// Movement and combat are mutually exclusive on the shared base, the
		// same reason moveToTarget() clears both before pathing.
		disengageTarget(false);
		pursuitTargetPos = approachPoint;
		pursuing = true;
		moveTo(approachPoint);
		return;
	}

	ManagedReference<SceneObject*> followed = agent->getFollowObject();
	const bool holdEnabled = SimPlayerManager::instance()->
		getPveMissionBoardLairEngagementHold();
	const bool recordMatches = engagedMissionLairOid == missionLairOid;
	const bool followingLair = followed != nullptr &&
		followed->getObjectID() == missionLairOid;

	if (!holdEnabled) {
		// Gate off: byte-for-byte the previous behaviour, so flipping the gate
		// is an exact revert rather than an approximate one.
		if (recordMatches && followingLair)
			return;
	} else {
		// Gate on: the engagement RECORD alone is never trusted. It is a cached
		// controller field, and the re-point below can itself make the follow
		// object the lair again -- so validating only in the hold branch would
		// leave the plain guard above able to match a STALE record forever and
		// return without ever calling startCombat. That is a bot standing at a
		// lair, pointing at it, never attacking: the same orphan shape this
		// chunk exists to remove. Both exits are validated against the real
		// link, and lair liveness is rechecked under the lock because
		// destruction is handed off asynchronously.
		bool linkIntact = false;
		float holdHamFraction = 1.f;
		bool repointed = false;
		const uint64 nowMs = System::getMiliTime();
		const uint64 retargetIntervalMs = static_cast<uint64>(
			SimPlayerManager::instance()->
				getPveMissionBoardLairRetargetIntervalMs());
		if (recordMatches) {
			Locker agentLock(agent);
			const uint8 hamPools[] = {CreatureAttribute::HEALTH,
				CreatureAttribute::ACTION, CreatureAttribute::MIND};
			for (uint8 i = 0; i < 3; ++i) {
				int maxHam = agent->getMaxHAM(hamPools[i]);
				int ham = agent->getHAM(hamPools[i]);
				if (maxHam > 0)
					holdHamFraction = Math::min(holdHamFraction,
						static_cast<float>(ham) / static_cast<float>(maxHam));
			}
			linkIntact = lair->getZone() != nullptr &&
				agent->isInCombat() && agent->hasDefender(lair);
			if (linkIntact && !followingLair &&
					(lastLairRetargetMs == 0 ||
						nowMs - lastLairRetargetMs >= retargetIntervalMs)) {
				lastLairRetargetMs = nowMs;
				// Cheap re-point only. No disengage, no startCombat -- the lair
				// is still a defender, so this just aims the bot back at it.
				agent->setTargetObject(lair);
				repointed = true;
			}
		}

		if (linkIntact) {
			if (repointed)
				MissionDiagLog::event("LAIR_ATTACK", identityId, "phase=hold"
					" lairOid=" + String::valueOf(missionLairOid) +
					" followOid=" + String::valueOf(followed == nullptr ? 0 :
						followed->getObjectID()) +
					" hamFraction=" + String::valueOf(holdHamFraction) +
					" intervalMs=" + String::valueOf(retargetIntervalMs));
			return;
		}
		// Link genuinely lost -- fall through and rebuild it below.
	}

	// WHY the held engagement was lost, and the state it was lost in. A bot that
	// kills a lair issues startCombat ONCE; a bot that dies re-issues it dozens
	// of times against the same encounter. These are the only ways the guards
	// above can fail, and nothing else records which one did.
	const uint64 previousEngagedOid = engagedMissionLairOid;
	const uint64 followedOid = followed == nullptr ? 0 :
		followed->getObjectID();
	const char* reengageCause =
		previousEngagedOid == 0 ? "first" :
		previousEngagedOid != missionLairOid ? "record_lost" :
		followed == nullptr ? "follow_null" :
		followedOid != missionLairOid ? "follow_other" : "link_lost";

	// WHAT the bot was doing with the creature it drifted onto. follow_other is
	// ambiguous on its own: it looks identical whether the agent was yanked away
	// by combat retargeting or was legitimately CHASING a wounded creature to
	// finish the kill. Those want opposite fixes -- the first wants the
	// engagement held, the second must not be interrupted at all, because the
	// objective is to kill every wave creature AND the lair for the XP, not to
	// reach the lair quickly. Owner correction 2026-09-29.
	//
	// So the follow target's own state is recorded: a low health fraction with
	// the bot as its attacker means chasing a nearly-dead creature; full health
	// on a fresh mob means retargeting. Bot skill tier and weapon go with it
	// because damage output is not uniform across the roster -- a 4xxx brawler
	// and a novice need different numbers of swings for the same creature, and
	// every per-mission figure so far has pooled the six bots and hidden that.
	float followHealthFraction = -1.f;
	int followLevel = -1;
	bool followIsLairMember = false;
	bool followCreatureTargetsBot = false;
	bool followBotTargetsCreature = false;
	String botWeaponName("none");
	float followDistance = -1.f;
	String followName("none");
	if (followed != nullptr) {
		CreatureObject* followCreature = followed->asCreatureObject();
		if (followCreature != nullptr) {
			Locker followLock(followCreature);
			// MIN across all three primary pools, not HEALTH alone: a creature
			// can be driven to incapacitation through ACTION or MIND, so a
			// health-only reading would call a nearly-dead creature healthy and
			// invert the chasing-versus-retargeting diagnosis this exists for.
			const uint8 followPools[] = {CreatureAttribute::HEALTH,
				CreatureAttribute::ACTION, CreatureAttribute::MIND};
			for (uint8 i = 0; i < 3; ++i) {
				int maxPool = followCreature->getMaxHAM(followPools[i]);
				if (maxPool <= 0)
					continue;
				float fraction = static_cast<float>(
					followCreature->getHAM(followPools[i])) /
					static_cast<float>(maxPool);
				if (followHealthFraction < 0.f || fraction < followHealthFraction)
					followHealthFraction = fraction;
			}
			followLevel = followCreature->getLevel();
			followName = followCreature->getCreatureName().toString();
			// This is the CREATURE's view: it has the bot as a defender, i.e.
			// the creature is fighting the bot. It does NOT mean the bot has
			// damaged it, which is the other direction and is read below under
			// the agent lock. Logging only this one would support the opposite
			// diagnosis just as well.
			followCreatureTargetsBot = followCreature->hasDefender(agent);
			AiAgent* followAgent = followCreature->asAiAgent();
			if (followAgent != nullptr) {
				ManagedReference<SceneObject*> home =
					followAgent->getHomeObject().get();
				followIsLairMember = home != nullptr &&
					home->getObjectID() == missionLairOid;
			}
		}
		followDistance = agent->getWorldPosition().distanceTo(
			followed->getWorldPosition());
	}
	int botSkillTier = 0;
	SimPlayerManager::instance()->isPlayerBotProgressionIdentity(
		identityId, botSkillTier);

	// Captured BEFORE disengageMissionLair(), which calls clearCombatState(true)
	// and drops every defender. Snapshotting after it would report
	// inCombat=false and defenders=0 on every re-engagement regardless of what
	// actually happened -- describing the failure as the opposite of what it is.
	float hamFraction = 1.f;
	bool inCombat = false;
	int defenders = 0;
	{
		Locker agentLock(agent);
		// Same primary pools isBelowRetreatThreshold uses -- 0,3,6, not a raw
		// 0..2 loop, which would report HEALTH/STRENGTH/CONSTITUTION instead.
		const uint8 hamPools[] = {CreatureAttribute::HEALTH,
			CreatureAttribute::ACTION, CreatureAttribute::MIND};
		for (uint8 i = 0; i < 3; ++i) {
			int maxHam = agent->getMaxHAM(hamPools[i]);
			int ham = agent->getHAM(hamPools[i]);
			if (maxHam > 0)
				hamFraction = Math::min(hamFraction,
					static_cast<float>(ham) / static_cast<float>(maxHam));
		}
		inCombat = agent->isInCombat();
		const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
			agent->getDefenderList();
		if (defenderList != nullptr)
			defenders = defenderList->size();
		// The BOT's view of the same pair, read here rather than inside the
		// creature lock above so the agent lock is never taken while holding a
		// creature lock. Together the two directions separate "chasing
		// something I am killing" from "something grabbed my attention".
		if (followed != nullptr)
			followBotTargetsCreature = agent->hasDefender(followed);
		WeaponObject* botWeapon = agent->getWeapon();
		if (botWeapon != nullptr)
			botWeaponName = botWeapon->getDisplayedName();
	}

	disengageMissionLair();
	// Snapshot the combat result + distance under the agent lock, then log AFTER
	// releasing it -- never hold the actor lock across MissionDiagLog file I/O.
	bool combatStarted = false;
	float engageDist = 0.f;
	{
		Locker agentLock(agent);
		engageDist = agent->getDistanceTo(lair);
		combatStarted = CombatManager::instance()->startCombat(agent, lair);
		if (combatStarted) {
			// The lair is a TangibleObject combat defender. This deliberately does
			// not touch targetOid or observerTargetOid; those fields remain owned
			// by the HUNTING target tick.
			agent->setTargetObject(lair);
			agent->activateAiBehavior(true);
		}
	}
	// DIAGNOSTIC: the decisive combat probe. started=1 means the hunter is in
	// weapon range and CombatManager accepted the lair as a defender; if this
	// fires repeatedly yet the lair never dies (no LAIR_DESTROYED_*), the gap is
	// combat damage/behaviour, not movement or targeting.
	MissionDiagLog::event("LAIR_ATTACK", identityId, "phase=engage"
		" started=" + String::valueOf(combatStarted) +
		" lairOid=" + String::valueOf(missionLairOid) +
		" dist=" + String::valueOf(engageDist) +
		" reengageCause=" + String(reengageCause) +
		" prevEngaged=" + String::valueOf(previousEngagedOid) +
		" followOid=" + String::valueOf(followedOid) +
		" hamFraction=" + String::valueOf(hamFraction) +
		" inCombat=" + String::valueOf(inCombat) +
		" defenders=" + String::valueOf(defenders) +
		" botTier=" + String::valueOf(botSkillTier) +
		" followName=" + followName +
		" followLevel=" + String::valueOf(followLevel) +
		" followHealth=" + String::valueOf(followHealthFraction) +
		" followDist=" + String::valueOf(followDistance) +
		" followIsMember=" + String::valueOf(followIsLairMember) +
		" creatureTargetsBot=" + String::valueOf(followCreatureTargetsBot) +
		" botTargetsCreature=" + String::valueOf(followBotTargetsCreature) +
		" botWeapon=" + botWeaponName);
	if (!combatStarted)
		return;
	engagedMissionLairOid = missionLairOid;
	updateAreaAttackGuard();
	lastLairRetargetMs = 0;
	// lastSeenSpawnNumber is deliberately NOT reset here. It tracks the
	// LAIR's wave counter, not this engagement: clearing it on engage or
	// disengage makes the next sample a fresh baseline, so the very
	// transition that should trigger a break-off is swallowed. It is
	// reset LAZILY, on an oid mismatch against
	// waveStateLairOid, not at any lair-clear site.
}

// Shed the aggro the previous wave holds on this bot, at a wave transition.
//
// Owner-approved scope 2026-09-29, four conditions, ALL required:
//   1. a mission lair fight only -- never a market hunt, so krayts and other
//      wilderness creatures are never touched;
//   2. members of THIS lair only -- lairObserverSnapshot is keyed by the lair
//      oid, so membership is structural rather than a predicate that could
//      drift;
//   3. solo only -- never while grouped with a player;
//   4. gated, default off.
//
// Hard-capped at kMaxWaveDeaggroPerLair, which is 2 because the lair itself
// cannot produce more transitions: LairObserverImplementation::checkForNewSpawns
// increments at reveal, again on the FIRST point of damage, again past half
// condition, then `default: return false`. Three waves, two transitions after
// the initial spawn. The cap is deliberately a constant, not a tunable -- it is
// a property of the upstream observer, not a preference.
//
// This removes the bot from each creature's defender list, which is the half
// that persists: disengageMissionLair already called clearCombatState and so
// cleared the BOT's list, but the creatures keep holding the bot and keep
// attacking. Nothing is despawned, pacified or damaged, and a creature re-aggros
// normally the moment the bot attacks it again -- this resets the current stack,
// it does not confer immunity.
void SimHunterController::shedWaveAggro(AiAgent* strongAgent, int spawnNumber) {
	if (strongAgent == nullptr || !missionHuntOrder || missionLairOid == 0)
		return;

	SimPlayerManager* manager = SimPlayerManager::instance();
	if (!manager->getPveMissionBoardWaveDeaggroEnabled())
		return;
	if (waveDeaggroCount >= kMaxWaveDeaggroPerLair)
		return;

	bool solo = false;
	{
		Locker agentLock(strongAgent);
		solo = strongAgent->getGroup() == nullptr;
	}
	if (!solo)
		return;

	Vector<ManagedReference<CreatureObject*> > members =
		manager->lairObserverSnapshot(strongAgent->getObjectID(),
			missionLairOid);

	int shed = 0;
	for (int i = 0; i < members.size(); ++i) {
		ManagedReference<CreatureObject*> member = members.get(i);
		if (member == nullptr || member == strongAgent)
			continue;
		// Locks taken and released per creature, never held across the loop:
		// the house idiom for a bilateral defender edit is the agent lock with
		// the other party cross-locked against it (:5314).
		Locker agentLock(strongAgent);
		Locker memberLock(member, strongAgent);
		if (!member->hasDefender(strongAgent))
			continue;
		member->removeDefender(strongAgent);
		strongAgent->removeDefender(member);
		++shed;
	}

	// The cap is spent only when something was actually shed. Consuming it on a
	// no-op would burn one of the two allowed events against an empty defender
	// list and leave the real stack in place.
	if (shed > 0)
		++waveDeaggroCount;
	MissionDiagLog::event("LAIR_WAVE", identityId, "event=deaggro"
		" lairOid=" + String::valueOf(missionLairOid) +
		" spawnNumber=" + String::valueOf(spawnNumber) +
		" shed=" + String::valueOf(shed) +
		" members=" + String::valueOf(members.size()) +
		" deaggroCount=" + String::valueOf(waveDeaggroCount));
}

void SimHunterController::disengageMissionLair() {
	if (engagedMissionLairOid == 0) {
		return;
	}
	ManagedReference<AiAgent*> strongAgent = agent;
	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<SceneObject*> object = zoneServer == nullptr ? nullptr :
		zoneServer->getObject(engagedMissionLairOid);
	TangibleObject* lair = object == nullptr ? nullptr :
		object->asTangibleObject();
	if (strongAgent != nullptr) {
		Locker agentLock(strongAgent);
		if (lair != nullptr) {
			Locker lairLock(lair, strongAgent);
			strongAgent->removeDefender(lair);
			lair->removeDefender(strongAgent);
		}
		strongAgent->clearCombatState(true);
		strongAgent->setTargetObject(nullptr);
		strongAgent->setFollowObject(nullptr);
		strongAgent->setWatchObject(nullptr);
		strongAgent->clearQueueActions(true);
		state = SimPlayerController::IDLE;
	}
	engagedMissionLairOid = 0;
	updateAreaAttackGuard();
	lastLairRetargetMs = 0;
	// lastSeenSpawnNumber is deliberately NOT reset here. It tracks the
	// LAIR's wave counter, not this engagement: clearing it on engage or
	// disengage makes the next sample a fresh baseline, so the very
	// transition that should trigger a break-off is swallowed. It is
	// reset LAZILY, on an oid mismatch against
	// waveStateLairOid, not at any lair-clear site.
}

void SimHunterController::onPveBotMissionLairDestroyed(uint64 lairOid) {
	if (!orderActive || !missionHuntOrder || !SimPlayerManager::instance()->
			isPveMissionBoardEnabled() || missionCleanupRequested ||
			missionLairOid != lairOid)
		return;

	disengageMissionLair();
	missionLairOid = 0;
	currentMissionOfferId = 0;
	missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
	updateMissionAdds(0);
	if (beginNextMissionOffer())
		return;

	missionOffersGenerated = false;
	missionOfferAttempts = 0;
	missionOfferVisitEpoch = 0;
	missionOfferIds.removeAll();
	terminalResolveWaitCycles = 0;
	beginMissionTerminalLeg();
}

void SimHunterController::engageTarget() {
	if (targetOid == 0 || agent == nullptr)
		return;

	CreatureObject* target = nullptr;
	AiAgent* targetAgent = nullptr;
	if (!targetIsLive(targetOid, target, targetAgent) || targetAgent == nullptr ||
			!target->isAttackableBy(agent.get()))
		return;

	if (agent->getDistanceTo(target) > getEngageRangeMeters()) {
		moveToTarget();
		return;
	}

	if (observerTargetOid != targetOid)
		registerTargetObserver(targetOid);
	if (observerTargetOid != targetOid || targetObserver == nullptr)
		return;

	{
		Locker agentLock(agent);
		if (!CombatManager::instance()->startCombat(agent, target))
			return;

		// startCombat() sets the defender/follow target and both combat states;
		// retain the explicit target assignment used by the hunter controller so
		// its existing combat AI target remains stable across ticks.
		agent->setTargetObject(target);
		// Wake the AI behavior tree so its combat socket runs NOW and fires the
		// weapon. Without this the tree stays on its long IDLE (Wait 3600) schedule
		// and the hunter "aims" but does not shoot for seconds-to-minutes — the
		// working SimPvPController does exactly this on engage (SimPvPController.cpp:842).
		agent->activateAiBehavior(true);
	}
	registerEngagedTelemetry(target->getObjectID());
	state = SimPlayerController::IDLE;
}

void SimHunterController::disengageTarget(bool dropObserverHandle) {
	if (dropObserverHandle) {
		dropTargetObserver();
		pursuing = false;
	}

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	uint64 oldTargetOid = targetOid;
	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<SceneObject*> targetObject = zoneServer == nullptr ? nullptr :
		zoneServer->getObject(oldTargetOid);
	AiAgent* targetAgent = targetObject == nullptr ? nullptr :
		targetObject->asAiAgent();

	Locker agentLock(strongAgent);
	if (targetAgent != nullptr) {
		Locker targetLock(targetAgent, strongAgent);
		strongAgent->removeDefender(targetAgent);
		targetAgent->removeDefender(strongAgent);
	}
	strongAgent->clearCombatState(true);
	strongAgent->setTargetObject(nullptr);
	strongAgent->setFollowObject(nullptr);
	strongAgent->setWatchObject(nullptr);
	strongAgent->clearQueueActions(true);
	strongAgent->clearPatrolPoints();
	strongAgent->clearSavedPatrolPoints();
	strongAgent->clearCurrentPath();
	strongAgent->stopWaiting();
	// The shared arrival watchdog resumes a non-zero base destination while
	// IDLE. Forget the old target route as part of the disengage choreography so
	// a kill/retreat cannot resurrect stale movement on the next watchdog tick.
	destination = Vector3(0, 0, 0);
	destinationLocal = Vector3(0, 0, 0);
	destinationCell = nullptr;
	simPath.removeAll();
	simPathIndex = 0;
	// finalDestination is deliberately preserved so a mid-travel ambush kill can
	// resume the active leg via the order-gated hybrid resume. Cancellation
	// paths close that gate (orderActive / missionCleanupRequested) BEFORE they
	// call disengage, so a finished route cannot revive. Do NOT bump the
	// work-loop generation here: that would kill the live arrival-check loop the
	// resume depends on and strand the active travel leg (code-review round 2).
	state = SimPlayerController::IDLE;
}

void SimHunterController::dropTargetObserver() {
	Reference<Observer*> observer = targetObserver;
	uint64 observedOid = observerTargetOid;
	targetObserver = nullptr;
	observerTargetOid = 0;
	if (observer == nullptr || observedOid == 0)
		return;

	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<SceneObject*> object = zoneServer == nullptr ? nullptr :
		zoneServer->getObject(observedOid);
	TangibleObject* target = object == nullptr ? nullptr :
		object->asTangibleObject();
	if (target != nullptr) {
		Locker targetLock(target);
		target->dropObserver(ObserverEventType::OBJECTDESTRUCTION, observer);
	}
}

void SimHunterController::clearEngagedTelemetry() {
	Vector<uint64> observedOids;
	Vector<Reference<Observer*> > observers;
	// Snapshot and clear under the telemetry lock, then drop the observers
	// OUTSIDE it: dropObserver takes a creature lock, and registerEngagedTelemetry
	// takes the creature lock before the telemetry lock, so holding both here in
	// the opposite order would invert the lock hierarchy.
	{
		Locker telemetryLock(&engagedTelemetryMutex);
		for (int i = 0; i < engagedTelemetry.size(); ++i) {
			observedOids.add(engagedTelemetry.elementAt(i).getKey());
			observers.add(engagedTelemetry.get(i));
		}
		engagedTelemetry.removeAll();
	}

	ZoneServer* zoneServer = ServerCore::getZoneServer();
	for (int i = 0; i < observedOids.size(); ++i) {
		if (zoneServer == nullptr || observers.get(i) == nullptr)
			continue;

		ManagedReference<SceneObject*> object = zoneServer->getObject(
			observedOids.get(i));
		TangibleObject* target = object == nullptr ? nullptr :
			object->asTangibleObject();
		if (target != nullptr) {
			Locker targetLock(target);
			target->dropObserver(ObserverEventType::OBJECTDESTRUCTION,
				observers.get(i));
		}
	}
}

void SimHunterController::registerEngagedTelemetry(uint64 creatureOid) {
	if (creatureOid == 0 || agent == nullptr)
		return;

	// Reserve the slot before doing any work so two ticks racing on the same
	// creature cannot both register an observer and double-count the kill.
	{
		Locker telemetryLock(&engagedTelemetryMutex);
		if (engagedTelemetry.contains(creatureOid))
			return;
		engagedTelemetry.put(creatureOid, nullptr);
	}

	// Any early return from here on must release the reservation.
	bool registered = false;
	auto releaseReservationIfUnused = [this, creatureOid, &registered]() {
		if (registered)
			return;
		Locker telemetryLock(&engagedTelemetryMutex);
		int index = engagedTelemetry.find(creatureOid);
		if (index != -1 && engagedTelemetry.get(index) == nullptr)
			engagedTelemetry.drop(creatureOid);
	};

	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<SceneObject*> object = zoneServer == nullptr ? nullptr :
		zoneServer->getObject(creatureOid);
	TangibleObject* creature = object == nullptr ? nullptr :
		object->asTangibleObject();
	if (creature == nullptr) {
		releaseReservationIfUnused();
		return;
	}

	uint64 identity = identityId;
	uint64 bodyOid = agent->getObjectID();
	Reference<Observer*> observer = new LambdaObserver(
		new LambdaObserverFunction(
			[identity, bodyOid, creatureOid](uint32, Observable*,
				ManagedObject* arg1, uint64) -> int {
				TangibleObject* participant = cast<TangibleObject*>(arg1);
				bool participantVerified = participant != nullptr &&
					participant->getObjectID() == bodyOid;
				// Destruction callbacks must remain write-only and lock-free with
				// respect to the hunter manager. The task handoff performs the
				// pveMutex accounting after target choreography has completed.
				Core::getTaskManager()->executeTask(
					[identity, bodyOid, creatureOid, participantVerified]() {
						SimPlayerManager::instance()->
							handlePveHunterTelemetryDestructionHandoff(
								identity, bodyOid, creatureOid,
								participantVerified);
					}, "SimPveHunterTelemetryDestructionHandoff");
				return 1;
			}, "SimPveHunterTelemetryDestructionObserver"));

	{
		Locker creatureLock(creature);
		creature->registerObserver(ObserverEventType::OBJECTDESTRUCTION,
			observer);
	}

	{
		Locker telemetryLock(&engagedTelemetryMutex);
		// Only fill our own reservation; a concurrent clear (order completed
		// mid-registration) drops it, and we must not resurrect the entry.
		int index = engagedTelemetry.find(creatureOid);
		if (index != -1 && engagedTelemetry.get(index) == nullptr) {
			engagedTelemetry.put(creatureOid, observer);
			registered = true;
		}
	}

	if (!registered) {
		Locker creatureLock(creature);
		creature->dropObserver(ObserverEventType::OBJECTDESTRUCTION, observer);
	}
}

void SimHunterController::registerTargetObserver(uint64 target) {
	if (target == 0 || agent == nullptr)
		return;

	dropTargetObserver();
	ZoneServer* zoneServer = ServerCore::getZoneServer();
	ManagedReference<SceneObject*> object = zoneServer == nullptr ? nullptr :
		zoneServer->getObject(target);
	TangibleObject* targetObject = object == nullptr ? nullptr :
		object->asTangibleObject();
	if (targetObject == nullptr)
		return;

	uint64 hunterOid = agent->getObjectID();
	Reference<Observer*> observer = new LambdaObserver(
		new LambdaObserverFunction(
			[hunterOid, target](uint32, Observable*, ManagedObject* arg1,
				uint64) -> int {
				TangibleObject* attacker = cast<TangibleObject*>(arg1);
				bool participantVerified = attacker != nullptr &&
					attacker->getObjectID() == hunterOid;
				// Destruction callbacks run inside target choreography. The
				// manager/controller handoff is queued and takes no target lock.
				Core::getTaskManager()->executeTask(
					[hunterOid, target, participantVerified]() {
						SimPlayerManager::instance()->
							handlePveHunterDestructionHandoff(
								hunterOid, target, participantVerified);
					}, "SimPveHunterDestructionHandoff");
				return 1;
			}, "SimPveHunterDestructionObserver"));

	{
		Locker targetLock(targetObject);
		targetObject->registerObserver(ObserverEventType::OBJECTDESTRUCTION,
			observer);
	}
	targetObserver = observer;
	observerTargetOid = target;
}

void SimHunterController::onHuntDestruction(uint64 destroyedTargetOid,
		bool participantVerified) {
	if ((!orderActive && !earlyGameActive) || destroyedTargetOid == 0 ||
			targetOid != destroyedTargetOid ||
			destructionHandled)
		return;

	if (earlyGameActive) {
		earlyGameDestroyedTargetOid.store(destroyedTargetOid,
			std::memory_order_release);
		earlyGameDestroyedParticipantVerified.store(participantVerified,
			std::memory_order_release);
		earlyGameDestructionPending.store(true, std::memory_order_release);
		scheduleActiveTick(100);
		return;
	}

	destructionHandled = true;
	dropTargetObserver();
	if (missionHuntOrder && targetMissionWave)
		SimPlayerManager::instance()->recordPveHunterMissionMemberKill(
			identityId, agent == nullptr ? 0 : agent->getObjectID(),
			currentMissionOfferId, missionLairOid, destroyedTargetOid,
			participantVerified);
	if (!participantVerified) {
		handleTargetUnavailable();
		return;
	}
	if (missionHuntOrder && SimPlayerManager::instance()->
			isPveMissionBoardEnabled()) {
		if (!targetMissionWave) {
			handleTargetUnavailable();
			return;
		}
	}

	SimPlayerManager::instance()->recordPveHunterKill(identityId,
		agent == nullptr ? 0 : agent->getObjectID(), destroyedTargetOid,
		order.harvestKind, order.requestedResourceType, true);
	targetOid = 0;
	targetMissionWave = false;
	order.kills++;
	disengageTarget(false);
	// The pursuit target is dead; clear the stale pursuit finalDestination so the
	// arrival resume / in-flight path result cannot drift the hunter toward the
	// corpse before the hunt loop reacquires (plan §Type Definitions: clear
	// finalDestination on cancellation).
	clearHybridMovementOnCancellation();
	if (missionHuntOrder && SimPlayerManager::instance()->
			isPveMissionBoardEnabled()) {
		updateMissionAdds(0);
		scheduleActiveTick(1000);
		return;
	}
	if (order.kills >= order.quota) {
		if (missionHuntOrder)
			beginMissionCleanup(false, "quota");
		else
			beginTravelHome(false);
	} else {
		updateMissionAdds(0);
		scheduleActiveTick(1000);
	}
}

void SimHunterController::handleTargetUnavailable() {
	dropTargetObserver();
	targetOid = 0;
	targetMissionWave = false;
	destructionHandled = false;
	updateMissionAdds(0);
	disengageTarget(false);
	// Target vanished mid-pursuit: drop the stale pursuit destination so movement
	// cannot resume toward an unavailable target before reacquisition.
	clearHybridMovementOnCancellation();
	resetCombatGuard();
	scheduleActiveTick(500);
}

void SimHunterController::beginRetreat() {
	if (retreatCycles >= SimPlayerManager::instance()->
		getPveHunterMaxRetreatCycles()) {
		dropTargetObserver();
		disengageTarget(false);
		if (missionHuntOrder)
			beginMissionCleanup(true, "max_retreat_cycles");
		else
			beginTravelHome(true);
		return;
	}

	retreatCycles++;
	lairRetreatReflees = 0;
	order.retreatCycles = retreatCycles;
	if (missionHuntOrder && missionLairOid != 0)
		MissionDiagLog::event("RETREATING", identityId,
			"reason=ham_threshold lairOid=" + String::valueOf(missionLairOid) +
			" targetOid=" + String::valueOf(targetOid) + " retreatCycle=" +
			String::valueOf(retreatCycles));
	setPhase(RETREATING);

	Vector3 current = agent->getWorldPosition();
	Vector3 away = current;
	CreatureObject* target = nullptr;
	AiAgent* targetAgent = nullptr;
	if (targetOid != 0 && targetIsLive(targetOid, target, targetAgent)) {
		Vector3 direction = current - target->getWorldPosition();
		direction.setZ(0.f);
		if (direction.length2d() < 0.01f)
			direction = Vector3::UNIT_X;
		else
			direction.normalize();
		away = current + direction *
			SimPlayerManager::instance()->getPveHunterRetreatRangeMeters();
	} else {
		dropTargetObserver();
		targetOid = 0;
		targetMissionWave = false;
	}

	// Retain the observer only for an exact same-target resume. A later fresh
	// target selection drops it before changing targetOid.
	disengageTarget(false);
	updateMissionAdds(0);
	moveTo(away);
	// F_0.9.8e: this used to wake the tick only on arrival, and a bot held in
	// combat never arrives.
	if (isLairRetreatDefenseOn())
		scheduleActiveTick(1000);
}

void SimHunterController::finishRetreatMove() {
	setPhase(HEALING);
	dwellUntilMs = System::getMiliTime() + 2000;
	scheduleActiveTick(2000);
}

void SimHunterController::resetCombatGuard() {
	phantomCombatTicks = 0;
	stalemateDefenderOid = 0;
	stalemateSelfHam = 0;
	stalemateDefenderHam = 0;
	lastCombatProgressMs = 0;
}

void SimHunterController::shedAllDefendersBilaterally(AiAgent* hunter) {
	if (hunter == nullptr)
		return;

	Locker agentLock(hunter);
	const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
		hunter->getDefenderList();
	Vector<ManagedReference<CreatureObject*> > defenders;

	if (defenderList != nullptr) {
		for (int i = 0; i < defenderList->size(); ++i) {
			ManagedReference<SceneObject*> object = defenderList->getSafe(i);
			CreatureObject* defender = object == nullptr ? nullptr :
				object->asCreatureObject();
			if (defender != nullptr)
				defenders.add(defender);
		}
	}

	for (int i = 0; i < defenders.size(); ++i) {
		ManagedReference<CreatureObject*> defender = defenders.get(i);
		if (defender == nullptr)
			continue;

		Locker defenderLock(defender, hunter);
		hunter->removeDefender(defender);
		defender->removeDefender(hunter);
	}
}

void SimHunterController::resetInterceptorCombat() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	// This helper owns and releases its locks before the pre-locked combat
	// methods below are called, matching the existing disengage choreography.
	shedAllDefendersBilaterally(strongAgent);

	Locker agentLock(strongAgent);
	strongAgent->clearCombatState(true);
	strongAgent->setTargetObject(nullptr);
	strongAgent->setFollowObject(nullptr);
	strongAgent->setWatchObject(nullptr);
	strongAgent->clearQueueActions(true);
	state = SimPlayerController::IDLE;
	resetCombatGuard();
}

void SimHunterController::clearStaleCombat(const String& reason) {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent != nullptr)
		shedAllDefendersBilaterally(strongAgent);
	dropTargetObserver();
	disengageMissionLair();
	disengageTarget(false);
	targetOid = 0;
	targetMissionWave = false;
	destructionHandled = false;
	resetCombatGuard();
	SimPlayerManager::instance()->info(
		"SimPveHunterCombatReset identity=" + String::valueOf(identityId) +
		" reason=" + reason, true);
}

bool SimHunterController::defendAgainstInterceptor(AiAgent* hunter,
		CreatureObject* attacker) {
	if (hunter == nullptr || attacker == nullptr ||
			attacker->isPlayerCreature() ||
			SimPlayerManager::instance()->isSimPresenceCreature(attacker) ||
			attacker->asAiAgent() == nullptr ||
			attacker->asAiAgent()->getSimPlayerBot() || attacker->isDead() ||
			attacker->isIncapacitated() || attacker->getParent() != nullptr ||
			!attacker->isAttackableBy(hunter) ||
			hunter->getDistanceTo(attacker) >
				SimPlayerManager::instance()->getPveHunterScanRadiusMeters() + 24.f) {
		logTravelDisengage(hunter, hunter->getDistanceTo(attacker) >
			SimPlayerManager::instance()->getPveHunterScanRadiusMeters() + 24.f ?
			"attacker_out_of_range" : "attacker_not_eligible");
		CreatureObject* missionTarget = nullptr;
		AiAgent* missionTargetAgent = nullptr;
		if (targetOid != 0 && targetIsLive(targetOid, missionTarget,
				missionTargetAgent))
			resetInterceptorCombat();
		else
			clearStaleCombat("intercept_cleared");
		return false;
	}

	// Already fighting this selected interceptor: the AI combat is driving
	// attacks, so avoid re-issuing startCombat on every arrival tick.
	ManagedReference<SceneObject*> current = hunter->getFollowObject();
	if (current != nullptr && current->getObjectID() == attacker->getObjectID())
		return true;

	// Fight back, mirroring engageTarget's real-combat contract (attacker
	// locked; startCombat cross-locks the defender). The selector already chose
	// this attacker, so do not inspect or reselect from the defender list here.
	{
		Locker agentLock(hunter);
		if (!CombatManager::instance()->startCombat(hunter, attacker))
			return false;

		hunter->setTargetObject(attacker);
		// Wake the combat behavior tree so the hunter shoots back promptly instead
		// of idling on its long behavior schedule (see engageTarget).
		hunter->activateAiBehavior(true);
	}
	registerEngagedTelemetry(attacker->getObjectID());

	// F_0.9.8b diagnostic (missiondiag only): the bot fought back on a travel
	// leg. It keeps walking -- retreat is evaluated only in HUNTING -- so these
	// lines show what it powers through on the way to a lair.
	if (MissionDiagLog::isLoggingEnabled() && isTravelDiagPhase()) {
		AiAgent* attackerAgent = attacker->asAiAgent();
		const CreatureTemplate* attackerTemplate = attackerAgent == nullptr ?
			nullptr : attackerAgent->getCreatureTemplate();
		String attackerName = attackerTemplate == nullptr ? String("unknown") :
			attackerTemplate->getTemplateName();
		int attackerLevel = attacker->getLevel();
		float distance = hunter->getDistanceTo(attacker);
		float hamFraction = simHunterMinHamFraction(hunter);
		int defenders = 0;
		{
			Locker agentLock(hunter);
			const DeltaVector<ManagedReference<SceneObject*> >* defenderList =
				hunter->getDefenderList();
			defenders = defenderList == nullptr ? 0 : defenderList->size();
		}
		SimHunterTravelDiag::noteEngage(hunter->getObjectID(), attackerName,
			attackerLevel, hamFraction);
		// The tally counts every engage; the log line is limited to one per 5s
		// per bot so a shed/re-engage loop cannot flood missiondiag.log.
		uint64 engageNowMs = System::getMiliTime();
		if (lastTravelEngageLogMs != 0 &&
				engageNowMs - lastTravelEngageLogMs < 5000) {
			++suppressedTravelEngages;
			return true;
		}
		lastTravelEngageLogMs = engageNowMs;
		const int suppressedEngages = suppressedTravelEngages;
		suppressedTravelEngages = 0;
		MissionDiagLog::event("TRAVEL_ENGAGE", identityId,
			"phase=" + getPhaseName() + " attacker=" + attackerName +
			" attackerOid=" + String::valueOf(attacker->getObjectID()) +
			" attackerLevel=" + String::valueOf(attackerLevel) +
			" distM=" + String::valueOf(static_cast<int>(distance)) +
			" defenders=" + String::valueOf(defenders) +
			" botHamFraction=" + String::valueOf(hamFraction) +
			" via=" + String(travelEngageViaThreat ? "threat" : "defenders") +
			" suppressedSinceLast=" + String::valueOf(suppressedEngages));
	}

	return true;
}

void SimHunterController::moveToPatrolPoint(uint64 nowMs) {
	if (state == MOVING || state == CALCULATING_PATH || targetOid != 0 ||
			nowMs - lastPatrolMoveMs < 6000 || species.key.isEmpty())
		return;

	float angle = (float)(System::random(628) / 100.f);
	float radius = 8.f + (float)System::random(20) / 2.f;
	// Offset X and Y(north); Z is height, set from terrain below. (This
	// engine's Vector3 is (x, y_north, z_height) — huntGround loads that way.)
	Vector3 patrolAnchor = missionHuntOrder && missionLairOid != 0 ?
		missionLairPosition : species.huntGround;
	Vector3 patrol = patrolAnchor +
		Vector3((float)std::cos(angle) * radius,
			(float)std::sin(angle) * radius, 0.f);
	// X and Y(north) carry the offset; set Z(height) from terrain. Writing
	// getHeight into Y would replace the north coordinate with a height value
	// and fling the hunter thousands of meters.
	if (agent->getZone() != nullptr)
		patrol.setZ(agent->getZone()->getHeight(patrol.getX(), patrol.getY()));
	lastPatrolMoveMs = nowMs;
	disengageTarget(false);
	moveTo(patrol);
}

void SimHunterController::logTravelHeartbeat(const String& phaseLabel,
		uint64 nowMs) {
	if (!TravelDiagLog::isLoggingEnabled())
		return;

	uint64 intervalMs = static_cast<uint64>(Math::max(1,
		SimPlayerManager::instance()->getTravelDiagHeartbeatSeconds())) * 1000;
	if (lastTravelHeartbeatMs != 0 && nowMs - lastTravelHeartbeatMs < intervalMs)
		return;
	lastTravelHeartbeatMs = nowMs;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	Vector3 position;
	bool inCombat = false;
	uint64 parentCell = 0;
	{
		Locker agentLock(strongAgent);
		position = strongAgent->getWorldPosition();
		inCombat = strongAgent->isInCombat();
		ManagedReference<SceneObject*> parent = strongAgent->getParent().get();
		if (parent != nullptr && parent->isCellObject())
			parentCell = parent->getObjectID();
	}

	// Distance to the leg's run target, and the delta since the last heartbeat -
	// a delta of ~0 across successive lines is the signature of a true stall as
	// opposed to a slow walk.
	float distance = position.distanceTo2d(travelDeparturePosition);
	float delta = lastTravelHeartbeatDistance < 0.f ? 0.f :
		(lastTravelHeartbeatDistance - distance);
	lastTravelHeartbeatDistance = distance;

	TravelDiagLog::event("HEARTBEAT", strongAgent->getObjectID(),
		"phase=" + phaseLabel +
		" ticketPhase=" + String::valueOf((int)ticketTravelPhase) +
		" travelActive=" + String::valueOf(isInterplanetaryTravelActive()) +
		" state=" + getDiagnosticStateName() +
		" hybridActive=" + String::valueOf(isHybridMovementActive()) +
		" collectorFound=" + String::valueOf(ticketCollectorFound) +
		" attempts=" + String::valueOf(ticketApproachAttempts) +
		" inCombat=" + String::valueOf(inCombat) +
		" parentCell=" + String::valueOf(parentCell) +
		" " + TravelDiagLog::fmtVec("pos", position) +
		" " + TravelDiagLog::fmtVec("target", travelDeparturePosition) +
		" " + TravelDiagLog::fmtDist("dist", distance) +
		" " + TravelDiagLog::fmtDist("closedSinceLast", delta) +
		" destZone=" + travelDestinationZone);
}

void SimHunterController::beginTravelHome(bool abandoned) {
	if (!orderActive)
		return;
	if (missionHuntOrder && !missionCleanupRequested && missionLairOid != 0) {
		beginMissionCleanup(abandoned, "travel_home");
		return;
	}

	orderAbandoned = abandoned;
	// Leaving the hunt always ends the target's observer lifetime. The only
	// retained handle is the same-target retreat/resume path above.
	dropTargetObserver();
	disengageMissionLair();
	disengageTarget(false);
	targetOid = 0;
	targetMissionWave = false;
	// Walk back to a city on the planet the hunter is ACTUALLY on. Using the
	// identity's home planet/city unconditionally produced a cross-planet
	// coordinate the moment a hunter was relocated (market dispatch) or cloned
	// elsewhere — moveTo() would then aim at a position that does not exist on
	// this terrain and the leg could never succeed. homePlanet/homeCity remain
	// the spawn/clone origin only (P.8.7 "wherever it is is the base").
	String returnPlanet = order.homePlanet;
	String returnCity = order.homeCity;
	{
		String currentPlanet;
		SimPlayerManager::ShuttleportLocation nearest;
		if (SimPlayerManager::instance()->getPveHunterCurrentRoutableCity(
				agent == nullptr ? 0 : agent->getObjectID(), currentPlanet,
				nearest) && !currentPlanet.isEmpty() &&
				currentPlanet != returnPlanet) {
			returnPlanet = nearest.planet;
			returnCity = nearest.name;
		}
	}

	Vector3 cantina;
	Vector3 medCenter;
	Vector3 home;
	if (!SimPlayerManager::instance()->getPveHomeLocations(
			returnPlanet, returnCity, cantina, medCenter, home)) {
		completeOrder(abandoned, abandoned ?
			epilogueAbandonReason("home_location_unavailable") :
			String("home_location_unavailable"));
		return;
	}
	travelHomeDestination = home;
	travelHomeDestinationSet = true;
	setPhase(TRAVEL_HOME);
	moveTo(home);
	armTravelCombatTick();
}

void SimHunterController::completeOrder(bool abandoned, const String& reason) {
	if (!orderActive)
		return;
	if (isStructureTraversalFeatureEnabled() && isTraversalActive())
		clearStructureTraversalState("hunter_order_complete");
	if (missionHuntOrder && !missionCleanupRequested && missionLairOid != 0) {
		beginMissionCleanup(abandoned, reason);
		return;
	}

	// Close the resume gate BEFORE disengaging: an arrival tick that already
	// passed its generation check and then blocks on the agent lock could
	// otherwise acquire it right after disengage and, with orderActive still
	// set, revive movement toward the finished route (code-review round 2).
	cancelPveDoctorRequest();
	SimPlayerManager::instance()->releasePlayerBotDoctorBuffFee(identityId);
	pveDoctorFeeReserved = false;
	SimPlayerManager::instance()->finishPveHunterBuffTrip(identityId, false);
	pveBuffTripAtHub = false;
	pveBuffTripReturning = false;
	if (SimPlayerManager::instance()->isPveMarketDispatchEnabled()) {
		String actualPlanet;
		if (agent != nullptr && agent->getZone() != nullptr)
			actualPlanet = agent->getZone()->getZoneName();
		SimPlayerManager::instance()->finishPveHunterRelocation(identityId,
			false, actualPlanet);
		clearInterplanetaryTravelState();
	}
	orderActive = false;
	dropTargetObserver();
	disengageMissionLair();
	disengageTarget(false);
	clearEngagedTelemetry();
	if (abandoned)
		SimPlayerManager::instance()->recordPveHunterAbandoned(identityId,
			agent == nullptr ? 0 : agent->getObjectID(), reason);
	else
		SimPlayerManager::instance()->recordPveHunterCompleted(identityId,
			agent == nullptr ? 0 : agent->getObjectID());

	orderAbandoned = false;
	missionAbandonReason = "";
	missionHuntOrder = false;
	missionTerminalFallback = false;
	missionTerminalResolved = false;
	missionCleanupRequested = false;
	missionTerminalPosition = Vector3();
	missionLairPosition = Vector3();
	missionLairOid = 0;
	terminalResolveWaitCycles = 0;
	missionAddsOverCapCycles = 0;
	missionLairRevealDeferCycles = 0;
	missionAddsEngaged = 0;
	targetOid = 0;
	targetMissionWave = false;
	setPhase(DONE);
	setPhase(IDLE_HOME);
	scheduleActiveTick(30000);
}

void SimHunterController::teardown(const String& reason) {
	// The body is going away, and with it the ownership of every queued corpse.
	SimPlayerManager::instance()->dropPlayerBotLootClaims(identityId);
	if (lootStepActive)
		finishPlayerBotLootStep("teardown", false);
	if (isStructureTraversalFeatureEnabled() && isTraversalActive())
		clearStructureTraversalState("hunter_teardown");
	activeTickGeneration++;
	advanceWorkLoopGeneration("hunterTeardown");
	cancelPveDoctorRequest();
	SimPlayerManager::instance()->releasePlayerBotDoctorBuffFee(identityId);
	pveDoctorFeeReserved = false;
	SimPlayerManager::instance()->finishPveHunterBuffTrip(identityId, false);
	pveBuffTripAtHub = false;
	pveBuffTripReturning = false;
	if (SimPlayerManager::instance()->isPveMarketDispatchEnabled()) {
		String actualPlanet;
		if (agent != nullptr && agent->getZone() != nullptr)
			actualPlanet = agent->getZone()->getZoneName();
		SimPlayerManager::instance()->finishPveHunterRelocation(identityId,
			false, actualPlanet);
		clearInterplanetaryTravelState();
	}
	clearHybridMovementOnCancellation();
	dropTargetObserver();
	disengageTarget(false);
	clearEngagedTelemetry();
	if (orderActive)
		SimPlayerManager::instance()->recordPveHunterAbandoned(identityId,
			agent == nullptr ? 0 : agent->getObjectID(), reason);
	orderActive = false;
}
