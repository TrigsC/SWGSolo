/*
 * SimPvPController.cpp
 * P.6.1 SimPvP squad controllers (leader city-loop driver + follow member).
 */

#include "SimPvPController.h"
#include "SimPlayerManager.h"
#include "CellNavDiagLog.h"

#include "engine/core/Core.h"
#include "server/ServerCore.h"
#include "server/zone/ZoneServer.h"
#include "server/zone/objects/cell/CellObject.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/CloseObjectsVector.h"
#include "server/zone/TreeEntry.h"
#include "templates/params/creature/CreatureAttribute.h"
#include "templates/params/creature/ObjectFlag.h"
#include "templates/params/creature/CreaturePosture.h"

#include "system/lang/System.h"

// ------------------------------------------------------
// Loiter task
// ------------------------------------------------------
void SimPvpLoiterTask::run() {
	Reference<SimPvPController*> strongRef = controller.get();
	if (strongRef == nullptr)
		return;

	uint64 capturedGeneration = generation;

	if (!strongRef->isWorkLoopGenerationCurrent(capturedGeneration, "pvp_loiter"))
		return;

	Core::getTaskManager()->executeTask([strongRef, capturedGeneration]() {
		if (!strongRef->isWorkLoopGenerationCurrent(capturedGeneration, "pvp_loiter"))
			return;

		strongRef->finishLoitering();
	}, "SimPvpLoiterLambda");
}

// ------------------------------------------------------
// Shared PvP bot behavior
// ------------------------------------------------------
SimPvpBotController::SimPvpBotController(AiAgent* aiAgent, uint64 squad, bool isImperial)
	: SimPlayerController(aiAgent) {
	squadId = squad;
	imperial = isImperial;
	deathReported = false;
	scoutRole = false;
	lastContactReportMs = 0;
	phantomCombatTicks = 0;
	stalemateDefenderOid = 0;
	stalemateSelfHam = 0;
	stalemateDefenderHam = 0;
	lastCombatProgressMs = 0;
	stalemateIgnoredOid = 0;
	stalemateIgnoreUntilMs = 0;
	setLoggingName("SimPvpBotController");
}

SimPvpBotController::~SimPvpBotController() {
}

void SimPvpBotController::resetStalemateProgress() {
	stalemateDefenderOid = 0;
	stalemateSelfHam = 0;
	stalemateDefenderHam = 0;
	lastCombatProgressMs = 0;
}

void SimPvpBotController::prepareForRelocation(const String& reason) {
	SimPlayerController::prepareForRelocation(reason);
	resetStalemateState();
}

void SimPvpBotController::onTick() {
	ManagedReference<AiAgent*> strongAgent = agent;

	if (strongAgent == nullptr)
		return;

	uint64 nowMs = System::getMiliTime();
	if (stalemateIgnoredOid != 0 && nowMs >= stalemateIgnoreUntilMs) {
		stalemateIgnoredOid = 0;
		stalemateIgnoreUntilMs = 0;
	}

	if (strongAgent->isDead()) {
		resetStalemateProgress();
		// Report once; the manager owns the roster (delayed corpse cleanup,
		// replacement at the squad's next city, leader promotion).
		if (!deathReported) {
			deathReported = true;
			SimPlayerManager::instance()->onPvpBotDied(squadId,
				strongAgent->getObjectID());
		}
		return;
	}

	if (strongAgent->isInCombat()) {
		// P.6.2a phantom-combat guard + P.6.3c combat leash: clear combat when
		// there's no reachable live enemy - the defender died/left (stale combat
		// that would freeze the bot) OR fled beyond the leash distance (so bots
		// disengage instead of chasing/attacking a target across the map at
		// 100m+). Cleared after a few ticks so a brief LOS/range blip doesn't
		// drop a genuine fight.
		float leash = SimPlayerManager::instance()->getPvpCombatLeashMeters();

		ManagedReference<SceneObject*> defenderScene =
			strongAgent->getMainDefender();
		CreatureObject* defender = defenderScene != nullptr ?
			defenderScene->asCreatureObject() : nullptr;

		bool reachableEnemy = false;
		if (defender != nullptr && !defender->isDead() &&
				!defender->isIncapacitated() &&
				defender->getZone() == strongAgent->getZone() &&
				strongAgent->getWorldPosition().distanceTo(
					defender->getWorldPosition()) < leash) {
			reachableEnemy = true;
		}

		if (reachableEnemy) {
			phantomCombatTicks = 0;

			uint64 defenderOid = defender->getObjectID();
			int selfHam = strongAgent->getHAM(CreatureAttribute::HEALTH) +
				strongAgent->getHAM(CreatureAttribute::ACTION) +
				strongAgent->getHAM(CreatureAttribute::MIND);
			int defenderHam = defender->getHAM(CreatureAttribute::HEALTH) +
				defender->getHAM(CreatureAttribute::ACTION) +
				defender->getHAM(CreatureAttribute::MIND);

			if (stalemateDefenderOid != defenderOid) {
				stalemateDefenderOid = defenderOid;
				stalemateSelfHam = selfHam;
				stalemateDefenderHam = defenderHam;
				lastCombatProgressMs = nowMs;
			} else {
				if (selfHam < stalemateSelfHam ||
						defenderHam < stalemateDefenderHam)
					lastCombatProgressMs = nowMs;

				stalemateSelfHam = selfHam;
				stalemateDefenderHam = defenderHam;
			}

			int breakSeconds =
				SimPlayerManager::instance()->getPvpStalemateBreakSeconds();
			if (breakSeconds > 0 && lastCombatProgressMs != 0 &&
					nowMs - lastCombatProgressMs >
						(uint64)breakSeconds * 1000) {
				uint64 brokenDefenderOid = defenderOid;
				uint64 idleMs = nowMs - lastCombatProgressMs;
				int graceSeconds = SimPlayerManager::instance()->
					getPvpStalemateGraceSeconds();

				{
					Locker locker(strongAgent);
					strongAgent->clearCombatState(true);
				}

				stalemateIgnoredOid = brokenDefenderOid;
				stalemateIgnoreUntilMs = nowMs +
					(uint64)graceSeconds * 1000;
				resetStalemateProgress();
				SimPlayerManager::instance()->recordPvpStalemateBreak(
					squadId, strongAgent->getObjectID(), brokenDefenderOid,
					idleMs);
				return;
			}
		} else {
			resetStalemateProgress();

			if (++phantomCombatTicks < 6)
				return;

			phantomCombatTicks = 0;

			Locker locker(strongAgent);
			strongAgent->clearCombatState(true);

			if (SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled()) {
				Logger::console.info("SimPvpBot squad=" +
					String::valueOf(squadId) + " oid=" +
					String::valueOf(strongAgent->getObjectID()) +
					" clearedCombat reason=noReachableEnemy", true);
			}
		}

		return;
	}

	phantomCombatTicks = 0;
	resetStalemateProgress();

	// P.7.5: combat is over but the bot is still on the floor (the jedi
	// recovery reflex only ticks in combat, and nothing in the stock AI ever
	// stands an NPC back up) — dust off and carry on.
	if (strongAgent->getPosture() == CreaturePosture::KNOCKEDDOWN) {
		Locker locker(strongAgent);
		strongAgent->setPosture(CreaturePosture::UPRIGHT, true, true);
	}

	scanForTargets();
}

void SimPvpBotController::scanForTargets() {
	ManagedReference<AiAgent*> strongAgent = agent;

	if (strongAgent == nullptr)
		return;

	Zone* zone = strongAgent->getZone();
	if (zone == nullptr)
		return;

	SimPlayerManager* manager = SimPlayerManager::instance();
	uint64 nowMs = System::getMiliTime();
	const float scanRadius = scoutRole ? manager->getPvpScoutScanRadiusMeters()
									   : manager->getPvpScanRadiusMeters();
	const bool allowBotVsBot = manager->isPvpBotVsBotCombatEnabled();

	CloseObjectsVector* vec = (CloseObjectsVector*) strongAgent->getCloseObjects();
	if (vec == nullptr)
		return;

	static const uint32 imperialHash = String("imperial").hashCode();
	static const uint32 rebelHash = String("rebel").hashCode();
	const uint32 enemyFaction = imperial ? rebelHash : imperialHash;

	Vector<TreeEntry*> objects;
	vec->safeCopyReceiversTo(objects, CloseObjectsVector::CREOTYPE);

	for (int i = 0; i < objects.size(); ++i) {
		SceneObject* obj = static_cast<SceneObject*>(objects.get(i));
		if (obj == nullptr)
			continue;

		CreatureObject* target = obj->asCreatureObject();
		if (target == nullptr || target == strongAgent.get())
			continue;

		if (target->isIncapacitated() || target->isDead())
			continue;

		// Starport loops hunt outdoors; never chase into buildings/cells.
		if (target->getParent() != nullptr)
			continue;

		if (target->getFaction() != enemyFaction)
			continue;

		bool targetIsPlayer = target->isPlayerCreature();

		if (!targetIsPlayer) {
			// Bot-vs-bot is gated: only other sim bots qualify, so squads can
			// never aggro ordinary world NPCs of the enemy faction.
			if (!allowBotVsBot)
				continue;

			AiAgent* targetAgent = target->asAiAgent();
			if (targetAgent == nullptr || !targetAgent->getSimPlayerBot())
				continue;
		}

		if (!target->isAttackableBy(strongAgent.get()))
			continue;

		if (strongAgent->getDistanceTo(target) >= scanRadius)
			continue;

		if (stalemateIgnoredOid != 0 &&
				nowMs < stalemateIgnoreUntilMs &&
				target->getObjectID() == stalemateIgnoredOid)
			continue;

		// P.6.2: a report-only scout calls the contact in (throttled) and
		// keeps observing instead of engaging - the manager converges a
		// patrol squad of this faction on the scout's city. Scouts still
		// defend themselves via the default combat trees if attacked.
		if (scoutRole && manager->isPvpScoutReportOnly()) {
			uint64 nowMs = System::getMiliTime();
			uint64 intervalMs =
				(uint64)manager->getPvpScoutReportIntervalSeconds() * 1000;

			if (lastContactReportMs == 0 ||
					nowMs - lastContactReportMs >= intervalMs) {
				lastContactReportMs = nowMs;
				manager->reportPvpContact(squadId, targetIsPlayer);
			}

			return;
		}

		{
			Locker locker(strongAgent);
			Locker crossLocker(target, strongAgent);

			strongAgent->setTargetObject(target);
			strongAgent->addDefender(target);
			strongAgent->setCombatState();

			// IDLE lets the base checkArrival resume the interrupted route
			// leg (moveTo(destination)) once combat ends.
			state = SimPlayerController::IDLE;
		}

		manager->recordPvpEngagement(squadId, targetIsPlayer);

		// P.6.3a: "contact!" callout (deduped by the announce cooldown, so a
		// squad's members engaging at once produce one leader shout).
		manager->announcePvpEvent(squadId,
			SimPlayerManager::PVP_ANNOUNCE_CONTACT);
		return;
	}
}

// ------------------------------------------------------
// Squad leader
// ------------------------------------------------------
SimPvPController::SimPvPController(AiAgent* aiAgent, uint64 squad, bool isImperial)
	: SimPvpBotController(aiAgent, squad, isImperial) {
	phase = PVP_FORMING;
	phaseSinceMs = System::getMiliTime();
	pathFailStreak = 0;
	shuttleTargetLocalPosition = Vector3(0, 0, 0);
	shuttleTargetCellOid = 0;
	shuttleTargetIsCollector = false;
	shuttleTargetInterplanetary = false;
	collectorDepartureActive = false;
	collectorDepartureEntry = false;
	collectorWorld = Vector3(0, 0, 0);
	collectorLocal = Vector3(0, 0, 0);
	collectorCell = nullptr;
	collectorOid = 0;
	collectorApproachAttempts = 0;
	collectorApproachStartedAtMs = 0;
	arrivalExitActive = false;
	arrivalOutdoor = Vector3(0, 0, 0);
	arrivalExitAttempts = 0;
	arrivalExitReenter = false;
	setLoggingName("SimPvPController");
}

SimPvPController::~SimPvPController() {
}

void SimPvPController::beginCityLoop(const String& planetName, const String& cityName,
		const Vector3& shuttlePos, const Vector3& hangoutPos) {
	planet = planetName;
	city = cityName;
	shuttleLocation = shuttlePos;
	shuttleTargetLocalPosition = shuttlePos;
	shuttleTargetCellOid = 0;
	shuttleTargetIsCollector = false;
	shuttleTargetInterplanetary = false;
	hangoutLocation = hangoutPos;
	transitLoiterSeconds = 0;
	pathFailStreak = 0;
	arrivalExitActive = false;
	arrivalExitAttempts = 0;
	arrivalExitReenter = false;
	setPhase(PVP_TO_HANGOUT);
	drivePhase("beginCityLoop");
}

void SimPvPController::beginTransitStop(const String& planetName, const String& cityName,
		const Vector3& padPos, int dwellSeconds) {
	planet = planetName;
	city = cityName;
	// hangout == shuttle == the pad: TO_HANGOUT "arrives" immediately, the
	// short transit loiter stands in for waiting on the connecting ship, then
	// TO_SHUTTLE/AWAITING re-enter the normal boarding path for the next leg.
	shuttleLocation = padPos;
	shuttleTargetLocalPosition = padPos;
	shuttleTargetCellOid = 0;
	shuttleTargetIsCollector = false;
	shuttleTargetInterplanetary = false;
	hangoutLocation = padPos;
	transitLoiterSeconds = dwellSeconds < 1 ? 1 : dwellSeconds;
	pathFailStreak = 0;
	arrivalExitActive = false;
	arrivalExitAttempts = 0;
	arrivalExitReenter = false;
	setPhase(PVP_TO_HANGOUT);
	drivePhase("beginTransitStop");
}

void SimPvPController::startSimLoop() {
	if (agent == nullptr)
		return;

	if (arrivalExitActive) {
		beginArrivalExit(arrivalOutdoor);
		return;
	}

	drivePhase("startSimLoop");
}

void SimPvPController::drivePhase(const String& reason) {
	if (agent == nullptr)
		return;

	if (SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled()) {
		Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
			" drivePhase=" + getPvpPhaseName() + " reason=" + reason +
			" city=" + planet + ":" + city, true);
	}

	switch (phase) {
	case PVP_FORMING:
	case PVP_TO_HANGOUT:
		setPhase(PVP_TO_HANGOUT);
		logMoveTarget("hangout", hangoutLocation);
		moveTo(hangoutLocation);
		break;
	case PVP_LOITERING:
		startLoitering();
		break;
	case PVP_TO_SHUTTLE:
		if (shuttleTargetIsCollector && shuttleTargetInterplanetary &&
				SimPlayerManager::instance()->isTicketCollectorTravelEnabled()) {
			beginCollectorDepartureApproach(reason);
			break;
		}
		logMoveTarget("shuttle", shuttleLocation);
		{
			CellObject* targetCell = nullptr;
			if (shuttleTargetCellOid != 0) {
				ZoneServer* zoneServer = ServerCore::getZoneServer();
				if (zoneServer != nullptr) {
					ManagedReference<SceneObject*> object =
						zoneServer->getObject(shuttleTargetCellOid);
					targetCell = object == nullptr ? nullptr :
						object.castTo<CellObject*>();
				}
			}

			if (targetCell == nullptr)
				moveTo(shuttleLocation);
			else
				moveTo(shuttleLocation, shuttleTargetLocalPosition, targetCell);
		}
		break;
	case PVP_AWAITING_SHUTTLE: {
		// Kill any stale work-loop chain, then keep exactly one 1s tick chain
		// alive so the leader keeps scanning while it waits at the pad. The
		// manager's shuttle-wait task performs the actual boarding.
		state = SimPlayerController::WAITING;
		uint64 generation = advanceWorkLoopGeneration("pvpAwaitShuttle");
		Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this, generation);
		task->schedule(1000);
		notifyReadyToTravel();
		break;
	}
	case PVP_ARRIVAL_REENTER:
	case PVP_ARRIVAL_EGRESS:
		// Arrival exit is driven by beginArrivalExit()/onArrived()/onPathFailed()
		// and the startSimLoop resume; drivePhase must not re-issue it here (that
		// would burn the bounded approach attempts).
		break;
	}
}

void SimPvPController::setPhase(PvpPhase newPhase) {
	phase = newPhase;
	phaseSinceMs = System::getMiliTime();
}

void SimPvPController::beginCollectorDepartureApproach(const String& reason) {
	SimPlayerManager* manager = SimPlayerManager::instance();
	ManagedReference<AiAgent*> strongAgent = agent;
	if (manager == nullptr || strongAgent == nullptr)
		return;

	if (!collectorDepartureActive) {
		collectorDepartureActive = true;
		collectorDepartureEntry = false;
		collectorApproachAttempts = 0;
		collectorApproachStartedAtMs = System::getMiliTime();
	}

	if (collectorApproachAttempts >= manager->getTicketCollectorApproachAttempts() ||
			System::getMiliTime() > collectorApproachStartedAtMs +
				(uint64)manager->getTicketCollectorApproachTtlSeconds() * 1000) {
		if (manager->isTicketCollectorFallbackToBoardFromNear()) {
			collectorDepartureActive = false;
			cellEgressSuppressed = false;
			setPhase(PVP_AWAITING_SHUTTLE);
			notifyReadyToTravel();
		} else {
			cancelCollectorDeparture("collectorApproachExhausted");
		}
		return;
	}

	collectorApproachAttempts++;
	ManagedReference<Zone*> zone;
	Vector3 currentWorld;
	{
		Locker agentLocker(strongAgent);
		zone = strongAgent->getZone();
		currentWorld = strongAgent->getWorldPosition();
	}

	if (zone == nullptr) {
		SimPlayerController::onPathFailed();
		return;
	}

	Vector3 resolvedWorld;
	Vector3 resolvedLocal;
	ManagedReference<CellObject*> resolvedCell;
	uint64 resolvedOid = 0;
	if (!manager->resolveNearestTicketCollector(zone, shuttleLocation,
			resolvedWorld, resolvedLocal, resolvedCell, resolvedOid)) {
		// The route target was classified as a collector at plan time, but the
		// world query can legitimately miss during a reload. Use the same bounded
		// fallback as a path failure rather than silently switching topology.
		if (manager->isTicketCollectorFallbackToBoardFromNear()) {
			collectorDepartureActive = false;
			cellEgressSuppressed = false;
			setPhase(PVP_AWAITING_SHUTTLE);
			notifyReadyToTravel();
		} else {
			cancelCollectorDeparture("collectorNotFound");
		}
		return;
	}

	collectorWorld = resolvedWorld;
	collectorLocal = resolvedLocal;
	collectorCell = resolvedCell;
	collectorOid = resolvedOid;

	bool contained = false;
	{
		Locker agentLocker(strongAgent);
		currentWorld = strongAgent->getWorldPosition();
		ManagedReference<SceneObject*> parent = strongAgent->getParent().get();
		contained = collectorCell != nullptr ?
			(parent != nullptr && parent->isCellObject() &&
				parent->getObjectID() == collectorCell->getObjectID()) :
			(parent == nullptr || !parent->isCellObject());
	}

	if (currentWorld.distanceTo(collectorWorld) <=
			manager->getTicketCollectorBoardRadiusMeters() && contained) {
		collectorDepartureActive = false;
		setPhase(PVP_AWAITING_SHUTTLE);
		notifyReadyToTravel();
		return;
	}

	Vector3 interiorWorld;
	Vector3 interiorLocal;
	ManagedReference<CellObject*> interiorCell;
	SimPlayerManager::StarportInteriorWaypointResult result =
		manager->resolveStarportInteriorWaypoint(zone, shuttleLocation,
			currentWorld, interiorWorld, interiorLocal, interiorCell);

	CellNavDiagLog::write("PVP_COLLECTOR_APPROACH squad=" +
		String::valueOf(squadId) + " agent=" +
		String::valueOf(strongAgent->getObjectID()) +
		" attempt=" + String::valueOf(collectorApproachAttempts) + " result=" +
		String::valueOf((int)result) + " collector=(" +
		String::valueOf(collectorWorld.getX()) + "," +
		String::valueOf(collectorWorld.getY()) + ") collectorCellOid=" +
		String::valueOf(collectorCell == nullptr ? 0 : collectorCell->getObjectID()) +
		" cur=(" + String::valueOf(currentWorld.getX()) + "," +
		String::valueOf(currentWorld.getY()) + ") action=" +
		String(result == SimPlayerManager::STARPORT_WAYPOINT_FOUND ?
			"moveToInterior" : result == SimPlayerManager::STARPORT_RESOLVE_FAILED ?
			"onPathFailed" : "plainMoveTo(collector)"));

	if (result == SimPlayerManager::STARPORT_RESOLVE_FAILED) {
		SimPlayerController::onPathFailed();
		return;
	}

	if (result == SimPlayerManager::STARPORT_WAYPOINT_FOUND) {
		collectorDepartureEntry = true;
		setPhase(PVP_TO_SHUTTLE);
		moveToInterior(interiorWorld, interiorLocal, interiorCell.get());
		return;
	}

	collectorDepartureEntry = false;
	cellEgressSuppressed = false;
	setPhase(PVP_TO_SHUTTLE);
	moveTo(collectorWorld, collectorWorld, collectorCell.get());

	if (SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled())
		Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
			" collectorApproach reason=" + reason, true);
}

void SimPvPController::beginArrivalExit(const Vector3& outdoorArrival) {
	if (agent == nullptr)
		return;

	SimPlayerManager* manager = SimPlayerManager::instance();
	if (manager == nullptr)
		return;

	if (!arrivalExitActive) {
		arrivalExitActive = true;
		arrivalExitAttempts = 0;
	}

	arrivalOutdoor = outdoorArrival;

	if (arrivalExitAttempts >= manager->getTicketCollectorApproachAttempts()) {
		// Bounded exit attempts exhausted: terminate cleanly (reposition outside +
		// drop from the barrier) instead of looping onPathFailed forever.
		abandonArrivalExit("attemptsExhausted");
		return;
	}

	arrivalExitAttempts++;
	ManagedReference<AiAgent*> strongAgent = agent;
	ManagedReference<Zone*> zone;
	Vector3 currentWorld;
	{
		Locker agentLocker(strongAgent);
		zone = strongAgent->getZone();
		currentWorld = strongAgent->getWorldPosition();
	}

	Vector3 interiorWorld;
	Vector3 interiorLocal;
	ManagedReference<CellObject*> interiorCell;
	SimPlayerManager::StarportInteriorWaypointResult result =
		manager->resolveStarportInteriorWaypoint(zone, outdoorArrival,
			currentWorld, interiorWorld, interiorLocal, interiorCell);

	if (result == SimPlayerManager::STARPORT_RESOLVE_FAILED) {
		// Transient miss: a bounded delayed retry (onPathFailed reschedules and the
		// startSimLoop resume re-drives beginArrivalExit); the attempts cap above
		// makes this terminate via abandonArrivalExit().
		setPhase(PVP_ARRIVAL_REENTER);
		SimPlayerController::onPathFailed();
		return;
	}

	clearCellEgressState();
	if (result == SimPlayerManager::STARPORT_WAYPOINT_FOUND) {
		arrivalExitReenter = true;
		setPhase(PVP_ARRIVAL_REENTER);
		moveToInterior(interiorWorld, interiorLocal, interiorCell.get());
		return;
	}

	arrivalExitReenter = false;
	setPhase(PVP_ARRIVAL_EGRESS);
	moveTo(outdoorArrival);
}

void SimPvPController::finishArrivalExit() {
	arrivalExitActive = false;
	arrivalExitAttempts = 0;
	arrivalExitReenter = false;
	clearCellEgressState();
}

void SimPvPController::abandonArrivalExit(const String& reason) {
	ManagedReference<AiAgent*> strongAgent = agent;
	uint64 oid = 0;
	if (strongAgent != nullptr) {
		oid = strongAgent->getObjectID();
		if (arrivalOutdoor.getX() != 0.f || arrivalOutdoor.getY() != 0.f) {
			// Invalidate in-flight work and tear down stale movement before the
			// reposition (board-path choreography: stale paths win this race live).
			prepareForRelocation(reason);
			Locker agentLocker(strongAgent);
			Zone* zone = strongAgent->getZone();
			if (zone != nullptr) {
				strongAgent->setMovementState(AiAgent::OBLIVIOUS);
				strongAgent->clearPatrolPoints();
				strongAgent->clearSavedPatrolPoints();
				strongAgent->clearCurrentPath();
				strongAgent->switchZone(zone->getZoneName(),
					arrivalOutdoor.getX(), arrivalOutdoor.getZ(),
					arrivalOutdoor.getY(), 0);
				// Re-anchor home outdoors: an OBLIVIOUS agent away from a stale
				// hollow home would otherwise PATHING_HOME back into the hollow.
				strongAgent->setHomeLocation(arrivalOutdoor.getX(),
					arrivalOutdoor.getZ(), arrivalOutdoor.getY(), nullptr);
			}
		}
	}
	finishArrivalExit();
	state = SimPlayerController::WAITING;
	// Drop out of the squad arrival barrier so it never wedges waiting on a bot
	// that could not exit; the barrier finalizes when the pending set empties.
	if (oid != 0)
		SimPlayerManager::instance()->onPvpArrivalExitComplete(squadId, oid);
	if (SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled())
		Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
			" abandonArrivalExit reason=" + reason, true);
}

void SimPvPController::cancelCollectorDeparture(const String& reason) {
	collectorDepartureActive = false;
	collectorDepartureEntry = false;
	cellEgressSuppressed = false;
	haltAgentMovement(reason);
	prepareForRelocation(reason);
	shuttleTargetIsCollector = false;
	shuttleTargetInterplanetary = false;
	// Terminal: drop the pending routed leg so the loiter->depart cycle does not
	// reselect the same unreachable collector leg forever (falls back to the simple
	// city shuttle).
	SimPlayerManager::instance()->abandonPvpRoutedTravel(squadId);
	setPhase(PVP_LOITERING);
	startLoitering();
}

void SimPvPController::onArrived() {
	pathFailStreak = 0;

	if (arrivalExitActive) {
		if (phase == PVP_ARRIVAL_REENTER) {
			clearCellEgressState();
			arrivalExitReenter = false;
			setPhase(PVP_ARRIVAL_EGRESS);
			moveTo(arrivalOutdoor);
			return;
		}

		if (phase == PVP_ARRIVAL_EGRESS) {
			bool outdoors = false;
			{
				Locker agentLocker(agent);
				ManagedReference<SceneObject*> parent = agent->getParent().get();
				outdoors = parent == nullptr || !parent->isCellObject();
			}
			if (outdoors) {
				finishArrivalExit();
				SimPlayerManager::instance()->onPvpArrivalExitComplete(
					squadId, agent->getObjectID());
			}
			return;
		}
	}

	if (collectorDepartureActive) {
		if (collectorDepartureEntry) {
			collectorDepartureEntry = false;
			cellEgressSuppressed = true;
			moveTo(collectorWorld, collectorWorld, collectorCell.get());
			return;
		}

		Vector3 currentWorld;
		bool contained = false;
		{
			Locker agentLocker(agent);
			currentWorld = agent->getWorldPosition();
			ManagedReference<SceneObject*> parent = agent->getParent().get();
			contained = collectorCell != nullptr ?
				(parent != nullptr && parent->isCellObject() &&
					parent->getObjectID() == collectorCell->getObjectID()) :
				(parent == nullptr || !parent->isCellObject());
		}
		if (currentWorld.distanceTo(collectorWorld) <=
				SimPlayerManager::instance()->getTicketCollectorBoardRadiusMeters() &&
				contained) {
			collectorDepartureActive = false;
			cellEgressSuppressed = false;
			setPhase(PVP_AWAITING_SHUTTLE);
			notifyReadyToTravel();
		} else {
			beginCollectorDepartureApproach("collectorArrivalOutsideGate");
		}
		return;
	}

	if (phase == PVP_TO_HANGOUT) {
		startLoitering();
	} else if (phase == PVP_TO_SHUTTLE) {
		setPhase(PVP_AWAITING_SHUTTLE);
		notifyReadyToTravel();
	}
	// Any other phase: stale arrival from an old leg; ignore.
}

void SimPvPController::onPathFailed() {
	if (arrivalExitActive) {
		SimPlayerController::onPathFailed();
		return;
	}

	if (collectorDepartureActive) {
		if (collectorApproachAttempts <
				SimPlayerManager::instance()->getTicketCollectorApproachAttempts() &&
				System::getMiliTime() <= collectorApproachStartedAtMs +
					(uint64)SimPlayerManager::instance()->
						getTicketCollectorApproachTtlSeconds() * 1000) {
			beginCollectorDepartureApproach("pathFailed");
			return;
		}

		if (SimPlayerManager::instance()->isTicketCollectorFallbackToBoardFromNear()) {
			collectorDepartureActive = false;
			cellEgressSuppressed = false;
			setPhase(PVP_AWAITING_SHUTTLE);
			notifyReadyToTravel();
		} else {
			cancelCollectorDeparture("collectorApproachExhausted");
		}
		return;
	}

	pathFailStreak++;

	Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
		" pathFailed phase=" + getPvpPhaseName() +
		" streak=" + String::valueOf(pathFailStreak), true);

	if (pathFailStreak >= 3) {
		forceAdvancePhase("pathFailStreakExhausted");
		return;
	}

	// Base behavior: retry startSimLoop() in 5s, which re-drives the current
	// phase (bounded by the streak counter above).
	SimPlayerController::onPathFailed();
}

void SimPvPController::prepareForRelocation(const String& reason) {
	SimPvpBotController::prepareForRelocation(reason);
	shuttleTargetLocalPosition = Vector3(0, 0, 0);
	shuttleTargetCellOid = 0;
	shuttleTargetIsCollector = false;
	shuttleTargetInterplanetary = false;
	collectorDepartureActive = false;
	collectorDepartureEntry = false;
	arrivalExitActive = false;
	arrivalExitAttempts = 0;
	arrivalExitReenter = false;
}

bool SimPvPController::acceptFoundPath(const Vector3& pathEnd) {
	// `destination` still holds the moveTo() target here (onPathFound only
	// overwrites it after acceptance). City legs are short navmesh or direct
	// overland paths, so the end must land near the target.
	float dx = pathEnd.getX() - destination.getX();
	float dy = pathEnd.getY() - destination.getY();
	bool accept = (dx * dx + dy * dy) <= 96.f * 96.f;

	// P.6.1c diagnostics-only: log EVERY path (accept or reject) so we can see
	// what target the path was computed against vs the phase's intended one.
	if (SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled()) {
		ManagedReference<AiAgent*> strongAgent = agent;
		Vector3 pos = strongAgent != nullptr ? strongAgent->getWorldPosition()
											  : Vector3(0, 0, 0);
		Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
			" pathFound phase=" + getPvpPhaseName() +
			" accept=" + String::valueOf(accept) +
			" pos=(" + String::valueOf((int)pos.getX()) + "," +
			String::valueOf((int)pos.getY()) + ")" +
			" pathEnd=(" + String::valueOf((int)pathEnd.getX()) + "," +
			String::valueOf((int)pathEnd.getY()) + ")" +
			" destination=(" + String::valueOf((int)destination.getX()) + "," +
			String::valueOf((int)destination.getY()) + ")", true);
	}

	return accept;
}

// P.6.1c diagnostics-only.
void SimPvPController::logMoveTarget(const char* label, const Vector3& target) {
	if (!SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled())
		return;

	ManagedReference<AiAgent*> strongAgent = agent;
	Vector3 pos = strongAgent != nullptr ? strongAgent->getWorldPosition()
										  : Vector3(0, 0, 0);
	Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
		" moveTo " + label +
		" from=(" + String::valueOf((int)pos.getX()) + "," +
		String::valueOf((int)pos.getY()) + ")" +
		" to=(" + String::valueOf((int)target.getX()) + "," +
		String::valueOf((int)target.getY()) + ")", true);
}

// P.6.1c diagnostics-only: heartbeat every ~8 ticks.
void SimPvPController::onTick() {
	SimPvpBotController::onTick();

	if (!SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled())
		return;

	if (++heartbeatTicks < 8)
		return;

	heartbeatTicks = 0;

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	if (phase != PVP_TO_HANGOUT && phase != PVP_TO_SHUTTLE)
		return;

	Vector3 pos = strongAgent->getWorldPosition();
	const Vector3& target = phase == PVP_TO_HANGOUT ? hangoutLocation
													: shuttleLocation;
	float dist = pos.distanceTo(target);

	Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
		" heartbeat phase=" + getPvpPhaseName() +
		" pos=(" + String::valueOf((int)pos.getX()) + "," +
		String::valueOf((int)pos.getY()) + ")" +
		" target=(" + String::valueOf((int)target.getX()) + "," +
		String::valueOf((int)target.getY()) + ")" +
		" distToTarget=" + String::valueOf((int)dist) +
		" moveState=" + String::valueOf(strongAgent->getMovementState()) +
		" patrolPts=" + String::valueOf(strongAgent->getPatrolPointSize()), true);
}

void SimPvPController::haltAgentMovement(const String& reason) {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	Locker locker(strongAgent);

	// Order matters: clearPatrolPoints() SAVES the active queue into
	// savedPatrolPoints while movementState is PATROLLING, so go OBLIVIOUS
	// first to make it a true clear. clearCurrentPath() drops the agent's
	// cached A* route (see onPathFound note) so the engine can't keep walking
	// the pre-interruption destination.
	strongAgent->setMovementState(AiAgent::OBLIVIOUS);
	strongAgent->clearPatrolPoints();
	strongAgent->clearSavedPatrolPoints();
	strongAgent->clearCurrentPath();

	if (SimPlayerManager::instance()->isPvpLogStateTransitionsEnabled()) {
		Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
			" haltAgentMovement reason=" + reason, true);
	}
}

void SimPvPController::forceAdvancePhase(const String& reason) {
	pathFailStreak = 0;

	Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
		" forceAdvancePhase from=" + getPvpPhaseName() + " reason=" + reason,
		true);

	switch (phase) {
	case PVP_FORMING:
		drivePhase("forceAdvance");
		break;
	case PVP_TO_HANGOUT: {
		// Could not reach the hangout: stop the leg dead (agent movement AND
		// controller route state, so no stale resume/path can revive it),
		// then post up where we are instead of retrying forever.
		haltAgentMovement("forceAdvanceToLoiter");
		prepareForRelocation("forceAdvanceToLoiter");
		startLoitering();
		// prepareForRelocation() killed the old tick chain; keep one alive so
		// the leader still scans for targets while it loiters.
		Reference<ArrivalCheckTask*> task =
			new ArrivalCheckTask(this, getWorkLoopGeneration());
		task->schedule(1000);
		break;
	}
	case PVP_LOITERING:
		enterToShuttle("forceAdvance");
		break;
	case PVP_TO_SHUTTLE:
	case PVP_AWAITING_SHUTTLE:
		// Could not reach the pad: stop the leg dead and board from wherever
		// we are (the same stuckFallback contract as P.4.5b travel - never
		// wedge at a port). drivePhase's AWAITING branch resets the work-loop
		// generation and keeps one fresh scan chain alive while waiting.
		haltAgentMovement("forceAdvanceToBoard");
		prepareForRelocation("forceAdvanceToBoard");
		setPhase(PVP_AWAITING_SHUTTLE);
		drivePhase("forceAdvance");
		break;
	case PVP_ARRIVAL_REENTER:
	case PVP_ARRIVAL_EGRESS:
		// Arrival exit owns the topology; a TTL only re-drives its current
		// resolver/path phase and never marks the squad outside.
		SimPlayerController::onPathFailed();
		break;
	}
}

void SimPvPController::startLoitering() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	setPhase(PVP_LOITERING);
	state = SimPlayerController::WAITING;

	int loiterMs;

	if (transitLoiterSeconds > 0) {
		// P.6.5a transit stop: brief wait at the pad for the connecting ship.
		// No "posted up" announce - the MOVEOUT callout already gave the route.
		loiterMs = transitLoiterSeconds * 1000;
	} else {
		SimPlayerManager* manager = SimPlayerManager::instance();
		int minSeconds = manager->getPvpLoiterMinSeconds();
		int maxSeconds = manager->getPvpLoiterMaxSeconds();

		if (maxSeconds < minSeconds)
			maxSeconds = minSeconds;

		loiterMs = minSeconds * 1000;
		if (maxSeconds > minSeconds)
			loiterMs += System::random((maxSeconds - minSeconds) * 1000);

		// P.6.3a: "posting up at the starport" callout to nearby players.
		SimPlayerManager::instance()->announcePvpEvent(squadId,
			SimPlayerManager::PVP_ANNOUNCE_ARRIVAL);
	}

	strongAgent->doAnimation("look_around");

	Reference<SimPvpLoiterTask*> task =
		new SimPvpLoiterTask(this, getWorkLoopGeneration());
	task->schedule(loiterMs);
}

void SimPvPController::finishLoitering() {
	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr)
		return;

	if (strongAgent->isDead())
		return;

	// Never walk away from a fight; check back shortly.
	if (strongAgent->isInCombat()) {
		Reference<SimPvpLoiterTask*> task =
			new SimPvpLoiterTask(this, getWorkLoopGeneration());
		task->schedule(5000);
		return;
	}

	// P.6.3a: "area clear, moving on" callout (suppressed at transit stops -
	// the squad is just waiting on its connection, not clearing an area).
	if (transitLoiterSeconds == 0)
		SimPlayerManager::instance()->announcePvpEvent(squadId,
			SimPlayerManager::PVP_ANNOUNCE_DEPARTURE);

	enterToShuttle("loiterComplete");
}

void SimPvPController::enterToShuttle(const String& reason) {
	SimPlayerManager::PvpDepartureTarget target;
	SimPlayerManager* manager = SimPlayerManager::instance();

	if (manager != nullptr && manager->onPvpSquadDepartureIntent(squadId,
			target)) {
		shuttleLocation = target.worldPos;
			shuttleTargetLocalPosition = target.localPos;
			shuttleTargetCellOid = target.cellOid;
			shuttleTargetIsCollector = target.isCollector;
			shuttleTargetInterplanetary = target.interplanetary;
	} else {
		// Keep the current city pad as the safe fallback if the squad row has
		// disappeared during a maintenance/reform race.
		shuttleTargetLocalPosition = shuttleLocation;
			shuttleTargetCellOid = 0;
			shuttleTargetIsCollector = false;
			shuttleTargetInterplanetary = false;
	}

	setPhase(PVP_TO_SHUTTLE);
	drivePhase(reason);
}

void SimPvPController::notifyReadyToTravel() {
	SimPlayerManager::instance()->onPvpSquadReadyToTravel(squadId);
}

void SimPvPController::interruptForConvergence() {
	if (agent == nullptr)
		return;

	// Already heading out - boarding will consume the pending destination.
	if (phase == PVP_TO_SHUTTLE || phase == PVP_AWAITING_SHUTTLE ||
			phase == PVP_ARRIVAL_REENTER || phase == PVP_ARRIVAL_EGRESS)
		return;

	Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
		" interruptForConvergence from=" + getPvpPhaseName(), true);

	// P.6.3a: "moving to reinforce" callout.
	SimPlayerManager::instance()->announcePvpEvent(squadId,
		SimPlayerManager::PVP_ANNOUNCE_CONVERGE);

	haltAgentMovement("convergence");
	prepareForRelocation("convergence");
	enterToShuttle("convergence");

	// prepareForRelocation() killed the old tick chain. drivePhase's moveTo
	// normally restarts one via onPathFound, but a combat-deferred moveTo
	// (state=IDLE) would otherwise have no chain to resume it - keep one alive.
	Reference<ArrivalCheckTask*> task =
		new ArrivalCheckTask(this, getWorkLoopGeneration());
	task->schedule(1000);
}

void SimPvPController::interruptForBreakOff() {
	if (agent == nullptr)
		return;

	// Already heading out - boarding will consume the pending destination.
	if (phase == PVP_TO_SHUTTLE || phase == PVP_AWAITING_SHUTTLE ||
			phase == PVP_ARRIVAL_REENTER || phase == PVP_ARRIVAL_EGRESS)
		return;

	Logger::console.info("SimPvpLeader squad=" + String::valueOf(squadId) +
		" interruptForBreakOff from=" + getPvpPhaseName(), true);

	haltAgentMovement("breakOff");
	prepareForRelocation("breakOff");
	enterToShuttle("breakOff");

	// prepareForRelocation() killed the old tick chain. Keep one alive when the
	// interrupted move was deferred by combat and had no path callback to do it.
	Reference<ArrivalCheckTask*> task =
		new ArrivalCheckTask(this, getWorkLoopGeneration());
	task->schedule(1000);
}

String SimPvPController::getPvpPhaseName() const {
	switch (phase) {
	case PVP_FORMING:
		return "forming";
	case PVP_TO_HANGOUT:
		return "movingToHangout";
	case PVP_LOITERING:
		return "loitering";
	case PVP_TO_SHUTTLE:
		return "movingToShuttle";
	case PVP_AWAITING_SHUTTLE:
		return "awaitingShuttle";
	case PVP_ARRIVAL_REENTER:
		return "arrivalReenter";
	case PVP_ARRIVAL_EGRESS:
		return "arrivalEgress";
	}

	return "unknown";
}

// ------------------------------------------------------
// Squad member
// ------------------------------------------------------
SimPvPMemberController::SimPvPMemberController(AiAgent* aiAgent, uint64 squad,
		bool isImperial, AiAgent* leader)
	: SimPvpBotController(aiAgent, squad, isImperial) {
	leaderAgent = leader;
	setLoggingName("SimPvPMemberController");
}

SimPvPMemberController::~SimPvPMemberController() {
}

void SimPvPMemberController::startSimLoop() {
	if (agent == nullptr)
		return;

	if (arrivalExitActive) {
		beginArrivalExit(arrivalOutdoor);
		return;
	}

	assertFollow();

	// Keep exactly one 1s tick chain alive for scanning/death detection. The
	// member never calls moveTo(); the FOLLOW trees own its movement.
	state = SimPlayerController::WAITING;
	uint64 generation = advanceWorkLoopGeneration("pvpMemberStart");
	Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this, generation);
	task->schedule(1000);
}

void SimPvPMemberController::onArrived() {
	if (!arrivalExitActive)
		return;

	if (arrivalExitReenter) {
		clearCellEgressState();
		arrivalExitReenter = false;
		moveTo(arrivalOutdoor);
		return;
	}

	bool outdoors = false;
	{
		Locker agentLocker(agent);
		ManagedReference<SceneObject*> parent = agent->getParent().get();
		outdoors = parent == nullptr || !parent->isCellObject();
	}
	if (outdoors) {
		arrivalExitActive = false;
		arrivalExitAttempts = 0;
		clearCellEgressState();
		SimPlayerManager::instance()->onPvpArrivalExitComplete(
			squadId, agent->getObjectID());
	}
}

void SimPvPMemberController::beginArrivalExit(const Vector3& outdoorArrival) {
	if (agent == nullptr)
		return;

	SimPlayerManager* manager = SimPlayerManager::instance();
	if (manager == nullptr)
		return;

	arrivalExitActive = true;
	arrivalOutdoor = outdoorArrival;

	if (arrivalExitAttempts >= manager->getTicketCollectorApproachAttempts()) {
		// Bounded: members enforce the same cap as the leader so a member that
		// cannot exit never blocks the squad arrival barrier forever.
		abandonArrivalExit("attemptsExhausted");
		return;
	}

	arrivalExitAttempts++;

	ManagedReference<AiAgent*> strongAgent = agent;
	ManagedReference<Zone*> zone;
	Vector3 currentWorld;
	{
		Locker agentLocker(strongAgent);
		zone = strongAgent->getZone();
		currentWorld = strongAgent->getWorldPosition();
		strongAgent->setMovementState(AiAgent::OBLIVIOUS);
		strongAgent->clearPatrolPoints();
		strongAgent->clearSavedPatrolPoints();
		strongAgent->clearCurrentPath();
	}

	Vector3 interiorWorld;
	Vector3 interiorLocal;
	ManagedReference<CellObject*> interiorCell;
	SimPlayerManager::StarportInteriorWaypointResult result =
		manager->resolveStarportInteriorWaypoint(zone, outdoorArrival,
			currentWorld, interiorWorld, interiorLocal, interiorCell);

	if (result == SimPlayerManager::STARPORT_RESOLVE_FAILED) {
		SimPlayerController::onPathFailed();
		return;
	}

	clearCellEgressState();
	if (result == SimPlayerManager::STARPORT_WAYPOINT_FOUND) {
		arrivalExitReenter = true;
		moveToInterior(interiorWorld, interiorLocal, interiorCell.get());
	} else {
		arrivalExitReenter = false;
		moveTo(outdoorArrival);
	}
}

void SimPvPMemberController::abandonArrivalExit(const String& reason) {
	ManagedReference<AiAgent*> strongAgent = agent;
	uint64 oid = 0;
	if (strongAgent != nullptr) {
		oid = strongAgent->getObjectID();
		if (arrivalOutdoor.getX() != 0.f || arrivalOutdoor.getY() != 0.f) {
			prepareForRelocation(reason);
			Locker agentLocker(strongAgent);
			Zone* zone = strongAgent->getZone();
			if (zone != nullptr) {
				strongAgent->setMovementState(AiAgent::OBLIVIOUS);
				strongAgent->clearPatrolPoints();
				strongAgent->clearSavedPatrolPoints();
				strongAgent->clearCurrentPath();
				strongAgent->switchZone(zone->getZoneName(),
					arrivalOutdoor.getX(), arrivalOutdoor.getZ(),
					arrivalOutdoor.getY(), 0);
				// Re-anchor home outdoors: an OBLIVIOUS agent away from a stale
				// hollow home would otherwise PATHING_HOME back into the hollow.
				strongAgent->setHomeLocation(arrivalOutdoor.getX(),
					arrivalOutdoor.getZ(), arrivalOutdoor.getY(), nullptr);
			}
		}
	}
	arrivalExitActive = false;
	arrivalExitAttempts = 0;
	arrivalExitReenter = false;
	clearCellEgressState();
	state = SimPlayerController::WAITING;
	if (oid != 0)
		SimPlayerManager::instance()->onPvpArrivalExitComplete(squadId, oid);
	// Re-establish the follow AND the tick chain: startSimLoop returned right after
	// beginArrivalExit, and the barrier finalization only calls assertFollow, so
	// without this the member's onTick (death report, combat scan, follow self-heal)
	// would stop permanently.
	assertFollow();
	Reference<ArrivalCheckTask*> task =
		new ArrivalCheckTask(this, getWorkLoopGeneration());
	task->schedule(1000);
	(void)reason;
}

void SimPvPMemberController::onPathFailed() {
	if (arrivalExitActive) {
		SimPlayerController::onPathFailed();
		return;
	}

	SimPlayerController::onPathFailed();
}

void SimPvPMemberController::onTick() {
	SimPvpBotController::onTick();

	ManagedReference<AiAgent*> strongAgent = agent;
	if (strongAgent == nullptr || strongAgent->isDead() ||
			strongAgent->isInCombat())
		return;

	if (arrivalExitActive)
		return;

	// Follow self-heal: combat and zone changes can clear followObject or the
	// FOLLOWING movement state; quietly re-assert it.
	assertFollow();
}

void SimPvPMemberController::assertFollow() {
	ManagedReference<AiAgent*> strongAgent = agent;
	ManagedReference<AiAgent*> leader = leaderAgent;

	if (strongAgent == nullptr || leader == nullptr)
		return;

	if (arrivalExitActive ||
			!SimPlayerManager::instance()->isPvpArrivalExitComplete(squadId))
		return;

	if (strongAgent->isDead() || strongAgent->isInCombat())
		return;

	if (leader->isDead())
		return; // Maintenance promotes a new leader and calls setLeader().

	ManagedReference<SceneObject*> currentFollow =
		strongAgent->getFollowObject().get();

	if (currentFollow != nullptr &&
			currentFollow->getObjectID() == leader->getObjectID() &&
			strongAgent->getMovementState() == AiAgent::FOLLOWING)
		return;

	Locker locker(strongAgent);
	Locker crossLocker(leader, strongAgent);

	strongAgent->setFollowObject(leader);
	strongAgent->setMovementState(AiAgent::FOLLOWING);
	strongAgent->activateAiBehavior(true);
}

void SimPvPMemberController::setLeader(AiAgent* leader) {
	leaderAgent = leader;
	assertFollow();
}
