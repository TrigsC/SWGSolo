/*
 * SimPlayerController.cpp
 * DEBUGGING ONLY: Added logs to track path generation quality
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
            // Cast removed for safety in this debug block, assuming logic handles it
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
    
    // LOG 1: Request
    Logger::console.info("SimPlayer [" + String::valueOf(agent->getObjectID()) + "]: Requesting move to " + targetPos.toString(), true);

    state = CALCULATING_PATH;
    
    WorldCoordinates startCoord(agent->getWorldPosition(), agent->getParentID());
    WorldCoordinates endCoord(targetPos, 0); 

    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord, endCoord, zone);
    Core::getTaskManager()->executeTask(task);
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path) {
    if (agent == nullptr || path == nullptr) return;
    
    Locker lock(agent);
    
    // LOG 2: Result
    int steps = path->size();
    Logger::console.info("SimPlayer [" + String::valueOf(agent->getObjectID()) + "]: Path FOUND. Steps: " + String::valueOf(steps), true);

    if (steps == 0) {
        delete path;
        onPathFailed();
        return;
    }

    state = MOVING;
    agent->clearPatrolPoints();
    
    for (int i = 0; i < path->size(); ++i) {
        PatrolPoint point(path->get(i));
        agent->addPatrolPoint(point);
    }
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    if (agent == nullptr) return;
    Logger::console.error("SimPlayer [" + String::valueOf(agent->getObjectID()) + "]: Path FAILED.");
    state = IDLE; 
}

void SimPlayerController::checkArrival() {
}

// --------------------------------------------------------
// MINER IMPLEMENTATION 
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
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->doAnimation("survey_start"); 

    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY);
    task->schedule(5000); 
}

void SimMinerController::finishSurvey() {
    if (agent == nullptr) return;
    Locker locker(agent);
    goToResource("iron");
}

void SimMinerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Locker locker(agent);
    Vector3 currentPos = agent->getWorldPosition();
    Vector3 targetPos;

    float angle = System::random(360) * (M_PI / 180.0f);
    float dist = 100.0f;
    
    float newX = currentPos.getX() + (dist * cos(angle));
    float newZ = currentPos.getY() + (dist * sin(angle)); 
    
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
    agent->clearPatrolPoints(); 
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(15000); 
}

void SimMinerController::finishSample() {
    performSurvey();
}