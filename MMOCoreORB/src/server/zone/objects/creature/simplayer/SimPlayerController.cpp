/*
 * SimPlayerController.cpp
 * DEBUG VERSION: Added Pathfinding Logging
 */

#include "SimPlayerController.h"
#include "engine/core/Core.h"
#include "engine/core/TaskManager.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h"
#include "server/zone/Zone.h"
#include "server/zone/managers/resource/ResourceManager.h"
#include "server/zone/objects/resource/ResourceSpawn.h"
#include "server/ServerCore.h"
#include "server/zone/ZoneServer.h"
#include "system/lang/System.h" 
#include "server/zone/objects/creature/ai/bt/BlackboardData.h"
#include "templates/params/creature/CreaturePosture.h"
#include "system/thread/Locker.h"

using namespace server::zone::objects::creature::ai::bt;

// --------------------------------------------------------
// TASKS
// --------------------------------------------------------
void SimPathFindTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    Vector<WorldCoordinates>* path = nullptr;
    try {
        Logger::console.info("SimPathFindTask: Requesting path...", true);
        path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);
    } catch (...) {
        path = nullptr;
    }

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        if (path != nullptr)
            strongCtrl->onPathFound(path);
        else
            strongCtrl->onPathFailed();
    }, "SimPathCallback");
}

void ArrivalCheckTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl != nullptr) strongCtrl->checkArrival();
}

void SimBehaviorTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    Core::getTaskManager()->executeTask([strongCtrl, this] () {
        if (type == FINISH_SURVEY) {
            if (strongCtrl->isMiner()) ((SimMinerController*)strongCtrl.get())->finishSurvey();
        } else if (type == FINISH_SAMPLE) {
            if (strongCtrl->isMiner()) ((SimMinerController*)strongCtrl.get())->finishSample();
        }
    }, "SimBehaviorLambda");
}

// --------------------------------------------------------
// BASE CONTROLLER
// --------------------------------------------------------
SimPlayerController::SimPlayerController(AiAgent* aiAgent) : state(IDLE) {
    agent = aiAgent;
}

SimPlayerController::~SimPlayerController() {
}

void SimPlayerController::moveTo(Vector3 targetPos) {
    if (agent == nullptr) return;

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Locker lock(agent);
    
    // DEBUG LOG: Requesting Move
    Logger::console.info("SimPlayer: Requesting move to " + targetPos.toString(), true);

    state = CALCULATING_PATH;
    
    WorldCoordinates startCoord(agent->getWorldPosition(), agent->getParentID());
    WorldCoordinates endCoord(targetPos, 0); // Assuming world travel for now

    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord, endCoord, zone);
    Core::getTaskManager()->executeTask(task);
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr || path == nullptr) return;
    
    Locker lock(agent);
    
    // DEBUG LOG: Path Result
    int steps = path->size();
    Logger::console.info("SimPlayer: Path found! Steps: " + String::valueOf(steps), true);

    if (steps == 0) {
        delete path;
        onPathFailed();
        return;
    }

    state = MOVING;
    agent->clearPatrolPoints();
    
    // Add points to agent
    for (int i = 0; i < path->size(); ++i) {
        PatrolPoint point(path->get(i));
        agent->addPatrolPoint(point);
    }
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    if (agent == nullptr) return;
    Logger::console.error("SimPlayer: Path generation FAILED or Empty.");
    state = IDLE; // Reset state so logic knows we failed
}

void SimPlayerController::checkArrival() {
    // Basic check, usually overridden
}

// --------------------------------------------------------
// MINER IMPLEMENTATION (Preserved)
// --------------------------------------------------------
SimMinerController::SimMinerController(AiAgent* aiAgent) : SimPlayerController(aiAgent) {
    targetResource = "";
    retryCount = 0;
}

SimMinerController::~SimMinerController() {}

void SimMinerController::startSimLoop() {
    performSurvey();
}

void SimMinerController::performSurvey() {
    if (agent == nullptr) return;
    Locker locker(agent);
    
    state = SURVEYING;
    Logger::console.info("SimMiner: State -> SURVEYING (5s)", true);
    
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->doAnimation("survey_start"); 

    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY);
    task->schedule(5000); 
}

void SimMinerController::finishSurvey() {
    if (agent == nullptr) return;
    Locker locker(agent);
    
    targetResource = pickRandomResource();
    Logger::console.info("SimMiner: Survey complete. Target found: " + targetResource, true);
    
    goToResource(targetResource);
}

String SimMinerController::pickRandomResource() {
    return "iron"; // Placeholder
}

void SimMinerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Locker locker(agent);
    Vector3 currentPos = agent->getWorldPosition();
    
    Vector3 targetPos;

    // Just pick a random spot 100m away for simulation
    float angle = System::random(360) * (M_PI / 180.0f);
    float dist = 100.0f;
    
    float newX = currentPos.getX() + (dist * cos(angle));
    float newZ = currentPos.getY() + (dist * sin(angle)); // North is Y
    
    targetPos.setX(newX);
    targetPos.setY(newZ); 
    targetPos.setZ(zone->getHeight(newX, newZ)); 

    moveTo(targetPos);
}

void SimMinerController::onArrived() {
    performSample();
}

void SimMinerController::performSample() {
    Locker locker(agent);
    state = SAMPLING;
    // Logger::console.info("SimMiner: State -> SAMPLING (15s)", true);

    agent->clearPatrolPoints(); 
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(15000); 
}

void SimMinerController::finishSample() {
    // Loop back to survey
    performSurvey();
}