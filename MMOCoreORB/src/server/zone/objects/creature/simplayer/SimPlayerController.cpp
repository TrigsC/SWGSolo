/*
 * SimPlayerController.cpp
 */

#include "SimPlayerController.h"
#include "engine/core/Core.h"       
#include "engine/core/TaskManager.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h"

// --------------------------------------------------------
// Task Implementation
// --------------------------------------------------------
void FindResourcePathTask::run() {
    // 1. Verify controller still exists (Bot might have despawned)
    ManagedReference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    // 2. Perform the heavy math (Blocking call is safe here on background thread)
    // Note: findPath returns a pointer to a new Vector, we own it now.
    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    // 3. Pass result back to the Main Thread
    // We create a lambda or simple closure to execute on the main thread safely
    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        if (path != nullptr && path->size() > 0) {
            strongCtrl->onPathFound(path);
        } else {
            strongCtrl->onPathFailed();
            if (path) delete path; // Clean up empty vector
        }
    }, "SimPlayerResultLambda");
}

// --------------------------------------------------------
// Controller Implementation
// --------------------------------------------------------

SimPlayerController::SimPlayerController(AiAgent* aiAgent) {
    agent = aiAgent;
    state = IDLE;
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    state = SEARCHING_RESOURCE;
    
    // --- STEP 1: Search (Mocked for Prototype) ---
    // Real implementation would query PlanetManager here.
    // For now, let's pick a spot 200m away in X direction for testing.
    Vector3 currentPos = agent->getWorldPosition();
    Vector3 targetPos = currentPos;
    targetPos.setX(currentPos.getX() + 200); // Simple 200m move

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    // --- STEP 2: Plan (Async) ---
    state = CALCULATING_PATH;
    
    WorldCoordinates startCoord = agent->getWorldCoordinates();
    WorldCoordinates endCoord;
    endCoord.setPoint(targetPos);
    endCoord.setZone(zone);

    // Launch the background task
    Reference<FindResourcePathTask*> task = new FindResourcePathTask(this, startCoord, endCoord, zone);
    task->execute(); // Pushes to the thread pool
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr || path == nullptr) {
        if (path) delete path;
        return;
    }

    state = MOVING;

    // Clear existing patrol points
    agent->clearPatrolPoints();

    // --- STEP 3: Travel ---
    // Convert WorldCoordinates to PatrolPoints
    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wc = path->get(i);
        Vector3 point = wc.getPoint();
        
        // Create a PatrolPoint (Core3 specific object)
        PatrolPoint pp;
        pp.setPosition(point.getX(), point.getZ(), point.getY()); // Ensure Y/Z mapping is correct for your engine version (usually X, Z, Y in SwgEmu)
        
        agent->addPatrolPoint(pp);
    }

    // Tell the agent to start "Patrolling" (Moving along the points)
    agent->setMovementState(AiAgent::PATROLLING);
    
    // Clean up the vector memory allocated by PathFinderManager
    delete path;
    
    Logger::console.info("SimPlayer: Path found and execution started!");
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    Logger::console.info("SimPlayer: Could not find path to resource.");
}