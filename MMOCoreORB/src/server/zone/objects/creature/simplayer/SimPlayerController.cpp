/*
 * SimPlayerController.cpp
 * Tuned for Navigation Reliability (Short Hops + Elevation Fix)
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

    Logger::console.info("SimPlayerTask: Calculating path...", true);

    // Perform the heavy math
    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    int pathSize = (path != nullptr) ? path->size() : -1;
    Logger::console.info("SimPlayerTask: Finished. Path nodes found: " + String::valueOf(pathSize), true);

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        if (path != nullptr && path->size() > 2) { // Logic Change: Require > 2 points to count as a valid "Navigated" path
            strongCtrl->onPathFound(path);
        } else {
            // If size is 2, it's a straight line (Raycast fallback). We treat this as failure to avoid walking through walls.
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

    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    state = SEARCHING_RESOURCE;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();

    // --- TUNED SCOUT LOGIC ---
    // Fix 1: Reduce distance to ensure NavMesh is loaded.
    // 100m to 250m is safe for Recast.
    int distance = 100 + System::random(150); 
    int angle = System::random(360);
    
    float rads = angle * (M_PI / 180.0f);
    float offsetX = distance * cos(rads);
    float offsetY = distance * sin(rads);

    float targetX = currentPos.getX() + offsetX;
    float targetY = currentPos.getY() + offsetY; // North/South
    
    // Fix 2: Lift the target slightly (+1.0m) to ensure it doesn't clip under the NavMesh
    float targetZ = zone->getHeight(targetX, targetY) + 1.0f; 

    Logger::console.info("SimPlayer: Looking for [" + resourceName + "] - Scouting point " + String::valueOf(distance) + "m away (Dir: " + String::valueOf(angle) + ")", true);
    Logger::console.info("DEBUG: Target Coords -> X:" + String::valueOf(targetX) + " Y:" + String::valueOf(targetY) + " Z:" + String::valueOf(targetZ), true);

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
    
    if (path == nullptr) return;

    state = MOVING;
    info("SUCCESS: Valid path found with " + String::valueOf(path->size()) + " waypoints. Moving...", true);

    // 1. Wipe the Agent's brain of distractions
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);

    // 2. Load the Path
    agent->clearPatrolPoints();
    
    // Convert WorldCoordinates to PatrolPoints
    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wc = path->get(i);
        Vector3 point = wc.getPoint();
        
        PatrolPoint pp;
        pp.setPosition(point.getX(), point.getZ(), point.getY());
        agent->addPatrolPoint(pp);
    }

    // 3. THE TRICK: Set "Home" to the Destination
    // This prevents the bot from "Leashing" back to where it spawned.
    // Instead, if it gets confused, it will try to return to the resource location!
    if (path->size() > 0) {
        WorldCoordinates lastWc = path->get(path->size() - 1);
        Vector3 lastPt = lastWc.getPoint();
        agent->setHomeLocation(lastPt.getX(), lastPt.getZ(), lastPt.getY());
    }

    // 4. Force Speed and State
    float runSpeed = agent->getRunSpeed();
    info(true) << "Run Speed: " << agent->getRunSpeed();
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);

    agent->setMovementState(AiAgent::PATROLLING);
    
    // 5. Kick the AI loop hard
    agent->activateAiBehavior(true); 
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    Logger::console.info("FAILURE: Path was too simple (Straight Line) or blocked. Retrying might be needed.", true);
    
    // Optional: Auto-retry logic could go here (call goToResource again)
}