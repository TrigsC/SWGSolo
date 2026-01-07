/*
 * SimPvPController.cpp
 * Combat Safety Update + Cycle-to-next-stop
 */

#include "SimPvPController.h"
#include "SimPlayerManager.h"

#include "engine/core/Core.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/objects/creature/ai/CreatureTemplate.h"
#include "server/zone/CloseObjectsVector.h"
#include "server/zone/TreeEntry.h"
#include "templates/params/creature/ObjectFlag.h"

#include "system/lang/System.h"

//#define DEBUG_SIMPVP

// ------------------------------------------------------
// Task
// ------------------------------------------------------
SimPvPBehaviorTask::SimPvPBehaviorTask(SimPvPController* ctrl)
	: controller(ctrl) {
}

void SimPvPBehaviorTask::run() {
	Reference<SimPvPController*> strongRef = controller.get();
	if (strongRef == nullptr)
		return;

	Core::getTaskManager()->executeTask([strongRef]() {
		strongRef->finishLoitering();
	}, "SimPvPLoiterLambda");
}

// ------------------------------------------------------
// Controller
// ------------------------------------------------------
SimPvPController::SimPvPController(AiAgent* aiAgent, bool imperial)
	: SimPlayerController(aiAgent) {
	isImperial = imperial;
	returningToShuttle = false;
	cycleRequested = false;

	runSpeed = 3.0f;
	setLoggingName("SimPvPController");

	// Default route (original behavior)
	spawnLocation = Vector3(4963.0f, -4892.0f, 3.0f);
	hangoutLocation = Vector3(4807.0f, -4700.0f, 4.0f);
}

SimPvPController::SimPvPController(AiAgent* aiAgent, bool imperial, const Vector3& spawnLoc, const Vector3& hangoutLoc)
	: SimPvPController(aiAgent, imperial) {
	setRoute(spawnLoc, hangoutLoc);
}

SimPvPController::~SimPvPController() {
}

void SimPvPController::setRoute(const Vector3& spawnLoc, const Vector3& hangoutLoc) {
	spawnLocation = spawnLoc;
	hangoutLocation = hangoutLoc;
}

void SimPvPController::setCycleContext(SimPlayerManager* mgr,
                                       const String& tmpl,
                                       const String& grpType,
                                       const String& planetName,
                                       const String& locName) {
	manager = mgr;
	templateName = tmpl;
	groupType = grpType;
	planet = planetName;
	locationName = locName;
}

void SimPvPController::startSimLoop() {
	if (agent == nullptr)
		return;

	agent->setFaction(isImperial ? String("imperial").hashCode() : String("rebel").hashCode());
	agent->setPvpStatusBitmask(ObjectFlag::OVERT | ObjectFlag::ATTACKABLE);
#ifdef DEBUG_SIMPVP
	Logger::console.info("SimPvP: Spawning at Shuttle. Moving to hangout.", true);
#endif
	startPatrol();
}

void SimPvPController::startPatrol() {
	state = SimPlayerController::MOVING;
	returningToShuttle = false;
	moveTo(hangoutLocation);
}

void SimPvPController::returnToShuttle() {
	state = SimPlayerController::MOVING;
	returningToShuttle = true;
#ifdef DEBUG_SIMPVP
	Logger::console.info("SimPvP: Patrol done. Returning to Shuttle.", true);
#endif
	moveTo(spawnLocation);
}

void SimPvPController::onArrived() {
	const uint64 oid = (agent != nullptr) ? agent->getObjectID() : 0;
#ifdef DEBUG_SIMPVP
	Logger::console.info(
		"SimPvP: onArrived oid=" + String::valueOf(oid) +
		" returningToShuttle=" + String::valueOf(returningToShuttle) +
		" cycleRequested=" + String::valueOf(cycleRequested) +
		" mgrSet=" + String::valueOf(manager != nullptr) +
		" groupType=" + groupType +
		" template=" + templateName,
		true
	);
#endif
	if (returningToShuttle) {
		// IMPORTANT: prevent spam / repeated cycle requests
		if (cycleRequested)
			return;

		cycleRequested = true;

		// Stop SimPlayer movement logic from re-issuing moveTo(spawn) again
		state = SimPlayerController::WAITING;

		requestCycleToNextStop();
		return;
	}

	startLoitering();
}

void SimPvPController::startLoitering() {
    state = SimPlayerController::WAITING;

    // Random loiter between 30s and 180s
    const int minMs = loiterMs;
    const int maxMs = 180000;
    const int randomized = minMs + System::random(maxMs - minMs);
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPvP: Arrived at Starport. Loitering for " +
                         String::valueOf(randomized / 1000) + "s...", true);
#endif
    if (agent != nullptr)
        agent->doAnimation("look_around");

    Reference<SimPvPBehaviorTask*> task = new SimPvPBehaviorTask(this);
    task->schedule(randomized);
}

void SimPvPController::finishLoitering() {
	if (agent == nullptr)
		return;

	// Don’t leave if in combat; extend loiter a bit
	if (agent->isInCombat()) {
		Logger::console.info("SimPvP: Combat in progress. Extending loiter...", true);
		Reference<SimPvPBehaviorTask*> task = new SimPvPBehaviorTask(this);
		task->schedule(5000);
		return;
	}

	returnToShuttle();
}

void SimPvPController::requestCycleToNextStop() {
	const uint64 oid = (agent != nullptr) ? agent->getObjectID() : 0;
#ifdef DEBUG_SIMPVP
	Logger::console.info(
		"SimPvP: requestCycleToNextStop oid=" + String::valueOf(oid) +
		" mgrSet=" + String::valueOf(manager != nullptr) +
		" groupType=" + groupType +
		" template=" + templateName +
		" planet=" + planet +
		" location=" + locationName,
		true
	);
#endif
	if (agent == nullptr) {
#ifdef DEBUG_SIMPVP
		Logger::console.info("SimPvP: requestCycleToNextStop failed (agent missing).", true);
#endif
		return;
	}

	// Self-heal manager if context wasn't set (toggleBot / fallback spawns)
	if (manager == nullptr) {
		manager = SimPlayerManager::instance();
#ifdef DEBUG_SIMPVP
		Logger::console.info("SimPvP: resolved manager via SimPlayerManager::instance() -> mgrSet=" + String::valueOf(manager != nullptr), true);
#endif
	}

	// Self-heal missing context so cycling still works even for fallback stormtroopers
	if (groupType.isEmpty())
		groupType = "pvp_solo";

	if (templateName.isEmpty()) {
		const CreatureTemplate* tmpl = agent->getCreatureTemplate();
		if (tmpl != nullptr)
			templateName = tmpl->getTemplateName();
	}

	if (planet.isEmpty()) {
		Zone* z = agent->getZone();
		if (z != nullptr)
			planet = z->getZoneName();
	}

	if (locationName.isEmpty())
		locationName = "unknown";

	if (manager == nullptr) {
		Logger::console.info("SimPvP: requestCycleToNextStop failed (manager missing).", true);
		agent->destroyObjectFromWorld(true);
		return;
	}
#ifdef DEBUG_SIMPVP
	Logger::console.info(
		"SimPvP: cycling oid=" + String::valueOf(oid) +
		" using groupType=" + groupType +
		" template=" + templateName +
		" from " + planet + ":" + locationName,
		true
	);
#endif
	// manager->cyclePvPBot(oid, groupType, templateName, isImperial, planet, locationName);
	manager->cyclePvPBotWhenShuttleReady(oid, groupType, templateName, isImperial, planet, locationName, 0);
}

void SimPvPController::onTick() {
	if (agent == nullptr || agent->isDead())
		return;

	if (agent->isInCombat())
		return;

	scanForTargets();
}

void SimPvPController::scanForTargets() {
	if (agent == nullptr)
		return;

	Zone* zone = agent->getZone();
	if (zone == nullptr)
		return;

	CloseObjectsVector* vec = (CloseObjectsVector*) agent->getCloseObjects();
	if (vec == nullptr)
		return;

	Vector<TreeEntry*> objects;
	vec->safeCopyReceiversTo(objects, CloseObjectsVector::CREOTYPE);

	for (int i = 0; i < objects.size(); ++i) {
		SceneObject* obj = static_cast<SceneObject*>(objects.get(i));
		if (obj == nullptr || !obj->isPlayerCreature())
			continue;

		CreatureObject* player = obj->asCreatureObject();
		if (player == nullptr || player->isIncapacitated() || player->isDead())
			continue;

		// Don’t attack players in buildings/cells
		if (player->getParent() != nullptr)
			continue;

		bool playerImp = (player->getFaction() == String("imperial").hashCode());
		bool playerReb = (player->getFaction() == String("rebel").hashCode());

		bool enemy = false;
		if (isImperial && playerReb) enemy = true;
		if (!isImperial && playerImp) enemy = true;

		if (!enemy)
			continue;

		if (!player->isAttackableBy(agent))
			continue;

		float dist = agent->getDistanceTo(player);
		if (dist >= 40.0f)
			continue;
#ifdef DEBUG_SIMPVP
		Logger::console.info("SimPvP: ENGAGING TARGET: " + player->getFirstName(), true);
#endif
		Locker locker(agent);
		Locker crossLocker(player, agent);

		agent->setTargetObject(player);
		agent->addDefender(player);
		agent->setCombatState();

		state = SimPlayerController::IDLE;
		return;
	}
}