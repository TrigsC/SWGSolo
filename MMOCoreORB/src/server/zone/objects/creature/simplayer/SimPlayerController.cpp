/*
 * SimPlayerController.cpp
 * Robust Version: Includes Auto-Retry for bad paths
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

    // Perform the heavy math
    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        // Validation: If size <= 2, it's usually a straight line (failure)
        if (path != nullptr && path->size() > 2) { 
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
    retryCount = 0; // Initialize counter
    setLoggingName("SimPlayerController");
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

Vector3 SimPlayerController::findNearestHighDensityResource(const String& resourceClass) {
    if (agent != nullptr) return agent->getWorldPosition();
    return Vector3(0, 0, 0);
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    // Save target for retries
    targetResource = resourceName;

    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    state = SEARCHING_RESOURCE;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();

    // --- RANDOM SCOUTING ---
    int distance = 100 + System::random(200); // 100m - 300m range
    int angle = System::random(360);
    
    float rads = angle * (M_PI / 180.0f);
    float offsetX = distance * cos(rads);
    float offsetY = distance * sin(rads);

    float targetX = currentPos.getX() + offsetX;
    float targetY = currentPos.getY() + offsetY; 
    
    // Lift target slightly to help NavMesh snapping
    float targetZ = zone->getHeight(targetX, targetY) + 1.0f; 

    // Only log if it's the first try to avoid spamming console on retries
    if (retryCount == 0) {
        Logger::console.info("SimPlayer: Looking for [" + resourceName + "]...", true);
    }

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
    
    // Success! Reset retry counter
    retryCount = 0;

    state = MOVING;
    Logger::console.info("SUCCESS: Path found (" + String::valueOf(path->size()) + " nodes). Moving.", true);

    // 1. Clear Distractions
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true); 

    // 2. Load Path
    agent->clearPatrolPoints();

    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wc = path->get(i);
        Vector3 point = wc.getPoint();
        
        PatrolPoint pp;
        pp.setPosition(point.getX(), point.getZ(), point.getY());
        agent->addPatrolPoint(pp);
    }

    // 3. Set Home to Destination (Anti-Leash)
    if (path->size() > 0) {
        WorldCoordinates lastWc = path->get(path->size() - 1);
        Vector3 lastPt = lastWc.getPoint();
        agent->setHomeLocation(lastPt.getX(), lastPt.getZ(), lastPt.getY());
    }

    // 4. Force Run
    float runSpeed = agent->getRunSpeed();
    Logger::console.info("Current Run Speed(" + String::valueOf(runSpeed) + "). Moving.", true);
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); 
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    
    if (retryCount < 10) {
        retryCount++;
        // Try again immediately with a different random spot
        Logger::console.info("Path failed (blocked/straight-line). Retrying attempt " + String::valueOf(retryCount) + "...", true);
        goToResource(targetResource);
    } else {
        Logger::console.info("FAILURE: Could not find any valid path after 10 attempts. I am stuck.", true);
        retryCount = 0;
    }
}