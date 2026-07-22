/*
 * SimHunterController.cpp
 * P.8.1 solo PvE hunt loop.
 */

#include "SimHunterController.h"

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
#include "server/zone/managers/combat/CombatManager.h"
#include "server/zone/managers/player/PlayerManager.h"
#include "server/chat/ChatManager.h"
#include "server/zone/managers/director/ScreenPlayTask.h"
#include "templates/params/ObserverEventType.h"
#include "server/zone/objects/creature/buffs/BuffType.h"
#include "templates/params/creature/CreatureAttribute.h"

#include <cmath>

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
		if (strong == nullptr || strong->activeTickGeneration != capturedGeneration)
			return;

		Core::getTaskManager()->executeTask([strong, capturedGeneration]() {
			if (strong->activeTickGeneration != capturedGeneration)
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
	case SimHunterController::TRAVEL_OUT: return "TRAVEL_OUT";
	case SimHunterController::TRAVEL_TO_TERMINAL: return "TRAVEL_TO_TERMINAL";
	case SimHunterController::ACCEPT_MISSION: return "ACCEPT_MISSION";
	case SimHunterController::TRAVEL_TO_LAIR: return "TRAVEL_TO_LAIR";
	case SimHunterController::AWAITING_WORLD: return "AWAITING_WORLD";
	case SimHunterController::HUNTING: return "HUNTING";
	case SimHunterController::RETREATING: return "RETREATING";
	case SimHunterController::HEALING: return "HEALING";
	case SimHunterController::TRAVEL_HOME: return "TRAVEL_HOME";
	case SimHunterController::DELIVER: return "DELIVER";
	case SimHunterController::MISSION_CLEANUP: return "MISSION_CLEANUP";
	case SimHunterController::CLONE_HOME: return "CLONE_HOME";
	case SimHunterController::DONE: return "DONE";
	default: return "UNKNOWN";
	}
}

SimHunterController::SimHunterController(AiAgent* aiAgent, uint64 identity)
	: SimPlayerController(aiAgent) {
	identityId = identity;
	orderActive = false;
	orderAbandoned = false;
	deathReported = false;
	phase = IDLE_HOME;
	phaseStartedAtMs = System::getMiliTime();
	huntStartedAtMs = 0;
	activeTickGeneration = 1;
	targetOid = 0;
	observerTargetOid = 0;
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
	terminalResolveWaitCycles = 0;
	missionAddsOverCapCycles = 0;
	missionAddsEngaged = 0;
	pveNeedDoctorBuff = false;
	pveNeedEntertainerBuff = false;
	pveDoctorFallbackNeeded = false;
	pveEntertainerFallbackNeeded = false;
	pveBuffProviderApproachActive = false;
	pveBuffApproachStage = PVE_BUFF_APPROACH_NONE;
	pveBuffInteractionDwellActive = false;
	pveDoctorRequestGen = 0;
	pveDoctorRequestActive = false;
	pveDoctorDeadlineSec = 0;
	pveDoctorProviderObject = nullptr;
	setLoggingName("SimHunterController");
}

SimHunterController::~SimHunterController() {
	teardown("destructor");
}

String SimHunterController::getPhaseName() const {
	return hunterPhaseName(phase);
}

void SimHunterController::scheduleActiveTick(int delayMs) {
	if (delayMs < 100)
		delayMs = 100;

	activeTickGeneration++;
	if (activeTickGeneration == 0)
		activeTickGeneration = 1;

	Reference<SimHunterActiveTickTask*> task =
		new SimHunterActiveTickTask(this, activeTickGeneration);
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

void SimHunterController::startSimLoop() {
	if (agent == nullptr)
		return;

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

	if (orderActive && order.issuedAtMs == newOrder.issuedAtMs)
		return;

	dropTargetObserver();
	disengageTarget(false);
	cancelPveDoctorRequest();
	advanceWorkLoopGeneration("hunterStartOrder");
	clearInteriorApproachLeg();
	resetHybridMovementState(true);
	order = newOrder;
	species = newSpecies;
	orderActive = true;
	missionHuntOrder = SimPlayerManager::instance()->isPveMissionHuntEnabled();
	orderAbandoned = false;
	deathReported = false;
	phase = IDLE_HOME;
	phaseStartedAtMs = 0;
	huntStartedAtMs = 0;
	targetOid = 0;
	retreatCycles = 0;
	missionTerminalFallback = false;
	missionTerminalResolved = false;
	missionCleanupRequested = false;
	missionTerminalPosition = Vector3();
	missionLairPosition = Vector3();
	missionLairOid = 0;
	terminalResolveWaitCycles = 0;
	missionAddsOverCapCycles = 0;
	missionAddsEngaged = 0;
	pveBuffProviders = PveBuffProviders();
	pveNeedDoctorBuff = false;
	pveNeedEntertainerBuff = false;
	pveDoctorFallbackNeeded = false;
	pveEntertainerFallbackNeeded = false;
	pveBuffProviderApproachActive = false;
	pveBuffApproachStage = PVE_BUFF_APPROACH_NONE;
	pveBuffInteractionDwellActive = false;
	pveDoctorRequestActive = false;
	pveDoctorDeadlineSec = 0;
	pveDoctorProviderObject = nullptr;
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

	if (!strongAgent->isInCombat())
		return;

	// Concurrency: onTick (ArrivalCheckTask) and runActiveTick
	// (SimHunterActiveTickTask) are independently scheduled and can run on
	// different task-pool threads. runActiveTick is the SOLE writer of targetOid
	// and the mission observer (both are HUNTING-scoped: targetOid is set only by
	// selectTarget during HUNTING and cleared before the phase is left). To avoid
	// racing that state, onTick never promotes a mission target — it only
	// self-defends, and only in the travel/movement legs where runActiveTick's
	// combat handler does not run. In HUNTING, runActiveTick owns combat.
	if (phase == HUNTING)
		return;

	// Interceptor-only self-defense: fight whatever is actually attacking, via
	// startCombat/setTargetObject/activateAiBehavior, without ever touching
	// targetOid or the observer. In the phases this code runs (travel/movement
	// legs — RETREATING/HEALING and HUNTING have already returned above, and
	// only those plus HUNTING ever hold a non-zero targetOid), targetOid is 0,
	// so resetInterceptorCombat preserves it and cannot abandon a mission target.
	ManagedReference<CreatureObject*> attacker;
	ManagedReference<AiAgent*> attackerAgent;
	if (selectActiveCombatAttacker(strongAgent, attacker, attackerAgent))
		defendAgainstInterceptor(strongAgent, attacker.get());
	else
		resetInterceptorCombat();
}

bool SimHunterController::shouldContinueArrivalChecks() const {
	return orderActive || state == MOVING || state == CALCULATING_PATH;
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
		moveToInterior(provider.worldPos, provider.localPos,
			provider.cell.get());
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
	}
}

void SimHunterController::finishPveBuffProviderFlow() {
	cancelPveDoctorRequest();
	pveBuffProviderApproachActive = false;
	pveBuffInteractionDwellActive = false;
	pveDoctorProviderObject = nullptr;
	pveBuffApproachStage = PVE_BUFF_APPROACH_COMPLETE;
	dwellUntilMs = 0;

	if (pveEntertainerFallbackNeeded)
		applyHunterBuffsForFamily(PVE_BUFF_FAMILY_ENTERTAINER);
	if (pveDoctorFallbackNeeded)
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
		pveDoctorFallbackNeeded = true;
		if (moveToNextPveBuffProvider())
			return;
		finishPveBuffProviderFlow();
		return;
	}

	SimPlayerManager::instance()->recordPveDoctorInteraction();
	scheduleActiveTick(1000);
}

void SimHunterController::beginBuffUp() {
	if (SimPlayerManager::instance()->isPveRealBuffsEnabled()) {
		bool needDoctor = false;
		bool needEntertainer = false;
		computeBuffNeeds(needDoctor, needEntertainer);
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
		pveBuffProviderApproachActive = false;
		pveBuffInteractionDwellActive = false;
		pveDoctorRequestActive = false;
		pveDoctorDeadlineSec = 0;

		PveBuffProviders providers;
		if (!SimPlayerManager::instance()->resolvePveBuffProviders(
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
	bool resolved = SimPlayerManager::instance()->getNearestMissionTerminal(
		order.homePlanet, order.homeCity, currentPosition, terminal,
		cityState);

	if (resolved) {
		missionTerminalPosition = terminal.position;
		missionTerminalResolved = true;
		missionTerminalFallback = false;
		SimPlayerManager::instance()->recordPveHunterMissionTerminal(
			identityId, bodyOid, terminal.planet, terminal.city,
			terminal.position);
		setPhase(TRAVEL_TO_TERMINAL);
		if (state != MOVING && state != CALCULATING_PATH)
			moveTo(missionTerminalPosition);
		return;
	}

	if (cityState == PVE_MISSION_TERMINAL_ABSENT ||
			terminalResolveWaitCycles >= SimPlayerManager::instance()->
			getPveMissionTerminalResolveWaitCycles()) {
		beginMissionFallback();
		return;
	}

	missionTerminalResolved = false;
	missionTerminalFallback = false;
	++terminalResolveWaitCycles;
	SimPlayerManager::instance()->recordPveHunterMissionTerminal(
		identityId, bodyOid, order.homePlanet, order.homeCity,
		Vector3());
	setPhase(TRAVEL_TO_TERMINAL);
	scheduleActiveTick(2000);
}

void SimHunterController::beginMissionFallback() {
	missionTerminalFallback = true;
	missionTerminalResolved = false;
	SimPlayerManager::instance()->recordPveHunterMissionTerminal(
		identityId, agent == nullptr ? 0 : agent->getObjectID(),
		order.homePlanet, order.homeCity, Vector3());
	spawnMissionLair();
}

void SimHunterController::beginMissionAccept() {
	if (!orderActive || !missionHuntOrder)
		return;

	setPhase(ACCEPT_MISSION);
	SimPlayerManager::instance()->announcePveHunterEvent(
		agent == nullptr ? 0 : agent->getObjectID(), species.key,
		"Signed on for a " + species.harvestKind + " hunting contract.");
	dwellUntilMs = System::getMiliTime() +
		static_cast<uint64>(SimPlayerManager::instance()->
			getPveMissionTerminalDwellSeconds()) * 1000;
	scheduleActiveTick(1000);
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
	missionAddsOverCapCycles = 0;
	updateMissionAdds(0);
	setPhase(TRAVEL_TO_LAIR);
	SimPlayerManager::instance()->announcePveHunterEvent(
		agent->getObjectID(), species.key,
		"Moving out to the " + species.harvestKind + " lair.");
	moveTo(missionLairPosition);
}

void SimHunterController::beginMissionCleanup(bool abandoned,
		const String& reason) {
	if (!orderActive || !missionHuntOrder)
		return;

	orderAbandoned = abandoned;
	// Close the resume gate BEFORE disengaging so an arrival tick that acquires
	// the agent lock right after disengage cannot revive movement toward the
	// lair being torn down (code-review round 2).
	missionCleanupRequested = true;
	dropTargetObserver();
	disengageTarget(false);
	targetOid = 0;
	missionAddsOverCapCycles = 0;
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
		return false;
	}

	const CreatureTemplate* targetTemplate = targetAgent->getCreatureTemplate();
	bool socialTarget = !targetAgent->getSocialGroup().isEmpty() ||
		(targetTemplate != nullptr && targetTemplate->isHerd());
	const DeltaVector<ManagedReference<SceneObject*> >* defenders =
		hunter->getDefenderList();
	int adds = defenders == nullptr ? 0 : defenders->size();
	updateMissionAdds(adds);

	if (!socialTarget || adds <= SimPlayerManager::instance()->
		getPveMissionMaxSimultaneousAdds()) {
		missionAddsOverCapCycles = 0;
		return false;
	}

	++missionAddsOverCapCycles;
	if (missionAddsOverCapCycles >= SimPlayerManager::instance()->
		getPveMissionAddsAbandonCycles()) {
		beginMissionCleanup(true, "social_adds_above_cap");
		return true;
	}

	beginRetreat();
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

void SimHunterController::onArrived() {
	if (agent == nullptr)
		return;

	uint64 nowMs = System::getMiliTime();
	if (phase == BUFF_UP) {
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
		beginMissionAccept();
		return;
	}

	if (phase == TRAVEL_TO_LAIR) {
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
			if (agent->getDistanceTo(target) <=
					SimPlayerManager::instance()->getPveHunterWeaponRangeMeters())
				engageTarget();
			else
				moveToTarget();
		} else {
			handleTargetUnavailable();
		}
		return;
	}

	if (phase == RETREATING) {
		finishRetreatMove();
		return;
	}

	if (phase == TRAVEL_HOME) {
		if (orderAbandoned) {
			completeOrder(true, "abandoned");
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
	// The base leaves the agent in CALCULATING_PATH; reset it or a failed
	// retreat/patrol path wedges the controller until the phase TTL fires.
	clearHybridMovementOnCancellation();
	state = IDLE;

	if (!orderActive)
		return;

	if (phase == BUFF_UP && pveBuffProviderApproachActive) {
		PveBuffApproachStage providerStage = pveBuffApproachStage;
		pveBuffProviderApproachActive = false;
		if (providerStage == PVE_BUFF_APPROACH_DOCTOR)
			pveDoctorFallbackNeeded = true;
		else
			pveEntertainerFallbackNeeded = true;
		if (moveToNextPveBuffProvider())
			return;
		finishPveBuffProviderFlow();
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_TERMINAL) {
		beginMissionFallback();
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_LAIR) {
		beginMissionCleanup(true, "lair_path_failed");
		return;
	}

	if (missionHuntOrder && phase == BUFF_UP) {
		beginMissionCleanup(true, "buff_path_failed");
		return;
	}

	if (phase == TRAVEL_OUT || phase == TRAVEL_HOME || phase == BUFF_UP) {
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

	setPhase(HUNTING);
	lastPatrolMoveMs = 0;
	scheduleActiveTick(100);
}

void SimHunterController::runActiveTick() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	uint64 nowMs = System::getMiliTime();
	if (strongAgent->isDead()) {
		cancelPveDoctorRequest();
		if (!deathReported) {
			deathReported = true;
			dropTargetObserver();
			disengageTarget(false);
			if (missionHuntOrder)
				beginMissionCleanup(true, "hunter_died");
			else
				setPhase(CLONE_HOME);
			SimPlayerManager::instance()->onPveHunterDied(identityId,
				strongAgent->getObjectID());
		}
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
	case TRAVEL_TO_LAIR: phaseTimeoutMs = 900000; break;
	case AWAITING_WORLD: phaseTimeoutMs = 300000; break;
	case RETREATING: phaseTimeoutMs = 180000; break;
	case HEALING: phaseTimeoutMs = 900000; break;
	case TRAVEL_HOME: phaseTimeoutMs = 900000; break;
	case MISSION_CLEANUP: phaseTimeoutMs = 120000; break;
	default: break;
	}
	if (phaseTimeoutMs != 0 && phaseStartedAtMs != 0 &&
			nowMs - phaseStartedAtMs >= phaseTimeoutMs) {
		if (phase == TRAVEL_HOME)
			completeOrder(true, "phase_timeout_" + getPhaseName());
		else if (missionHuntOrder && phase == MISSION_CLEANUP)
			completeOrder(true, "cleanup_timeout");
		else if (missionHuntOrder)
			beginMissionCleanup(true, "phase_timeout_" + getPhaseName());
		else
			beginTravelHome(true);
		return;
	}

	if (phase == ANNOUNCE_JOB) {
		beginBuffUp();
		scheduleActiveTick(500);
		return;
	}

	if (phase == BUFF_UP) {
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
				SimPlayerManager::instance()->recordPveBuffSource(
					bodyOid, "real-doctor");
				if (moveToNextPveBuffProvider())
					return;
				finishPveBuffProviderFlow();
				return;
			}

			if (nowMs / 1000 >= pveDoctorDeadlineSec) {
				cancelPveDoctorRequest();
				pveDoctorFallbackNeeded = true;
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
		if (missionTerminalResolved &&
				(state == MOVING || state == CALCULATING_PATH))
			scheduleActiveTick(2000);
		else
			beginMissionTerminalLeg();
		return;
	}

	if (missionHuntOrder && phase == ACCEPT_MISSION) {
		if (dwellUntilMs == 0 || nowMs >= dwellUntilMs)
			spawnMissionLair();
		else
			scheduleActiveTick((int)Math::max(100,
				(int)(dwellUntilMs - nowMs)));
		return;
	}

	if (missionHuntOrder && phase == TRAVEL_TO_LAIR) {
		scheduleActiveTick(2000);
		return;
	}

	if (missionHuntOrder && phase == MISSION_CLEANUP) {
		continueAfterMissionCleanup();
		return;
	}

	if (phase == TRAVEL_HOME || phase == DELIVER || phase == DONE) {
		scheduleActiveTick(1000);
		return;
	}

	if (missionHuntOrder && missionLairOid != 0) {
		PveHuntLair lair;
		if (!SimPlayerManager::instance()->getPveHuntLair(
				agent == nullptr ? 0 : agent->getObjectID(), lair) ||
				!lair.alive || lair.cleanupQueued) {
			beginMissionCleanup(true, "lair_unavailable");
			return;
		}
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

	if (phase == RETREATING) {
		if (state == MOVING || state == CALCULATING_PATH)
			scheduleActiveTick(2000);
		else
			finishRetreatMove();
		return;
	}

	if (phase == HEALING) {
		if (isReadyToResume(strongAgent)) {
			setPhase(HUNTING);
			if (targetOid != 0)
				moveToTarget();
			else
				scanForTarget();
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
		else if (strongAgent->getDistanceTo(target) <=
				SimPlayerManager::instance()->getPveHunterWeaponRangeMeters())
			engageTarget();
		else
			moveToTarget();
	} else {
		scanForTarget();
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

	const CreatureTemplate* targetTemplate = targetAgent->getCreatureTemplate();
	if (targetTemplate == nullptr)
		return false;

	String targetName = targetTemplate->getTemplateName().toLowerCase();
	return species.templateFilter.isEmpty() ||
		targetName.contains(species.templateFilter.toLowerCase());
}

void SimHunterController::scanForTarget() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr || strongAgent->isInCombat())
		return;

	// Query the zone's QuadTree directly (like the proven spike scan at
	// SimPlayerManager :9662). getCloseObjects() is NOT maintained for these
	// spawned/moved bots (same class of gap as NPC updateActiveAreas), so it
	// returns an empty/stale set even when creatures are right next to the
	// hunter (lair packs) — the cause of hunters never acquiring a target.
	Zone* zone = nullptr;
	Vector3 hunterPosition;
	{
		Locker agentLock(strongAgent);
		zone = strongAgent->getZone();
		if (zone == nullptr)
			return;
		hunterPosition = strongAgent->getWorldPosition();
	}

	float scanRadius =
		SimPlayerManager::instance()->getPveHunterScanRadiusMeters();
	SortedVector<TreeEntry*> closeObjects;
	zone->getInRangeObjects(hunterPosition.getX(), hunterPosition.getZ(),
		hunterPosition.getY(), scanRadius, &closeObjects, true, true);

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

		selectTarget(candidateAgent);
		return;
	}
}

bool SimHunterController::selectActiveCombatAttacker(AiAgent* hunter,
		ManagedReference<CreatureObject*>& outAttacker,
		ManagedReference<AiAgent*>& outAttackerAgent) {
	outAttacker = nullptr;
	outAttackerAgent = nullptr;

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

	float weaponRange = SimPlayerManager::instance()->
		getPveHunterWeaponRangeMeters();
	float hysteresis = weaponRange * 0.5f;
	if (hasFollowedAttacker && followedDistance <= weaponRange &&
			!(nearestDistance + hysteresis < followedDistance)) {
		outAttacker = followedAttacker;
		outAttackerAgent = followedAttackerAgent;
	} else {
		outAttacker = nearestAttacker;
		outAttackerAgent = nearestAttackerAgent;
	}

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
				hunter->getDistanceTo(attacker) >
					SimPlayerManager::instance()->getPveHunterWeaponRangeMeters())
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
		destructionHandled = false;
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

	if (agent->getDistanceTo(target) <=
			SimPlayerManager::instance()->getPveHunterWeaponRangeMeters()) {
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

void SimHunterController::engageTarget() {
	if (targetOid == 0 || agent == nullptr)
		return;

	CreatureObject* target = nullptr;
	AiAgent* targetAgent = nullptr;
	if (!targetIsLive(targetOid, target, targetAgent) || targetAgent == nullptr ||
			!target->isAttackableBy(agent.get()))
		return;

	if (agent->getDistanceTo(target) >
			SimPlayerManager::instance()->getPveHunterWeaponRangeMeters()) {
		moveToTarget();
		return;
	}

	if (observerTargetOid != targetOid)
		registerTargetObserver(targetOid);
	if (observerTargetOid != targetOid || targetObserver == nullptr)
		return;

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
	if (!orderActive || destroyedTargetOid == 0 || targetOid != destroyedTargetOid ||
			destructionHandled)
		return;

	destructionHandled = true;
	dropTargetObserver();
	if (!participantVerified) {
		handleTargetUnavailable();
		return;
	}

	SimPlayerManager::instance()->recordPveHunterKill(identityId,
		agent == nullptr ? 0 : agent->getObjectID(), destroyedTargetOid,
		order.harvestKind, order.requestedResourceType, true);
	targetOid = 0;
	order.kills++;
	disengageTarget(false);
	// The pursuit target is dead; clear the stale pursuit finalDestination so the
	// arrival resume / in-flight path result cannot drift the hunter toward the
	// corpse before the hunt loop reacquires (plan §Type Definitions: clear
	// finalDestination on cancellation).
	clearHybridMovementOnCancellation();
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
	order.retreatCycles = retreatCycles;
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
	}

	// Retain the observer only for an exact same-target resume. A later fresh
	// target selection drops it before changing targetOid.
	disengageTarget(false);
	updateMissionAdds(0);
	moveTo(away);
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
	disengageTarget(false);
	targetOid = 0;
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
	Locker agentLock(hunter);
	if (!CombatManager::instance()->startCombat(hunter, attacker))
		return false;

	hunter->setTargetObject(attacker);
	// Wake the combat behavior tree so the hunter shoots back promptly instead
	// of idling on its long behavior schedule (see engageTarget).
	hunter->activateAiBehavior(true);

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
	disengageTarget(false);
	targetOid = 0;
	Vector3 cantina;
	Vector3 medCenter;
	Vector3 home;
	if (!SimPlayerManager::instance()->getPveHomeLocations(
			order.homePlanet, order.homeCity, cantina, medCenter, home)) {
		completeOrder(abandoned, "home_location_unavailable");
		return;
	}
	setPhase(TRAVEL_HOME);
	moveTo(home);
}

void SimHunterController::completeOrder(bool abandoned, const String& reason) {
	if (!orderActive)
		return;
	if (missionHuntOrder && !missionCleanupRequested && missionLairOid != 0) {
		beginMissionCleanup(abandoned, reason);
		return;
	}

	// Close the resume gate BEFORE disengaging: an arrival tick that already
	// passed its generation check and then blocks on the agent lock could
	// otherwise acquire it right after disengage and, with orderActive still
	// set, revive movement toward the finished route (code-review round 2).
	cancelPveDoctorRequest();
	orderActive = false;
	dropTargetObserver();
	disengageTarget(false);
	if (abandoned)
		SimPlayerManager::instance()->recordPveHunterAbandoned(identityId,
			agent == nullptr ? 0 : agent->getObjectID(), reason);
	else
		SimPlayerManager::instance()->recordPveHunterCompleted(identityId,
			agent == nullptr ? 0 : agent->getObjectID());

	orderAbandoned = false;
	missionHuntOrder = false;
	missionTerminalFallback = false;
	missionTerminalResolved = false;
	missionCleanupRequested = false;
	missionTerminalPosition = Vector3();
	missionLairPosition = Vector3();
	missionLairOid = 0;
	terminalResolveWaitCycles = 0;
	missionAddsOverCapCycles = 0;
	missionAddsEngaged = 0;
	targetOid = 0;
	setPhase(DONE);
	setPhase(IDLE_HOME);
	scheduleActiveTick(30000);
}

void SimHunterController::teardown(const String& reason) {
	activeTickGeneration++;
	advanceWorkLoopGeneration("hunterTeardown");
	cancelPveDoctorRequest();
	clearHybridMovementOnCancellation();
	dropTargetObserver();
	disengageTarget(false);
	if (orderActive)
		SimPlayerManager::instance()->recordPveHunterAbandoned(identityId,
			agent == nullptr ? 0 : agent->getObjectID(), reason);
	orderActive = false;
}
