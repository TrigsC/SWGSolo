/*
 * SimPlayerController.cpp
 * Phase 10: DEBUG MODE (Tracing the "Flying" Bug)
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

    Vector<WorldCoordinates>* path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);

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
    stuckWatchdogCount = 0;
    simPathIndex = 0;
    setLoggingName("SimPlayerController");
}

SimPlayerController::~SimPlayerController() {
    agent = nullptr;
}

// --------------------------------------------------------
// LOGIC
// --------------------------------------------------------
void SimPlayerController::startSimLoop() {
    state = DECIDING;
    String res = pickRandomResource();
    targetResource = res; 
    Logger::console.info("SimPlayer: Loop -> I want [" + res + "]", true);
    performSurvey();
}

String SimPlayerController::pickRandomResource() {
    int roll = System::random(4);
    if (roll == 0) return "iron";
    if (roll == 1) return "gas";
    if (roll == 2) return "water";
    return "copper";
}

void SimPlayerController::performSurvey() {
    if (agent == nullptr) return;
    state = SURVEYING;

    agent->setMovementState(AiAgent::OBLIVIOUS);
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    agent->doAnimation("manipulate_high"); 

    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY);
    task->schedule(4000); 
}

void SimPlayerController::finishSurvey() {
    goToResource(targetResource);
}

String SimPlayerController::findActualResourceSpawn(const String& genericType) {
    ZoneServer* zoneServer = ServerCore::getZoneServer();
    if (zoneServer && zoneServer->getResourceManager()) {
        return genericType + "_spawn"; 
    }
    return genericType;
}

void SimPlayerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();

    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    int distance = 80 + System::random(120); 
    int angle = System::random(360);
    float rads = angle * (M_PI / 180.0f);
    
    // Calculate 2D spot
    float targetX = currentPos.getX() + (distance * cos(rads));
    float targetY = currentPos.getY() + (distance * sin(rads));
    
    // Get Elevation (Z)
    float targetZ = zone->getHeight(targetX, targetY); 
    
    // DEBUG: Check if we are getting weird elevation
    Logger::console.info("SimPlayer: Planning Target -> X:" + String::valueOf(targetX) + " Y(North):" + String::valueOf(targetY) + " Z(Elev):" + String::valueOf(targetZ), true);

    // Lift slightly
    targetZ += 1.0f;

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
    if (agent == nullptr) { if (path) delete path; return; }
    if (path == nullptr || path->size() == 0) { if (path) delete path; onPathFailed(); return; }

    retryCount = 0;
    state = MOVING;

    // Save path
    simPath = *path; 
    simPathIndex = 0;
    
    if (simPath.size() > 0) {
        Vector3 endPt = simPath.get(simPath.size() - 1).getPoint();
        destination = endPt;
    }

    Logger::console.info("SimPlayer: Path Loaded (" + String::valueOf(simPath.size()) + " nodes). Taking control.", true);

    // Reset
    agent->setFollowObject(nullptr);
    agent->setWatchObject(nullptr);
    agent->setTargetObject(nullptr);
    agent->clearCombatState(true);
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->clearPatrolPoints(); 

    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    
    float runSpeed = agent->getRunSpeed();
    if (runSpeed < 5.0f) runSpeed = 6.0f;
    agent->setRunSpeed(runSpeed);

    // KICKSTART
    if (simPath.size() > 0) {
        Vector3 firstPt = simPath.get(0).getPoint();
        
        // DEBUG: Verify Coordinate Mapping
        // If Y is -4000, and we put it in Slot 2 (Z), the bot flies/digs.
        // Core3 Vector3: X, Y(North), Z(Elev)
        // setNextStepPosition(X, Z, Y) -> (X, Elev, North)
        Logger::console.info("SimPlayer: Step 0 -> X:" + String::valueOf(firstPt.getX()) + " Z(Elev):" + String::valueOf(firstPt.getZ()) + " Y(North):" + String::valueOf(firstPt.getY()), true);
        
        agent->setNextStepPosition(firstPt.getX(), firstPt.getZ(), firstPt.getY(), nullptr);
        
        PatrolPoint pp; 
        pp.setPosition(firstPt.getX(), firstPt.getZ(), firstPt.getY());
        agent->broadcastNextPositionUpdate(&pp);
    }

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); 
    
    delete path;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(250);
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    if (retryCount < 10) {
        retryCount++;
        goToResource(targetResource);
    } else {
        startSimLoop(); 
    }
}

// --------------------------------------------------------
// PUPPET DRIVER (The Brain)
// --------------------------------------------------------
void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->isDead() || agent->getZone() == nullptr) return;
    if (state != MOVING) return;

    Vector3 currentPos = agent->getWorldPosition();
    
    // Check Final Dest
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY();
    float distSq = (dx*dx) + (dy*dy);

    if (distSq < 16.0f) { 
        Logger::console.info("SimPlayer: ARRIVED at final target.", true);
        performSample();
        return;
    } 

    // Safety check
    if (simPath.size() == 0 || simPathIndex >= simPath.size()) {
        Logger::console.info("SimPlayer: Path exhausted but not at target?", true);
        performSample();
        return;
    }

    // Check Waypoint
    Vector3 targetPt = simPath.get(simPathIndex).getPoint();
    
    float wx = targetPt.getX() - currentPos.getX();
    float wy = targetPt.getY() - currentPos.getY(); // Y is North
    float waypointDistSq = (wx*wx) + (wy*wy);

    // DEBUG: Monitor loop variables every 1 second (approx)
    if (System::random(4) == 0) { // Don't spam every 250ms
        // Logger::console.info("SimPlayer: Index " + String::valueOf(simPathIndex) + " Dist: " + String::valueOf(sqrt(waypointDistSq)) + "m", true);
    }

    if (waypointDistSq < 16.0f) {
        // CLOSE ENOUGH -> NEXT POINT
        simPathIndex++;
        
        if (simPathIndex >= simPath.size()) {
            performSample();
            return;
        } else {
            Vector3 nextPt = simPath.get(simPathIndex).getPoint();
            
            // LOGGING THE CRITICAL UPDATE
            // If the bot flies here, check Z(Elev) vs Y(North)
            Logger::console.info("SimPlayer: Advance -> Index " + String::valueOf(simPathIndex) + 
                " | Target: " + String::valueOf(nextPt.getX()) + ", " + String::valueOf(nextPt.getY()) + ", " + String::valueOf(nextPt.getZ()), true);

            // FORCE UPDATE
            agent->setNextStepPosition(nextPt.getX(), nextPt.getZ(), nextPt.getY(), nullptr);
            
            PatrolPoint pp;
            pp.setPosition(nextPt.getX(), nextPt.getZ(), nextPt.getY());
            agent->broadcastNextPositionUpdate(&pp);
            
            agent->activateAiBehavior(true);
            stuckWatchdogCount = 0;
        }
    } else {
        // STUCK CHECK
        float moveDx = currentPos.getX() - lastWatchdogPos.getX();
        float moveDy = currentPos.getY() - lastWatchdogPos.getY();
        float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

        if (movedDistSq < 0.01f) {
            stuckWatchdogCount++;
            if (stuckWatchdogCount > 8) { // 2 seconds
                
                Logger::console.info("SimPlayer: STUCK. Nudging...", true);
                
                float dist = sqrt(waypointDistSq);
                if (dist > 0) {
                    float dirX = wx / dist;
                    float dirY = wy / dist;
                    
                    float nudgeX = currentPos.getX() + (dirX * 0.5f);
                    float nudgeY = currentPos.getY() + (dirY * 0.5f);
                    float nudgeZ = currentPos.getZ() + 0.2f;
                    
                    // DEBUG TELEPORT
                    Logger::console.info("SimPlayer: Teleporting to -> " + String::valueOf(nudgeX) + ", " + String::valueOf(nudgeY) + ", " + String::valueOf(nudgeZ), true);
                    
                    agent->teleport(nudgeX, nudgeZ, nudgeY, 0);
                }
                
                // Re-assert command
                agent->setNextStepPosition(targetPt.getX(), targetPt.getZ(), targetPt.getY(), nullptr);
                agent->activateAiBehavior(true);
                stuckWatchdogCount = 0;
            }
        } else {
            stuckWatchdogCount = 0;
        }
    }

    lastWatchdogPos = currentPos;

    Reference<ArrivalCheckTask*> task = new ArrivalCheckTask(this);
    task->schedule(250);
}

void SimPlayerController::performSample() {
    state = SAMPLING;
    Logger::console.info("SimPlayer: State -> SAMPLING (15s)", true);

    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task = new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE);
    task->schedule(15000);
}

void SimPlayerController::finishSample() {
    Logger::console.info("SimPlayer: Done sampling.", true);
    agent->setPosture(CreaturePosture::UPRIGHT, true);
    agent->doAnimation("stop_sample"); 
    startSimLoop();
}

Vector3 SimPlayerController::findNearestHighDensityResource(const String& resourceClass) {
    return Vector3(0,0,0);
}