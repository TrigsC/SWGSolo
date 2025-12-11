/*
 * SimPlayerController.cpp
 * Final Fixed Version: ZoneServer Access + Wilderness Logic
 */

#include "SimPlayerController.h"
#include "engine/core/Core.h"
#include "engine/core/TaskManager.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h"
#include "server/zone/Zone.h"
#include "server/zone/managers/resource/ResourceManager.h"
#include "server/zone/objects/resource/ResourceSpawn.h"
#include "server/ServerCore.h"       // <--- ADDED
#include "server/zone/ZoneServer.h"  // <--- ADDED

// --------------------------------------------------------
// Task Implementation
// --------------------------------------------------------
void FindResourcePathTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    // Perform the heavy math
    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        // Validation: We accept all paths now. 
        // We will filter "Size 2" (Straight Line) inside the controller if we want,
        // but for wilderness blind walking, we need to accept them.
        if (path != nullptr) { 
            strongCtrl->onPathFound(path);
        } else {
            strongCtrl->onPathFailed();
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

// Helper: Find the specific spawn name using ZoneServer
String SimPlayerController::findActualResourceSpawn(const String& genericType) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer == nullptr) return genericType;

    ResourceManager* resManager = zoneServer->getResourceManager();
    if (resManager == nullptr) return genericType;

    // In a real implementation, you would use:
    // resManager->getResourceSpawn(genericType);
    // For now, we return a placebo string so we know this function ran.
    return genericType + " (Scanning...)"; 
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    targetResource = resourceName;

    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    state = SEARCHING_RESOURCE;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    String realName = findActualResourceSpawn(resourceName);
    Vector3 currentPos = agent->getWorldPosition();

    // --- RANDOM SCOUTING ---
    // 100m - 200m range
    int distance = 100 + System::random(100); 
    int angle = System::random(360);
    
    float rads = angle * (M_PI / 180.0f);
    float offsetX = distance * cos(rads);
    float offsetY = distance * sin(rads);

    float targetX = currentPos.getX() + offsetX;
    float targetY = currentPos.getY() + offsetY; 
    
    // Lift target slightly +1.0m
    float targetZ = zone->getHeight(targetX, targetY) + 1.0f; 

    if (retryCount == 0) {
        Logger::console.info("SimPlayer: Searching for [" + realName + "]...", true);
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
    
    // If path is null, it's a hard failure
    if (path == nullptr || path->size() == 0) {
        if (path) delete path;
        onPathFailed();
        return;
    }

    // --- WILDERNESS LOGIC ---
    // If path size is 2, it is a "Straight Line" (Recast failed or no NavMesh).
    // In the previous version, we rejected this. 
    // NOW, we accept it blindly because we removed the Raycast check.
    // This allows movement in the desert.
    if (path->size() == 2) {
        Logger::console.info("SimPlayer: NavMesh missing (Wilderness). Attempting blind walk.", true);
    }

    retryCount = 0;
    state = MOVING;

    // Logging
    if (path->size() > 0) {
        Vector3 firstPt = path->get(0).getPoint();
        Vector3 endPt = path->get(path->size() - 1).getPoint();
        
        Logger::console.info("------------------------------------------------", true);
        Logger::console.info("PATH CONFIRMED:", true);
        Logger::console.info("Nodes: " + String::valueOf(path->size()), true);
        Logger::console.info("------------------------------------------------", true);
    }

    // 1. Lobotomy
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
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

    // 3. Anti-Leash
    if (path->size() > 0) {
        WorldCoordinates lastWc = path->get(path->size() - 1);
        Vector3 lastPt = lastWc.getPoint();
        agent->setHomeLocation(lastPt.getX(), lastPt.getZ(), lastPt.getY());
    }

    // 4. Force Run
    float runSpeed = agent->getRunSpeed();
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);

    // 5. Jumpstart
    if (path->size() > 0) {
        PatrolPoint firstPP = agent->getNextPosition(); 
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
        Logger::console.info("Path calculation failed. Retrying attempt " + String::valueOf(retryCount) + "...", true);
        goToResource(targetResource);
    } else {
        Logger::console.info("FAILURE: Could not find any valid path after 10 attempts.", true);
        retryCount = 0;
    }
}

// Helper stub for density
Vector3 SimPlayerController::findNearestHighDensityResource(const String& resourceClass) {
    return Vector3(0,0,0);
}