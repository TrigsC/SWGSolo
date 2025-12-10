/*
 * SimPlayerController.cpp
 * Verbose Debugging Version
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
    // 1. Verify controller still exists
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    // DEBUG LOG
    Logger::console.info("SimPlayerTask: Background task started. Calculating path...", true);

    // 2. Perform the heavy math
    // Note: Core3/Recast usually expects (X, Z, Y) or (X, Y, Z) depending on the utils.
    // We pass the WorldCoordinates straight through.
    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    int pathSize = (path != nullptr) ? path->size() : -1;
    Logger::console.info("SimPlayerTask: Calculation finished. Path size: " + String::valueOf(pathSize), true);

    // 3. Pass result back to Main Thread
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
    //setLoggingName("SimPlayerController"); // Helps identify logs
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) {
        Logger::console.info("ERROR: Agent is null in goToResource!", true);
        return;
    }

    // --- LOGIC FIX 1: STOP THE DEFAULT AI ---
    // If we don't do this, the default AI will leash back to its spawn point.
    // We set the "Home" to where we are currently standing to reset the tether.
    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    // DEBUG: Print current position to understand the Axis
    // SWG Standard: X=East/West, Z=North/South, Y=Elevation (Up)
    Vector3 currentPos = agent->getWorldPosition();
    Logger::console.info("DEBUG: Current Pos -> X:" + String::valueOf(currentPos.getX()) + 
         " Z(North):" + String::valueOf(currentPos.getZ()) + 
         " Y(Up):" + String::valueOf(currentPos.getY()), true);

    state = SEARCHING_RESOURCE;
    
    Zone* zone = agent->getZone();
    if (zone == nullptr) {
        Logger::console.info("ERROR: Agent is not in a valid Zone!", true);
        return;
    }

    // --- STEP 1: Search ---
    // Calculate Target (200m East)
    // Note: In SWG, X is East/West.
    float targetX = currentPos.getX() + 200; 
    float targetZ = currentPos.getZ();       // Stay on same North/South line
    
    // --- LOGIC FIX 2: SNAP TO TERRAIN ---
    // We ask the zone for the elevation at the new coordinates.
    // Core3 Zone::getHeight usually takes (x, y) where y is the North/South axis.
    float terrainHeight = zone->getHeight(targetX, targetZ); 

    Logger::console.info("DEBUG: Target Calculated -> X:" + String::valueOf(targetX) + " Z:" + String::valueOf(targetZ), true);
    Logger::console.info("DEBUG: Terrain Snap -> Old Y:" + String::valueOf(currentPos.getY()) + " New Y:" + String::valueOf(terrainHeight), true);

    // Construct the target vector
    // Note: Vector3 constructor is usually (X, Y, Z). 
    // BUT WorldCoordinates might expect (X, Z, Y). 
    // Let's rely on standard Core3 Vector3 (X, Y, Z).
    Vector3 targetPos(targetX, terrainHeight, targetZ);

    // --- STEP 2: Plan (Async) ---
    state = CALCULATING_PATH;
    
    // Construct WorldCoordinates
    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(targetPos, nullptr); // nullptr cell = terrain

    Logger::console.info("DEBUG: Launching Async Path Task...", true);

    Reference<FindResourcePathTask*> task = new FindResourcePathTask(this, startCoord, endCoord, zone);
    task->execute(); 
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr) {
        if (path) delete path;
        return;
    }
    
    if (path == nullptr || path->size() == 0) {
        Logger::console.info("ERROR: Path returned EMPTY. Destination is unreachable or inside an obstacle.", true);
        if (path) delete path;
        onPathFailed();
        return;
    }

    state = MOVING;
    Logger::console.info("SUCCESS: Path found with " + String::valueOf(path->size()) + " waypoints. Processing...", true);

    // Log the first point to sanity check
    if (path->size() > 0) {
        Vector3 firstPt = path->get(0).getPoint();
        Logger::console.info("DEBUG: First Waypoint -> X:" + String::valueOf(firstPt.getX()) + " Y:" + String::valueOf(firstPt.getY()) + " Z:" + String::valueOf(firstPt.getZ()), true);
    }

    // Clear existing patrol points
    agent->clearPatrolPoints();

    // Convert WorldCoordinates to PatrolPoints
    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wc = path->get(i);
        Vector3 point = wc.getPoint();
        
        PatrolPoint pp;
        // CRITICAL: PatrolPoint::setPosition takes (X, Z, Y) 
        // This is the most common place for bugs. 
        // We map Vector3(x, y, z) -> PatrolPoint(x, z, y)
        pp.setPosition(point.getX(), point.getZ(), point.getY());
        
        agent->addPatrolPoint(pp);
    }

    // Force the state
    Logger::console.info("DEBUG: Setting Movement State to PATROLLING and Activating AI.", true);
    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); // Force immediate update
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    Logger::console.info("FAILURE: Could not find path to resource.", true);
}