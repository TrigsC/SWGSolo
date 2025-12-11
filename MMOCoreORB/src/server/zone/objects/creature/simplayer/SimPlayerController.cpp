/*
 * SimPlayerController.cpp
 * Debugging Version: Coordinate Verification + Aggressive AI Override
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
    retryCount = 0;
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

    targetResource = resourceName;

    // Reset Home to current location temporarily so we don't leash while planning
    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    state = SEARCHING_RESOURCE;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();

    // --- RANDOM SCOUTING ---
    int distance = 100 + System::random(200); 
    int angle = System::random(360);
    
    float rads = angle * (M_PI / 180.0f);
    float offsetX = distance * cos(rads);
    float offsetY = distance * sin(rads);

    float targetX = currentPos.getX() + offsetX;
    float targetY = currentPos.getY() + offsetY; 
    
    // Lift target slightly +1.0m
    float targetZ = zone->getHeight(targetX, targetY) + 1.0f; 

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
    
    retryCount = 0;
    state = MOVING;

    // --- COORDINATE LOGGING ---
    // This answers "Where are they going?"
    if (path->size() > 0) {
        Vector3 startPt = agent->getWorldPosition();
        Vector3 firstPt = path->get(0).getPoint();
        Vector3 endPt = path->get(path->size() - 1).getPoint();
        
        Logger::console.info("------------------------------------------------", true);
        Logger::console.info("PATH DEBUG:", true);
        Logger::console.info("Start Pos: " + String::valueOf(startPt.getX()) + ", " + String::valueOf(startPt.getY()) + ", " + String::valueOf(startPt.getZ()), true);
        Logger::console.info("First Step: " + String::valueOf(firstPt.getX()) + ", " + String::valueOf(firstPt.getY()) + ", " + String::valueOf(firstPt.getZ()), true);
        Logger::console.info("Final Dest: " + String::valueOf(endPt.getX()) + ", " + String::valueOf(endPt.getY()) + ", " + String::valueOf(endPt.getZ()), true);
        Logger::console.info("Nodes: " + String::valueOf(path->size()), true);
        Logger::console.info("------------------------------------------------", true);
    }

    // 1. Aggressive Lobotomy (Clear Distractions)
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    // Force OBLIVIOUS first to reset internal aggro timers
    agent->setMovementState(AiAgent::OBLIVIOUS);

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
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);
    Logger::console.info("SimPlayer: Speed set to " + String::valueOf(runSpeed), true);

    // 5. Jumpstart
    // Explicitly shove the first point into the "Next Step" slot
    if (path->size() > 0) {
        PatrolPoint firstPP = agent->getNextPosition(); // Actually gets the 0th point from vector
        agent->setNextStepPosition(firstPP.getPositionX(), firstPP.getPositionZ(), firstPP.getPositionY(), firstPP.getCell());
    }

    // 6. Activate
    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); 
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    
    if (retryCount < 10) {
        retryCount++;
        Logger::console.info("Path failed (blocked/straight-line). Retrying attempt " + String::valueOf(retryCount) + "...", true);
        goToResource(targetResource);
    } else {
        Logger::console.info("FAILURE: Could not find any valid path after 10 attempts. I am stuck.", true);
        retryCount = 0;
    }
}