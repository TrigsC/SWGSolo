/*
 * SimPlayerController.cpp
 * Final Compilable Version
 */

#include "SimPlayerController.h"
#include "engine/core/Core.h"
#include "engine/core/TaskManager.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h"
#include "server/zone/Zone.h"

// --------------------------------------------------------
// Task Implementation
// --------------------------------------------------------
void FindResourcePathTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    Logger::console.info("SimPlayerTask: Background task started.", true);

    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    int pathSize = (path != nullptr) ? path->size() : -1;
    Logger::console.info("SimPlayerTask: Calculation finished. Path size: " + String::valueOf(pathSize), true);

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        if (path != nullptr && path->size() > 0) {
            strongCtrl->onPathFound(path);
        } else {
            strongCtrl->onPathFailed();
            if (path) delete path;
        }
    }, "SimPlayerResultLambda");
}

// --------------------------------------------------------
// Controller Implementation
// --------------------------------------------------------

SimPlayerController::SimPlayerController(AiAgent* aiAgent) {
    agent = aiAgent;
    state = IDLE;
    setLoggingName("SimPlayerController"); // Now valid because we inherit Logger
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

// Stub method to satisfy the header - we aren't using this yet
Vector3 SimPlayerController::findNearestHighDensityResource(const String& resourceClass) {
    if (agent != nullptr) return agent->getWorldPosition();
    return Vector3(0, 0, 0);
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    // Reset Home to prevent leash behavior
    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    state = SEARCHING_RESOURCE;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();

    // --- SCOUT LOGIC: Random Direction ---
    // Instead of querying the complex ResourceManager, we simulate scouting
    int distance = 500 + System::random(1000); // 500m to 1500m
    int angle = System::random(360);
    
    float rads = angle * (M_PI / 180.0f);
    float offsetX = distance * cos(rads);
    float offsetY = distance * sin(rads);

    float targetX = currentPos.getX() + offsetX;
    float targetY = currentPos.getY() + offsetY; // North/South
    float targetZ = zone->getHeight(targetX, targetY); 

    // 'info' is now valid because we inherit from Logger
    Logger::console.info("SimPlayer: Scouting " + resourceName + " at " + String::valueOf(distance) + "m (Dir: " + String::valueOf(angle) + ")", true);
    Logger::console.info("DEBUG: Target -> X:" + String::valueOf(targetX) + " Y:" + String::valueOf(targetY) + " Z:" + String::valueOf(targetZ), true);

    Vector3 targetPos(targetX, targetY, targetZ);

    state = CALCULATING_PATH;
    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr); 

    Reference<FindResourcePathTask*> task = new FindResourcePathTask(this, startCoord, endCoord, zone);
    task->execute(); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) {
        if (path) delete path;
        return;
    }
    
    if (path == nullptr || path->size() == 0) {
        Logger::console.info("ERROR: Path returned EMPTY.", true);
        if (path) delete path;
        onPathFailed();
        return;
    }

    state = MOVING;
    Logger::console.info("SUCCESS: Path found with " + String::valueOf(path->size()) + " waypoints. Moving...", true);

    agent->clearPatrolPoints();

    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wc = path->get(i);
        Vector3 point = wc.getPoint();
        
        PatrolPoint pp;
        pp.setPosition(point.getX(), point.getZ(), point.getY());
        agent->addPatrolPoint(pp);
    }

    // Force Run Speed
    float runSpeed = agent->getRunSpeed();
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); 
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    Logger::console.info("FAILURE: Could not find path.", true);
}