/*
 * SimPlayerController.cpp
 * Phase 2 Fixed: Movement Kickstart Enabled
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

// --------------------------------------------------------
// Task Implementations
// --------------------------------------------------------

void FindResourcePathTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    Logger::console.info("SimPlayerTask: Requesting path from Recast engine...", true);
    
    uint64 startTime = System::getMiliTime();
    
    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

    uint64 endTime = System::getMiliTime();
    Logger::console.info("SimPlayerTask: Recast finished in " + String::valueOf(endTime - startTime) + "ms.", true);

    Core::getTaskManager()->executeTask([strongCtrl, path] () {
        if (path != nullptr) { 
            strongCtrl->onPathFound(path);
        } else {
            strongCtrl->onPathFailed();
        }
    }, "SimPlayerResultLambda");
}

void ArrivalCheckTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;
    
    Core::getTaskManager()->executeTask([strongCtrl] () {
        strongCtrl->checkArrival();
    }, "SimPlayerArrivalLambda");
}

void SimBehaviorTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    int capturedType = type;
    Core::getTaskManager()->executeTask([strongCtrl, capturedType] () {
        if (capturedType == SimBehaviorTask::FINISH_SURVEY) 
            strongCtrl->finishSurvey();
        else if (capturedType == SimBehaviorTask::FINISH_SAMPLE) 
            strongCtrl->finishSample();
    }, "SimPlayerBehaviorLambda");
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

// --------------------------------------------------------
// STEP 1: START & DECIDE
// --------------------------------------------------------
void SimPlayerController::startSimLoop() {
    state = DECIDING;
    String res = pickRandomResource();
    targetResource = res; 
    
    Logger::console.info("================================================", true);
    Logger::console.info("SimPlayer: NEW LOOP STARTED", true);
    Logger::console.info("SimPlayer: Desire determined -> I want [" + res + "]", true);
    
    performSurvey();
}

String SimPlayerController::pickRandomResource() {
    int roll = System::random(4);
    if (roll == 0) return "iron";
    if (roll == 1) return "gas";
    if (roll == 2) return "water";
    if (roll == 3) return "copper";
    return "steel";
}

// --------------------------------------------------------
// STEP 2: SURVEY VISUALS
// --------------------------------------------------------
void SimPlayerController::performSurvey() {
    if (agent == nullptr) return;
    state = SURVEYING;

    Logger::console.info("SimPlayer: State -> SURVEYING (4s delay)", true);

    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->doAnimation("manipulate_high"); 

    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY);
    task->schedule(4000); 
}

void SimPlayerController::finishSurvey() {
    Logger::console.info("SimPlayer: Survey complete. Selected location.", true);
    goToResource(targetResource);
}

// --------------------------------------------------------
// STEP 3: MOVEMENT LOGIC
// --------------------------------------------------------
String SimPlayerController::findActualResourceSpawn(const String& genericType) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer && zoneServer->getResourceManager()) {
        return genericType + "_spawn_found"; 
    }
    return genericType;
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();

    // Random Scout Logic
    int distance = 100 + System::random(200); 
    int angle = System::random(360);
    
    float rads = angle * (M_PI / 180.0f);
    float offsetX = distance * cos(rads);
    float offsetY = distance * sin(rads);

    float targetX = currentPos.getX() + offsetX;
    float targetY = currentPos.getY() + offsetY; 
    float targetZ = zone->getHeight(targetX, targetY) + 1.0f; 

    if (retryCount == 0) {
        Logger::console.info("SimPlayer: Scouting Math -> Dist: " + String::valueOf(distance) + "m / Angle: " + String::valueOf(angle), true);
        Logger::console.info("SimPlayer: Destination -> " + String::valueOf(targetX) + ", " + String::valueOf(targetY), true);
    }

    destination.setX(targetX);
    destination.setY(targetY);
    destination.setZ(targetZ);

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
        if (path) delete path;
        onPathFailed();
        return;
    }

    if (path->size() == 2) {
        Logger::console.info("SimPlayer: Wilderness blind walk detected (Path Size 2).", true);
    } else {
        Logger::console.info("SimPlayer: NavMesh path found (Path Size " + String::valueOf(path->size()) + ").", true);
    }

    retryCount = 0;
    state = MOVING;

    // Update dest to match path end
    if (path->size() > 0) {
        Vector3 endPt = path->get(path->size() - 1).getPoint();
        destination = endPt;
    }

    // AI Reset
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->clearPatrolPoints();

    for (int i = 0; i < path->size(); ++i) {
        WorldCoordinates wc = path->get(i);
        Vector3 point = wc.getPoint();
        PatrolPoint pp;
        pp.setPosition(point.getX(), point.getZ(), point.getY());
        agent->addPatrolPoint(pp);
    }

    if (path->size() > 0) {
        WorldCoordinates lastWc = path->get(path->size() - 1);
        Vector3 lastPt = lastWc.getPoint();
        agent->setHomeLocation(lastPt.getX(), lastPt.getZ(), lastPt.getY());
    }

    float runSpeed = agent->getRunSpeed();
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);

    // --- KICKSTART LOGIC ---
    if (path->size() > 0) {
        // 1. Manually set the "Next Step" to the first point
        PatrolPoint firstPP = agent->getNextPosition(); 
        
        // 2. FORCE UPDATE the internal movement system
        agent->setNextStepPosition(firstPP.getPositionX(), firstPP.getPositionZ(), firstPP.getPositionY(), firstPP.getCell());
        
        // 3. BROADCAST to clients (This is the visual "Go" signal)
        agent->broadcastNextPositionUpdate(&firstPP);
    }

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); 
    
    delete path;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(1000);
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    if (retryCount < 10) {
        retryCount++;
        Logger::console.info("SimPlayer: Path failed. Retrying (" + String::valueOf(retryCount) + "/10)...", true);
        goToResource(targetResource);
    } else {
        Logger::console.info("SimPlayer: FATAL - Stuck after 10 tries. Resetting Loop.", true);
        startSimLoop(); 
    }
}

// --------------------------------------------------------
// STEP 4: ARRIVAL CHECK
// --------------------------------------------------------
void SimPlayerController::checkArrival() {
    if (agent == nullptr || state != MOVING) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY();
    float distSq = (dx*dx) + (dy*dy);
    float dist = sqrt(distSq);

    Logger::console.info("SimPlayer: Distance to target -> " + String::valueOf(dist) + "m", true);

    if (distSq < 25.0f) { // 5 meters
        Logger::console.info("SimPlayer: ARRIVED at target.", true);
        performSample();
    } else {
        // Keep checking
        Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
        task->schedule(1000);
    }
}

// --------------------------------------------------------
// STEP 5: SAMPLE VISUALS
// --------------------------------------------------------
void SimPlayerController::performSample() {
    state = SAMPLING;
    Logger::console.info("SimPlayer: State -> SAMPLING (15s delay)", true);

    agent->clearPatrolPoints();
    agent->setMovementState(AiAgent::OBLIVIOUS);
    
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(15000);
}

void SimPlayerController::finishSample() {
    Logger::console.info("SimPlayer: Sampling finished.", true);
    agent->doAnimation("stop_sample"); 
    
    startSimLoop();
}

Vector3 SimPlayerController::findNearestHighDensityResource(const String& resourceClass) {
    return Vector3(0,0,0);
}