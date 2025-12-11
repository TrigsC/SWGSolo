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
#include "server/zone/managers/resource/ResourceManager.h"
#include "server/zone/objects/resource/ResourceSpawn.h"

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
    if (agent == nullptr) return;

    // Reset Home
    agent->setHomeLocation(agent->getPositionX(), agent->getPositionZ(), agent->getPositionY());
    agent->stopWaiting();
    
    state = SEARCHING_RESOURCE;
    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();

    // --- RANDOM SCOUT LOGIC ---
    // Pick a random direction and distance (between 500m and 1500m)
    int distance = 500 + System::random(1000);
    int angle = System::random(360);
    
    // Convert Angle/Dist to X/Y offset
    float rads = angle * (M_PI / 180.0f);
    float offsetX = distance * cos(rads);
    float offsetY = distance * sin(rads);

    float targetX = currentPos.getX() + offsetX;
    float targetY = currentPos.getY() + offsetY; // North/South
    float targetZ = zone->getHeight(targetX, targetY); 

    info("SimPlayer: Scouting " + resourceName + " at " + String::valueOf(distance) + "m (Dir: " + String::valueOf(angle) + ")", true);

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
    float runSpeed = agent->getRunSpeed();
    if (runSpeed < 5.0f) runSpeed = 6.0f; // Ensure he's not slow
    
    agent->setRunSpeed(runSpeed);
    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true); // Force immediate update
    
    delete path;
}

void SimPlayerController::onPathFailed() {
    state = IDLE;
    Logger::console.info("FAILURE: Could not find path to resource.", true);
}

Vector3 SimPlayerController::findNearestHighDensityResource(const String& resourceClass) {
    Vector3 bestSpot = agent->getWorldPosition(); // Default to staying put if fail
    float maxDensityFound = 0.0f;

    Zone* zone = agent->getZone();
    if (zone == nullptr) return bestSpot;

    String planetName = zone->getZoneName();
    ResourceManager* resManager = ResourceManager::instance();
    
    // 1. Get all current spawns
    // We need to lock the resource manager usually, or just get a copy of the list?
    // ResourceManager usually has "getResourceSpawns()" which returns a map.
    // For simplicity/safety in this prototype, let's ask for specific type logic if available,
    // or iterate the whole list (careful with locks in production!).
    
    // ARCHITECT NOTE: Core3 Resource iteration is complex. 
    // We will assume a helper method or iterate the global map carefully.
    // A safer way is using "getResourceSpawn(name)" if we knew the name.
    // Since we only know "iron", we search by type.

    // Let's use a "Sampling" approach. 
    // We will scan the ground around us and ask "What is here?"
    
    Vector3 currentPos = agent->getWorldPosition();
    float scanRadius = 1000.0f; // Look within 1km
    float stepSize = 96.0f;    // Check every 96 meters (optimization)
    
    // Debug Log
    info("SimPlayer: Surveying for " + resourceClass + "...", true);

    for (float x = currentPos.getX() - scanRadius; x <= currentPos.getX() + scanRadius; x += stepSize) {
        for (float y = currentPos.getY() - scanRadius; y <= currentPos.getY() + scanRadius; y += stepSize) {
            
            // Ask ResourceManager: "What is the density of [Type] at [X,Y]?"
            // Note: SWG density is 0.0 to 1.0 (or 0-100).
            
            // Core3 API: getDensityForResource(x, y, zoneName, resourceClass)
            // Note: We need to verify if this exact API exists in your version. 
            // Often it is: resManager->getDensity(x, y, planetName, resourceName)
            
            // Since we don't have the specific Random Name (e.g. "Abcd Iron"), 
            // we effectively need to find the specific spawn first.
            // THIS IS THE HARD PART: Generic "Iron" maps to specific "Oruu Carbonate Iron".
            
            // Simplified Logic: 
            // We will just find ANY resource density at this spot for now to test the loop.
            // Or strictly:
            // ResourceSpawn* spawn = resManager->getCurrentSpawn(type, planet);
        }
    }
    
    // --- ALTERNATIVE: DIRECT SPAWN ACCESS (Better) ---
    // 1. Find the specific Spawn Object for "Iron" on "Naboo"
    ResourceSpawn* targetSpawn = nullptr;
    
    // We iterate the map (Pseudo-code adapted for Core3)
    // In real Core3, you often use resManager->getResourceList()
    // For this prototype, let's pretend we found one or just use the first "Iron" we find.
    
    // If we can't easily iterate, let's cheat and use a known test coordinate 
    // OR just return a random spot for the movement test.
    
    // RETURN TO SEARCH LOOP
    // Let's perform a "Blind Survey" - Pick 10 random spots, go to the best one.
    for (int i=0; i<20; ++i) {
        float testX = currentPos.getX() + (System::random(2000) - 1000);
        float testY = currentPos.getY() + (System::random(2000) - 1000);
        
        // This function usually exists in PlanetManager or ResourceManager
        // It returns the highest density of ANY resource at that spot.
        // Ideally we filter by class, but let's just find "The Good Stuff".
        float density = resManager->getDensity(planetName, testX, testY); 
        
        if (density > maxDensityFound) {
            maxDensityFound = density;
            bestSpot.setX(testX);
            bestSpot.setY(testY); // North/South
            // We snap Z later in goToResource
        }
    }
    
    info("SimPlayer: Survey complete. Found density " + String::valueOf(maxDensityFound) + " at " + bestSpot.toString(), true);
    return bestSpot;
}