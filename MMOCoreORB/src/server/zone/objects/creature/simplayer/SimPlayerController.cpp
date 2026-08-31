/*
 * SimPlayerController.cpp
 * Debugging Startup Hang + Robust Retry
 */

#include "SimPlayerController.h"
#include "TravelDiagLog.h"
#include "SimPlayerManager.h"
#include "CellNavDiagLog.h"
#include "StructureTraversalDiagLog.h"
#include "engine/core/Core.h"
#include "engine/core/TaskManager.h"
#include "server/zone/managers/collision/PathFinderManager.h"
#include "server/zone/managers/collision/CollisionManager.h"
#include "server/zone/objects/creature/ai/PatrolPoint.h"
#include "server/zone/objects/building/BuildingObject.h"
#include "server/zone/objects/cell/CellObject.h"
#include "server/zone/objects/scene/SceneObject.h"
#include "server/zone/InRangeObjectsVector.h"
#include "server/zone/CloseObjectsVector.h"
#include "server/zone/managers/planet/PlanetTravelPoint.h"
#include "server/zone/managers/planet/PlanetManager.h"
#include "templates/collision/BaseBoundingVolume.h"
#include "templates/SharedObjectTemplate.h"
#include "templates/appearance/PortalLayout.h"
#include "templates/appearance/FloorMesh.h"
#include "templates/appearance/PathGraph.h"
#include "templates/appearance/PathNode.h"
#include "templates/appearance/CellProperty.h"
#include "server/zone/Zone.h"
#include "server/zone/managers/resource/ResourceManager.h"
#include "server/zone/objects/resource/ResourceSpawn.h"
#include "server/ServerCore.h"
#include "server/zone/ZoneServer.h"
#include "system/lang/System.h" 
#include "server/zone/objects/creature/ai/bt/BlackboardData.h"
#include "templates/params/creature/CreaturePosture.h" 

using namespace server::zone::objects::creature::ai::bt;

#include <cmath>
#include <cfloat>
#include <chrono>
#include <vector>

//#define DEBUG_SIMPVP

static bool isCellNavDiagAgent(AiAgent* agent) {
    return agent != nullptr &&
        SimPlayerManager::instance()->isCellNavDiagBot(agent->getObjectID());
}

static void logCellNavDiag(AiAgent* agent, const String& line) {
    if (isCellNavDiagAgent(agent))
        CellNavDiagLog::write(line);
}

static String getStructureTraversalPhaseName(
        StructureTraversalPhase phase) {
    switch (phase) {
    case StructureTraversalPhase::Idle:
        return "Idle";
    case StructureTraversalPhase::ApproachDoor:
        return "ApproachDoor";
    case StructureTraversalPhase::InteriorRoute:
        return "InteriorRoute";
    case StructureTraversalPhase::Egress:
        return "Egress";
    case StructureTraversalPhase::Reentry:
        return "Reentry";
    case StructureTraversalPhase::CombatPaused:
        return "CombatPaused";
    case StructureTraversalPhase::Resuming:
        return "Resuming";
    default:
        return "Unknown";
    }
}

static Vector3 transformFromStructureModelSpace(
        const Vector3& modelPoint, BuildingObject* building);

// Every SimPathFindTask binds its own probe inputs at construction. Reading
// them off the controller later would let a newer task's refresh re-label an
// older task's path, which would corrupt the Phase 1 evidence.
SimPathFindTask::SimPathFindTask(SimPlayerController* ctrl,
        WorldCoordinates start, WorldCoordinates end, Zone* z, uint64 g)
    : controller(ctrl), startCoord(start), endCoord(end), zone(z), generation(g),
      useRecastPath(false), useDirectOverlandPath(false),
      directTargetUsesTerrainHeight(false),
      probeRayHeight(ctrl == nullptr ? 1.f : ctrl->getProbeRayHeight()),
      probeAgentOid(ctrl == nullptr ? 0 : ctrl->getProbeAgentOid()),
      navArea(nullptr), recastStart(), recastEnd(), allowPartial(true) {
}

SimPathFindTask::SimPathFindTask(SimPlayerController* ctrl,
        WorldCoordinates start, WorldCoordinates end, Zone* z, NavArea* area,
        const Vector3& recastStartPosition, const Vector3& recastEndPosition,
        bool partial, uint64 g)
    : controller(ctrl), startCoord(start), endCoord(end), zone(z), generation(g),
      useRecastPath(true), useDirectOverlandPath(false),
      directTargetUsesTerrainHeight(false),
      probeRayHeight(ctrl == nullptr ? 1.f : ctrl->getProbeRayHeight()),
      probeAgentOid(ctrl == nullptr ? 0 : ctrl->getProbeAgentOid()),
      navArea(area), recastStart(recastStartPosition),
      recastEnd(recastEndPosition), allowPartial(partial) {
}

SimPathFindTask::SimPathFindTask(SimPlayerController* ctrl,
        WorldCoordinates start, WorldCoordinates end, Zone* z,
        bool directOverland, bool terrainHeight, float rayHeight,
        uint64 rayAgentOid, uint64 g)
    : controller(ctrl), startCoord(start), endCoord(end), zone(z), generation(g),
      useRecastPath(false), useDirectOverlandPath(directOverland),
      directTargetUsesTerrainHeight(terrainHeight),
      probeRayHeight(rayHeight > 0.f ? rayHeight :
          (ctrl == nullptr ? 1.f : ctrl->getProbeRayHeight())),
      probeAgentOid(rayAgentOid != 0 ? rayAgentOid :
          (ctrl == nullptr ? 0 : ctrl->getProbeAgentOid())),
      navArea(nullptr), recastStart(), recastEnd(), allowPartial(true) {
}

void SimPlayerController::refreshProbeRayHeight() {
    // Called on the ISSUING thread only. getRayOriginPoint reads creature
    // height and posture, which is agent state, so the value is snapshotted
    // here and the task worker later reads only the plain float.
    SimPlayerManager* manager = SimPlayerManager::instance();

    // Costs a lock, so it is only paid when the probe can actually use it.
    if (manager == nullptr || !manager->isStructureTraversalZeroClipEnabled())
        return;

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr)
        return;

    // engine3's Locker is re-entrancy-safe -- it no-ops when the calling thread
    // already holds the lock (doLock = !isLockedByCurrentThread()) -- so this is
    // safe from callers that already hold the agent lock and from those that do
    // not. That is what lets moveToWithOrigin refresh unconditionally below.
    Locker agentLocker(strongAgent);
    probeRayHeight = CollisionManager::getRayOriginPoint(strongAgent);
    probeAgentOid = strongAgent->getObjectID();
}

// Is the straight chord between two path nodes something the bot can actually
// walk, despite intersecting an object's mesh?
//
// The navmesh is the authority on walkability -- it is baked from what an agent
// can stand on, so a staircase or bridge deck IS the mesh there, while a wall is
// a hole in it. Stock CollisionManager::checkMovementCollision consults it the
// same way and for the same reason; the only thing that made D7's probe
// disagree with stock is chord LENGTH. Stock tests a per-tick micro-segment that
// hugs the surface, D7 tests a node-to-node chord tens of metres long that does
// not.
//
// Verdict: if recast can walk between the two endpoints without meaningfully
// detouring, the chord is walkable and the mesh intersection is a false
// positive. A real obstruction forces recast to go around, which shows up as a
// path materially longer than the chord.
//
// Runs on the pathfinding worker thread, which is where getRecastPath is
// normally called from, and only on a segment the cheap ray already flagged.
bool SimPlayerController::isSegmentWalkableByNavmesh(Zone* zone,
        const Vector3& rayStart, const Vector3& rayEnd, float segmentLength) {
    SimPlayerManager* manager = SimPlayerManager::instance();

    if (manager == nullptr ||
            !manager->isStructureTraversalZeroClipWalkableConfirmEnabled() ||
            zone == nullptr || segmentLength < 0.001f)
        return false;

    ManagedReference<NavArea*> startArea;
    ManagedReference<NavArea*> endArea;

    // Both ends inside ONE mesh, or recast has nothing to say about the chord.
    if (!findNavAreaAt(zone, rayStart, startArea) ||
            !findNavAreaAt(zone, rayEnd, endArea) ||
            startArea == nullptr || startArea != endArea)
        return false;

    Vector<WorldCoordinates> recastPath;
    // NOT a length, despite the name and signature. getRecastPath accumulates
    // x^2 + z^2 of each point's ABSOLUTE world coordinates into this — roughly
    // 1.2e7 per point out at x=3500 — so it is meaningless as a distance.
    // It is not unused, though: findPathFromWorldToWorld feeds it to a
    // `finalLengthSq` comparison to pick between candidate routes
    // (PathFinderManager.cpp:380). That is a relative comparator between
    // candidates, not a distance, and it cannot be read as one here — stock's
    // other caller, CollisionManager::checkMovementCollision, passes a value IN
    // and ignores what comes back. Measure the real length from the points.
    float unusedRecastLen = 0.f;

    if (!PathFinderManager::instance()->getRecastPath(rayStart, rayEnd,
            startArea, &recastPath, unusedRecastLen, false))
        return false;

    if (recastPath.size() < 2)
        return false;

    float recastLength = 0.f;

    for (int i = 0; i + 1 < recastPath.size(); ++i)
        recastLength += recastPath.get(i).getWorldPosition().distanceTo(
            recastPath.get(i + 1).getWorldPosition());

    float tolerance = manager->
        getStructureTraversalZeroClipWalkableToleranceRatio();

    if (tolerance < 1.f)
        tolerance = 1.f;

    // A detour means the obstruction is real. Straight-through means the mesh
    // covers the chord and the bot walks over the geometry the ray hit.
    return recastLength > 0.f && recastLength <= segmentLength * tolerance;
}

ZeroClipClearanceResult SimPlayerController::probeEmittedPathClearance(
        Zone* zone, Vector<WorldCoordinates>* path, float rayHeight,
        uint64 ignoredAgentOid) {
    ZeroClipClearanceResult result;
    auto started = std::chrono::steady_clock::now();
    auto finish = [&started, &result]() {
        result.elapsedUs = (uint64)std::chrono::duration_cast<
            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                started).count();
        return result;
    };
    SimPlayerManager* manager = SimPlayerManager::instance();

    // Skipped, NOT clear: we learned nothing about this path.
    if (manager == nullptr || zone == nullptr || path == nullptr ||
            path->size() < 2)
        return finish();

    int maxSegments = manager->
        getStructureTraversalZeroClipMaxProbedSegments();
    float maxSegmentMeters = manager->
        getStructureTraversalZeroClipMaxSegmentMeters();
    int maxCandidates = manager->getStructureTraversalZeroClipMaxCandidates();
    float broadPhasePad = manager->
        getStructureTraversalZeroClipBroadPhasePadMeters();

    if (maxSegments < 1)
        maxSegments = 1;
    if (maxCandidates < 1)
        maxCandidates = 1;

    // ---- 1. Collect the segments worth probing, at torso height. ----
    Vector<Vector3> segmentStarts;
    Vector<Vector3> segmentEnds;
    Vector<float> segmentLengths;
    // Original path-segment indices: earlier segments can be skipped, so the
    // position in these vectors is not the position in the path.
    Vector<int> segmentPathIndex;
    bool segmentCapHit = false;
    bool skippedAnySegment = false;

    for (int i = 0; i + 1 < path->size(); ++i) {
        if (segmentStarts.size() >= maxSegments) {
            segmentCapHit = true;
            break;
        }

        // Cell-local nodes are not comparable to world coordinates; a segment
        // that enters a cell is the interior traversal's business, not D7's.
        if (path->get(i).getCell() != nullptr ||
                path->get(i + 1).getCell() != nullptr) {
            skippedAnySegment = true;
            continue;
        }

        Vector3 rayStart = path->get(i).getWorldPosition();
        Vector3 rayEnd = path->get(i + 1).getWorldPosition();
        float segmentLength = rayStart.distanceTo(rayEnd);

        if (segmentLength < 0.001f || segmentLength > maxSegmentMeters) {
            skippedAnySegment = true;
            continue;
        }

        rayStart.setZ(rayStart.getZ() + rayHeight);
        rayEnd.setZ(rayEnd.getZ() + rayHeight);

        segmentStarts.add(rayStart);
        segmentEnds.add(rayEnd);
        segmentLengths.add(segmentLength);
        segmentPathIndex.add(i);
    }

    result.segments = segmentStarts.size();

    if (result.segments == 0) {
        result.outcome = segmentCapHit ? ZeroClipOutcome::Truncated :
            ZeroClipOutcome::Skipped;
        return finish();
    }

    try {
        // ---- 2. ONE world query for the whole path. ----
        // The first observe run issued a query PER SEGMENT, which made long
        // routes unaffordable and forced a 16-segment cap that truncated 88 of
        // 89 of them. One query over the path's bounding sphere costs about the
        // same as one segment's did, so the cap can now cover real routes.
        Vector3 minPoint = segmentStarts.get(0);
        Vector3 maxPoint = segmentStarts.get(0);

        auto stretch = [&minPoint, &maxPoint](const Vector3& point) {
            minPoint.set(Math::min(minPoint.getX(), point.getX()),
                Math::min(minPoint.getY(), point.getY()),
                Math::min(minPoint.getZ(), point.getZ()));
            maxPoint.set(Math::max(maxPoint.getX(), point.getX()),
                Math::max(maxPoint.getY(), point.getY()),
                Math::max(maxPoint.getZ(), point.getZ()));
        };

        for (int i = 0; i < segmentStarts.size(); ++i) {
            stretch(segmentStarts.get(i));
            stretch(segmentEnds.get(i));
        }

        Vector3 centre = (minPoint + maxPoint) * 0.5f;
        float searchRadius = centre.distanceTo(maxPoint) + broadPhasePad;

        // Reference-counted snapshot, NOT the raw InRangeObjectsVector. The
        // query result outlives the zone read lock, and this probe walks it on
        // the pathfinding worker while the rest of the server keeps despawning
        // objects. The walkable confirmation below widens that window further
        // by adding a millisecond-scale recast query INSIDE the segment loop,
        // so a raw TreeEntry* here is a use-after-free waiting for a despawn to
        // land in the gap.
        SortedVector<ManagedReference<TreeEntry*> > objects;
        zone->getInRangeObjects(centre.getX(), 0, centre.getY(), searchRadius,
            &objects, true, true);

        // ---- 3. Segments outer, objects inner, so the FIRST hit found is the
        //         earliest obstruction along the route the bot will walk. ----
        int narrowPhaseTests = 0;
        bool budgetHit = false;

        for (int s = 0; s < segmentStarts.size() && !budgetHit; ++s) {
            const Vector3& rayStart = segmentStarts.get(s);
            const Vector3& rayEnd = segmentEnds.get(s);
            float segmentLength = segmentLengths.get(s);

            // The world query is unordered, so the first intersecting object
            // is not necessarily the nearest one. Scan the whole segment and
            // keep the minimum hitAt, then stop: segments are visited in route
            // order, so the earliest blocking SEGMENT wins, and within it the
            // nearest OBJECT wins.
            float nearestHit = FLT_MAX;
            SharedObjectTemplate* nearestTemplate = nullptr;

            for (int i = 0; i < objects.size(); ++i) {
                if (narrowPhaseTests >= maxCandidates) {
                    budgetHit = true;
                    break;
                }

                SceneObject* object =
                    static_cast<SceneObject*>(objects.get(i).get());
                if (object == nullptr ||
                        object->getObjectID() == ignoredAgentOid ||
                        (object->getReceiverFlags() &
                            CloseObjectsVector::COLLIDABLETYPE) == 0)
                    continue;

                const AppearanceTemplate* appearance =
                    object->getAppearanceTemplate();
                if (appearance == nullptr)
                    continue;

                // Cheap bounding-sphere cull BEFORE any budget is spent, exactly
                // as stock CollisionManager::checkMovementCollision does it. The
                // pad decides what is fetched; this decides what is worth
                // intersecting. Without it the query hands every nearby object
                // straight to the mesh test.
                const BaseBoundingVolume* bounding =
                    appearance->getBoundingVolume();
                if (bounding == nullptr)
                    continue;

                const Sphere& objectSphere = bounding->getBoundingSphere();
                Vector3 objectPosition = object->getPosition() +
                    objectSphere.getCenter();
                float targetRadius = objectSphere.getRadius() + segmentLength;

                if (CollisionManager::getPointIntersection(objectPosition,
                        rayStart, rayEnd, targetRadius, segmentLength) ==
                            FLT_MAX)
                    continue;

                narrowPhaseTests++;

                float hitAt = CollisionManager::getAppearanceIntersection(object,
                    rayStart, rayEnd, 0.f, segmentLength);

                if (hitAt == FLT_MAX || hitAt >= nearestHit)
                    continue;

                nearestHit = hitAt;
                nearestTemplate = object->getObjectTemplate();
            }

            if (nearestHit != FLT_MAX) {
                // The appearance ray is a cheap PRE-FILTER, not a verdict. It
                // tests the straight chord between two path nodes, which for a
                // staircase or a bridge dives through the solid mass under the
                // walkable surface -- geometry the bot is meant to walk ON.
                // MEASURED 2026-08-28 (run `20260828-201648-d7p2-navmesh`): all
                // 68 recast-produced routes were flagged, 64 of them by one
                // Naboo bridge staircase. Confirm before believing it.
                if (isSegmentWalkableByNavmesh(zone, rayStart, rayEnd,
                        segmentLength)) {
                    result.walkableReclassified++;
                    continue;
                }

                result.outcome = ZeroClipOutcome::WouldBlock;
                result.hitAt = nearestHit;
                result.hitSegment = segmentPathIndex.get(s);
                result.blockingTemplate = nearestTemplate == nullptr ?
                    String("none") :
                    nearestTemplate->getFullTemplateString();
                result.candidates = narrowPhaseTests;
                return finish();
            }
        }

        result.candidates = narrowPhaseTests;

        // Worst-evidence-first: a path may only be called clear when every
        // segment it contains was itself examined to completion.
        if (budgetHit || segmentCapHit)
            result.outcome = ZeroClipOutcome::Truncated;
        else if (skippedAnySegment)
            result.outcome = ZeroClipOutcome::Skipped;
        else
            result.outcome = ZeroClipOutcome::Clear;
    } catch (...) {
        // Phase 1 is observe-only. A diagnostic failure must never change the
        // emitted path -- but it must never be recorded as a clear route either.
        result.outcome = ZeroClipOutcome::Error;
    }

    return finish();
}

// D7 Phase 2. Refuse only a CONCLUSIVE obstruction, and only while the budget
// holds.
//
// Fails OPEN on skipped/truncated/errored probes, which is the opposite of the
// harness exit assertion's fail-closed rule and deliberately so: a test oracle
// that cannot see must not pass the subject, but a mover that cannot see must
// still move. Half the probes on this server are inconclusive by construction
// (any cell-local segment downgrades the whole path), so failing closed here
// would refuse the majority of all movement.
bool SimPlayerController::shouldRejectClippingPath(
        const ZeroClipClearanceResult& result, bool& capExhausted) {
    capExhausted = false;

    SimPlayerManager* manager = SimPlayerManager::instance();

    if (manager == nullptr ||
            !manager->isStructureTraversalZeroClipEnforceEnabled() ||
            !result.wouldBlock())
        return false;

    int rejectionCap = manager->getStructureTraversalZeroClipRejectionCap();

    if (rejectionCap < 1)
        rejectionCap = 1;

    // Budget spent: walk the obstructed route rather than strand the bot. The
    // counter is NOT incremented here -- onPathFound can still reject this path
    // for combat, hybrid cancellation or a stale endpoint, and a path that is
    // never walked is not a residual clip. The caller records it only once the
    // route reached state = MOVING.
    if (zeroClipRejections >= rejectionCap) {
        capExhausted = true;
        return false;
    }

    zeroClipRejections++;

    return true;
}

// Runs on the path-delivery task thread, unlocked, immediately before the point
// where onPathFound would have taken the route.
//
// MEASURED 2026-08-28 (run `20260828-155527-d7p2-enforce`): routing a refusal
// into onPathFailed() the way the acceptFoundPath rejection does is WRONG here,
// and cost 7 scenarios. For a structure traversal onPathFailed() is terminal —
// `ST_FAIL reason=path_failed` straight to Idle, no retry — so a single
// obstruction killed the whole traversal and the rejection budget was never
// reached at all. The two rejections are not alike: a stale endpoint means the
// request itself was wrong, while a clipping route means the DESTINATION is
// still valid and still reachable and only this particular answer is unusable.
// So re-ask.
void SimPlayerController::rejectClippingPath(Vector<WorldCoordinates>* path) {
    if (path != nullptr)
        delete path;

    if (combatDriverMoveActive && isStructureTraversalFeatureEnabled() &&
            isTraversalActive()) {
        combatDriverMoveActive = false;
        state = IDLE;
        return;
    }

    // Cell egress owns its own exit-set ladder, and failing into it moves on to
    // the next candidate DOOR — a better answer than re-asking for the same one.
    if (cellEgressActive) {
        failCellEgress();
        return;
    }

    if (agent == nullptr || agent->getZone() == nullptr) {
        onPathFailed();
        return;
    }

    Zone* zone = agent->getZone();

    // The navmesh rung (D7 §6.1 ladder). MEASURED 2026-08-28 (run
    // `20260828-163536-d7p2-repath`): simply re-asking the SAME question is
    // useless against a deterministic pathfinder — 113 of 114 refusal runs got
    // back a byte-identical clipping route and walked it once the budget ran
    // out. A rung has to ask a DIFFERENT question, and recast is the one
    // available answer that routes around geometry instead of through it.
    //
    // Scoped to outdoor destinations. A recast route is a world path and cannot
    // enter a cell, so handing one to a cell-targeted leg would end it short of
    // the target, where acceptFoundPath rejects it as stale and the traversal
    // dies terminally. Cell targets keep the identical re-ask below, which is
    // weak but safe. This is also where the traffic is: 94 of the 113 clip
    // walks in that run were outdoor-destination miner legs, and the geometry
    // they hit (starports, gungan walls, bridge stairs) sits in exactly the
    // city/POI areas that ARE navmeshed.
    if (destinationCell == nullptr) {
        ManagedReference<NavArea*> currentArea;
        ManagedReference<NavArea*> targetArea;
        Vector3 currentPosition = agent->getWorldPosition();

        // Same area for both ends: recast cannot path between two meshes, and
        // an off-mesh wilderness leg has no rung to climb at all.
        if (findNavAreaAt(zone, currentPosition, currentArea) &&
                findNavAreaAt(zone, destination, targetArea) &&
                currentArea != nullptr && currentArea == targetArea) {
            state = CALCULATING_PATH;

            uint64 navmeshGeneration = advanceWorkLoopGeneration(
                "zeroClipRejectionNavmesh");
            WorldCoordinates navStart(agent);
            WorldCoordinates navEnd(destination, nullptr);
            Reference<SimPathFindTask*> navTask = new SimPathFindTask(this,
                navStart, navEnd, zone, currentArea, currentPosition,
                destination, false, navmeshGeneration);

            navTask->schedule(100);
            return;
        }
    }

    // Bottom rung: same destination, fresh request. Bounded by
    // zeroClipRejections, so once the budget is spent
    // shouldRejectClippingPath() stops refusing, the next answer is walked, and
    // the leg completes rather than stranding the bot.
    state = CALCULATING_PATH;

    uint64 movementGeneration = advanceWorkLoopGeneration(
        "zeroClipRejectionRepath");
    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(destinationLocal, destinationCell);
    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord,
        endCoord, zone, movementGeneration);

    task->schedule(100);
}

// --------------------------------------------------------
// TASKS
// --------------------------------------------------------
void SimPathFindTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    uint64 capturedGeneration = generation;

    if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "path_find"))
        return;

#ifdef DEBUG_SIMPVP
    // DEBUG: Trace start
    Logger::console.info("SimPlayer: [Thread] Pathfinding started...", true);
#endif
    Vector<WorldCoordinates>* path = nullptr;
    bool pathUsesNavmesh = useRecastPath;
    bool pathIsOverland = useDirectOverlandPath;
    
    try {
        if (useRecastPath) {
            Vector<WorldCoordinates>* recastPath =
                new Vector<WorldCoordinates>();
            float length = 0.f;

            if (navArea != nullptr &&
                    PathFinderManager::instance()->getRecastPath(
                        recastStart, recastEnd, navArea, recastPath, length,
                        allowPartial)) {
                path = recastPath;
            } else {
                delete recastPath;
            }
        } else if (useDirectOverlandPath) {
            // This is an explicit overland request, not a guess based on the
            // resulting node count. The target height is terrain-derived only
            // after the controller has confirmed the agent is off-mesh (or
            // this is the sanctioned exit egress leg).
            Vector3 start = startCoord.getWorldPosition();
            Vector3 end = endCoord.getWorldPosition();
            if (directTargetUsesTerrainHeight)
                end.setZ(zone->getHeight(end.getX(), end.getY()));

            path = new Vector<WorldCoordinates>();
            path->add(WorldCoordinates(start, nullptr));
            path->add(WorldCoordinates(end, nullptr));
        } else {
            path = PathFinderManager::instance()->findPath(startCoord, endCoord, zone);
        }
    } catch (...) {
        Logger::console.info("SimPlayer: [Thread] EXCEPTION in findPath!", true);
        path = nullptr;
    }

    // D7 Phase 1, observe-only. Deliberately probes the path that is ACTUALLY
    // about to be handed to the mover, whichever branch produced it: the
    // generic branch reaches PathFinderManager's own unchecked 2-node fallback
    // ("path could not be evaluated"), which is how non-hybrid miners get their
    // straight lines. Probing only the explicit overland branch would leave the
    // largest population out of the evidence. `enforce` is not consulted here,
    // so this block cannot alter `path`.
    //
    // The intersection work runs HERE, on the pathfinding worker, so it never
    // adds latency to path delivery -- but nothing is COMMITTED (no counters,
    // no log line) until the delivery-time generation check accepts the path.
    // A superseded task's path is deleted and never walked, so recording it
    // would put routes nobody ever travelled into the Phase 2 evidence.
    bool zeroClipProbed = false;
    ZeroClipClearanceResult zeroClipResult;
    String zeroClipLeg = "unknown";
    float zeroClipRayHeight = probeRayHeight;
    uint64 zeroClipAgentOid = probeAgentOid;
    int zeroClipNodes = 0;
    {
        SimPlayerManager* zeroClipManager = SimPlayerManager::instance();
        if (zeroClipManager != nullptr && path != nullptr &&
                zeroClipManager->isStructureTraversalZeroClipEnabled()) {
            try {
                zeroClipLeg = useRecastPath ? "navmesh" :
                    (useDirectOverlandPath ? "directOverland" : "generic");
                zeroClipNodes = path->size();
                zeroClipResult = strongCtrl->probeEmittedPathClearance(zone,
                    path, zeroClipRayHeight, zeroClipAgentOid);
                zeroClipProbed = true;
            } catch (...) {
                // Observation must never change the emitted path.
                zeroClipProbed = false;
            }
        }
    }

#ifdef DEBUG_SIMPVP
    // DEBUG: Trace end
    if (path != nullptr) {
        Logger::console.info("SimPlayer: [Thread] Pathfinding success. Nodes: " + String::valueOf(path->size()), true);
    }
    else {
        Logger::console.info("SimPlayer: [Thread] Pathfinding returned NULL.", true);
    }
#endif

    Core::getTaskManager()->executeTask([strongCtrl, path, capturedGeneration,
            pathUsesNavmesh, pathIsOverland, zeroClipProbed, zeroClipResult,
            zeroClipLeg, zeroClipRayHeight, zeroClipAgentOid, zeroClipNodes] () {
        if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "path_find_result")) {
            if (path != nullptr)
                delete path;
            return;
        }

        // D7 Phase 2 enforcement sits HERE rather than inside onPathFound: the
        // probe result lives in this lambda, and refusing before the virtual
        // dispatch covers every controller subclass with one branch. The
        // decision costs nothing new -- the intersection work already ran on
        // the pathfinding worker, before this task was ever queued.
        bool zeroClipEnforcementRejected = false;
        bool zeroClipCapExhausted = false;

        if (path != nullptr) {
            if (zeroClipProbed && strongCtrl->shouldRejectClippingPath(
                    zeroClipResult, zeroClipCapExhausted)) {
                strongCtrl->rejectClippingPath(path);
                zeroClipEnforcementRejected = true;
            } else {
                strongCtrl->onPathFound(path, pathUsesNavmesh, pathIsOverland);
            }
        } else {
            strongCtrl->onPathTaskFailed(pathUsesNavmesh);
        }

        // Passing the generation check is NOT the same as being walked:
        // onPathFound can still reject for hybrid cancellation, combat, a
        // too-short path, or a stale endpoint, and it deletes the path when it
        // does. Commit the observation only against a route that reached
        // state = MOVING, so Phase 1 block rates describe journeys the bots
        // actually made. (`path` may already be freed here -- the log uses the
        // captured node count, never the pointer.)
        //
        // An ENFORCEMENT rejection never reaches state = MOVING, so it has to
        // be admitted separately or enforcement would erase its own evidence:
        // the blocked routes would vanish from the counters entirely.
        bool zeroClipWalked = strongCtrl->consumeProbePathAccepted();

        if (zeroClipProbed && (zeroClipWalked || zeroClipEnforcementRejected)) {
            SimPlayerManager* zeroClipManager = SimPlayerManager::instance();

            if (zeroClipManager != nullptr) {
                zeroClipManager->recordZeroClipClearanceCheck();

                switch (zeroClipResult.outcome) {
                case ZeroClipOutcome::WouldBlock:
                    zeroClipManager->recordZeroClipWouldBlock();
                    break;
                case ZeroClipOutcome::Truncated:
                    zeroClipManager->recordZeroClipTruncated();
                    break;
                case ZeroClipOutcome::Skipped:
                    zeroClipManager->recordZeroClipSkipped();
                    break;
                case ZeroClipOutcome::Error:
                    zeroClipManager->recordZeroClipError();
                    break;
                case ZeroClipOutcome::Clear:
                    break;
                }

                // `wouldBlock` stays the observation count and keeps its
                // meaning across Phase 1 and Phase 2 runs; `blocked` counts
                // only the routes enforcement actually refused. Both are
                // incremented for a refusal, so `wouldBlock - blocked` is the
                // number that were seen and walked anyway.
                if (zeroClipEnforcementRejected)
                    zeroClipManager->recordZeroClipBlocked();

                // Only a route that actually reached MOVING is a residual clip.
                if (zeroClipCapExhausted && zeroClipWalked)
                    zeroClipManager->recordZeroClipCapExhausted();

                if (zeroClipResult.walkableReclassified > 0)
                    zeroClipManager->recordZeroClipWalkableReclassified(
                        zeroClipResult.walkableReclassified);

                if (zeroClipManager->
                        isStructureTraversalZeroClipLoggingEnabled()) {
                    bool blocked = zeroClipResult.wouldBlock();
                    StructureTraversalDiagLog::writeZeroClip(
                        String("ST_CLEARANCE result=") +
                        ZeroClipClearanceResult::outcomeName(
                            zeroClipResult.outcome) +
                        // What enforcement DID, which "would_block" alone can
                        // no longer tell you once Phase 2 exists: a walked
                        // obstruction means enforcement was off or the
                        // rejection budget was spent.
                        " action=" + (zeroClipEnforcementRejected ?
                            String("rejected") :
                            (blocked ? String("walked") : String("none"))) +
                        " leg=" + zeroClipLeg +
                        " agent=" + String::valueOf(zeroClipAgentOid) +
                        " conclusive=" +
                        String::valueOf(zeroClipResult.isConclusive() ? 1 : 0) +
                        " nodes=" + String::valueOf(zeroClipNodes) +
                        " segments=" + String::valueOf(zeroClipResult.segments) +
                        " hitSegment=" + (blocked ?
                            String::valueOf(zeroClipResult.hitSegment) :
                            String("none")) +
                        " rayHeight=" + String::valueOf(zeroClipRayHeight) +
                        " hitAt=" + (blocked ?
                            String::valueOf(zeroClipResult.hitAt) :
                            String("none")) +
                        " blockingTemplate=" + (blocked ?
                            zeroClipResult.blockingTemplate : String("none")) +
                        " walkableReclassified=" +
                        String::valueOf(zeroClipResult.walkableReclassified) +
                        " candidates=" +
                        String::valueOf(zeroClipResult.candidates) +
                        " elapsedUs=" +
                        String::valueOf(zeroClipResult.elapsedUs));
                }
            }
        }
    }, "SimPlayerResultLambda");
}

void ArrivalCheckTask::run() {
    Reference<SimPlayerController*> strongCtrl = controller.get();
    if (strongCtrl == nullptr) return;

    uint64 capturedGeneration = generation;

    if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "arrival_check"))
        return;
    
    Core::getTaskManager()->executeTask([strongCtrl, capturedGeneration] () {
        if (!strongCtrl->isWorkLoopGenerationCurrent(capturedGeneration, "arrival_check"))
            return;

        strongCtrl->checkArrival();
    }, "SimPlayerArrivalLambda");
}

void SimBehaviorTask::run() {
    Reference<SimPlayerController*> baseCtrl = controller.get();
    if (baseCtrl == nullptr) return;

    int capturedType = type;
    uint64 capturedGeneration = generation;
    String taskType = String("behavior_") + String::valueOf(capturedType);

    if (!baseCtrl->isWorkLoopGenerationCurrent(capturedGeneration, taskType))
        return;

    Core::getTaskManager()->executeTask([baseCtrl, capturedType, capturedGeneration, taskType] () {
        if (!baseCtrl->isWorkLoopGenerationCurrent(capturedGeneration, taskType))
            return;

        SimMinerController* miner = dynamic_cast<SimMinerController*>(baseCtrl.get());
        if (miner == nullptr) return;

        if (capturedType == SimBehaviorTask::FINISH_SURVEY) miner->finishSurvey();
        else if (capturedType == SimBehaviorTask::FINISH_SAMPLE) miner->finishSample();
        else if (capturedType == SimBehaviorTask::START_STATIONED_SAMPLE) miner->startStationedSample();
    }, "SimPlayerBehaviorLambda");
}

void StructureTraversalResumeMonitorTask::run() {
    Reference<SimPlayerController*> strongController = controller.get();
    if (strongController == nullptr ||
            !strongController->isTraversalGenerationCurrent(traversalGeneration))
        return;

    uint64 capturedTraversalGeneration = traversalGeneration;
    Core::getTaskManager()->executeTask(
        [strongController, capturedTraversalGeneration]() {
            if (!strongController->isTraversalGenerationCurrent(
                    capturedTraversalGeneration))
                return;

            strongController->checkStructureTraversalResume(
                capturedTraversalGeneration);
        }, "SimPlayerTraversalResumeLambda");
}

class SimRetryTask : public Task {
    WeakReference<SimPlayerController*> controller;
    uint64 generation;
public:
    SimRetryTask(SimPlayerController* ctrl, uint64 g) : controller(ctrl), generation(g) {}
    void run() override {
        Reference<SimPlayerController*> strong = controller.get();
        if (strong != nullptr) {
            uint64 capturedGeneration = generation;

            if (!strong->isWorkLoopGenerationCurrent(capturedGeneration, "retry"))
                return;

            Core::getTaskManager()->executeTask([strong, capturedGeneration]() {
                if (!strong->isWorkLoopGenerationCurrent(capturedGeneration, "retry"))
                    return;

                strong->startSimLoop();
            }, "SimRetryLambda");
        }
    }
};

// ========================================================
// BASE SIMPLAYER CONTROLLER
// ========================================================

SimPlayerController::SimPlayerController(AiAgent* aiAgent) {
    agent = aiAgent;
    probeAgentOid = aiAgent == nullptr ? 0 : aiAgent->getObjectID();
    // Torso-height default until the first locked refresh; the telemetry
    // reports which of the two produced any given line.
    probeRayHeight = 1.f;
    probePathAccepted = false;
    state = IDLE;
    simPathIndex = 0;
    stuckWatchdogCount = 0;
    rePathAttempts = 0;
    runSpeed = 3.0f;
    interplanetaryTravelActive = false;
    travelDestinationZone = "";
    travelDeparturePosition = Vector3(0, 0, 0);
    travelDestinationArrival = Vector3(0, 0, 0);
    travelDestinationStarport = "";
    travelStartedAtMs = 0;
    travelBoardRadius = 20.f;
    ticketTravelPhase = TICKET_TRAVEL_NONE;
    ticketCollectorWorld = Vector3(0, 0, 0);
    ticketCollectorLocal = Vector3(0, 0, 0);
    ticketCollectorCell = nullptr;
    ticketCollectorOid = 0;
    ticketCollectorFound = false;
    ticketArrivalCollectorFound = false;
    ticketArrivalOutdoor = Vector3(0, 0, 0);
    ticketApproachAttempts = 0;
    workLoopGeneration = 1;
    setLoggingName("SimPlayerController");
    destination = Vector3(0, 0, 0);
    destinationLocal = Vector3(0, 0, 0);
    destinationCell = nullptr;
    cellEgressActive = false;
    cellEgressResumeWorld = Vector3(0, 0, 0);
    cellEgressResumeLocal = Vector3(0, 0, 0);
    cellEgressResumeCell = nullptr;
    cellEgressAttempts = 0;
    cellEgressCandidates.removeAll();
    cellEgressCandidateLocals.removeAll();
    cellEgressCandidateCellIndexes.removeAll();
    cellEgressCandidateInHollow.removeAll();
    cellEgressCandidateIndex = 0;
    cellEgressCandidateAttempts = 0;
    cellEgressTotalAttempts = 0;
    cellEgressExitSetBuilt = false;
    cellEgressBudgetExhaustedRecorded = false;
    hollowEscalationAttempts = 0;
    hollowDoorEgressSelectedCandidateIndex = -1;
    hollowEscalationActive = false;
    hollowEscalationTarget = Vector3(0, 0, 0);
    cellEgressSuppressed = false;
    finalDestination = Vector3(0, 0, 0);
    hasFinalDestination = false;
    onMeshMode = false;
    navmeshModeDebounceCounter = 0;
    navmeshRepathAttempts = 0;
    zeroClipRejections = 0;
    hybridLeg = HYBRID_LEG_NONE;
    hybridEgressPoint = Vector3(0, 0, 0);
    interiorApproachLeg = false;
    diagnosticLastParentCellOid = 0;
    diagnosticParentCellInitialized = false;
    structureTraversalPhase = StructureTraversalPhase::Idle;
    traversalGeneration = 1;
    traversalLastAppliedWorldPosition = Vector3(0, 0, 0);
    traversalWatchdogPositionInitialized = false;
    traversalPeaceSinceMs = 0;
    traversalResumeMonitorGeneration.store(0);
    traversalResumeInProgress = false;
    combatDriverMoveActive = false;
    farSideRejectionPending = false;
}

SimPlayerController::~SimPlayerController() {
    clearStructureTraversalState("destructor");
    clearCellEgressState();
    agent = nullptr;
}

void SimPlayerController::moveTo(Vector3 targetPos) {
    moveToWithOrigin(targetPos, targetPos, nullptr,
        TraversalMoveOrigin::External);
}

void SimPlayerController::moveToInterior(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell) {
    interiorApproachLeg = true;
    moveToWithOrigin(worldPos, localPos, targetCell,
        TraversalMoveOrigin::External);
}

void SimPlayerController::moveTo(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell) {
    moveToWithOrigin(worldPos, localPos, targetCell,
        TraversalMoveOrigin::External);
}

void SimPlayerController::moveToCombat(Vector3 targetPos) {
    moveToCombat(targetPos, targetPos, nullptr);
}

void SimPlayerController::moveToCombat(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell) {
    moveToWithOrigin(worldPos, localPos, targetCell,
        TraversalMoveOrigin::CombatDriver);
}

void SimPlayerController::enterStructure(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell) {
    if (!isStructureTraversalFeatureEnabled()) {
        moveToInterior(worldPos, localPos, targetCell);
        return;
    }
    if (agent == nullptr)
        return;

    clearStructureTraversalState("external_enter_preemption");
    cellEgressAttempts = 0;
    structureTraversalIntent.finalTargetWorld = worldPos;
    structureTraversalIntent.finalTargetLocal = localPos;
    structureTraversalIntent.finalTargetCell = targetCell;
    structureTraversalIntent.reentryCell = targetCell;
    structureTraversalIntent.owningBuildingOid = 0;
    if (targetCell != nullptr && targetCell->getParent() != nullptr)
        structureTraversalIntent.owningBuildingOid =
            targetCell->getParent().get()->getObjectID();
    structureTraversalIntent.entryReentryWaypoint = localPos;
    structureTraversalIntent.generation = traversalGeneration;
    structureTraversalIntent.createdAtMs = System::getMiliTime();
    structureTraversalIntent.lastPhaseAtMs =
        structureTraversalIntent.createdAtMs;
    structureTraversalIntent.exitIntent = false;
    structureTraversalIntent.active = targetCell != nullptr;

    if (!structureTraversalIntent.active) {
        moveTo(worldPos, localPos, targetCell);
        return;
    }

    ManagedReference<SceneObject*> parent = agent == nullptr ? nullptr :
        agent->getParent().get();
    bool currentlyInside = parent != nullptr && parent->isCellObject();
    uint64 currentBuildingOid = 0;
    if (currentlyInside) {
        ManagedReference<BuildingObject*> currentBuilding =
            parent->getParent().get().castTo<BuildingObject*>();
        if (currentBuilding != nullptr)
            currentBuildingOid = currentBuilding->getObjectID();
    }

    if (currentlyInside && currentBuildingOid !=
            structureTraversalIntent.owningBuildingOid)
        setStructureTraversalPhase(StructureTraversalPhase::Egress,
            "cross_building_enter");
    else if (currentlyInside)
        setStructureTraversalPhase(StructureTraversalPhase::InteriorRoute,
            "same_building_enter");
    else
        setStructureTraversalPhase(StructureTraversalPhase::ApproachDoor,
            "outdoor_enter");

    interiorApproachLeg = true;
    moveToWithOrigin(worldPos, localPos, targetCell,
        TraversalMoveOrigin::Internal);
}

void SimPlayerController::exitStructure(Vector3 outdoorDest) {
    if (!isStructureTraversalFeatureEnabled()) {
        moveTo(outdoorDest);
        return;
    }
    if (agent == nullptr)
        return;

    bool pausedPreemption = isTraversalActive() &&
        structureTraversalPhase == StructureTraversalPhase::CombatPaused;
    uint64 previousBuildingOid = pausedPreemption ?
        structureTraversalIntent.owningBuildingOid : 0;
    ManagedReference<CellObject*> previousReentryCell = pausedPreemption ?
        structureTraversalIntent.reentryCell : nullptr;
    Vector3 previousReentryWaypoint = pausedPreemption ?
        structureTraversalIntent.entryReentryWaypoint : outdoorDest;

    ManagedReference<SceneObject*> parent = agent == nullptr ? nullptr :
        agent->getParent().get();
    if (pausedPreemption) {
        clearStructureTraversalState("external_exit_preemption");
        cellEgressAttempts = 0;
        structureTraversalIntent.finalTargetWorld = outdoorDest;
        structureTraversalIntent.finalTargetLocal = outdoorDest;
        structureTraversalIntent.finalTargetCell = nullptr;
        structureTraversalIntent.reentryCell = previousReentryCell;
        structureTraversalIntent.owningBuildingOid = previousBuildingOid;
        structureTraversalIntent.entryReentryWaypoint =
            previousReentryWaypoint;
        structureTraversalIntent.generation = traversalGeneration;
        structureTraversalIntent.createdAtMs = System::getMiliTime();
        structureTraversalIntent.lastPhaseAtMs =
            structureTraversalIntent.createdAtMs;
        structureTraversalIntent.exitIntent = true;
        structureTraversalIntent.active = true;
        setStructureTraversalPhase(StructureTraversalPhase::CombatPaused,
            "external_exit_preemption_paused");
        pauseStructureTraversal("external_exit_preemption");
        return;
    }

    if (parent == nullptr || !parent->isCellObject()) {
        clearStructureTraversalState("exit_requested_outdoors");
        moveTo(outdoorDest);
        return;
    }

    ManagedReference<BuildingObject*> building =
        parent->getParent().get().castTo<BuildingObject*>();
    if (building == nullptr) {
        clearStructureTraversalState("exit_missing_building");
        moveTo(outdoorDest);
        return;
    }

    clearStructureTraversalState("external_exit_preemption");
    cellEgressAttempts = 0;
    structureTraversalIntent.finalTargetWorld = outdoorDest;
    structureTraversalIntent.finalTargetLocal = outdoorDest;
    structureTraversalIntent.finalTargetCell = nullptr;
    structureTraversalIntent.reentryCell = cast<CellObject*>(parent.get());
    structureTraversalIntent.owningBuildingOid = building->getObjectID();
    structureTraversalIntent.entryReentryWaypoint =
        agent->getPosition();
    structureTraversalIntent.generation = traversalGeneration;
    structureTraversalIntent.createdAtMs = System::getMiliTime();
    structureTraversalIntent.lastPhaseAtMs =
        structureTraversalIntent.createdAtMs;
    structureTraversalIntent.exitIntent = true;
    structureTraversalIntent.active = true;
    setStructureTraversalPhase(StructureTraversalPhase::Egress,
        "exit_requested");

    moveToWithOrigin(outdoorDest, outdoorDest, nullptr,
        TraversalMoveOrigin::Internal);
}

void SimPlayerController::moveToWithOrigin(Vector3 worldPos, Vector3 localPos,
        CellObject* targetCell, TraversalMoveOrigin origin) {
    // Every path request funnels through here. Without this, only the three
    // explicit directOverland sites had a measured ray height and all generic /
    // navmesh legs probed at the 1.0m default -- which was every single line in
    // the first observe run.
    refreshProbeRayHeight();

    if (agent == nullptr) return;

    combatDriverMoveActive = origin == TraversalMoveOrigin::CombatDriver;

    bool traversalFeatureEnabled = isStructureTraversalFeatureEnabled();
    bool pausedPreemption = origin == TraversalMoveOrigin::External &&
        traversalFeatureEnabled && isTraversalActive() &&
        structureTraversalPhase == StructureTraversalPhase::CombatPaused;
    uint64 previousBuildingOid = pausedPreemption ?
        structureTraversalIntent.owningBuildingOid : 0;
    ManagedReference<CellObject*> previousReentryCell = pausedPreemption ?
        structureTraversalIntent.reentryCell : nullptr;
    Vector3 previousReentryWaypoint = pausedPreemption ?
        structureTraversalIntent.entryReentryWaypoint : localPos;

    if (origin == TraversalMoveOrigin::External && isTraversalActive() &&
            traversalFeatureEnabled) {
        clearStructureTraversalState("external_move_preemption");
        cellEgressAttempts = 0;
    }

    if (pausedPreemption) {
        cellEgressAttempts = 0;
        structureTraversalIntent.finalTargetWorld = worldPos;
        structureTraversalIntent.finalTargetLocal = localPos;
        structureTraversalIntent.finalTargetCell = targetCell;
        if (targetCell != nullptr)
            structureTraversalIntent.reentryCell = targetCell;
        else
            structureTraversalIntent.reentryCell = previousReentryCell;
        structureTraversalIntent.owningBuildingOid = previousBuildingOid;
        if (targetCell != nullptr && targetCell->getParent() != nullptr)
            structureTraversalIntent.owningBuildingOid =
                targetCell->getParent().get()->getObjectID();
        ManagedReference<SceneObject*> parent = agent->getParent().get();
        if (structureTraversalIntent.owningBuildingOid == 0 && parent != nullptr &&
                parent->isCellObject() && parent->getParent() != nullptr)
            structureTraversalIntent.owningBuildingOid =
                parent->getParent().get()->getObjectID();
        structureTraversalIntent.entryReentryWaypoint = targetCell != nullptr ?
            localPos : previousReentryWaypoint;
        structureTraversalIntent.generation = traversalGeneration;
        structureTraversalIntent.createdAtMs = System::getMiliTime();
        structureTraversalIntent.lastPhaseAtMs =
            structureTraversalIntent.createdAtMs;
        structureTraversalIntent.exitIntent = targetCell == nullptr;
        structureTraversalIntent.active = true;
        setStructureTraversalPhase(StructureTraversalPhase::CombatPaused,
            "external_move_preemption_paused");
        pauseStructureTraversal("external_move_preemption");
        return;
    }

    if (origin == TraversalMoveOrigin::CombatDriver &&
            traversalFeatureEnabled && isTraversalActive() &&
            structureTraversalPhase != StructureTraversalPhase::CombatPaused &&
            isCombatDriverActive())
        pauseStructureTraversal("combat_driver_start");

    if (origin == TraversalMoveOrigin::CombatDriver &&
            traversalFeatureEnabled && isTraversalActive() &&
            cellEgressActive) {
        clearCellEgressState();
        advanceWorkLoopGeneration("combatMoveReplacesEgressLeg");
    }

    if (cellEgressActive && origin == TraversalMoveOrigin::External) {
        clearCellEgressState();
        advanceWorkLoopGeneration("moveToCancelsCellEgress");
    }

    bool diagnostic = isCellNavDiagAgent(agent.get());
    if (diagnostic) {
        CellNavDiagLog::write(
            "MOVE_REQUEST_ENTRY " + CellNavDiagLog::fmtPos(agent.get()) +
            " requested=" + CellNavDiagLog::fmtPos(worldPos, localPos,
                targetCell) +
            " distance=" + String::valueOf(
                agent->getWorldPosition().distanceTo(worldPos)) +
            " interiorApproachLeg=" +
                String::valueOf(interiorApproachLeg) +
            " hybridActive=" + String::valueOf(isHybridMovementActive()) +
            " hybridOnMesh=" + String::valueOf(onMeshMode));
    }

    Zone* zone = agent->getZone();
    if (zone == nullptr) {
        if (diagnostic)
            CellNavDiagLog::write("MOVE_REQUEST_REJECT reason=no_zone");
        return;
    }

    if (!zone->isWithinBoundaries(worldPos)) {
        if (diagnostic)
            CellNavDiagLog::write("MOVE_REQUEST_REJECT reason=outside_boundaries");
        onPathFailed();
        return;
    }

    if (interiorApproachLeg)
        resetHybridMovementState(true);

    if (isHybridMovementActive()) {
        finalDestination = worldPos;
        hasFinalDestination = true;
        onMeshMode = agent->isInNavMesh();
        navmeshModeDebounceCounter = 0;
        navmeshRepathAttempts = 0;
        hybridLeg = HYBRID_LEG_NONE;
        hybridEgressPoint = Vector3(0, 0, 0);
    }

    destination = worldPos;
    destinationLocal = localPos;
    destinationCell = targetCell;

    if (isTraversalActive() && isStructureTraversalFeatureEnabled()) {
        // Navmesh coverage at BOTH ends of the leg. This sits on the path-request
        // path deliberately: the previous probe lived inside the hollow scan,
        // which only fires when a bot is hollow-stuck, so the one configuration
        // that needed measuring never produced a single sample. A probe has to
        // sit somewhere that survives the change it is measuring.
        // IDENTITY, not presence. Recast cannot route between two DISJOINT
        // NavAreas, so "both navmeshed" is necessary but not sufficient -- if
        // the pad owns one area and the street another, a recast path between
        // them fails exactly as observed. A boolean hid that distinction.
        Zone* navZone = agent->getZone();
        ManagedReference<NavArea*> botArea;
        ManagedReference<NavArea*> targetArea;
        Vector3 botWorld = agent->getWorldPosition();
        uint64 navAtBot = findNavAreaAt(navZone, botWorld, botArea) &&
            botArea != nullptr ? botArea->getObjectID() : 0;
        uint64 navAtTarget = findNavAreaAt(navZone, worldPos, targetArea) &&
            targetArea != nullptr ? targetArea->getObjectID() : 0;
        int sameArea = (navAtBot != 0 && navAtBot == navAtTarget) ? 1 : 0;

        StructureTraversalDiagLog::write(
            "ST_PATH result=request agent=" +
            String::valueOf(agent->getObjectID()) + " generation=" +
            String::valueOf(traversalGeneration) + " building=" +
            String::valueOf(structureTraversalIntent.owningBuildingOid) +
            " cell=" + String::valueOf(getTraversalTargetCellOid()) +
            " navAreaBot=" + String::valueOf(navAtBot) +
            " navAreaTarget=" + String::valueOf(navAtTarget) +
            " sameArea=" + String::valueOf(sameArea) +
            " botPos=(" + String::valueOf(botWorld.getX()) + "," +
            String::valueOf(botWorld.getY()) + "," +
            String::valueOf(botWorld.getZ()) + ") " +
            StructureTraversalDiagLog::fmtPos(worldPos, localPos, targetCell));
    }

    bool traversalCombatBusy = agent->isInCombat() ||
        isCombatDriverActive();
    if (traversalCombatBusy && origin != TraversalMoveOrigin::CombatDriver &&
            traversalFeatureEnabled && isTraversalActive()) {
        state = IDLE;
        pauseStructureTraversal("move_combat");
        return;
    }

    if (agent->isInCombat() &&
            !(origin == TraversalMoveOrigin::CombatDriver &&
                traversalFeatureEnabled && isTraversalActive())) {
        state = IDLE;
        if (diagnostic)
            CellNavDiagLog::write("MOVE_REQUEST_HELD reason=in_combat");
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer moveTo: isInCombat", true);
#endif
        return;
    }

    if (beginCellEgressIfNeeded(worldPos, localPos, targetCell,
            origin != TraversalMoveOrigin::External,
            origin == TraversalMoveOrigin::CombatDriver))
        return;

    if (isHybridMovementActive()) {
        if (diagnostic)
            CellNavDiagLog::write("MOVE_REQUEST_PATH mode=hybrid");
        requestHybridPath();
        return;
    }

    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH; 
    uint64 movementGeneration = advanceWorkLoopGeneration("moveTo");

    float dist = agent->getWorldPosition().distanceTo(worldPos);
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer moveTo: Requesting move to " + worldPos.toString() + " (Dist: " + String::valueOf(dist) + "m)", true);
#endif

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(localPos, targetCell);

    if (diagnostic)
        CellNavDiagLog::write("MOVE_REQUEST_PATH mode=cell_aware start=" +
            CellNavDiagLog::fmtPos(startCoord) + " end=" +
            CellNavDiagLog::fmtPos(endCoord));

    Reference<SimPathFindTask*> task =
        new SimPathFindTask(this, startCoord, endCoord, zone, movementGeneration);
    
    task->schedule(100); 
}

bool SimPlayerController::beginCellEgressIfNeeded(Vector3 worldPos,
        Vector3 localPos, CellObject* targetCell, bool preserveTraversal,
        bool combatDriver) {
    if (agent == nullptr || cellEgressActive || cellEgressSuppressed)
        return false;

    bool traversalFeatureEnabled = isStructureTraversalFeatureEnabled();
    bool traversalCombatBusy = agent->isInCombat() ||
        (traversalFeatureEnabled && isTraversalActive() &&
            isCombatDriverActive());
    if (traversalCombatBusy) {
        if (!combatDriver && traversalFeatureEnabled && isTraversalActive()) {
            pauseStructureTraversal("egress_combat");
            return true;
        }

        if (!combatDriver)
            return false;
    }

    // Combat movement owns its own leg while a traversal is paused. It must
    // not create/replace the traversal egress resume record or change the
    // traversal phase.
    if (combatDriver && traversalFeatureEnabled && isTraversalActive())
        return false;

    Zone* zone = agent->getZone();
    if (zone == nullptr)
        return false;

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    if (parent == nullptr || !parent->isCellObject()) {
        // Outdoors: this is not a cell exit, and the situation has changed since
        // any prior stuck exit, so refresh the per-stuck-exit attempt budget.
        cellEgressAttempts = 0;
        return false;
    }

    SimPlayerManager* manager = SimPlayerManager::instance();
    bool exitSetEnabled = manager != nullptr && manager->
        isStructureTraversalZeroClipExitSetEnabled();

    // In a cell but the legacy exit attempts are exhausted: fall through to the
    // normal (pre-fix) path rather than looping egress forever. D7 replaces this
    // building-wide cap only when its exit-set gate is enabled.
    int egressAttemptCap = 2;
    if (isStructureTraversalFeatureEnabled())
        egressAttemptCap = manager->
            getStructureTraversalEgressAttemptCap();
    if (!exitSetEnabled && cellEgressAttempts >= egressAttemptCap)
        return false;

    ManagedReference<CellObject*> cell = cast<CellObject*>(parent.get());
    if (cell == nullptr)
        return false;

    if (targetCell != nullptr) {
        if (targetCell->getObjectID() == cell->getObjectID())
            return false;

        // Gate-off preserves the F.0.4.11 behavior exactly: cell targets from
        // inside are left to the legacy path. Gate-on only adds the missing
        // cross-building egress leg; same-building navigation remains owned by
        // the existing interior path request.
        if (!isStructureTraversalFeatureEnabled())
            return false;

        ManagedReference<BuildingObject*> targetBuilding =
            targetCell->getParent().get().castTo<BuildingObject*>();
        ManagedReference<BuildingObject*> currentBuilding =
            cell->getParent().get().castTo<BuildingObject*>();
        if (targetBuilding == nullptr || currentBuilding == nullptr ||
                targetBuilding->getObjectID() == currentBuilding->getObjectID())
            return false;
    }

    ManagedReference<BuildingObject*> building =
        cell->getParent().get().castTo<BuildingObject*>();
    if (building == nullptr)
        return false;

    // Leave via the exterior portal NEAREST the bot (so a front-hall bot exits the
    // front door), not the single template ejection point which can be on a far /
    // dead-end side (e.g. a starport's landing pad). Fall back to getEjectionPoint()
    // for buildings with no readable portal layout (cantina-style still works).
    Vector3 agentWorld = agent->getWorldPosition();
    Vector3 ejection;
    {
        Locker buildingLocker(building);
        ejection = building->getNearestExteriorPortalPoint(agentWorld);
        if (ejection.getX() == 0.f && ejection.getY() == 0.f)
            ejection = building->getEjectionPoint();
    }

    if ((ejection.getX() == 0.f && ejection.getY() == 0.f) ||
            !zone->isWithinBoundaries(ejection))
        return false;

    cellEgressResumeWorld = worldPos;
    cellEgressResumeLocal = localPos;
    cellEgressResumeCell = targetCell;
    cellEgressActive = true;
    cellEgressAttempts++;

    if (exitSetEnabled) {
        cellEgressCandidates.removeAll();
        cellEgressCandidateLocals.removeAll();
        cellEgressCandidateCellIndexes.removeAll();
        cellEgressCandidateInHollow.removeAll();
        cellEgressCandidateIndex = 0;
        cellEgressCandidateAttempts = 0;
        cellEgressTotalAttempts = 0;
        cellEgressExitSetBuilt = false;
        cellEgressBudgetExhaustedRecorded = false;
    }

    if (traversalFeatureEnabled && isTraversalActive()) {
        // entryReentryWaypoint is strictly CELL-LOCAL (it is fed back to
        // moveToInterior as the local path coordinate). The ejection point is
        // a WORLD point, so it gets its own field — writing it here is the
        // world-coord-as-cell-local bug class this project keeps hitting.
        structureTraversalIntent.egressWaypointWorld = ejection;
        structureTraversalIntent.egressAttempts = cellEgressAttempts;
        setStructureTraversalPhase(StructureTraversalPhase::Egress,
            preserveTraversal ? "internal_egress" : "egress");
    }

    destination = ejection;
    destinationLocal = ejection;
    destinationCell = nullptr;
    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH;
    uint64 movementGeneration = advanceWorkLoopGeneration("cellEgress");

    if (isCellNavDiagAgent(agent.get()))
        CellNavDiagLog::write("CELL_EGRESS_BEGIN cell=" +
            String::valueOf(cell->getObjectID()) + " ejection=" +
            ejection.toString());

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(ejection, nullptr);
    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord,
        endCoord, zone, movementGeneration);
    task->schedule(100);
    return true;
}

bool SimPlayerController::buildCellEgressExitSet(
        bool hollowDoorEgressTelemetry) {
    SimPlayerManager* manager = SimPlayerManager::instance();
    bool hollowDoorEgressObserve = manager != nullptr && manager->
        isStructureTraversalHollowDoorEgressObserveEnabled();
    bool farSideEgress = manager != nullptr && manager->
        isStructureTraversalFarSideEgressEnabled();
    if (manager == nullptr ||
            (!manager->isStructureTraversalZeroClipExitSetEnabled() &&
                !hollowDoorEgressObserve && !farSideEgress) ||
            agent == nullptr || agent->getZone() == nullptr ||
            cellEgressExitSetBuilt)
        return false;

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    CellObject* cell = parent == nullptr || !parent->isCellObject() ? nullptr :
        cast<CellObject*>(parent.get());
    Zone* zone = agent->getZone();
    ManagedReference<BuildingObject*> building;
    int sourceCellNumber = 0;
    if (cell != nullptr) {
        building = cell->getParent().get().castTo<BuildingObject*>();
        sourceCellNumber = cell->getCellNumber();
    } else {
        uint64 owningBuildingOid = structureTraversalIntent.owningBuildingOid;
        if (owningBuildingOid != 0) {
            ManagedReference<SceneObject*> object = zone->getZoneServer()->
                getObject(owningBuildingOid);
            building = object == nullptr ? nullptr :
                object->asBuildingObject();
        }
    }
    if (building == nullptr)
        return false;

    cellEgressExitSetBuilt = true;
    cellEgressCandidates.removeAll();
    cellEgressCandidateLocals.removeAll();
    cellEgressCandidateCellIndexes.removeAll();
    cellEgressCandidateInHollow.removeAll();
    manager->recordZeroClipExitSetBuilt();

    bool worldPortalCorroborated = false;
    int cellsVisited = 0;
    int cellsWithWorldPortal = 0;
    int rejectedBounds = 0;
    int rejectedDuplicate = 0;
    int rejectedElevation = 0;
    int exteriorNodes = 0;
    int globalNodes = 0;
    int entrancesRaw = 0;
    int candidateSourceCount = 0;
    int nodeTypeCounts[PathNode::Invalid + 1] = {0};
        Vector<Vector3> retainedEntranceModels;
        String unavailableReason;
        Vector<const PathNode*> entrances;
    {
        // Deliberately NO building Locker. Everything read below --
        // getObjectTemplate/getPortalLayout/getFloorMesh/getPathGraph/
        // getCellProperty -- is immutable template data loaded at boot, and the
        // stock readers of exactly these members take no lock either
        // (PathFinderManager::findPathFromCellToWorld,
        // CollisionManager::checkLineOfSightInBuilding). Taking one here would
        // add an agent->building lock-order edge that nothing else in this path
        // establishes, for no protection. Do not "fix" this by adding a Locker.
        SharedObjectTemplate* objectTemplate = building->getObjectTemplate();
        const PortalLayout* portalLayout = objectTemplate == nullptr ? nullptr :
            objectTemplate->getPortalLayout();
        bool useCellPortals = hollowDoorEgressTelemetry && manager->
            isStructureTraversalHollowDoorEgressUseCellPortalsEnabled();
        const FloorMesh* exteriorFloorMesh = nullptr;
        if (portalLayout == nullptr) {
            unavailableReason = "no_portal_layout";
        } else if (!useCellPortals) {
            if (portalLayout->getFloorMeshNumber() <= 0) {
                unavailableReason = "no_floor_mesh";
            } else {
                exteriorFloorMesh = portalLayout->getFloorMesh(0);
                if (exteriorFloorMesh == nullptr)
                    unavailableReason = "no_floor_mesh";
            }
        }
        const PathGraph* exteriorPathGraph = exteriorFloorMesh == nullptr ?
            nullptr : exteriorFloorMesh->getPathGraph();
        if (!useCellPortals && exteriorPathGraph == nullptr &&
                unavailableReason.isEmpty())
            unavailableReason = "no_exterior_path_graph";

        if (exteriorPathGraph != nullptr) {
            const Vector<PathNode*>* pathNodes =
                exteriorPathGraph->getPathNodes();
            exteriorNodes = pathNodes == nullptr ? 0 : pathNodes->size();

            if (pathNodes != nullptr) {
                for (int i = 0; i < pathNodes->size(); ++i) {
                    const PathNode* node = pathNodes->get(i);
                    if (node == nullptr)
                        continue;

                    if (node->getGlobalGraphNodeID() != -1)
                        ++globalNodes;

                    int nodeType = static_cast<int>(node->getType());
                    if (nodeType >= 0 && nodeType <= PathNode::Invalid)
                        ++nodeTypeCounts[nodeType];
                }
            }
        }

        // The path graph is authoritative. A worldPortal BFS is only a
        // corroborating observation because its <= 1 convention has no
        // consumers and must never select a door by itself.
        if (portalLayout != nullptr) {
            Vector<int> pending;
            SortedVector<int> visited;
            pending.add(sourceCellNumber);
            visited.put(sourceCellNumber);

            for (int pendingIndex = 0; pendingIndex < pending.size();
                    ++pendingIndex) {
                int cellId = pending.get(pendingIndex);
                const CellProperty* property = cellId >= 0 &&
                    cellId < portalLayout->getCellProperties().size() ?
                    portalLayout->getCellProperty(cellId) : nullptr;
                if (property == nullptr)
                    continue;

                ++cellsVisited;

                if (property->hasWorldPortal()) {
                    worldPortalCorroborated = true;
                    ++cellsWithWorldPortal;
                }

                const SortedVector<int>& connected =
                    property->getConnectedCells();
                for (int connectedIndex = 0;
                        connectedIndex < connected.size(); ++connectedIndex) {
                    int connectedCell = connected.get(connectedIndex);
                    if (!visited.contains(connectedCell)) {
                        visited.put(connectedCell);
                        pending.add(connectedCell);
                    }
                }
            }
        }

        if (exteriorPathGraph != nullptr) {
            // MEASURED 2026-08-21 (live_d7-nodetype-diagnosis): a BUILDING's
            // exterior path graph contains ZERO BuildingEntrance (type 3) nodes
            // -- its doors are typed CellPortal (type 0). Histogram over two real
            // buildings: CellPortal=8, CellWaypoint=25, BuildingEntrance=0.
            // PathGraph::getEntrances() filters on type 3, so it can only ever
            // return empty here; type 3 is a CITY-level path-graph concept.
            //
            // getEntrances() is still CALLED, but only so entrancesRaw stays in
            // the telemetry next to doorNodes -- it no longer selects anything.
            // It is stock code and may well be correct for city graphs, so it is
            // deliberately not changed, only stopped from being relied on here.
            entrancesRaw = exteriorPathGraph->getEntrances().size();

            const Vector<PathNode*>* pathNodes =
                exteriorPathGraph->getPathNodes();

            if (pathNodes != nullptr) {
                for (int i = 0; i < pathNodes->size(); ++i) {
                    const PathNode* node = pathNodes->get(i);

                    if (node == nullptr ||
                            node->getType() != PathNode::CellPortal)
                        continue;

                    entrances.add(node);
                }
            }
        }

        auto resolveExteriorPortal = [portalLayout, building] (
                const Vector3& candidate, Vector3& doorLocal,
                int& targetCellIndex, float& nearestPortalDist) {
            doorLocal = Vector3(0, 0, 0);
            targetCellIndex = -1;
            nearestPortalDist = -1.f;

            if (portalLayout == nullptr ||
                    portalLayout->getCellProperties().size() <= 0)
                return;

            const CellProperty* exterior = portalLayout->getCellProperty(0);
            if (exterior == nullptr)
                return;

            bool found = false;
            for (int portalIndex = 0;
                    portalIndex < exterior->getNumberOfPortals();
                    ++portalIndex) {
                const CellPortal* portal = exterior->getPortal(portalIndex);
                if (portal == nullptr)
                    continue;

                // Portal geometry is Y-UP MODEL space. Keep this conversion
                // identical to PathFinderManager Retry 5: center() is the
                // doorway's vertical middle, so subtract the Y extent before
                // swapping to the cell-local (x, north, height) frame.
                const AABB& portalBounds = portalLayout->getPortalBounds(
                    portal->getGeometryIndex());
                Vector3 floorModel = portalBounds.center() -
                    Vector3(0, portalBounds.extents().getY(), 0);
                Vector3 portalLocal(floorModel.getX(), floorModel.getZ(),
                    floorModel.getY());
                Vector3 portalWorld = transformFromStructureModelSpace(
                    floorModel, building);
                float distance = candidate.distanceTo(portalWorld);

                if (!found || distance < nearestPortalDist) {
                    found = true;
                    nearestPortalDist = distance;
                    targetCellIndex = portal->getTargetCellIndex();
                    doorLocal = portalLocal;
                }
            }
        };

        Vector<Vector3> sourceWorlds;
        Vector<Vector3> sourceModels;
        Vector<Vector3> sourceLocals;
        Vector<int> sourceCellIndexes;
        Vector<float> sourcePortalDistances;
        if (useCellPortals && portalLayout != nullptr) {
            // CellProperty portal geometry is the authoritative doorway source
            // for this opt-in mode. Keep the exact stock Y-UP model conversion:
            // center minus the vertical extent is the floor, then transform it
            // to world space and swap (x, north, height) for cell-local space.
            for (int cellIndex = 0; cellIndex < portalLayout->
                    getCellProperties().size(); ++cellIndex) {
                const CellProperty* property = portalLayout->getCellProperty(
                    cellIndex);
                if (property == nullptr)
                    continue;

                for (int portalIndex = 0;
                        portalIndex < property->getNumberOfPortals();
                        ++portalIndex) {
                    const CellPortal* portal = property->getPortal(portalIndex);
                    if (portal == nullptr || portal->getTargetCellIndex() > 1)
                        continue;

                    const AABB& portalBounds = portalLayout->getPortalBounds(
                        portal->getGeometryIndex());
                    Vector3 floorModel = portalBounds.center() -
                        Vector3(0, portalBounds.extents().getY(), 0);
                    sourceModels.add(floorModel);
                    sourceWorlds.add(transformFromStructureModelSpace(
                        floorModel, building));
                    sourceLocals.add(Vector3(floorModel.getX(),
                        floorModel.getZ(), floorModel.getY()));
                    // Leg B walks the bot INTO the building, so the
                    // destination is the INTERIOR member of the portal pair.
                    // Portals are stored on both sides: the exterior owns one
                    // targeting an interior cell, and that interior cell owns
                    // the mirror targeting 0. Measured starport set is all
                    // "cell=15 targetCell=0" / "cell=16 targetCell=0", so
                    // getTargetCellIndex() alone yields 0 -- and getCell(0) is
                    // an explicit error path in BuildingObject.idl that prints
                    // a stack trace. getCell() takes a keyed PortalLayout cell
                    // index directly (no -1): Retry 6 feeds the same index to
                    // getCell() and getFloorMesh().
                    int targetIndex = portal->getTargetCellIndex();
                    sourceCellIndexes.add(targetIndex == 0 ? cellIndex :
                        targetIndex);
                    sourcePortalDistances.add(0.f);
                }
            }
        } else if (!useCellPortals) {
            // Gate-off behavior remains the PathGraph CellPortal source. The
            // portal lookup here is only the existing Leg B enrichment; it is
            // not consulted at all for the zero-clip-only path.
            for (int i = 0; i < entrances.size(); ++i) {
                const PathNode* entrance = entrances.get(i);
                if (entrance == nullptr)
                    continue;

                Vector3 model = entrance->getPosition();
                Vector3 candidate = transformFromStructureModelSpace(model,
                    building);
                sourceModels.add(model);
                sourceWorlds.add(candidate);
                sourceLocals.add(Vector3(0, 0, 0));
                sourceCellIndexes.add(-1);
                sourcePortalDistances.add(-1.f);
            }
        }
        candidateSourceCount = useCellPortals ? sourceWorlds.size() :
            entrances.size();

        Vector3 agentWorld = agent->getWorldPosition();
        float maxVertical = manager->
            getStructureTraversalZeroClipExitCandidateMaxVerticalMeters();

        for (int i = 0; i < sourceWorlds.size(); ++i) {
            Vector3 candidate = sourceWorlds.get(i);
            if (!zone->isWithinBoundaries(candidate)) {
                ++rejectedBounds;

                if (manager->isStructureTraversalZeroClipLoggingEnabled())
                    StructureTraversalDiagLog::write(
                        "ST_EGRESS exitSet=rejected reason=out_of_bounds world=" +
                        String::valueOf(candidate.getX()) + "," +
                        String::valueOf(candidate.getY()) + "," +
                        String::valueOf(candidate.getZ()));

                continue;
            }

            // A CellPortal set spans every level of the POB. A door tens of
            // metres above or below the bot's own floor is not an exit it can
            // walk to -- targeting one drove the bot vertically and tripped the
            // z-sanity watchdog 240 times on 2026-08-23. Reject by elevation
            // BEFORE ordering, so such a door can never be chosen at all.
            if (maxVertical > 0.f &&
                    fabs(candidate.getZ() - agentWorld.getZ()) >
                        maxVertical) {
                ++rejectedElevation;

                // Log the REJECTED door too. Reporting only survivors hides the
                // very information a diagnosis needs -- a door cut by a
                // threshold I chose by hand is exactly what must stay visible,
                // or the filter silently decides the answer.
                if (manager->isStructureTraversalZeroClipLoggingEnabled())
                    StructureTraversalDiagLog::write(
                        "ST_EGRESS exitSet=rejected reason=elevation world=" +
                        String::valueOf(candidate.getX()) + "," +
                        String::valueOf(candidate.getY()) + "," +
                        String::valueOf(candidate.getZ()) + " dz=" +
                        String::valueOf(candidate.getZ() -
                            agentWorld.getZ()) + " dist=" +
                        String::valueOf(candidate.distanceTo(agentWorld)) +
                        " maxVertical=" + String::valueOf(maxVertical));

                continue;
            }

            bool duplicate = false;
            // 3D, deliberately: distanceTo2d discards the vertical axis, which
            // is exactly how a door 77m up came to be ranked nearest.
            float candidateDistance = candidate.distanceTo(agentWorld);
            int insertIndex = cellEgressCandidates.size();
            for (int existing = 0; existing < cellEgressCandidates.size();
                    ++existing) {
                Vector3 existingCandidate = cellEgressCandidates.get(existing);
                if (candidate.distanceTo(existingCandidate) < 0.01f) {
                    duplicate = true;
                    ++rejectedDuplicate;
                    // Both halves of a portal pair resolve to the same
                    // doorway, and whichever is seen first wins the slot. If
                    // that one could not name an interior cell, adopt this
                    // one's -- dropping it strands arrival on
                    // target_cell_index_unresolved.
                    if (existing < cellEgressCandidateCellIndexes.size() &&
                            cellEgressCandidateCellIndexes.get(existing) <= 0 &&
                            sourceCellIndexes.get(i) > 0) {
                        cellEgressCandidateCellIndexes.setElementAt(existing,
                            sourceCellIndexes.get(i));
                        if (existing < cellEgressCandidateLocals.size())
                            cellEgressCandidateLocals.setElementAt(existing,
                                sourceLocals.get(i));
                    }
                    break;
                }
                float existingDistance = existingCandidate.distanceTo(
                    agentWorld);
                if (insertIndex == cellEgressCandidates.size() &&
                        candidateDistance < existingDistance)
                    insertIndex = existing;
            }

            if (!duplicate) {
                Vector3 doorLocal = sourceLocals.get(i);
                int targetCellIndex = sourceCellIndexes.get(i);
                float nearestPortalDist = sourcePortalDistances.get(i);
                if (hollowDoorEgressTelemetry && !useCellPortals)
                    resolveExteriorPortal(candidate, doorLocal,
                        targetCellIndex, nearestPortalDist);
                cellEgressCandidates.add(insertIndex, candidate);
                cellEgressCandidateLocals.add(insertIndex, doorLocal);
                cellEgressCandidateCellIndexes.add(insertIndex,
                    targetCellIndex);
                int inHollow = hollowDoorEgressTelemetry &&
                    isWithinOwningBuildingHollowAt(candidate, building) ? 1 : 0;
                cellEgressCandidateInHollow.add(insertIndex, inHollow);
                retainedEntranceModels.add(insertIndex, sourceModels.get(i));

                if (hollowDoorEgressTelemetry)
                    StructureTraversalDiagLog::write(
                        "ST_HOLLOW doorEgress cellResolve door=" +
                        String::valueOf(candidate.getX()) + "," +
                        String::valueOf(candidate.getY()) + "," +
                        String::valueOf(candidate.getZ()) +
                        " nearestPortalDist=" +
                        String::valueOf(nearestPortalDist) +
                        " targetCellIndex=" +
                        String::valueOf(targetCellIndex));
            }
        }
    }

    if (hollowDoorEgressTelemetry ||
            manager->isStructureTraversalZeroClipLoggingEnabled()) {
        auto writeExitSetTelemetry = [hollowDoorEgressTelemetry] (
                const String& line) {
            if (hollowDoorEgressTelemetry)
                StructureTraversalDiagLog::write(line);
            else
                StructureTraversalDiagLog::writeZeroClip(line);
        };
        if (!unavailableReason.isEmpty())
            writeExitSetTelemetry(
                "ST_EGRESS exitSet=unavailable reason=" + unavailableReason);

        if (unavailableReason.isEmpty()) {
            writeExitSetTelemetry(
                "ST_EGRESS exitSet=graph building=" +
                String::valueOf(building->getObjectID()) + " cellNumber=" +
                String::valueOf(sourceCellNumber) + " exteriorNodes=" +
                String::valueOf(exteriorNodes) + " globalNodes=" +
                String::valueOf(globalNodes) + " entrancesRaw=" +
                String::valueOf(entrancesRaw) + " doorNodes=" +
                String::valueOf(candidateSourceCount));

            String nodeTypesLine = "ST_EGRESS exitSet=nodeTypes";
            for (int nodeType = 0; nodeType <= PathNode::Invalid; ++nodeType) {
                if (nodeTypeCounts[nodeType] == 0)
                    continue;

                nodeTypesLine += " " + PathNode::typeToString(
                    static_cast<PathNode::PathNodeType>(nodeType)) + "=" +
                    String::valueOf(nodeTypeCounts[nodeType]);
            }
            writeExitSetTelemetry(nodeTypesLine);

            int candidateLogCount = cellEgressCandidates.size() < 8 ?
                cellEgressCandidates.size() : 8;
            Vector3 agentWorld = agent->getWorldPosition();
            for (int i = 0; i < candidateLogCount; ++i) {
                const Vector3& model = retainedEntranceModels.get(i);
                const Vector3& world = cellEgressCandidates.get(i);
                String candidateLine =
                    "ST_EGRESS exitSet=candidate index=" +
                    String::valueOf(i) + " model=" +
                    String::valueOf(model.getX()) + "," +
                    String::valueOf(model.getY()) + "," +
                    String::valueOf(model.getZ()) + " world=" +
                    String::valueOf(world.getX()) + "," +
                    String::valueOf(world.getY()) + "," +
                    String::valueOf(world.getZ()) + " distFromBot=" +
                    String::valueOf(world.distanceTo(agentWorld)) + " dz=" +
                    String::valueOf(world.getZ() - agentWorld.getZ());
                if (hollowDoorEgressTelemetry)
                    candidateLine += " inHollow=" + String::valueOf(
                        cellEgressCandidateInHollow.get(i));
                writeExitSetTelemetry(candidateLine);
            }
        }

        writeExitSetTelemetry(
            "ST_EGRESS exitSet=build candidates=" +
            String::valueOf(cellEgressCandidates.size()) +
            " worldPortalCorroborated=" +
            String::valueOf(worldPortalCorroborated) + " entrancesRaw=" +
            String::valueOf(entrancesRaw) + " rejectedBounds=" +
            String::valueOf(rejectedBounds) + " rejectedDuplicate=" +
            String::valueOf(rejectedDuplicate) + " rejectedElevation=" +
            String::valueOf(rejectedElevation) + " cellsVisited=" +
            String::valueOf(cellsVisited) + " cellsWithWorldPortal=" +
            String::valueOf(cellsWithWorldPortal));
    }

    return cellEgressCandidates.size() > 0;
}

bool SimPlayerController::startNextCellEgressCandidate() {
    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager == nullptr || !manager->isStructureTraversalZeroClipExitSetEnabled() ||
            !cellEgressExitSetBuilt || agent == nullptr ||
            agent->getZone() == nullptr)
        return false;

    int perCandidateCap = manager->
        getStructureTraversalZeroClipEgressCandidateAttemptCap();
    int totalCeiling = manager->
        getStructureTraversalZeroClipEgressTotalAttemptCeiling();
    if (perCandidateCap < 1)
        perCandidateCap = 1;
    if (totalCeiling < 1)
        totalCeiling = 1;

    if (cellEgressCandidates.size() == 0)
        return false;

    while (cellEgressCandidateIndex < cellEgressCandidates.size() &&
            cellEgressTotalAttempts < totalCeiling) {
        if (cellEgressCandidateAttempts >= perCandidateCap) {
            cellEgressCandidateIndex++;
            cellEgressCandidateAttempts = 0;
            continue;
        }

        Vector3 candidate = cellEgressCandidates.get(cellEgressCandidateIndex);
        cellEgressCandidateAttempts++;
        cellEgressTotalAttempts++;
        manager->recordZeroClipExitCandidateTried();

        destination = candidate;
        destinationLocal = candidate;
        destinationCell = nullptr;
        stuckWatchdogCount = 0;
        lastWatchdogPos = agent->getWorldPosition();
        state = CALCULATING_PATH;
        uint64 movementGeneration = advanceWorkLoopGeneration(
            "cellEgressExitSetCandidate");

        WorldCoordinates startCoord(agent);
        WorldCoordinates endCoord(candidate, nullptr);
        Reference<SimPathFindTask*> task = new SimPathFindTask(this,
            startCoord, endCoord, agent->getZone(), movementGeneration);
        task->schedule(100);
        return true;
    }

    if (!cellEgressBudgetExhaustedRecorded) {
        cellEgressBudgetExhaustedRecorded = true;
        manager->recordZeroClipEgressCandidateBudgetExhausted();
    }
    return false;
}

void SimPlayerController::onPathFound(Vector<WorldCoordinates>* path,
        bool pathUsesNavmesh, bool pathIsOverland) {
    // Cleared on entry, set only where the path is actually taken below.
    probePathAccepted = false;

    if (agent == nullptr) { if (path) delete path; return; }

    bool diagnostic = isCellNavDiagAgent(agent.get());
    if (diagnostic) {
        CellNavDiagLog::write("PATH_FOUND_ENTRY usesNavmesh=" +
            String::valueOf(pathUsesNavmesh) + " overland=" +
            String::valueOf(pathIsOverland) + " nodes=" +
            String::valueOf(path == nullptr ? 0 : path->size()));

        if (path != nullptr) {
            for (int i = 0; i < path->size(); ++i) {
                CellNavDiagLog::write("PATH_NODE index=" + String::valueOf(i) +
                    " " + CellNavDiagLog::fmtPos(path->get(i)));
            }
        }
    }

    if (isHybridMovementActive() && !shouldResumeHybridTravel()) {
        // The order completed/abandoned or entered lair cleanup while this path
        // was in flight. Drop the result instead of re-entering MOVING toward a
        // finished target. (Cancellation closes the resume gate before disengage,
        // so this is the deterministic last line against a stale in-flight task.)
        if (path) delete path;
        state = IDLE;
        if (diagnostic)
            CellNavDiagLog::write("PATH_REJECT reason=hybrid_resume_cancelled");
        return;
    }

    if (isHybridMovementActive()) {
        bool expectsNavmesh = hybridLeg == HYBRID_LEG_NAVMESH_FINAL ||
            hybridLeg == HYBRID_LEG_NAVMESH_EXIT;
        if (expectsNavmesh != pathUsesNavmesh) {
            delete path;
            if (diagnostic)
                CellNavDiagLog::write("PATH_REJECT reason=hybrid_mode_mismatch");
            onPathTaskFailed(expectsNavmesh);
            return;
        }
    }

    if (isHybridMovementActive() && pathIsOverland &&
            hybridLeg == HYBRID_LEG_OVERLAND_FINAL &&
            agent->isInNavMesh()) {
        // A direct task can outlive the boundary tick that scheduled it. Do
        // not accept an overland route after the agent has entered a mesh.
        delete path;
        onMeshMode = true;
        navmeshModeDebounceCounter = 0;
        if (diagnostic)
            CellNavDiagLog::write("PATH_REJECT reason=overland_result_on_mesh");
        requestHybridPath();
        return;
    }
    
    bool combatDriverResult = combatDriverMoveActive;
    bool traversalCombatBusy = agent->isInCombat() ||
        isCombatDriverActive();
    if (traversalCombatBusy && isStructureTraversalFeatureEnabled() &&
            isTraversalActive() && !combatDriverResult) {
        if (path) delete path;
        state = IDLE;
        pauseStructureTraversal("path_found_combat");
        if (diagnostic)
            CellNavDiagLog::write("PATH_REJECT reason=traversal_combat_pause");
        return;
    }

    if (agent->isInCombat() &&
            !(combatDriverResult && isStructureTraversalFeatureEnabled() &&
                isTraversalActive())) {
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer onPathFound: Path found but Agent is in Combat. Holding.", true);
#endif
        if (path) delete path;
        state = IDLE;
        if (diagnostic)
            CellNavDiagLog::write("PATH_REJECT reason=in_combat");
        return;
    }

    if (path == nullptr || path->size() < 2) {
        if (combatDriverResult && isStructureTraversalFeatureEnabled() &&
                isTraversalActive()) {
            combatDriverMoveActive = false;
            if (path) delete path;
            state = IDLE;
            return;
        }
        if (path) delete path;
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer onPathFound: Path too short. Retrying in 5s.", true);
#endif
        if (isHybridMovementActive() && pathUsesNavmesh)
            onPathTaskFailed(true);
        else if (cellEgressActive)
            failCellEgress();
        else
            onPathFailed();
        if (diagnostic)
            CellNavDiagLog::write("PATH_REJECT reason=short_path");
        return;
    }

    // P.6.1b: reject a path that does not end where the current destination
    // points (stale result that slipped a generation race). The retry path
    // recomputes against the correct target.
    if (!acceptFoundPath(path->get(path->size() - 1).getWorldPosition())) {
        delete path;
        if (diagnostic)
            CellNavDiagLog::write("PATH_REJECT reason=stale_path_end");
        if (farSideRejectionPending) {
            farSideRejectionPending = false;
            if (tryStartFarSideInteriorLeg())
                return;
        }
        if (combatDriverMoveActive && isStructureTraversalFeatureEnabled() &&
                isTraversalActive()) {
            combatDriverMoveActive = false;
            state = IDLE;
            return;
        }
        if (cellEgressActive)
            failCellEgress();
        else
            onPathFailed();
        return;
    }

    state = MOVING;
    probePathAccepted = true;
    // The budget is per stretch of blocked routing, not per lifetime: a route
    // the bot actually walks means the pathfinder is producing usable answers
    // again, so the next obstruction gets a full set of rejections.
    zeroClipRejections = 0;
    combatDriverMoveActive = false;
    simPath.removeAll();
    simPathIndex = 0;

    for (int i = 0; i < path->size(); ++i) {
        simPath.add(path->get(i));
    }

    WorldCoordinates finalPoint = simPath.get(simPath.size() - 1);
    destination = finalPoint.getWorldPosition();
    destinationLocal = finalPoint.getPoint();
    destinationCell = finalPoint.getCell();

    if (isStructureTraversalFeatureEnabled() && isTraversalActive()) {
        if (!combatDriverResult) {
            if (cellEgressActive) {
                setStructureTraversalPhase(StructureTraversalPhase::Egress,
                    "egress_path_found");
            } else if (structureTraversalPhase !=
                    StructureTraversalPhase::Reentry) {
                ManagedReference<SceneObject*> currentParent =
                    agent->getParent().get();
                if (currentParent != nullptr && currentParent->isCellObject())
                    setStructureTraversalPhase(
                        StructureTraversalPhase::InteriorRoute,
                        "interior_path_found");
                else if (destinationCell != nullptr)
                    setStructureTraversalPhase(
                        StructureTraversalPhase::ApproachDoor,
                        "entry_path_found");
            }
        }
        StructureTraversalDiagLog::write(
            "ST_PATH result=accepted agent=" +
            String::valueOf(agent->getObjectID()) + " generation=" +
            String::valueOf(traversalGeneration) + " building=" +
            String::valueOf(structureTraversalIntent.owningBuildingOid) +
            " cell=" + String::valueOf(getTraversalTargetCellOid()) +
            " nodes=" +
            String::valueOf(path->size()) + " " +
            StructureTraversalDiagLog::fmtPos(finalPoint));
    }

    if (isHybridMovementActive()) {
        onMeshMode = agent->isInNavMesh();
        navmeshModeDebounceCounter = 0;
        navmeshRepathAttempts = 0;
    }
#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer onPathFound: Path Found (" + String::valueOf(path->size()) + " nodes). Moving...", true);
#endif
    if (diagnostic)
        CellNavDiagLog::write("PATH_ACCEPTED destination=" +
            CellNavDiagLog::fmtPos(finalPoint));
    agent->setHomeLocation(finalPoint.getX(), finalPoint.getZ(),
        finalPoint.getY(), finalPoint.getCell());

    bool preserveCombatDriver = combatDriverResult &&
        isStructureTraversalFeatureEnabled() && isTraversalActive();
    if (!preserveCombatDriver) {
        agent->setFollowObject(nullptr);
        agent->setWatchObject(nullptr);
        agent->setTargetObject(nullptr);
        agent->clearCombatState(true);
    }
    agent->clearPatrolPoints();
    agent->clearSavedPatrolPoints();
    // P.6.1d: invalidate the AGENT's cached A* route. findNextPosition
    // (AiAgentImplementation) reuses currentFoundPath while PATROLLING WITHOUT
    // re-checking it still matches the current patrol target, so a route left
    // over from a previous leg (e.g. before a switchZone teleport) would be
    // followed toward the OLD destination even though we just queued a fresh
    // path here. Nulling it forces a re-pathfind to the new patrol[0].
    agent->clearCurrentPath();
    agent->stopWaiting();

    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));

    queueMorePathNodes();

    if (agent->getPatrolPointSize() > 0) {
        PatrolPoint next = agent->getNextPosition();
        agent->setNextStepPosition(next.getPositionX(), next.getPositionZ(), next.getPositionY(), next.getCell());
    }

    agent->setMovementState(AiAgent::PATROLLING);
    agent->activateAiBehavior(true);

    delete path;

    // Ensure loop is active
    Reference<ArrivalCheckTask*> task =
        new ArrivalCheckTask(this, getWorkLoopGeneration());
    task->schedule(500); 
}

void SimPlayerController::onPathTaskFailed(bool pathUsesNavmesh) {
    if (isCellNavDiagAgent(agent.get()))
        CellNavDiagLog::write("PATH_TASK_FAILED usesNavmesh=" +
            String::valueOf(pathUsesNavmesh) + " hybrid=" +
            String::valueOf(isHybridMovementActive()));

    if (combatDriverMoveActive && isStructureTraversalFeatureEnabled() &&
            isTraversalActive()) {
        combatDriverMoveActive = false;
        state = IDLE;
        return;
    }

    if (cellEgressActive) {
        if (isStructureTraversalFeatureEnabled())
            SimPlayerManager::instance()->
                recordStructureTraversalEgressPathFailure();
        failCellEgress();
        return;
    }

    if (!isHybridMovementActive() || !pathUsesNavmesh) {
        onPathFailed();
        return;
    }

    int retryBudget = SimPlayerManager::instance()->getPveNavmeshRepathTries();
    if (navmeshRepathAttempts < retryBudget) {
        navmeshRepathAttempts++;
        requestHybridPath();
        return;
    }

    navmeshRepathAttempts = 0;
    onPathFailed();
}

bool SimPlayerController::findNavAreaAt(Zone* zone, const Vector3& position,
        ManagedReference<NavArea*>& area) const {
    if (zone == nullptr)
        return false;

    SortedVector<ManagedReference<NavArea*> > areas;
    zone->getInRangeNavMeshes(position.getX(), position.getY(), &areas, false);

    for (int i = 0; i < areas.size(); ++i) {
        ManagedReference<NavArea*> candidate = areas.get(i);
        if (candidate != nullptr &&
                candidate->containsPoint(position.getX(), position.getY())) {
            area = candidate;
            return true;
        }
    }

    return false;
}

bool SimPlayerController::resolveHybridExit(Zone* zone,
        const Vector3& currentPosition, Vector3& boundary,
        Vector3& egress, ManagedReference<NavArea*>& area) const {
    if (zone == nullptr || !hasFinalDestination)
        return false;

    // getNavMeshCollisions discards rays whose origin is already inside the
    // mesh (tca < 0). Cast from the wilderness destination back toward the
    // hunter so the first collision is the reliable exit boundary.
    SortedVector<ManagedReference<NavArea*> > areas;
    zone->getInRangeNavMeshes(currentPosition.getX(), currentPosition.getY(),
        &areas, true);
    if (areas.size() == 0)
        return false;

    SortedVector<NavCollision*> collisions;
    PathFinderManager::instance()->getNavMeshCollisions(&collisions, &areas,
        finalDestination, currentPosition);

    Vector3 collisionPosition;
    NavArea* selectedArea = nullptr;
    for (int i = 0; i < collisions.size(); ++i) {
        NavCollision* collision = collisions.get(i);
        if (collision == nullptr)
            continue;

        NavArea* collisionArea = collision->getNavArea();
        if (selectedArea == nullptr || collisionArea == area.get()) {
            selectedArea = collisionArea;
            collisionPosition = collision->getPosition();
            if (collisionArea == area.get())
                break;
        }
    }

    for (int i = 0; i < collisions.size(); ++i)
        delete collisions.get(i);

    if (selectedArea == nullptr)
        return false;

    area = selectedArea;
    collisionPosition.setZ(CollisionManager::getWorldFloorCollision(
        collisionPosition.getX(), collisionPosition.getY(), zone, true));
    if (!zone->isWithinBoundaries(collisionPosition))
        return false;

    Vector3 outward = finalDestination - collisionPosition;
    outward.setZ(0.f);
    float outwardLength = outward.length2d();
    if (outwardLength < 0.001f)
        return false;
    outward.normalize();

    // NavCollision is deliberately just inside the mesh. Probe far enough to
    // clear the complete active-area radius, but keep the search bounded so a
    // malformed mesh cannot turn a movement request into an unbounded loop.
    const float probeStep = 8.f;
    const int maxProbeSteps = 128;
    for (int step = 1; step <= maxProbeSteps; ++step) {
        Vector3 candidate = collisionPosition + outward *
            (probeStep * static_cast<float>(step));
        candidate.setZ(CollisionManager::getWorldFloorCollision(
            candidate.getX(), candidate.getY(), zone, true));

        if (!zone->isWithinBoundaries(candidate))
            continue;

        SortedVector<ManagedReference<NavArea*> > candidateAreas;
        zone->getInRangeNavMeshes(candidate.getX(), candidate.getY(),
            &candidateAreas, false);

        // The candidate is valid only after both the ground/water snap and
        // the nav-region query agree that it is outside every NavArea.
        if (candidateAreas.size() == 0) {
            egress = candidate;
            boundary = collisionPosition;
            return true;
        }
    }

    return false;
}

void SimPlayerController::scheduleHybridDirectPath(const Vector3& target,
        HybridLeg leg) {
    if (agent == nullptr || agent->getZone() == nullptr ||
            !hasFinalDestination || !isHybridMovementActive())
        return;

    Zone* zone = agent->getZone();
    if (!zone->isWithinBoundaries(target)) {
        onPathTaskFailed(false);
        return;
    }

    destination = target;
    destinationLocal = target;
    destinationCell = nullptr;
    hybridLeg = leg;
    stuckWatchdogCount = 0;
    lastWatchdogPos = agent->getWorldPosition();
    state = CALCULATING_PATH;
    uint64 movementGeneration = advanceWorkLoopGeneration(
        "hybridDirectOverland");

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(target, nullptr);
    refreshProbeRayHeight();
    float rayHeight = getProbeRayHeight();
    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord,
        endCoord, zone, true, leg != HYBRID_LEG_EGRESS,
        rayHeight, agent->getObjectID(), movementGeneration);
    task->schedule(100);
}

void SimPlayerController::requestHybridPath() {
    if (agent == nullptr || agent->getZone() == nullptr ||
            !hasFinalDestination) {
        onPathFailed();
        return;
    }

    Vector3 requestWorld = finalDestination;
    Vector3 requestLocal = finalDestination;
    ManagedReference<CellObject*> requestCell;
    if (destinationCell != nullptr) {
        requestWorld = destination;
        requestLocal = destinationLocal;
        requestCell = destinationCell;
    }

    if (beginCellEgressIfNeeded(requestWorld, requestLocal,
            requestCell.get()))
        return;

    if (!isHybridMovementActive()) {
        onPathFailed();
        return;
    }

    Zone* zone = agent->getZone();
    Vector3 currentPosition = agent->getWorldPosition();

    if (!onMeshMode) {
        scheduleHybridDirectPath(finalDestination,
            HYBRID_LEG_OVERLAND_FINAL);
        return;
    }

    ManagedReference<NavArea*> currentArea;
    if (!findNavAreaAt(zone, currentPosition, currentArea)) {
        onPathTaskFailed(true);
        return;
    }

    ManagedReference<NavArea*> targetArea;
    bool targetOnSameMesh = findNavAreaAt(zone, finalDestination, targetArea) &&
        targetArea == currentArea;

    Vector3 pathEnd = finalDestination;
    HybridLeg leg = HYBRID_LEG_NAVMESH_FINAL;
    if (!targetOnSameMesh) {
        Vector3 boundary;
        Vector3 egress;
        ManagedReference<NavArea*> exitArea;
        if (!resolveHybridExit(zone, currentPosition, boundary, egress,
                exitArea)) {
            onPathTaskFailed(true);
            return;
        }

        // The resolved area is the one used for the recast leg. This also
        // makes the area provenance explicit instead of inferring it from
        // path node count.
        currentArea = exitArea;
        pathEnd = boundary;
        hybridEgressPoint = egress;
        leg = HYBRID_LEG_NAVMESH_EXIT;
    } else {
        hybridEgressPoint = Vector3(0, 0, 0);
    }

    destination = pathEnd;
    destinationLocal = pathEnd;
    destinationCell = nullptr;
    hybridLeg = leg;
    stuckWatchdogCount = 0;
    lastWatchdogPos = currentPosition;
    state = CALCULATING_PATH;
    uint64 movementGeneration = advanceWorkLoopGeneration(
        "hybridRecastPath");

    WorldCoordinates startCoord(agent);
    WorldCoordinates endCoord(pathEnd, nullptr);
    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord,
        endCoord, zone, currentArea, currentPosition, pathEnd, false,
        movementGeneration);
    task->schedule(100);
}

void SimPlayerController::onPathFailed() {
    if (isCellNavDiagAgent(agent.get()))
        CellNavDiagLog::write("PATH_FAILED reason=base_retry interiorApproachLeg=" +
            String::valueOf(interiorApproachLeg));

    if (hollowEscalationActive && isStructureTraversalFeatureEnabled()) {
        hollowEscalationActive = false;
        hollowEscalationTarget = Vector3(0, 0, 0);
        SimPlayerManager::instance()->recordStructureTraversalHollowEscalationFailed();
        StructureTraversalDiagLog::write(
            "ST_EGRESS escalation=result status=still_inside agent=" +
            String::valueOf(agent == nullptr ? 0 : agent->getObjectID()) +
            " position=" + (agent == nullptr ? String("(null)") :
                agent->getWorldPosition().toString()));
    }

    if (combatDriverMoveActive && isStructureTraversalFeatureEnabled() &&
            isTraversalActive()) {
        combatDriverMoveActive = false;
        state = IDLE;
        return;
    }

    if (isStructureTraversalFeatureEnabled() && isTraversalActive() &&
            traversalResumeInProgress) {
        int resumeAttemptCap = SimPlayerManager::instance()->
            getStructureTraversalResumeAttemptCap();
        if (structureTraversalIntent.resumeAttempts < resumeAttemptCap) {
            structureTraversalIntent.resumeAttempts++;
            state = IDLE;
            simPath.removeAll();
            simPathIndex = 0;
            if (resumeStructureTraversalFromCurrentPosition())
                return;
        }

        traversalResumeInProgress = false;
        SimPlayerManager::instance()->recordStructureTraversalResumeFailure();
    }

#ifdef DEBUG_SIMPVP
    Logger::console.info("SimPlayer onPathFailed: Pathfinding failed/unreachable. Retrying in 5s...", true);
#endif
    if (isStructureTraversalFeatureEnabled() && isTraversalActive()) {
        StructureTraversalDiagLog::write(
            "ST_FAIL reason=path_failed agent=" +
            String::valueOf(agent == nullptr ? 0 : agent->getObjectID()) +
            " generation=" + String::valueOf(traversalGeneration));
        // Terminal traversal failure: the hybrid leg died with the path, so
        // drop finalDestination/hybridLeg here.  Derived controllers that
        // delegate to us for the traversal case (hunter, PvP) would otherwise
        // skip their own clearHybridMovementOnCancellation() and wedge until
        // the phase TTL fires.  Placed after the resume branch above, which
        // returns early and must keep its hybrid state for the retry.
        clearHybridMovementOnCancellation();
        clearStructureTraversalState("path_failed");
    }
    interiorApproachLeg = false;
    state = IDLE;

    Reference<SimRetryTask*> task =
        new SimRetryTask(this, getWorkLoopGeneration());
    task->schedule(5000); // 5 seconds
}

String SimPlayerController::getTraversalPhaseName() const {
    return getStructureTraversalPhaseName(structureTraversalPhase);
}

uint64 SimPlayerController::getTraversalTargetCellOid() const {
    return structureTraversalIntent.finalTargetCell == nullptr ? 0 :
        structureTraversalIntent.finalTargetCell->getObjectID();
}

bool SimPlayerController::isStructureTraversalFeatureEnabled() const {
    return SimPlayerManager::instance() != nullptr &&
        SimPlayerManager::instance()->isStructureTraversalEnabled();
}

bool SimPlayerController::tryStartFarSideInteriorLeg() {
    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager == nullptr ||
            !manager->isStructureTraversalFarSideEgressEnabled() ||
            agent == nullptr || agent->getZone() == nullptr)
        return false;

    // A previous exit-set observation may have left its one-shot state behind,
    // but it must not make a later traversal reuse stale candidates. Preserve
    // cellEgressAttempts: unlike the diagnostic candidate cursors, that counter
    // is the existing per-traversal egress budget.
    if (cellEgressActive || cellEgressExitSetBuilt)
        clearCellEgressState();

    int hollowEscalationAttemptCap = manager->
        getStructureTraversalHollowEscalationAttemptCap();
    int egressAttemptCap = manager->getStructureTraversalEgressAttemptCap();
    int candidateAttemptCap = manager->
        getStructureTraversalZeroClipEgressCandidateAttemptCap();
    int totalAttemptCeiling = manager->
        getStructureTraversalZeroClipEgressTotalAttemptCeiling();
    if (hollowEscalationAttemptCap < 1)
        hollowEscalationAttemptCap = 1;
    if (egressAttemptCap < 1)
        egressAttemptCap = 1;
    if (candidateAttemptCap < 1)
        candidateAttemptCap = 1;
    if (totalAttemptCeiling < 1)
        totalAttemptCeiling = 1;

    if (hollowEscalationAttempts >= hollowEscalationAttemptCap ||
            cellEgressAttempts >= egressAttemptCap ||
            cellEgressCandidateAttempts >= candidateAttemptCap ||
            cellEgressTotalAttempts >= totalAttemptCeiling)
        return false;

    if (!buildCellEgressExitSet(true)) {
        clearCellEgressState();
        return false;
    }

    int selectedIndex = -1;
    for (int i = 0; i < cellEgressCandidates.size(); ++i) {
        if (i < cellEgressCandidateInHollow.size() &&
                cellEgressCandidateInHollow.get(i) == 0) {
            selectedIndex = i;
            break;
        }
    }

    if (selectedIndex < 0 || selectedIndex >=
            cellEgressCandidateLocals.size() || selectedIndex >=
            cellEgressCandidateCellIndexes.size()) {
        clearCellEgressState();
        return false;
    }

    Zone* zone = agent->getZone();
    ManagedReference<SceneObject*> object = zone->getZoneServer()->getObject(
        structureTraversalIntent.owningBuildingOid);
    BuildingObject* building = object == nullptr ? nullptr :
        object->asBuildingObject();
    int cellIndex = cellEgressCandidateCellIndexes.get(selectedIndex);
    if (building == nullptr || cellIndex <= 0) {
        clearCellEgressState();
        return false;
    }

    CellObject* targetCell = building->getCell(cellIndex);
    if (targetCell == nullptr) {
        clearCellEgressState();
        return false;
    }

    Vector3 doorWorld = cellEgressCandidates.get(selectedIndex);
    Vector3 doorLocal = cellEgressCandidateLocals.get(selectedIndex);
    float distanceFromBot = doorWorld.distanceTo(agent->getWorldPosition());

    // This is a cell-targeted internal leg. In particular, do not call
    // moveToInterior(): its External origin clears the traversal state that
    // this leg belongs to.
    interiorApproachLeg = true;
    moveToWithOrigin(doorWorld, doorLocal, targetCell,
        TraversalMoveOrigin::Internal);

    if (state != CALCULATING_PATH) {
        interiorApproachLeg = false;
        clearCellEgressState();
        return false;
    }

    hollowEscalationAttempts++;
    cellEgressAttempts++;
    cellEgressCandidateAttempts++;
    cellEgressTotalAttempts++;
    StructureTraversalDiagLog::write(
        "ST_HOLLOW farSide action=interior_leg door=" +
        doorWorld.toString() + " cellIndex=" + String::valueOf(cellIndex) +
        " cellOid=" + String::valueOf(targetCell->getObjectID()) +
        " distance=" + String::valueOf(distanceFromBot));
    return true;
}

// Template method: invariants first, then whatever the subclass adds. A
// subclass cannot reach the invariants to skip them.
bool SimPlayerController::acceptFoundPath(const Vector3& pathEnd) {
    if (!acceptFoundPathInvariants(pathEnd))
        return false;

    return acceptFoundPathHook(pathEnd);
}

bool SimPlayerController::acceptFoundPathInvariants(const Vector3& pathEnd) {
    farSideRejectionPending = false;

    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager == nullptr ||
            !manager->isStructureTraversalFarSideEgressEnabled() ||
            !isStructureTraversalFeatureEnabled() || !isTraversalActive() ||
            !structureTraversalIntent.exitIntent)
        return true;

    Zone* zone = agent == nullptr ? nullptr : agent->getZone();
    if (zone == nullptr)
        return true;

    ManagedReference<SceneObject*> object = zone->getZoneServer()->getObject(
        structureTraversalIntent.owningBuildingOid);
    BuildingObject* building = object == nullptr ? nullptr :
        object->asBuildingObject();
    if (building == nullptr ||
            !isWithinOwningBuildingHollowAt(pathEnd, building) ||
            isWithinOwningBuildingHollowAt(destination, building))
        return true;

    farSideRejectionPending = true;
    StructureTraversalDiagLog::write(
        "ST_PATH result=rejected reason=far_side_no_progress agent=" +
        String::valueOf(agent->getObjectID()) + " generation=" +
        String::valueOf(traversalGeneration) + " pathEnd=" +
        pathEnd.toString() + " destination=" + destination.toString() +
        " building=" + String::valueOf(
            structureTraversalIntent.owningBuildingOid));
    return false;
}

uint64 SimPlayerController::advanceTraversalGeneration(const String& reason) {
    (void)reason;
    traversalGeneration++;
    if (traversalGeneration == 0)
        traversalGeneration = 1;
    return traversalGeneration;
}

void SimPlayerController::setStructureTraversalPhase(
        StructureTraversalPhase phase, const String& reason) {
    StructureTraversalPhase previous = structureTraversalPhase;
    // Redundant-phase transitions are not events. Without this the same run
    // emitted 128 no-op ST_PHASE lines (36 ApproachDoor->ApproachDoor,
    // 91 Egress->Egress, 1 InteriorRoute->InteriorRoute) that the original
    // never produced -- behaviour was identical, but the trace was polluted.
    if (previous == phase)
        return;

    structureTraversalPhase = phase;

    if (StructureTraversalDiagLog::isLoggingEnabled()) {
        StructureTraversalDiagLog::write(
            "ST_PHASE agent=" + String::valueOf(
                agent == nullptr ? 0 : agent->getObjectID()) +
            " generation=" + String::valueOf(traversalGeneration) +
            " from=" + getStructureTraversalPhaseName(previous) + " to=" +
            getTraversalPhaseName() + " building=" +
            String::valueOf(structureTraversalIntent.owningBuildingOid) +
            " cell=" + String::valueOf(getTraversalTargetCellOid()) +
            " reason=" + reason);
    }
}

void SimPlayerController::clearStructureTraversalState(const String& reason) {
    bool hadState = structureTraversalIntent.active ||
        structureTraversalPhase != StructureTraversalPhase::Idle;

    // Publish the cancellation BEFORE handing the AI map back. Suppression can
    // be installed from a different task thread (hunter death/teardown versus
    // the resume monitor); restoring first would let that thread observe a
    // still-active traversal AND an un-advanced generation, install after our
    // only restore, and strand the bot on the no-op MOVE map. Advancing here
    // is what makes the installer's post-install generation check sound.
    uint64 oldGeneration = traversalGeneration;
    if (hadState) {
        structureTraversalIntent.active = false;
        advanceTraversalGeneration(reason);
    }

    hollowDoorEgressSelectedCandidateIndex = -1;
    traversalResumeMonitorGeneration.store(0);
    traversalPeaceSinceMs = 0;
    traversalResumeInProgress = false;
    combatDriverMoveActive = false;
    if (SimPlayerManager::instance() != nullptr &&
            SimPlayerManager::instance()->
                isStructureTraversalFarSideEgressEnabled())
        hollowEscalationAttempts = 0;
    if (!hadState)
        return;

    if (cellEgressActive) {
        clearCellEgressState();
    }
    advanceWorkLoopGeneration("structureTraversalReset");

    structureTraversalIntent.clear();
    hollowEscalationActive = false;
    hollowEscalationTarget = Vector3(0, 0, 0);
    structureTraversalPhase = StructureTraversalPhase::Idle;
    traversalWatchdogPositionInitialized = false;

    if (StructureTraversalDiagLog::isLoggingEnabled()) {
        StructureTraversalDiagLog::write(
            "ST_PHASE agent=" + String::valueOf(
                agent == nullptr ? 0 : agent->getObjectID()) +
            " generation=" + String::valueOf(oldGeneration) +
            " to=Idle reason=" + reason);
    }
}

void SimPlayerController::pauseStructureTraversal(const String& reason) {
    if (!isStructureTraversalFeatureEnabled() || !isTraversalActive())
        return;

    // Capture the generation of the traversal we decided to pause. Reading
    bool alreadyPaused = structureTraversalPhase ==
        StructureTraversalPhase::CombatPaused;
    if (!alreadyPaused) {
        if (agent != nullptr) {
            Locker locker(agent);
            agent->setMovementState(AiAgent::OBLIVIOUS);
            agent->clearPatrolPoints();
            agent->clearSavedPatrolPoints();
            agent->clearCurrentPath();
        }
        advanceWorkLoopGeneration("structureTraversalCombatPause");
        setStructureTraversalPhase(StructureTraversalPhase::CombatPaused,
            reason);
    }

    state = IDLE;
    traversalPeaceSinceMs = 0;
    if (StructureTraversalDiagLog::isLoggingEnabled())
        StructureTraversalDiagLog::write(
            "ST_COMBAT_PAUSE agent=" + String::valueOf(
                agent == nullptr ? 0 : agent->getObjectID()) +
            " generation=" + String::valueOf(traversalGeneration) +
            " building=" + String::valueOf(
                structureTraversalIntent.owningBuildingOid) +
            " cell=" + String::valueOf(getTraversalTargetCellOid()) +
            " reason=" + reason);
    scheduleStructureTraversalResumeMonitor();
}

void SimPlayerController::scheduleStructureTraversalResumeMonitor() {
    if (!isStructureTraversalFeatureEnabled() || !isTraversalActive() ||
            structureTraversalPhase != StructureTraversalPhase::CombatPaused)
        return;

    uint64 expectedGeneration = 0;
    if (!traversalResumeMonitorGeneration.compare_exchange_strong(
            expectedGeneration, traversalGeneration))
        return;
    Reference<StructureTraversalResumeMonitorTask*> task =
        new StructureTraversalResumeMonitorTask(this, traversalGeneration);
    task->schedule(1000);
}

void SimPlayerController::checkStructureTraversalResume(uint64 generation) {
    if (!isStructureTraversalFeatureEnabled() || !isTraversalActive() ||
            generation != traversalGeneration ||
            structureTraversalPhase != StructureTraversalPhase::CombatPaused)
        return;

    // This is the current monitor instance. A stale task never clears this
    // flag, so a preemption can safely arm a replacement for its new
    // traversal generation.
    uint64 expectedGeneration = generation;
    if (!traversalResumeMonitorGeneration.compare_exchange_strong(
            expectedGeneration, 0))
        return;

    if (agent == nullptr) {
        clearStructureTraversalState("resume_controller_unavailable");
        return;
    }

    bool dead = false;
    bool incapacitated = false;
    bool inCombat = false;
    {
        Locker locker(agent);
        dead = agent->isDead();
        incapacitated = agent->isIncapacitated();
        inCombat = agent->isInCombat();
    }

    if (dead || incapacitated) {
        clearStructureTraversalState(dead ? "resume_dead" :
            "resume_incapacitated");
        state = WAITING;
        return;
    }

    bool combatDriverActive = isCombatDriverActive();

    // Without this the "still paused" branch is completely silent: a bot that
    // never resumes and a monitor that stopped re-arming look identical in the
    // log. resumeFailures cannot distinguish them either, since neither reaches
    // a failure path.
    StructureTraversalDiagLog::write(
        "ST_RESUME_TICK agent=" + String::valueOf(agent->getObjectID()) +
        " generation=" + String::valueOf(generation) +
        " inCombat=" + String::valueOf(inCombat) +
        " combatDriverActive=" + String::valueOf(combatDriverActive) +
        " peaceSinceMs=" + String::valueOf(traversalPeaceSinceMs));

    if (inCombat || combatDriverActive) {
        traversalPeaceSinceMs = 0;
        scheduleStructureTraversalResumeMonitor();
        return;
    }

    uint64 nowMs = System::getMiliTime();
    if (traversalPeaceSinceMs == 0)
        traversalPeaceSinceMs = nowMs;

    int settleMs = SimPlayerManager::instance()->
        getStructureTraversalResumeSettleMs();
    if (settleMs > 0 && nowMs - traversalPeaceSinceMs <
            (uint64)settleMs) {
        scheduleStructureTraversalResumeMonitor();
        return;
    }

    if (generation != traversalGeneration || !isTraversalActive() ||
            structureTraversalPhase != StructureTraversalPhase::CombatPaused)
        return;

    setStructureTraversalPhase(StructureTraversalPhase::Resuming,
        "combat_peace_settled");
    traversalResumeInProgress = true;

    int resumeAttemptCap = SimPlayerManager::instance()->
        getStructureTraversalResumeAttemptCap();
    if (structureTraversalIntent.resumeAttempts >= resumeAttemptCap) {
        traversalResumeInProgress = false;
        SimPlayerManager::instance()->recordStructureTraversalResumeFailure();
        onPathFailed();
        return;
    }

    structureTraversalIntent.resumeAttempts++;
    if (StructureTraversalDiagLog::isLoggingEnabled())
        StructureTraversalDiagLog::write(
            "ST_RESUME agent=" + String::valueOf(agent->getObjectID()) +
            " generation=" + String::valueOf(traversalGeneration) +
            " attempt=" + String::valueOf(
                structureTraversalIntent.resumeAttempts) +
            " building=" + String::valueOf(
                structureTraversalIntent.owningBuildingOid) +
            " cell=" + String::valueOf(getTraversalTargetCellOid()));
    if (!resumeStructureTraversalFromCurrentPosition()) {
        // Revalidate the generation we captured before charging a failure. A
        // preemption may have installed a REPLACEMENT traversal while we were
        // resuming; onPathFailed() would clear that new traversal, and the
        // resume-failure counter would be charged against work that is no
        // longer ours.
        if (generation != traversalGeneration || !isTraversalActive())
            return;

        traversalResumeInProgress = false;
        SimPlayerManager::instance()->recordStructureTraversalResumeFailure();
        onPathFailed();
    }
}

bool SimPlayerController::resumeStructureTraversalFromCurrentPosition() {
    if (!isStructureTraversalFeatureEnabled() || !isTraversalActive() ||
            agent == nullptr || agent->getZone() == nullptr)
        return false;

    {
        Locker locker(agent);
        agent->setMovementState(AiAgent::OBLIVIOUS);
        agent->clearPatrolPoints();
        agent->clearSavedPatrolPoints();
        agent->clearCurrentPath();
    }
    advanceWorkLoopGeneration("resumeRecomputesCurrentMovement");

    // DELIBERATELY NOT re-installing leash suppression here. Resume runs on
    // the monitor's task thread while hunter death/order-completion/teardown
    // run on their own, so installing the no-op MOVE map from here is a
    // cross-thread install racing a teardown, and no check-then-act ordering
    // makes it safe without serializing failure handling across those threads.
    // Suppression is therefore installed ONLY from the controller's own
    // synchronous walk/enter decision points, and a combat pause hands the
    // real tree back for good. Cost: after a combat interruption the rest of
    // that door walk runs unsuppressed and can be leashed again -- a graceful
    // degradation of the thing being measured, not a stranded bot. The next
    // escalation attempt re-arms it.
    Vector3 resumeWorld = structureTraversalIntent.finalTargetWorld;
    Vector3 resumeLocal = structureTraversalIntent.finalTargetLocal;
    ManagedReference<CellObject*> resumeCell =
        structureTraversalIntent.finalTargetCell;
    if (resumeCell == nullptr && !structureTraversalIntent.exitIntent)
        return false;
    ManagedReference<SceneObject*> parent = agent->getParent().get();
    bool currentlyInside = parent != nullptr && parent->isCellObject();

    if (hollowEscalationActive) {
        if (hollowEscalationTarget.getX() == 0.f &&
                hollowEscalationTarget.getY() == 0.f &&
                hollowEscalationTarget.getZ() == 0.f)
            return false;

        destination = hollowEscalationTarget;
        destinationLocal = hollowEscalationTarget;
        destinationCell = nullptr;
        state = CALCULATING_PATH;
        setStructureTraversalPhase(StructureTraversalPhase::Egress,
            "resume_hollow_escalation");
        uint64 movementGeneration = advanceWorkLoopGeneration(
            "resumeHollowEscalation");
        WorldCoordinates startCoord(agent);
        WorldCoordinates endCoord(hollowEscalationTarget, nullptr);
        refreshProbeRayHeight();
        float rayHeight = getProbeRayHeight();
        Reference<SimPathFindTask*> task = new SimPathFindTask(this,
            startCoord, endCoord, agent->getZone(), true, false,
            rayHeight, agent->getObjectID(), movementGeneration);
        task->schedule(100);
        return true;
    }

    // A combat driver may have moved the bot while the old egress path was
    // still marked active. The intent is the durable record; discard only the
    // stale work-loop egress leg and recompute the next leg from the observed
    // parent/position below.
    if (cellEgressActive) {
        clearCellEgressState();
        advanceWorkLoopGeneration("resumeRecomputesCellEgress");
    }

    if (resumeCell != nullptr) {
        if (currentlyInside) {
            ManagedReference<BuildingObject*> currentBuilding =
                parent->getParent().get().castTo<BuildingObject*>();
            ManagedReference<BuildingObject*> targetBuilding =
                resumeCell->getParent().get().castTo<BuildingObject*>();
            bool sameBuilding = currentBuilding != nullptr &&
                targetBuilding != nullptr &&
                currentBuilding->getObjectID() == targetBuilding->getObjectID();

            if (sameBuilding) {
                setStructureTraversalPhase(
                    StructureTraversalPhase::InteriorRoute,
                    "resume_current_interior");
                interiorApproachLeg = true;
            } else {
                setStructureTraversalPhase(StructureTraversalPhase::Egress,
                    "resume_current_cross_building");
            }
        } else {
            setStructureTraversalPhase(StructureTraversalPhase::Reentry,
                "resume_current_outdoors_reentry");
            interiorApproachLeg = true;
        }

        moveToWithOrigin(resumeWorld, resumeLocal, resumeCell.get(),
            TraversalMoveOrigin::Internal);
        return true;
    }

    // A starport collector/hollow is an outdoor engine parent (cell 0), but it
    // is still physically inside the owning building footprint.  A direct
    // cell-0 -> cell-0 request cannot cross that portal graph.  Re-enter the
    // stashed cell first, then the arrival branch below starts the normal
    // egress leg toward the durable outdoor destination.
    if (!currentlyInside && isWithinOwningBuildingHollow() &&
            structureTraversalIntent.reentryCell != nullptr) {
        ManagedReference<CellObject*> reentryCell =
            structureTraversalIntent.reentryCell;
        Vector3 reentryLocal =
            structureTraversalIntent.entryReentryWaypoint;
        Vector3 reentryWorld = WorldCoordinates(reentryLocal,
            reentryCell).getWorldPosition();
        setStructureTraversalPhase(StructureTraversalPhase::Reentry,
            "resume_hollow_reentry");
        interiorApproachLeg = true;
        moveToWithOrigin(reentryWorld, reentryLocal, reentryCell.get(),
            TraversalMoveOrigin::Internal);
        return true;
    }

    if (currentlyInside) {
        // Do not infer "outdoors" from a cell/graph id of zero. An enclosed
        // hollow can expose that id while still being parented to a building;
        // the cell parent is the re-entry/egress rule that avoids the
        // both-endpoints-cell-0 pathfinder limitation.
        setStructureTraversalPhase(StructureTraversalPhase::Egress,
            "resume_current_egress");
        moveToWithOrigin(resumeWorld, resumeLocal, nullptr,
            TraversalMoveOrigin::Internal);
    } else {
        clearInteriorApproachLeg();
        moveToWithOrigin(resumeWorld, resumeLocal, nullptr,
            TraversalMoveOrigin::Internal);
    }

    return true;
}

float SimPlayerController::getOwningBuildingHollowMissDistance(
        const Vector3& worldPosition, BuildingObject* building) const {
    if (building == nullptr || building->getBoundingVolume() == nullptr)
        return -1.f;

    Vector3 modelPoint = PathFinderManager::transformToModelSpace(
        worldPosition, building);
    const AABB& bounds = building->getBoundingVolume()->getBoundingBox();
    // Model space is Y-up (see the derivation above transformFromStructureModelSpace):
    // .X = east, .Y = height, .Z = north. The two HORIZONTAL axes are therefore
    // X and Z. Measuring the miss from X and Y instead spent the containment
    // margin on height and left north with a hard, margin-less boundary, which
    // let a bot that had escalated only ~10m north report cleared_hollow.
    float missEast = Math::max(0.f, Math::max(
        bounds.getXMin() - modelPoint.getX(),
        modelPoint.getX() - bounds.getXMax()));
    float missNorth = Math::max(0.f, Math::max(
        bounds.getZMin() - modelPoint.getZ(),
        modelPoint.getZ() - bounds.getZMax()));
    return Math::sqrt(missEast * missEast + missNorth * missNorth);
}

bool SimPlayerController::isWithinOwningBuildingHollowAt(
        const Vector3& worldPosition, BuildingObject* building) const {
    if (building == nullptr || building->getBoundingVolume() == nullptr)
        return false;

    Vector3 modelPoint = PathFinderManager::transformToModelSpace(
        worldPosition, building);
    const AABB& bounds = building->getBoundingVolume()->getBoundingBox();
    // Height is model .Y, not .Z (Y-up model space). Gating on .Z tested the
    // NORTH range here, duplicating a horizontal axis and leaving the real
    // vertical extent unchecked.
    bool withinHeight = modelPoint.getY() >= bounds.getYMin() &&
        modelPoint.getY() <= bounds.getYMax();
    if (!withinHeight)
        return false;

    return getOwningBuildingHollowMissDistance(worldPosition, building) <=
        SimPlayerManager::instance()->
            getStructureTraversalHollowContainmentMarginMeters();
}

bool SimPlayerController::isWithinOwningBuildingHollow() const {
    if (agent == nullptr || structureTraversalIntent.owningBuildingOid == 0 ||
            agent->getZone() == nullptr)
        return false;

    ManagedReference<SceneObject*> object = agent->getZone()->getZoneServer()->
        getObject(structureTraversalIntent.owningBuildingOid);
    BuildingObject* building = object == nullptr ? nullptr :
        object->asBuildingObject();
    return isWithinOwningBuildingHollowAt(agent->getWorldPosition(), building);
}

// Exact inverse of PathFinderManager::transformToModelSpace, which is the space
// PathNode positions live in (see findNearestGlobalNode's caller there).
//
// That transform builds `switched(x, z, y)` and rotates with rot[1][1] = 1, so
// the rotation-invariant (vertical) axis is component .Y and the rotation mixes
// .X with .Z. Working the row-vector multiply through gives:
//
//   m.X =  (wx-bX)*cos + (wy-bY)*sin
//   m.Y =  (wz-bZ)                      <- height is .Y, NOT .Z
//   m.Z = -(wx-bX)*sin + (wy-bY)*cos    <- north is .Z, NOT .Y
//
// So the horizontal pair to un-rotate is (.X, .Z) and the height passthrough is
// .Y. Using (.X, .Y) instead silently folds node height into the north
// coordinate — the same Y/Z axis-swap class as the F.0.4.7 world-coord-as-
// cell-local bug — so keep this paired with the derivation above.
static Vector3 transformFromStructureModelSpace(
        const Vector3& modelPoint, BuildingObject* building) {
    float rad = -building->getDirection()->getRadians();
    float cosRad = cos(rad);
    float sinRad = sin(rad);

    float worldX = building->getPositionX() + modelPoint.getX() * cosRad -
        modelPoint.getZ() * sinRad;
    float worldY = building->getPositionY() + modelPoint.getX() * sinRad +
        modelPoint.getZ() * cosRad;
    float worldZ = building->getPositionZ() + modelPoint.getY();
    return Vector3(worldX, worldY, worldZ);
}

// Diagnostics ONLY -- never gates or alters movement.
//
// Answers "does the straight escalation line pass through the building itself?",
// which is the realism question for Option C: a bot that clips through a
// starport wall is unacceptable here even when the traversal functionally
// succeeds.
//
// An AABB test CANNOT answer this. The escalation only fires when the bot is
// already inside the owning building's box (the trigger logs
// hollowMissDistance=0), so any segment from that point trivially intersects
// the box and the answer would always be "yes". The building's APPEARANCE mesh
// is the real geometry: a point standing in the open on the landing pad is not
// inside a wall, so the ray only registers a hit when it actually crosses one.
//
// Returns CollisionManager's normalized hit distance along the segment, or
// FLT_MAX when the line reaches the destination without touching the mesh.
float SimPlayerController::hollowEscalationSegmentGeometryHit(
        const Vector3& arrivalWorld, const Vector3& destination,
        BuildingObject* building) const {
    if (building == nullptr)
        return FLT_MAX;

    float distance = arrivalWorld.distanceTo(destination);
    if (distance < 0.001f)
        return FLT_MAX;

    return CollisionManager::getAppearanceIntersection(building, arrivalWorld,
        destination, 0.f, distance);
}

void SimPlayerController::observeBuildingExits(Zone* zone,
        BuildingObject* building, const Vector3& botWorld) {
    if (zone == nullptr || building == nullptr)
        return;

    SharedObjectTemplate* objectTemplate = building->getObjectTemplate();
    const PortalLayout* portalLayout = objectTemplate == nullptr ? nullptr :
        objectTemplate->getPortalLayout();

    if (portalLayout == nullptr) {
        StructureTraversalDiagLog::write(
            "ST_HOLLOW buildingExits building=" +
            String::valueOf(building->getObjectID()) +
            " result=no_portal_layout");
        return;
    }

    const Vector<Reference<CellProperty*> >& cellProperties =
        portalLayout->getCellProperties();
    int worldPortals = 0;
    int outsideHollow = 0;

    for (int cellIndex = 0; cellIndex < cellProperties.size(); ++cellIndex) {
        const CellProperty* property = portalLayout->getCellProperty(cellIndex);

        if (property == nullptr)
            continue;

        for (int p = 0; p < property->getNumberOfPortals(); ++p) {
            const CellPortal* portal = property->getPortal(p);

            if (portal == nullptr || portal->getTargetCellIndex() > 1)
                continue;

            ++worldPortals;

            // Portal AABBs are Y-UP MODEL space; center() is the doorway's
            // vertical middle, so drop by the Y extent, per stock
            // computeExteriorPortalWorldPoint.
            int geometryIndex = portal->getGeometryIndex();
            const AABB& bounds = portalLayout->getPortalBounds(geometryIndex);
            Vector3 modelDoor = bounds.center() -
                Vector3(0, bounds.extents().getY(), 0);
            Vector3 doorWorld = transformFromStructureModelSpace(modelDoor,
                building);
            float miss = getOwningBuildingHollowMissDistance(doorWorld,
                building);
            bool inHollow = isWithinOwningBuildingHollowAt(doorWorld, building);

            if (!inHollow)
                ++outsideHollow;

            StructureTraversalDiagLog::write(
                "ST_HOLLOW buildingExit cell=" + String::valueOf(cellIndex) +
                " targetCell=" + String::valueOf(portal->getTargetCellIndex()) +
                " geometryIndex=" + String::valueOf(geometryIndex) +
                " door=(" + String::valueOf(doorWorld.getX()) + "," +
                String::valueOf(doorWorld.getY()) + "," +
                String::valueOf(doorWorld.getZ()) + ")" +
                " hollowMiss=" + String::valueOf(miss) +
                " inHollow=" + String::valueOf(inHollow ? 1 : 0) +
                " distFromBot=" +
                String::valueOf(doorWorld.distanceTo(botWorld)));
        }
    }

    StructureTraversalDiagLog::write(
        "ST_HOLLOW buildingExits building=" +
        String::valueOf(building->getObjectID()) + " cells=" +
        String::valueOf(cellProperties.size()) + " worldPortals=" +
        String::valueOf(worldPortals) + " outsideHollow=" +
        String::valueOf(outsideHollow));
}

void SimPlayerController::observeHollowRadialScan(Zone* zone,
        BuildingObject* building, const Vector3& originWorld) {
    SimPlayerManager* manager = SimPlayerManager::instance();
    int rays = manager == nullptr ? 0 : manager->
        getStructureTraversalHollowScanRays();
    float rayMarginMeters = manager == nullptr ? 0.f : manager->
        getStructureTraversalHollowScanRayMarginMeters();
    float minOpeningDeg = manager == nullptr ? 0.f : manager->
        getStructureTraversalHollowScanMinOpeningDeg();

    if (rays < 8)
        rays = 8;
    else if (rays > 360)
        rays = 360;

    Vector3 origin = originWorld;
    origin.setZ(origin.getZ() + getProbeRayHeight());

    uint64 agentOid = agent == nullptr ? 0 : agent->getObjectID();
    uint64 buildingOid = building == nullptr ? 0 : building->getObjectID();
    String templateName = "none";
    if (building != nullptr && building->getObjectTemplate() != nullptr)
        templateName = building->getObjectTemplate()->getFullTemplateString();

    struct HollowOpening {
        float centreBearingDeg;
        float widthDeg;
        float escapeDistance;
        Vector3 target;
    };

    std::vector<int> escaped;
    std::vector<HollowOpening> openings;
    float rayLength = 0.f;
    int escapedCount = 0;
    // escaped = reachesOutside AND !blocked. Reporting only the conjunction
    // makes "every ray hit geometry" indistinguishable from "no endpoint
    // qualified as outside" -- the two have completely different fixes.
    int reachedOutsideCount = 0;
    int blockedCount = 0;
    // Which object actually walls the pad. If this comes back as the starport
    // itself then the way out is THROUGH the building, and the existing cell
    // machinery already handles that. If it names something else, the
    // walk-through-the-building plan is wrong and must be abandoned.
    Vector<String> blockerTemplates;
    Vector<int> blockerCounts;
    String scanStatus = "ok";
    auto started = std::chrono::steady_clock::now();

    auto pointAtBearing = [&origin, &rayLength](float bearingRadians) {
        return Vector3(origin.getX() + cos(bearingRadians) * rayLength,
            origin.getY() + sin(bearingRadians) * rayLength,
            origin.getZ());
    };

    auto boundaryDistanceAtBearing = [this, &origin, building, &rayLength,
            &pointAtBearing](float bearingRadians) {
        if (building == nullptr || rayLength <= 0.f)
            return 0.f;

        float originMissDistance = getOwningBuildingHollowMissDistance(
            origin, building);
        if (originMissDistance > 0.f)
            return 0.f;

        Vector3 end = pointAtBearing(bearingRadians);
        if (getOwningBuildingHollowMissDistance(end, building) <= 0.f)
            return rayLength;

        float low = 0.f;
        float high = rayLength;
        for (int i = 0; i < 24; ++i) {
            float middle = (low + high) * 0.5f;
            Vector3 point(origin.getX() + cos(bearingRadians) * middle,
                origin.getY() + sin(bearingRadians) * middle,
                origin.getZ());
            if (getOwningBuildingHollowMissDistance(point, building) > 0.f)
                high = middle;
            else
                low = middle;
        }

        return high;
    };

    try {
        if (manager != nullptr && zone != nullptr && building != nullptr &&
                building->getBoundingVolume() != nullptr) {
            // Keep the scan length tied to the same model-space hollow bounds
            // used by isWithinOwningBuildingHollowAt() and
            // getOwningBuildingHollowMissDistance(). The horizontal axes in
            // model space are X/Z; model Y is height.
            Vector3 modelPoint = PathFinderManager::transformToModelSpace(
                origin, building);
            const AABB& bounds = building->getBoundingVolume()->
                getBoundingBox();
            float farCornerX = Math::max(
                fabs(modelPoint.getX() - bounds.getXMin()),
                fabs(modelPoint.getX() - bounds.getXMax()));
            float farCornerZ = Math::max(
                fabs(modelPoint.getZ() - bounds.getZMin()),
                fabs(modelPoint.getZ() - bounds.getZMax()));
            rayLength = sqrt(farCornerX * farCornerX +
                farCornerZ * farCornerZ) + rayMarginMeters;

            if (rayLength < 0.001f)
                rayLength = rayMarginMeters;

            escaped.assign(rays, 0);

            // Match D7 Part 1: one broad-phase query for the whole probe disc,
            // with the per-ray sphere cull before the appearance narrow phase.
            InRangeObjectsVector objects;
            float queryRadius = rayLength + manager->
                getStructureTraversalZeroClipBroadPhasePadMeters();
            zone->getInRangeObjects(origin.getX(), 0, origin.getY(),
                queryRadius, &objects, true, true);

            float bearingStepRadians = 2.f * (float)M_PI / (float)rays;
            for (int rayIndex = 0; rayIndex < rays; ++rayIndex) {
                float bearingRadians = bearingStepRadians * rayIndex;
                Vector3 rayEnd = pointAtBearing(bearingRadians);
                // A ray escapes if its ENDPOINT is outside the hollow region
                // at all. Requiring it to be rayMarginMeters outside conflated
                // two different jobs for one number -- rayMarginMeters is how
                // far PAST the far corner to cast, while the miss distance is
                // measured from the building AABB, so for a roughly centred bot
                // NO endpoint could satisfy it and every ray failed regardless
                // of geometry. That is what produced escaped=0 on the first run.
                bool reachesOutside = getOwningBuildingHollowMissDistance(
                    rayEnd, building) > 0.f;
                bool blocked = false;

                for (int objectIndex = 0; objectIndex < objects.size();
                        ++objectIndex) {
                    SceneObject* object = static_cast<SceneObject*>(
                        objects.get(objectIndex));
                    if (object == nullptr || object->getObjectID() == agentOid ||
                            (object->getReceiverFlags() &
                                CloseObjectsVector::COLLIDABLETYPE) == 0)
                        continue;

                    const AppearanceTemplate* appearance =
                        object->getAppearanceTemplate();
                    if (appearance == nullptr)
                        continue;

                    const BaseBoundingVolume* bounding =
                        appearance->getBoundingVolume();
                    if (bounding == nullptr)
                        continue;

                    const Sphere& objectSphere = bounding->getBoundingSphere();
                    Vector3 objectPosition = object->getPosition() +
                        objectSphere.getCenter();
                    float targetRadius = objectSphere.getRadius() + rayLength;

                    if (CollisionManager::getPointIntersection(objectPosition,
                            origin, rayEnd, targetRadius, rayLength) == FLT_MAX)
                        continue;

                    if (CollisionManager::getAppearanceIntersection(object,
                            origin, rayEnd, 0.f, rayLength) != FLT_MAX) {
                        blocked = true;

                        SharedObjectTemplate* blockerTemplate =
                            object->getObjectTemplate();
                        String blockerName = blockerTemplate == nullptr ?
                            String("unknown") :
                            blockerTemplate->getFullTemplateString();
                        int existing = -1;

                        for (int t = 0; t < blockerTemplates.size(); ++t) {
                            if (blockerTemplates.get(t) == blockerName) {
                                existing = t;
                                break;
                            }
                        }

                        if (existing >= 0)
                            blockerCounts.set(existing,
                                blockerCounts.get(existing) + 1);
                        else if (blockerTemplates.size() < 16) {
                            blockerTemplates.add(blockerName);
                            blockerCounts.add(1);
                        }

                        break;
                    }
                }

                if (reachesOutside)
                    reachedOutsideCount++;

                if (blocked)
                    blockedCount++;

                if (!blocked && reachesOutside) {
                    escaped[rayIndex] = 1;
                    escapedCount++;
                }
            }

            float bearingStepDeg = 360.f / (float)rays;
            int firstNonEscaped = -1;
            for (int i = 0; i < rays; ++i) {
                if (!escaped[i]) {
                    firstNonEscaped = i;
                    break;
                }
            }

            if (firstNonEscaped < 0) {
                float centreBearingDeg = 0.f;
                float centreBearingRadians = 0.f;
                openings.push_back({centreBearingDeg, 360.f,
                    boundaryDistanceAtBearing(centreBearingRadians),
                    pointAtBearing(centreBearingRadians)});
            } else {
                int run = (firstNonEscaped + 1) % rays;
                int visited = 0;
                while (visited < rays) {
                    if (!escaped[run]) {
                        run = (run + 1) % rays;
                        visited++;
                        continue;
                    }

                    int start = run;
                    int end = run;
                    int count = 1;
                    run = (run + 1) % rays;
                    visited++;
                    while (visited < rays && escaped[run]) {
                        end = run;
                        count++;
                        run = (run + 1) % rays;
                        visited++;
                    }

                    float widthDeg = count * bearingStepDeg;
                    if (widthDeg < minOpeningDeg)
                        continue;

                    int unwrappedEnd = end < start ? end + rays : end;
                    float centreIndex = (float)(start + unwrappedEnd) * 0.5f;
                    while (centreIndex < 0.f)
                        centreIndex += rays;
                    while (centreIndex >= rays)
                        centreIndex -= rays;

                    float centreBearingDeg = centreIndex * bearingStepDeg;
                    float centreBearingRadians = centreBearingDeg *
                        (float)M_PI / 180.f;
                    openings.push_back({centreBearingDeg, widthDeg,
                        boundaryDistanceAtBearing(centreBearingRadians),
                        pointAtBearing(centreBearingRadians)});
                }
            }
        }
    } catch (...) {
        // Phase 1 is diagnostics-only, so movement is untouched -- but a failed
        // scan must NOT be reported as openings=0. That value is the result
        // that would refute the two-door premise, and evidence and error have
        // to stay distinguishable. Discard whatever partial data was gathered
        // and say plainly that the scan errored.
        scanStatus = "error";
        openings.clear();
        escapedCount = 0;
        reachedOutsideCount = 0;
        blockedCount = 0;
        blockerTemplates.removeAll();
        blockerCounts.removeAll();
    }

    uint64 elapsedUs = (uint64)std::chrono::duration_cast<
        std::chrono::microseconds>(std::chrono::steady_clock::now() -
            started).count();
    StructureTraversalDiagLog::writeHollowScan(
        "ST_HOLLOW scan status=" + scanStatus + " rays=" + String::valueOf(rays) +
        " escaped=" + String::valueOf(escapedCount) +
        " reachedOutside=" + String::valueOf(reachedOutsideCount) +
        " blocked=" + String::valueOf(blockedCount) +
        " rayLength=" + String::valueOf(rayLength) +
        " openings=" + String::valueOf((int)openings.size()) +
        " elapsedUs=" + String::valueOf(elapsedUs) +
        " building=" + String::valueOf(buildingOid) +
        " template=" + templateName +
        " agent=" + String::valueOf(agentOid) +
        " origin=" + origin.toString());

    // Is the pad navmeshed at all? If not, hybrid movement cannot help here and
    // the mode 2 answer is something else entirely.
    int navMeshCount = -1;
    try {
        if (zone != nullptr) {
            SortedVector<ManagedReference<NavArea*> > navAreas;
            zone->getInRangeNavMeshes(origin.getX(), origin.getY(), &navAreas,
                true);
            navMeshCount = navAreas.size();
        }
    } catch (...) {
        navMeshCount = -1;
    }

    StructureTraversalDiagLog::writeHollowScan(
        "ST_HOLLOW navmesh areasAtOrigin=" + String::valueOf(navMeshCount) +
        " origin=" + origin.toString());

    if (blockerTemplates.size() > 0) {
        String blockerLine = "ST_HOLLOW blockers";

        // Descending by ray count; at most 16 entries, so an insertion-style
        // pass is cheaper than dragging in a comparator.
        Vector<int> order;
        for (int i = 0; i < blockerTemplates.size(); ++i)
            order.add(i);

        for (int i = 0; i < order.size(); ++i) {
            for (int j = i + 1; j < order.size(); ++j) {
                if (blockerCounts.get(order.get(j)) >
                        blockerCounts.get(order.get(i))) {
                    int swap = order.get(i);
                    order.set(i, order.get(j));
                    order.set(j, swap);
                }
            }
        }

        for (int i = 0; i < order.size(); ++i)
            blockerLine += " " + blockerTemplates.get(order.get(i)) + "=" +
                String::valueOf(blockerCounts.get(order.get(i)));

        StructureTraversalDiagLog::writeHollowScan(blockerLine);
    }

    for (int i = 0; i < (int)openings.size(); ++i) {
        const HollowOpening& opening = openings[i];
        StructureTraversalDiagLog::writeHollowScan(
            "ST_HOLLOW opening index=" + String::valueOf(i) +
            " centreBearing=" + String::valueOf(opening.centreBearingDeg) +
            " widthDeg=" + String::valueOf(opening.widthDeg) +
            " escapeDist=" + String::valueOf(opening.escapeDistance) +
            " target=" + opening.target.toString());
    }
}

bool SimPlayerController::resolveHollowEscalationTarget(Zone* zone,
        BuildingObject* building, const Vector3& agentWorld,
        const Vector3& finalDestination, Vector3& target, String& source,
        int& candidates, int& nodesExamined, int& rejectedHollow,
        int& rejectedBounds, int& rejectedWater) {
    target = Vector3(0, 0, 0);
    source = "";
    candidates = 0;
    nodesExamined = 0;
    rejectedHollow = 0;
    rejectedBounds = 0;
    rejectedWater = 0;
    int acceptedNodes = 0;
    if (zone == nullptr || building == nullptr)
        return false;

    SharedObjectTemplate* objectTemplate = building->getObjectTemplate();
    const PortalLayout* portalLayout = objectTemplate == nullptr ? nullptr :
        objectTemplate->getPortalLayout();

    // This is the same managed-object choreography as the ordinary egress
    // leg. The D1 caller has released the agent lock before reaching here, so
    // the building lock is never nested under it.
    Vector3 exteriorPortal;
    {
        Locker buildingLocker(building);
        exteriorPortal = building->getNearestExteriorPortalPoint(agentWorld);
        if (exteriorPortal.getX() == 0.f && exteriorPortal.getY() == 0.f)
            exteriorPortal = building->getEjectionPoint();
    }

    Vector3 travelPointAnchor = exteriorPortal;
    if (travelPointAnchor.getX() == 0.f && travelPointAnchor.getY() == 0.f)
        travelPointAnchor = building->getWorldPosition();

    bool isStarport = objectTemplate != nullptr &&
        objectTemplate->getFullTemplateString().toLowerCase().indexOf(
            "starport") >= 0;

    bool travelPointFound = false;
    bool travelPointInterplanetary = false;
    String travelPointRejected = "none";

    if (isStarport && SimPlayerManager::instance()->
            isStructureTraversalHollowEscalationPreferTravelPoint()) {
        PlanetManager* planetManager = zone->getPlanetManager();
        Reference<PlanetTravelPoint*> travelPoint = planetManager == nullptr ?
            nullptr : planetManager->getNearestPlanetTravelPoint(
                travelPointAnchor, 256.f, true);
        if (travelPoint != nullptr) {
            travelPointFound = true;
            travelPointInterplanetary = travelPoint->isInterplanetary();
        }
        if (travelPointInterplanetary) {
            Vector3 candidate = travelPoint->getArrivalPosition();
            if (isWithinOwningBuildingHollowAt(candidate, building)) {
                travelPointRejected = "hollow";
            } else if (!zone->isWithinBoundaries(candidate)) {
                travelPointRejected = "bounds";
            } else if (SimPlayerManager::instance()->
                    isStructureTraversalPointInWater(zone, candidate)) {
                travelPointRejected = "water";
            } else {
                target = candidate;
                source = "travel_point";
                candidates = 1;
            }
        }
    }

    StructureTraversalDiagLog::write(
        "ST_EGRESS escalation=travelpoint found=" +
        String::valueOf(travelPointFound ? 1 : 0) + " interplanetary=" +
        String::valueOf(travelPointInterplanetary ? 1 : 0) +
        " rejected=" + travelPointRejected);

    auto emitNodeTelemetry = [&]() {
        StructureTraversalDiagLog::write(
            "ST_EGRESS escalation=nodes examined=" +
            String::valueOf(nodesExamined) + " rejectedHollow=" +
            String::valueOf(rejectedHollow) + " rejectedBounds=" +
            String::valueOf(rejectedBounds) + " rejectedWater=" +
            String::valueOf(rejectedWater) + " accepted=" +
            String::valueOf(acceptedNodes));
    };

    if (source == "travel_point") {
        emitNodeTelemetry();
        return true;
    }

    const FloorMesh* exteriorFloorMesh = portalLayout == nullptr ||
        portalLayout->getFloorMeshNumber() <= 0 ? nullptr :
        portalLayout->getFloorMesh(0);
    const PathGraph* pathGraph = exteriorFloorMesh == nullptr ? nullptr :
        exteriorFloorMesh->getPathGraph();
    if (pathGraph == nullptr) {
        emitNodeTelemetry();
        return false;
    }

    Vector<const PathNode*> globalNodes = pathGraph->getGlobalNodes();
    nodesExamined = globalNodes.size();
    float nearestDistance = 0.f;
    bool found = false;
    for (int i = 0; i < globalNodes.size(); ++i) {
        const PathNode* node = globalNodes.get(i);
        if (node == nullptr)
            continue;

        Vector3 candidate = transformFromStructureModelSpace(
            node->getPosition(), building);
        if (isWithinOwningBuildingHollowAt(candidate, building)) {
            ++rejectedHollow;
            continue;
        }
        if (!zone->isWithinBoundaries(candidate)) {
            ++rejectedBounds;
            continue;
        }
        if (SimPlayerManager::instance()->isStructureTraversalPointInWater(
                zone, candidate)) {
            ++rejectedWater;
            continue;
        }

        ++candidates;
        ++acceptedNodes;
        float distance = candidate.distanceTo2d(finalDestination);
        if (!found || distance < nearestDistance) {
            found = true;
            nearestDistance = distance;
            target = candidate;
        }
    }

    if (found)
        source = "exterior_node";
    emitNodeTelemetry();
    return found;
}

SimPlayerController::HollowEscalationOutcome
SimPlayerController::beginHollowEscalation(const Vector3& arrivalWorld) {
    SimPlayerManager* manager = SimPlayerManager::instance();
    bool hollowScanEnabled = manager != nullptr && manager->
        isStructureTraversalHollowScanEnabled();
    bool hollowDoorEgressObserve = manager != nullptr && manager->
        isStructureTraversalHollowDoorEgressObserveEnabled();
    if (manager == nullptr ||
            (!manager->isStructureTraversalHollowEscalationEnabled() &&
                !hollowScanEnabled && !hollowDoorEgressObserve) ||
            !isTraversalActive() || agent == nullptr ||
            !structureTraversalIntent.exitIntent || agent->isInCombat() ||
            isCombatDriverActive())
        return HollowEscalationOutcome::NotHandled;

    // An entry leg is IN FLIGHT -- report InProgress, NOT NotHandled. Without this the
    // escalation preempts the very entry it just scheduled. Measured
    // 2026-08-26: all 11 door entries were killed a uniform ~1.006s after
    // "ST_PATH result=accepted" -- one arrival-check tick -- while the bot was
    // still 1.95m short of the doorway. It has not crossed yet, so the
    // isCellObject() guard below cannot see it, isWithinOwningBuildingHollow()
    // is still legitimately true, and hollowEscalationAttempts is already at
    // the cap, so the re-check reported still_inside/attempt_cap and failed the
    // traversal. Same class as the fixed external_move_preemption: a leg
    // destroyed by the machinery that started it.
    //
    // It must NOT return NotHandled: that outcome's arrival tail runs
    // clearInteriorApproachLeg() + onArrived(), which drops the entry latch and
    // reports the intermediate leg boundary as the traversal's arrival. Under
    // the harness that ended the exit step 1.06s into Leg B while the bot was
    // still 1.95m short of the doorway.
    if (interiorApproachLeg)
        return HollowEscalationOutcome::InProgress;

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    if (parent != nullptr && parent->isCellObject())
        return HollowEscalationOutcome::NotHandled;

    Zone* zone = agent->getZone();
    if (zone == nullptr)
        return HollowEscalationOutcome::Failed;

    ManagedReference<SceneObject*> object = zone->getZoneServer()->getObject(
        structureTraversalIntent.owningBuildingOid);
    BuildingObject* building = object == nullptr ? nullptr :
        object->asBuildingObject();
    if (building == nullptr || !isWithinOwningBuildingHollowAt(arrivalWorld,
            building))
        return HollowEscalationOutcome::NotHandled;

    // Phase 1 is ADDITIVE, not a substitute: observe, then let whatever the
    // escalation gate would have done happen unchanged. Returning here would
    // silently suppress escalation whenever the scan gate is on, which is a
    // behaviour change, not an observation.
    if (hollowScanEnabled)
        observeHollowRadialScan(zone, building, arrivalWorld);

    bool hollowDoorEgressWalk = manager->
        isStructureTraversalHollowDoorEgressWalkEnabled();
    if (hollowDoorEgressObserve) {
        observeBuildingExits(zone, building, agent->getWorldPosition());
        buildCellEgressExitSet(true);

        int doorCount = cellEgressCandidates.size();
        bool doorsFound = doorCount > 0;
        Vector3 botPos = agent->getWorldPosition();
        float nearestDist = 0.f;
        float nearestDz = 0.f;
        Vector3 nearestDoor;
        int selectedCandidateIndex = 0;
        if (doorsFound) {
            nearestDoor = cellEgressCandidates.get(0);
            nearestDist = nearestDoor.distanceTo(botPos);
            nearestDz = nearestDoor.getZ() - botPos.getZ();

            if (manager->
                    isStructureTraversalHollowDoorEgressUseCellPortalsEnabled() &&
                    cellEgressCandidateInHollow.size() == doorCount) {
                // We only get here while the bot IS inside the hollow, so
                // prefer a door that is reachable FROM the hollow -- i.e.
                // inHollow == 1. The outside-hollow doors are the real exits
                // to the world, but they sit on the far side of the building
                // (measured: cells 1/2/3 at x~3539-3541 versus pad-side cells
                // 15/16 at x~3608-3614, bot on the pad at x~3616), so walking
                // straight at one is either a stall or a clip through the
                // building. The route is pad -> pad-side door -> interior ->
                // west exit, and the interior leg is what Retry 6 multicell
                // already solves. Candidates are sorted by 3D distance, so
                // taking the first match preserves distance ordering within
                // the reachable group; with no pad-side door we fall back to
                // the nearest candidate overall.
                for (int candidateIndex = 0; candidateIndex < doorCount;
                        ++candidateIndex) {
                    if (cellEgressCandidateInHollow.get(candidateIndex) == 1) {
                        selectedCandidateIndex = candidateIndex;
                        break;
                    }
                }
            }
        }

        StructureTraversalDiagLog::write(
            "ST_HOLLOW doorEgress result=" +
            String(doorsFound ? "found" : "none") + " doors=" +
            String::valueOf(doorCount) + " nearestDist=" +
            String::valueOf(nearestDist) + " nearestDz=" +
            String::valueOf(nearestDz) + " building=" +
            String::valueOf(building->getObjectID()) + " agent=" +
            String::valueOf(agent->getObjectID()) + " botPos=" +
            String::valueOf(botPos.getX()) + "," +
            String::valueOf(botPos.getY()) + "," +
            String::valueOf(botPos.getZ()));

        if (hollowDoorEgressWalk && doorsFound &&
                hollowEscalationAttempts < manager->
                    getStructureTraversalHollowEscalationAttemptCap()) {
            hollowEscalationAttempts++;
            hollowDoorEgressSelectedCandidateIndex = selectedCandidateIndex;
            Vector3 selectedDoor = cellEgressCandidates.get(
                selectedCandidateIndex);
            float selectedDistance = selectedDoor.distanceTo(botPos);            StructureTraversalDiagLog::write(
                "ST_HOLLOW doorEgress action=walking target=" +
                String::valueOf(selectedDoor.getX()) + "," +
                String::valueOf(selectedDoor.getY()) + "," +
                String::valueOf(selectedDoor.getZ()) + " dist=" +
                String::valueOf(selectedDistance) + " attempt=" +
                String::valueOf(hollowEscalationAttempts));
            setStructureTraversalPhase(StructureTraversalPhase::Egress,
                "hollow_door_egress");
            moveToWithOrigin(selectedDoor, selectedDoor, nullptr,
                TraversalMoveOrigin::Internal);
            return HollowEscalationOutcome::Started;
        }
    }

    // With escalation off (its default) there is nothing further to do, and the
    // scan above has already recorded what it saw.
    if (!manager->isStructureTraversalHollowEscalationEnabled())
        return HollowEscalationOutcome::NotHandled;

    int attemptCap = manager->
        getStructureTraversalHollowEscalationAttemptCap();
    if (hollowEscalationAttempts >= attemptCap) {
        SimPlayerManager::instance()->
            recordStructureTraversalHollowEscalationFailed();
        StructureTraversalDiagLog::write(
            "ST_EGRESS escalation=result status=still_inside reason=attempt_cap "
            "agent=" + String::valueOf(agent->getObjectID()));
        return HollowEscalationOutcome::Failed;
    }

    hollowEscalationAttempts++;
    SimPlayerManager::instance()->
        recordStructureTraversalHollowEscalationTriggered();
    StructureTraversalDiagLog::write(
        "ST_EGRESS escalation=triggered reason=truncated_arrival_in_hollow "
        "agent=" + String::valueOf(agent->getObjectID()) + " arrival=" +
        arrivalWorld.toString() + " requested=" +
        structureTraversalIntent.finalTargetWorld.toString() +
        " hollowMissDistance=" + String::valueOf(
            getOwningBuildingHollowMissDistance(arrivalWorld, building)));

    Vector3 target;
    String source;
    int candidates = 0;
    int nodesExamined = 0;
    int rejectedHollow = 0;
    int rejectedBounds = 0;
    int rejectedWater = 0;
    if (!resolveHollowEscalationTarget(zone, building, arrivalWorld,
            structureTraversalIntent.finalTargetWorld, target, source,
            candidates, nodesExamined, rejectedHollow, rejectedBounds,
            rejectedWater)) {
        const Vector3& finalDestination =
            structureTraversalIntent.finalTargetWorld;
        bool finalDestinationSet = finalDestination.getX() != 0.f ||
            finalDestination.getY() != 0.f || finalDestination.getZ() != 0.f;
        if (SimPlayerManager::instance()->
                isStructureTraversalHollowEscalationDirectFallback() &&
                finalDestinationSet) {
            target = finalDestination;
            source = "final_destination";
        } else {
            StructureTraversalDiagLog::write(
                "ST_EGRESS escalation=result status=no_candidate agent=" +
                String::valueOf(agent->getObjectID()) + " candidates=" +
                String::valueOf(candidates));
            SimPlayerManager::instance()->
                recordStructureTraversalHollowEscalationFailed();
            return HollowEscalationOutcome::Failed;
        }
    }

    if (source == "final_destination") {
        float geometryHit = hollowEscalationSegmentGeometryHit(arrivalWorld,
            target, building);
        StructureTraversalDiagLog::write(
            "ST_EGRESS escalation=direct dest=" + target.toString() +
            " crossesGeometry=" + String::valueOf(
                geometryHit < 1.f ? 1 : 0) +
            " hitAt=" + (geometryHit < 1.f ?
                String::valueOf(geometryHit) : String("none")) +
            " segmentLen=" + String::valueOf(
                arrivalWorld.distanceTo(target)));
    }

    StructureTraversalDiagLog::write(
        "ST_EGRESS escalation=target source=" + source + " candidates=" +
        String::valueOf(candidates) + " chosen=" + target.toString());

    hollowEscalationActive = true;
    hollowEscalationTarget = target;
    destination = target;
    destinationLocal = target;
    destinationCell = nullptr;
    stuckWatchdogCount = 0;
    lastWatchdogPos = arrivalWorld;
    state = CALCULATING_PATH;
    setStructureTraversalPhase(StructureTraversalPhase::Egress,
        "hollow_escalation");
    uint64 movementGeneration = advanceWorkLoopGeneration(
        "hollowEscalation");

    WorldCoordinates startCoord(arrivalWorld, nullptr);
    WorldCoordinates endCoord(target, nullptr);
    refreshProbeRayHeight();
    float rayHeight = getProbeRayHeight();
    Reference<SimPathFindTask*> task = new SimPathFindTask(this, startCoord,
        endCoord, zone, true, false, rayHeight, agent->getObjectID(),
        movementGeneration);
    task->schedule(100);
    return HollowEscalationOutcome::Started;
}

SimPlayerController::HollowEscalationOutcome
SimPlayerController::completeStructureTraversalIfArrived(
        const Vector3& arrivalWorld) {
    if (!isStructureTraversalFeatureEnabled() || !isTraversalActive() ||
            agent == nullptr)
        return HollowEscalationOutcome::NotHandled;

    if (hollowEscalationActive) {
        ManagedReference<SceneObject*> object = agent->getZone() == nullptr ?
            nullptr : agent->getZone()->getZoneServer()->getObject(
                structureTraversalIntent.owningBuildingOid);
        BuildingObject* building = object == nullptr ? nullptr :
            object->asBuildingObject();
        bool stillInside = isWithinOwningBuildingHollowAt(arrivalWorld,
            building);
        hollowEscalationActive = false;
        hollowEscalationTarget = Vector3(0, 0, 0);
        if (stillInside) {
            SimPlayerManager::instance()->
                recordStructureTraversalHollowEscalationFailed();
            StructureTraversalDiagLog::write(
                "ST_EGRESS escalation=result status=still_inside agent=" +
                String::valueOf(agent->getObjectID()) + " arrival=" +
                arrivalWorld.toString());
            return HollowEscalationOutcome::Failed;
        }

        StructureTraversalDiagLog::write(
            "ST_EGRESS escalation=result status=cleared_hollow agent=" +
            String::valueOf(agent->getObjectID()) + " arrival=" +
            arrivalWorld.toString());
        setStructureTraversalPhase(StructureTraversalPhase::Egress,
            "hollow_escalation_cleared");
        return HollowEscalationOutcome::ResumeFinalDestination;
    }

    if (hollowDoorEgressSelectedCandidateIndex >= 0) {
        int selectedIndex = hollowDoorEgressSelectedCandidateIndex;
        hollowDoorEgressSelectedCandidateIndex = -1;

        auto emitNoCell = [](const String& reason) {
            StructureTraversalDiagLog::write(
                "ST_HOLLOW doorEgress result=no_cell reason=" + reason);
        };

        String noCellReason;
        Zone* zone = agent->getZone();
        ManagedReference<SceneObject*> object = zone == nullptr ? nullptr :
            zone->getZoneServer()->getObject(
                structureTraversalIntent.owningBuildingOid);
        BuildingObject* building = object == nullptr ? nullptr :
            object->asBuildingObject();
        if (building == nullptr) {
            noCellReason = "building_unresolved";
        } else if (selectedIndex >= cellEgressCandidates.size() ||
                selectedIndex >= cellEgressCandidateLocals.size() ||
                selectedIndex >= cellEgressCandidateCellIndexes.size()) {
            noCellReason = "candidate_state_unresolved";
        } else {
            int cellIndex = cellEgressCandidateCellIndexes.get(selectedIndex);
            // <= 0, not < 0: cell id 0 is the exterior and BuildingObject.idl
            // errors + prints a stack trace when asked for it.
            if (cellIndex <= 0) {
                noCellReason = "target_cell_index_unresolved";
            } else {
                CellObject* targetCell = building->getCell(cellIndex);
                if (targetCell == nullptr) {
                    noCellReason = "target_cell_missing";
                } else {
                    Vector3 doorWorld = cellEgressCandidates.get(selectedIndex);
                    Vector3 doorLocal = cellEgressCandidateLocals.get(
                        selectedIndex);
                    float distanceFromBot = doorWorld.distanceTo(arrivalWorld);
                    StructureTraversalDiagLog::write(
                        "ST_HOLLOW doorEgress action=entering door=" +
                        String::valueOf(doorWorld.getX()) + "," +
                        String::valueOf(doorWorld.getY()) + "," +
                        String::valueOf(doorWorld.getZ()) + " cellIndex=" +
                        String::valueOf(cellIndex) + " cellOid=" +
                        String::valueOf(targetCell->getObjectID()) +
                        " distFromBot=" + String::valueOf(distanceFromBot));
                    // NOT moveToInterior(): that uses TraversalMoveOrigin::
                    // External, which clearStructureTraversalState()s as
                    // "external_move_preemption" -- the entry would destroy the
                    // very traversal it is a leg of. Measured: the bot reached
                    // the door, was preempted at +67.3s, and the assertion then
                    // fired against a cleared state. Internal keeps the
                    // traversal alive; set the approach latch by hand.
                    interiorApproachLeg = true;                    moveToWithOrigin(doorWorld, doorLocal, targetCell,
                        TraversalMoveOrigin::Internal);
                    StructureTraversalDiagLog::write(
                        "ST_HOLLOW doorEgress result=entered "
                        "reason=door_cell_resolved");
                    return HollowEscalationOutcome::Started;
                }
            }
        }

        emitNoCell(noCellReason);
    }

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    if (structureTraversalIntent.exitIntent &&
            (parent == nullptr || !parent->isCellObject()) &&
            isWithinOwningBuildingHollow())
        return beginHollowEscalation(arrivalWorld);

    if (structureTraversalIntent.exitIntent &&
            (parent == nullptr || !parent->isCellObject()) &&
            !isWithinOwningBuildingHollow()) {
        if (SimPlayerManager::instance()->
                isStructureTraversalHollowEscalationEnabled())
            hollowEscalationAttempts = 0;
        clearStructureTraversalState("exit_complete_outdoors");
        return HollowEscalationOutcome::NotHandled;
    }

    // A door-entry leg of an EXIT has just landed us inside a cell. The exit is
    // NOT finished: the bot went in the pad-side door and still has to come out
    // the far side, which is the interior leg Retry 6 multicell exists to route.
    // Reporting NotHandled here let the ordinary arrival tail call onArrived()
    // and declare the egress complete while the bot stood inside cell 15.
    if (structureTraversalIntent.exitIntent && interiorApproachLeg &&
            parent != nullptr && parent->isCellObject())
        return HollowEscalationOutcome::ResumeFinalDestination;

    if (structureTraversalIntent.finalTargetCell != nullptr &&
            parent != nullptr && parent->isCellObject() &&
            parent->getObjectID() ==
                structureTraversalIntent.finalTargetCell->getObjectID()) {
        traversalResumeInProgress = false;
        clearStructureTraversalState("target_cell_arrived");
    }
    return HollowEscalationOutcome::NotHandled;
}

void SimPlayerController::recordTraversalMovementStep(
        const Vector3& previousPosition, const Vector3& currentPosition) {
    if (!isStructureTraversalFeatureEnabled() || !isTraversalActive() ||
            agent == nullptr)
        return;

    if (!traversalWatchdogPositionInitialized) {
        traversalLastAppliedWorldPosition = previousPosition;
        traversalWatchdogPositionInitialized = true;
    }

    float delta = traversalLastAppliedWorldPosition.distanceTo(currentPosition);
    float teleportThreshold = SimPlayerManager::instance()->
        getStructureTraversalTeleportAnomalyMeters();
    if (delta > teleportThreshold) {
        traversalTeleportAnomalyCount.fetch_add(1,
            std::memory_order_relaxed);
        SimPlayerManager::instance()->recordStructureTraversalTeleportAnomaly();
        StructureTraversalDiagLog::write(
            "ST_TELEPORT_ANOMALY agent=" + String::valueOf(
                agent->getObjectID()) + " generation=" +
            String::valueOf(traversalGeneration) + " delta=" +
            String::valueOf(delta) + " previous=" +
            previousPosition.toString() + " current=" +
            currentPosition.toString() + " " +
            StructureTraversalDiagLog::fmtPos(agent));
    }

    // The z-sanity reference is CollisionManager::getWorldFloorCollision, which
    // returns the WORLD/terrain floor; CollisionManager exposes no cell-aware
    // overload (CollisionManager.h:66-67). Inside a building that reference is
    // meaningless -- a bot on an interior ramp or an upper floor legitimately
    // sits metres above terrain, so the check fires on correct movement.
    //
    // F_0.8.1 stage 2 measured it: a PvP bot walking a Theed starport ramp to a
    // ticket collector logged 15 consecutive violations, every step an
    // identical 2.40m with dz 0.184m (a constant ~4.4-degree slope) while floor
    // stayed pinned at terrain height 6.0 and the bot climbed to 13.67. Smooth,
    // monotonic, arriving exactly on target, with teleportsDetected at 0 -- a
    // correct walk, not a clip.
    //
    // Outdoors the check keeps its full value, which is where terrain clipping
    // matters and where it was validated. Indoor z-sanity is recorded as
    // coverage debt rather than a silently wrong number; clipping on the path
    // side is already covered by the zero-clip clearance probe.
    ManagedReference<SceneObject*> zSanityParent = agent->getParent().get();
    bool zSanityIndoors = zSanityParent != nullptr &&
        zSanityParent->isCellObject();

    Zone* zone = agent->getZone();
    if (zone != nullptr && !zSanityIndoors) {
        float floor = CollisionManager::getWorldFloorCollision(
            currentPosition.getX(), currentPosition.getY(),
            currentPosition.getZ(), zone, true);
        float zDelta = currentPosition.getZ() - floor;
        if (zDelta < 0.f)
            zDelta = -zDelta;

        if (floor != 0.f && zDelta > SimPlayerManager::instance()->
                getStructureTraversalZSanityMeters()) {
            traversalZSanityViolationCount.fetch_add(1,
                std::memory_order_relaxed);
            SimPlayerManager::instance()->recordStructureTraversalZSanityViolation();
            StructureTraversalDiagLog::write(
                "ST_ZSANITY agent=" + String::valueOf(
                    agent->getObjectID()) + " generation=" +
                String::valueOf(traversalGeneration) + " position=" +
                currentPosition.toString() + " floor=" +
                String::valueOf(floor) + " delta=" + String::valueOf(zDelta) +
                " " + StructureTraversalDiagLog::fmtPos(agent));
        }
    }

    traversalLastAppliedWorldPosition = currentPosition;
}

void SimPlayerController::clearCellEgressState() {
    // Deliberately does NOT reset cellEgressAttempts: the attempt cap must
    // accumulate across repeated in-cell egress failures so it cannot loop
    // forever. The counter is reset only when a move is issued from OUTDOORS
    // (see beginCellEgressIfNeeded) — i.e. once the situation has actually changed.
    cellEgressActive = false;
    cellEgressResumeWorld = Vector3(0, 0, 0);
    cellEgressResumeLocal = Vector3(0, 0, 0);
    cellEgressResumeCell = nullptr;
    cellEgressSuppressed = false;
    hollowDoorEgressSelectedCandidateIndex = -1;
    cellEgressCandidates.removeAll();
    cellEgressCandidateLocals.removeAll();
    cellEgressCandidateCellIndexes.removeAll();
    cellEgressCandidateInHollow.removeAll();
    cellEgressCandidateIndex = 0;
    cellEgressCandidateAttempts = 0;
    cellEgressTotalAttempts = 0;
    cellEgressExitSetBuilt = false;
    cellEgressBudgetExhaustedRecorded = false;
}

void SimPlayerController::failCellEgress() {
    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager != nullptr && manager->isStructureTraversalZeroClipExitSetEnabled() &&
            cellEgressActive) {
        if (!cellEgressExitSetBuilt)
            buildCellEgressExitSet();

        // Keep the egress/traversal state and the bot in its current cell while
        // another authoritative exit candidate remains under budget.
        if (startNextCellEgressCandidate())
            return;
    }

    clearCellEgressState();
    destination = Vector3(0, 0, 0);
    destinationLocal = Vector3(0, 0, 0);
    destinationCell = nullptr;
    simPath.removeAll();
    simPathIndex = 0;
    interiorApproachLeg = false;
    state = IDLE;
    onPathFailed();
}

void SimPlayerController::resetHybridMovementState(bool clearFinalDestination) {
    onMeshMode = false;
    navmeshModeDebounceCounter = 0;
    navmeshRepathAttempts = 0;
    hybridLeg = HYBRID_LEG_NONE;
    hybridEgressPoint = Vector3(0, 0, 0);

    if (clearFinalDestination) {
        finalDestination = Vector3(0, 0, 0);
        hasFinalDestination = false;
    }
}

void SimPlayerController::clearHybridMovementOnCancellation() {
    clearCellEgressState();
    bool hadHybridMovement = isHybridMovementActive() || interiorApproachLeg;
    interiorApproachLeg = false;
    if (!hadHybridMovement)
        return;

    resetHybridMovementState(true);
}

uint64 SimPlayerController::advanceWorkLoopGeneration(const String& reason) {
    (void)reason;
    workLoopGeneration++;

    if (workLoopGeneration == 0)
        workLoopGeneration = 1;

    return workLoopGeneration;
}

bool SimPlayerController::isWorkLoopGenerationCurrent(
        uint64 capturedGeneration, const String& taskType) {
    (void)taskType;
    uint64 currentGeneration = workLoopGeneration;

    if (capturedGeneration == currentGeneration)
        return true;

    return false;
}

void SimPlayerController::onStaleWorkLoopTaskIgnored(
        const String& taskType, uint64 capturedGeneration,
        uint64 currentGeneration) {
#ifdef DEBUG_SIMPVP
    Logger::console.info(
        String("SimPlayerStaleTaskIgnored taskType=") + taskType +
        " capturedGeneration=" + String::valueOf(capturedGeneration) +
        " currentGeneration=" + String::valueOf(currentGeneration),
        true);
#endif
}

String SimPlayerController::getDiagnosticStateName() const {
    switch (state) {
    case IDLE:
        return "IDLE";
    case DECIDING:
        return "DECIDING";
    case SURVEYING:
        return "SURVEYING";
    case CALCULATING_PATH:
        return "CALCULATING_PATH";
    case PERFORMING_ACTION:
        return "PERFORMING_ACTION";
    case MOVING:
        return "MOVING";
    case SAMPLING:
        return "SAMPLING";
    case WAITING:
        return "WAITING";
    default:
        return "UNKNOWN";
    }
}

void SimPlayerController::queueMorePathNodes() {
    if (agent == nullptr) return;
    if (simPathIndex < 0) simPathIndex = 0;

    int pathSize = simPath.size();
    if (simPathIndex >= pathSize) return;

    int currentQueued = agent->getPatrolPointSize();
    int slots = 18 - currentQueued; 

    while (slots > 0 && simPathIndex < pathSize) {
        WorldCoordinates node = simPath.get(simPathIndex);
        Vector3 p = node.getWorldPosition();

        if (simPathIndex == 0) {
            Vector3 cur = agent->getWorldPosition();
            float dx0 = p.getX() - cur.getX();
            float dy0 = p.getY() - cur.getY();
            if ((dx0*dx0 + dy0*dy0) < 1.0f) { 
                simPathIndex++;     
                continue;
            }
        }

        const Vector3& localPoint = node.getPoint();
        PatrolPoint pp(localPoint.getX(), localPoint.getZ(),
            localPoint.getY(), node.getCell());
        agent->addPatrolPoint(pp);

        if (isCellNavDiagAgent(agent.get()))
            CellNavDiagLog::write("QUEUE_NODE index=" +
                String::valueOf(simPathIndex) + " " +
                CellNavDiagLog::fmtPos(pp.getCoordinates()));

        simPathIndex++;
        slots--;
    }
}

void SimPlayerController::checkArrival() {
    if (agent == nullptr || agent->getZone() == nullptr) return;

    // P.6.6: onTick may itself re-drive the work loop (the PvP combat lane issues
    // a fresh moveTo/engage/teardown), each of which advances the generation and
    // arranges its own continuation (moveTo -> SimPathFindTask -> onPathFound, or
    // engage/teardown set IDLE for the successor scheduled below). If that
    // happened, running the rest of checkArrival against this now-stale snapshot
    // would fork a SECOND arrival chain on the same generation (ArrivalCheckTask
    // only rejects stale-generation tasks, not duplicates). Mirror the onArrived
    // tail instead: schedule exactly one successor and stop. For every existing
    // controller onTick is a no-op / pure scan and never advances the generation,
    // so this path is byte-for-byte unchanged for them.
    uint64 preTickGeneration = getWorkLoopGeneration();
    onTick();
    if (getWorkLoopGeneration() != preTickGeneration) {
        // CALCULATING_PATH means a moveTo is in flight and onPathFound owns the
        // next schedule; anything else (engage/teardown left IDLE) needs one here.
        if (state != CALCULATING_PATH && shouldContinueArrivalChecks() &&
                !(isStructureTraversalFeatureEnabled() && isTraversalActive() &&
                    structureTraversalPhase ==
                        StructureTraversalPhase::CombatPaused)) {
            Reference<ArrivalCheckTask*> task =
                new ArrivalCheckTask(this, getWorkLoopGeneration());
            task->schedule(nextArrivalDelayMillis(1000));
        }
        return;
    }

    Locker locker(agent);

    // The getZone() guard at the top of this function runs BEFORE the lock, so
    // a teardown can complete while this task is blocked here: harness bot
    // destruction and recovery despawn both call destroyObjectFromWorld under
    // this same lock. Without a recheck the task resumes on a world-destroyed
    // agent -- refilling patrol points, calling findNextPosition, and recording
    // a movement step whose anomaly tallies have already been folded into the
    // harness slot and are therefore discarded. Rechecking here makes every
    // writer either finish before teardown's fold or exit after destruction.
    // Early return without rescheduling matches the pre-lock guard exactly: a
    // zoneless agent has no arrival chain to continue.
    if (agent->getZone() == nullptr)
        return;

    bool diagnostic = isCellNavDiagAgent(agent.get());
    ManagedReference<SceneObject*> currentParent = agent->getParent().get();
    uint64 currentParentCellOid = currentParent != nullptr &&
        currentParent->isCellObject() ? currentParent->getObjectID() : 0;

    if (isTraversalActive() && isStructureTraversalFeatureEnabled() &&
            !cellEgressActive && currentParent != nullptr &&
            currentParent->isCellObject() &&
            structureTraversalPhase == StructureTraversalPhase::ApproachDoor)
        setStructureTraversalPhase(StructureTraversalPhase::InteriorRoute,
            "entered_structure");

    if (diagnostic) {
        bool parentChanged = diagnosticParentCellInitialized &&
            diagnosticLastParentCellOid != currentParentCellOid;

        if (parentChanged)
            CellNavDiagLog::write("PARENT_CELL_CHANGED old=" +
                String::valueOf(diagnosticLastParentCellOid) + " new=" +
                String::valueOf(currentParentCellOid) + " " +
                CellNavDiagLog::fmtPos(agent.get()));

        diagnosticLastParentCellOid = currentParentCellOid;
        diagnosticParentCellInitialized = true;

        if (SimPlayerManager::instance()->getCellNavDiagLogEveryTick()) {
            String nextPatrol = "none";
            if (agent->getPatrolPointSize() > 0)
                nextPatrol = CellNavDiagLog::fmtPos(
                    agent->getNextPosition().getCoordinates());

            Vector3 diagnosticCurrentWorld = agent->getWorldPosition();
            float diagnosticDx = diagnosticCurrentWorld.getX() -
                destination.getX();
            float diagnosticDy = diagnosticCurrentWorld.getY() -
                destination.getY();

            CellNavDiagLog::write("ARRIVAL_TICK state=" +
                getDiagnosticStateName() + " current=" +
                CellNavDiagLog::fmtPos(agent.get()) + " destination=" +
                    CellNavDiagLog::fmtPos(destination, destinationLocal,
                    destinationCell.get()) + " distance2d=" +
                String::valueOf(Math::sqrt(diagnosticDx * diagnosticDx +
                    diagnosticDy * diagnosticDy)) + " patrolPoints=" +
                String::valueOf(agent->getPatrolPointSize()) +
                " next=" + nextPatrol);
        }
    }

    if (agent->isDead()) {
        if (isStructureTraversalFeatureEnabled() && isTraversalActive()) {
            locker.release();
            clearStructureTraversalState("arrival_dead");
            state = WAITING;
            return;
        }
        // SimPvPController::onTick schedules recycle for dead bots. Do not
        // destroy the object while holding its own lock; that can deadlock
        // against world/database cleanup paths.
        state = WAITING;
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=dead");
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: isDead", true);
#endif
        return;
    }

    if (agent->isIncapacitated()) {
        if (isStructureTraversalFeatureEnabled() && isTraversalActive()) {
            locker.release();
            clearStructureTraversalState("arrival_incapacitated");
            state = WAITING;
            return;
        }
        state = WAITING;
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=incapacitated");
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: isIncapacitated", true);
#endif
        locker.release();
        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
        task->schedule(nextArrivalDelayMillis(1000));
        return;
    }

    bool traversalCombatBusy = agent->isInCombat() ||
        isCombatDriverActive();
    if (traversalCombatBusy && isStructureTraversalFeatureEnabled() &&
            isTraversalActive()) {
        state = IDLE;
        locker.release();
        pauseStructureTraversal("arrival_combat");
        return;
    }

    if (agent->isInCombat()) {
        state = IDLE; 
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=in_combat");
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: isInCombat", true);
#endif
        locker.release();
        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
        task->schedule(nextArrivalDelayMillis(1000));
        return;
    }

    if (isStructureTraversalFeatureEnabled() && isTraversalActive() &&
            structureTraversalPhase == StructureTraversalPhase::CombatPaused) {
        locker.release();
        scheduleStructureTraversalResumeMonitor();
        return;
    }

    if (isHybridMovementActive() &&
            (state == MOVING || (state == IDLE && shouldResumeHybridTravel()))) {
        bool observedOnMesh = agent->isInNavMesh();
        if (observedOnMesh != onMeshMode) {
            navmeshModeDebounceCounter++;
            int debounceTicks =
                SimPlayerManager::instance()->getPveNavmeshModeDebounceTicks();
            if (debounceTicks < 1)
                debounceTicks = 1;

            if (navmeshModeDebounceCounter >= debounceTicks) {
                onMeshMode = observedOnMesh;
                navmeshModeDebounceCounter = 0;
                if (diagnostic)
                    CellNavDiagLog::write("ARRIVAL_BRANCH=hybrid_mode_changed onMesh=" +
                        String::valueOf(onMeshMode));
                locker.release();
                requestHybridPath();
                return;
            }
        } else {
            navmeshModeDebounceCounter = 0;
        }
    }

    if (isHybridMovementActive() && state == IDLE &&
            shouldResumeHybridTravel()) {
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=hybrid_resume");
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Resuming hybrid path to " +
            finalDestination.toString(), true);
#endif
        locker.release();
        requestHybridPath();
        return;
    }

    // An egress leg that went IDLE was interrupted (e.g. a combat hold). The
    // generic resume below would re-drive moveTo() to the ejection waypoint and
    // discard the stashed real destination, so fail the egress here and let the
    // controller's recovery re-issue the real move (re-attempting egress, bounded
    // by the attempt cap).
    if (cellEgressActive && state == IDLE) {
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=cell_egress_interrupted");
        locker.release();
        failCellEgress();
        return;
    }

    if (!isHybridMovementActive() && state == IDLE && destination.getX() != 0) {
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=resume_destination");
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Resuming path to " + destination.toString(), true);
#endif
        Vector3 resumeDestination = destination;
        Vector3 resumeLocalDestination = destinationLocal;
        ManagedReference<CellObject*> resumeCell = destinationCell;
        bool formalTraversal = isStructureTraversalFeatureEnabled() &&
            isTraversalActive();
        if (formalTraversal && resumeCell != nullptr) {
            setStructureTraversalPhase(StructureTraversalPhase::Reentry,
                "idle_resume_reentry");
            interiorApproachLeg = true;
        }
        locker.release();
        if (formalTraversal)
            moveToWithOrigin(resumeDestination, resumeLocalDestination,
                resumeCell.get(), TraversalMoveOrigin::Internal);
        else
            moveTo(resumeDestination, resumeLocalDestination, resumeCell.get());
        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
        task->schedule(nextArrivalDelayMillis(1000));
        return;
    }

    if (state != MOVING) {
        locker.release();

        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=state_not_moving state=" +
                getDiagnosticStateName());

        if (!shouldContinueArrivalChecks())
            return;

        Reference<ArrivalCheckTask*> task =
            new ArrivalCheckTask(this, getWorkLoopGeneration());
        task->schedule(nextArrivalDelayMillis(1000));
        return;
    }

    agent->writeBlackboard("moveMode", BlackboardData((uint32)DataVal::RUN));
    if (agent->isWaiting()) agent->stopWaiting();

    if (agent->getPatrolPointSize() < 5 && simPathIndex < simPath.size()) {
        queueMorePathNodes();
    }

    Vector3 currentPos = agent->getWorldPosition();
    float dx = currentPos.getX() - destination.getX();
    float dy = currentPos.getY() - destination.getY(); 
    float distSq = (dx*dx) + (dy*dy);

    bool arrived = false;

    if (distSq < 16.0f) arrived = true;
    bool queueExhausted = agent->getPatrolPointSize() == 0 &&
        simPathIndex >= simPath.size();
    if (queueExhausted) arrived = true;

    if (isHybridMovementActive() &&
            (hybridLeg == HYBRID_LEG_NAVMESH_FINAL ||
             hybridLeg == HYBRID_LEG_OVERLAND_FINAL)) {
        float finalDx = currentPos.getX() - finalDestination.getX();
        float finalDy = currentPos.getY() - finalDestination.getY();
        if ((finalDx * finalDx) + (finalDy * finalDy) >= 16.0f)
            arrived = false;
    }

    // Cell-egress "arrival" means the agent is actually OUTDOORS, not merely
    // within 4m of the ejection point. The egress path ends with outdoor (cell 0)
    // nodes; without this, the coarse 4m proximity check can fire while the agent
    // is still a few metres inside the last cell (short of the final portal),
    // stranding it. Keep consuming nodes to cross the portal unless the path is
    // genuinely exhausted (truly stuck -> handled as arrived-inside below).
    if (arrived && cellEgressActive && !queueExhausted &&
            currentParent != nullptr && currentParent->isCellObject())
        arrived = false;

    // The MIRROR of the egress guard above, for the entry direction. A leg whose
    // destination is a CellObject has not arrived until the agent's parent IS
    // that cell: the final path node is the one that performs the transition,
    // and the coarse 4m radius fires before it runs.
    //
    // MEASURED 2026-08-26 on the starport door at (3613.8,-4845.38,5.83582):
    // the bot closes to 1.95m and climbs 5.085 -> 5.471 toward the elevated
    // sill, distSq ~= 3.8 < 16 declares "arrived" while parent is still null,
    // and the cell is never entered. Across four matrix runs that single false
    // arrival surfaced as controller_path_failed, then exit_not_outdoors, then
    // exit_budget_exceeded, depending on which handler consumed it.
    //
    // queueExhausted is deliberately still honoured: if the path really is spent
    // and the portal was not crossed, fall through as arrived so the existing
    // stuck/escalation handling reports it instead of looping to the budget.
    if (arrived && !queueExhausted && destinationCell != nullptr &&
            (currentParent == nullptr || currentParent->getObjectID() !=
                destinationCell->getObjectID()))
        arrived = false;

    if (arrived) {
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=arrived final=" +
                CellNavDiagLog::fmtPos(agent.get()) + " destination=" +
                CellNavDiagLog::fmtPos(destination, destinationLocal,
                    destinationCell.get()));
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: Arrived at destination.", true);
#endif
        agent->clearPatrolPoints();
        state = WAITING;

        if (cellEgressActive) {
            bool outdoors = currentParent == nullptr ||
                !currentParent->isCellObject();
            if (outdoors) {
                Vector3 resumeWorld = cellEgressResumeWorld;
                Vector3 resumeLocal = cellEgressResumeLocal;
                ManagedReference<CellObject*> resumeCell = cellEgressResumeCell;
                clearCellEgressState();
                if (diagnostic)
                    CellNavDiagLog::write("CELL_EGRESS_RESUME from=" +
                        CellNavDiagLog::fmtPos(agent.get()) + " to=" +
                        CellNavDiagLog::fmtPos(resumeWorld, resumeLocal,
                            resumeCell.get()));

                bool formalTraversal = isStructureTraversalFeatureEnabled() &&
                    isTraversalActive();
                if (formalTraversal && resumeCell != nullptr) {
                    // Cross-building resume: re-arm the cell-aware approach leg
                    // so hybrid movement cannot drop the target cell.
                    setStructureTraversalPhase(
                        StructureTraversalPhase::Reentry,
                        "egress_complete_reentry");
                    interiorApproachLeg = true;
                } else {
                    clearInteriorApproachLeg();
                }
                locker.release();
                if (formalTraversal) {
                    moveToWithOrigin(resumeWorld, resumeLocal, resumeCell.get(),
                        TraversalMoveOrigin::Internal);
                } else {
                    moveTo(resumeWorld, resumeLocal, resumeCell.get());
                }
                return;
            }

            // The egress path was consumed but the agent is STILL inside a cell:
            // the leg did not actually reach the exterior. Fail the egress rather
            // than falling through to onArrived() (which would report the real
            // destination as reached while wedged inside).
            if (diagnostic)
                CellNavDiagLog::write("CELL_EGRESS_ARRIVED_INSIDE");
            locker.release();
            failCellEgress();
            return;
        }

        if (isHybridMovementActive()) {
            if (hybridLeg == HYBRID_LEG_OVERLAND_FINAL &&
                    agent->isInNavMesh()) {
                // A short wilderness leg can reach a city before two
                // arrival samples have elapsed. Never fire onArrived from a
                // direct route that ended on a navmesh.
                onMeshMode = true;
                navmeshModeDebounceCounter = 0;
                locker.release();
                requestHybridPath();
                return;
            }

            if (hybridLeg == HYBRID_LEG_NAVMESH_EXIT) {
                // The recast leg reached the resolved boundary. The logical
                // destination survives this sub-leg; first cross a bounded,
                // validated off-mesh egress waypoint.
                Vector3 egress = hybridEgressPoint;
                locker.release();
                scheduleHybridDirectPath(egress, HYBRID_LEG_EGRESS);
                return;
            }

            if (hybridLeg == HYBRID_LEG_EGRESS) {
                // This is the sanctioned transition: the egress probe was
                // validated outside every NavArea before it was scheduled.
                if (agent->isInNavMesh()) {
                    locker.release();
                    onPathFailed();
                    return;
                }
                onMeshMode = false;
                navmeshModeDebounceCounter = 0;
                locker.release();
                scheduleHybridDirectPath(finalDestination,
                    HYBRID_LEG_OVERLAND_FINAL);
                return;
            }

            // Only the logical target clears finalDestination. Boundary and
            // egress acceptance above deliberately leave it intact.
            resetHybridMovementState(true);
        }

        bool hollowExitContinuation =
            isTraversalActive() && isStructureTraversalFeatureEnabled() &&
            currentParent != nullptr && currentParent->isCellObject() &&
            structureTraversalPhase == StructureTraversalPhase::Reentry &&
            structureTraversalIntent.exitIntent &&
            structureTraversalIntent.finalTargetCell == nullptr;
        if (isTraversalActive() && isStructureTraversalFeatureEnabled() &&
                currentParent != nullptr && currentParent->isCellObject() &&
                structureTraversalPhase == StructureTraversalPhase::Reentry)
            setStructureTraversalPhase(hollowExitContinuation ?
                StructureTraversalPhase::Egress :
                StructureTraversalPhase::InteriorRoute,
                hollowExitContinuation ? "reentry_complete_egress" :
                    "reentry_complete");

        if (hollowExitContinuation) {
            Vector3 finalWorld = structureTraversalIntent.finalTargetWorld;
            locker.release();
            moveToWithOrigin(finalWorld, finalWorld, nullptr,
                TraversalMoveOrigin::Internal);
            return;
        }

        // The D1 resolver reads the owning building's template and takes the
        // building lock for getNearestExteriorPortalPoint(), and D8's hollow
        // scan performs a read-locked world query. Release the agent lock
        // before entering either path; the gate-off and ordinary arrival paths
        // below retain their existing choreography.
        // ONE branch for both gates. A separate scan-only branch would call
        // onArrived() while ignoring an escalation outcome of Started, racing
        // the movement escalation had just scheduled. The scan runs inside
        // beginHollowEscalation and is purely additive, so the outcome handling
        // below stays correct whichever gate is on.
        bool hollowEscalationArrival =
            (SimPlayerManager::instance()->
                isStructureTraversalHollowEscalationEnabled() ||
             SimPlayerManager::instance()->
                isStructureTraversalHollowScanEnabled() ||
             SimPlayerManager::instance()->
                isStructureTraversalHollowDoorEgressObserveEnabled()) &&
            !agent->isInCombat() && !isCombatDriverActive() &&
            (hollowEscalationActive ||
                (structureTraversalIntent.exitIntent &&
                    (currentParent == nullptr ||
                        !currentParent->isCellObject()) &&
                    isWithinOwningBuildingHollow()));
        if (hollowEscalationArrival) {
            locker.release();
            HollowEscalationOutcome escalationOutcome =
                completeStructureTraversalIfArrived(currentPos);
            if (agent->isInCombat() || isCombatDriverActive()) {
                pauseStructureTraversal("hollow_escalation_combat_race");
                return;
            }
            if (escalationOutcome == HollowEscalationOutcome::Started)
                return;
            if (escalationOutcome == HollowEscalationOutcome::InProgress) {
                // The entry leg owns the bot. Re-arm the arrival check and
                // change nothing else: no onArrived(), no latch clear.
                if (shouldContinueArrivalChecks()) {
                    Reference<ArrivalCheckTask*> task =
                        new ArrivalCheckTask(this, getWorkLoopGeneration());
                    task->schedule(nextArrivalDelayMillis(1000));
                }

                return;
            }
            if (escalationOutcome ==
                    HollowEscalationOutcome::ResumeFinalDestination) {
                Vector3 finalWorld = structureTraversalIntent.finalTargetWorld;
                Vector3 finalLocal = structureTraversalIntent.finalTargetLocal;
                ManagedReference<CellObject*> finalCell =
                    structureTraversalIntent.finalTargetCell;
                clearInteriorApproachLeg();
                moveToWithOrigin(finalWorld, finalLocal, finalCell.get(),
                    TraversalMoveOrigin::Internal);
                return;
            }
            if (escalationOutcome == HollowEscalationOutcome::Failed) {
                onPathFailed();
                return;
            }

            // NotHandled is the ONLY outcome in the Phase 1 configuration
            // (scan on, escalation off), so it must reproduce the ordinary
            // arrival tail EXACTLY -- clearing the interior approach latch and
            // re-arming the arrival check. Falling through with a bare
            // onArrived() would leave a stale latch and terminate the tick
            // chain of any controller whose continuation depends on
            // shouldContinueArrivalChecks(), which would make merely OBSERVING
            // a behaviour change.
            clearInteriorApproachLeg();
            onArrived();

            if (shouldContinueArrivalChecks()) {
                Reference<ArrivalCheckTask*> task =
                    new ArrivalCheckTask(this, getWorkLoopGeneration());
                task->schedule(nextArrivalDelayMillis(1000));
            }

            return;
        }

        // Honour the outcome. Discarding it is what let a mid-traversal leg
        // boundary be reported as the traversal's arrival -- the same mistake
        // the escalation branch above already guards against.
        HollowEscalationOutcome arrivalOutcome =
            completeStructureTraversalIfArrived(currentPos);
        if (arrivalOutcome == HollowEscalationOutcome::ResumeFinalDestination) {
            Vector3 finalWorld = structureTraversalIntent.finalTargetWorld;
            Vector3 finalLocal = structureTraversalIntent.finalTargetLocal;
            ManagedReference<CellObject*> finalCell =
                structureTraversalIntent.finalTargetCell;
            clearInteriorApproachLeg();
            locker.release();
            moveToWithOrigin(finalWorld, finalLocal, finalCell.get(),
                TraversalMoveOrigin::Internal);
            return;
        }

        clearInteriorApproachLeg();

        locker.release();
        onArrived();

        if (shouldContinueArrivalChecks()) {
            Reference<ArrivalCheckTask*> task =
                new ArrivalCheckTask(this, getWorkLoopGeneration());
            task->schedule(nextArrivalDelayMillis(1000));
        }

        return;
    } 

    if (diagnostic)
        CellNavDiagLog::write("ARRIVAL_BRANCH=move_step");

    agent->findNextPosition(2.0f, false);
    recordTraversalMovementStep(currentPos, agent->getWorldPosition());
    
    float moveDx = currentPos.getX() - lastWatchdogPos.getX();
    float moveDy = currentPos.getY() - lastWatchdogPos.getY();
    float movedDistSq = (moveDx*moveDx) + (moveDy*moveDy);

    // --- STUCK WATCHDOG WITH BOUNDED ESCALATION ---
    // No forward progress: first soft-nudge the next step, then re-path a
    // bounded number of times, then give up via onPathFailed() so the planner
    // can reassign instead of the miner spinning in MOVING forever. Any forward
    // progress (else branch) refreshes both counters.
    static const int kStuckSoftNudgeTicks = 5;
    static const int kStuckRePathTicks = 12;
    static const int kMaxRePathAttempts = 2;

    if (movedDistSq < 0.05f) {
        stuckWatchdogCount++;

        if (stuckWatchdogCount >= kStuckRePathTicks) {
            Vector3 resumeDestination = destination;
            Vector3 resumeLocalDestination = destinationLocal;
            ManagedReference<CellObject*> resumeCell = destinationCell;
            locker.release();

            // A stalled cell-egress leg must fail cleanly rather than re-path via
            // moveTo(), which would cancel the egress and lose the stashed real
            // destination. failCellEgress() clears egress state and hands off to
            // onPathFailed recovery; the bounded attempt cap prevents looping.
            if (cellEgressActive) {
                if (diagnostic)
                    CellNavDiagLog::write("ARRIVAL_BRANCH=cell_egress_stuck");
                failCellEgress();
                return;
            }

            if (rePathAttempts < kMaxRePathAttempts && shouldRepathWhenStuck()) {
                rePathAttempts++;
                if (diagnostic)
                    CellNavDiagLog::write("ARRIVAL_BRANCH=stuck_repath attempt=" +
                        String::valueOf(rePathAttempts));
#ifdef DEBUG_SIMPVP
                Logger::console.info("SimPlayer checkArrival: stuck; re-path attempt " + String::valueOf(rePathAttempts), true);
#endif
                // Both paths advance the work-loop generation and schedule a
                // fresh path-find + arrival loop, so do not reschedule here.
                if (isHybridMovementActive())
                    requestHybridPath();
                else if (isStructureTraversalFeatureEnabled() &&
                        isTraversalActive())
                    moveToWithOrigin(resumeDestination, resumeLocalDestination,
                        resumeCell.get(), TraversalMoveOrigin::Internal);
                else
                    moveTo(resumeDestination, resumeLocalDestination,
                        resumeCell.get());
            } else {
                if (diagnostic)
                    CellNavDiagLog::write("ARRIVAL_BRANCH=stuck_watchdog_exhausted");
#ifdef DEBUG_SIMPVP
                Logger::console.info("SimPlayer checkArrival: stuck; re-path budget exhausted, failing path.", true);
#endif
                onPathFailed();
            }

            return;
        }

        if (stuckWatchdogCount > kStuckSoftNudgeTicks) {
        if (diagnostic)
            CellNavDiagLog::write("ARRIVAL_BRANCH=stuck_soft_nudge count=" +
                String::valueOf(stuckWatchdogCount));
#ifdef DEBUG_SIMPVP
        Logger::console.info("SimPlayer checkArrival: stuckWatchdogCount > 5.", true);
#endif
             if (agent->getPatrolPointSize() > 0) {
                 PatrolPoint next = agent->getNextPosition();
                 agent->setNextStepPosition(next.getPositionX(), next.getPositionZ(), next.getPositionY(), next.getCell());
             }
             agent->activateAiBehavior(true);
        }
    } else {
        stuckWatchdogCount = 0;
        rePathAttempts = 0;
    }

    lastWatchdogPos = currentPos;

    locker.release();
    Reference<ArrivalCheckTask*> task =
        new ArrivalCheckTask(this, getWorkLoopGeneration());
    task->schedule(nextArrivalDelayMillis(500));
}

bool SimPlayerController::pickDestinationInNavMesh(Zone* zone, const Vector3& currentPos, Vector3& out, int minSearchRadius, int maxSearchRadius) {
    if (zone == nullptr || agent == nullptr) return false;
    if (!agent->isInNavMesh()) return false;

    if (minSearchRadius < 1)
        minSearchRadius = 1;

    if (maxSearchRadius < minSearchRadius)
        maxSearchRadius = minSearchRadius;

    int distance = minSearchRadius;
    if (maxSearchRadius > minSearchRadius)
        distance += System::random(maxSearchRadius - minSearchRadius);

    Sphere area(currentPos, (float)distance);

    Vector3 result;
    if (PathFinderManager::instance()->getSpawnPointInArea(area, zone, result, true) &&
            zone->isWithinBoundaries(result)) {
        out = result;
        return true;
    }
    return false;
}

// ========================================================
// CELL-NAVIGATION DIAGNOSTIC CONTROLLER
// ========================================================

SimCellNavDiagController::SimCellNavDiagController(AiAgent* aiAgent)
        : SimPlayerController(aiAgent) {
    diagnosticWorldPos = Vector3(0, 0, 0);
    diagnosticLocalPos = Vector3(0, 0, 0);
    diagnosticCell = nullptr;
    diagnosticRouteReady = false;
    diagnosticRouteIssued = false;
    diagnosticExitWorldPos = Vector3(0, 0, 0);
    diagnosticExitReady = false;
    diagnosticExitIssued = false;
    diagnosticReturnWorldPos = Vector3(0, 0, 0);
    diagnosticReturnReady = false;
    diagnosticReturnIssued = false;
    diagnosticFinalIssued = false;
    setLoggingName("SimCellNavDiagController");
}

SimCellNavDiagController::~SimCellNavDiagController() {
}

void SimCellNavDiagController::setDiagnosticRoute(const Vector3& worldPos,
        const Vector3& localPos, CellObject* cell) {
    diagnosticWorldPos = worldPos;
    diagnosticLocalPos = localPos;
    diagnosticCell = cell;
    diagnosticRouteReady = cell != nullptr;
}

void SimCellNavDiagController::startSimLoop() {
    if (diagnosticRouteIssued || !diagnosticRouteReady || agent == nullptr) {
        state = WAITING;
        return;
    }

    diagnosticRouteIssued = true;
    state = WAITING;

    logCellNavDiag(agent.get(), "DIAG_ROUTE_REQUEST " +
        CellNavDiagLog::fmtPos(diagnosticWorldPos, diagnosticLocalPos,
            diagnosticCell.get()));
    moveToInterior(diagnosticWorldPos, diagnosticLocalPos,
        diagnosticCell.get());
}

void SimCellNavDiagController::setDiagnosticExit(const Vector3& exitWorldPos) {
    diagnosticExitWorldPos = exitWorldPos;
    diagnosticExitReady = true;
}

void SimCellNavDiagController::setDiagnosticReturn(const Vector3& returnWorldPos) {
    diagnosticReturnWorldPos = returnWorldPos;
    diagnosticReturnReady = true;
}

void SimCellNavDiagController::onArrived() {
    state = WAITING;
    logCellNavDiag(agent.get(), "ARRIVED final=" +
        CellNavDiagLog::fmtPos(agent.get()));

    // Round-trip: the first arrival is INSIDE the cell. Issue the exit leg back
    // to an outdoor point (cell=nullptr) so findPath(cell-origin -> outdoor) and
    // the whole exit is traced. The subsequent moveTo -> onPathFound restarts the
    // arrival loop even though shouldContinueArrivalChecks() is false.
    if (diagnosticExitReady && !diagnosticExitIssued) {
        diagnosticExitIssued = true;
        // RESEARCH: reach the (enclosed hollow) collector by a DIRECTED route
        // through the portal graph — suppress the generic egress that would
        // otherwise send the bot out the nearest front door and around the
        // perimeter (which can't reach the walled hollow).
        cellEgressSuppressed = true;
        logCellNavDiag(agent.get(), "DIAG_EXIT_REQUEST directRoute=1 from=" +
            CellNavDiagLog::fmtPos(agent.get()) + " to=" +
            CellNavDiagLog::fmtPos(diagnosticExitWorldPos,
                diagnosticExitWorldPos, nullptr));
        moveTo(diagnosticExitWorldPos, diagnosticExitWorldPos, nullptr);
        return;
    }

    if (diagnosticExitIssued && !diagnosticReturnIssued) {
        float dxc = agent->getWorldPosition().getX() -
            diagnosticExitWorldPos.getX();
        float dyc = agent->getWorldPosition().getY() -
            diagnosticExitWorldPos.getY();
        logCellNavDiag(agent.get(), "DIAG_ROUNDTRIP_COMPLETE final=" +
            CellNavDiagLog::fmtPos(agent.get()) + " collectorTarget=(" +
            String::valueOf(diagnosticExitWorldPos.getX()) + "," +
            String::valueOf(diagnosticExitWorldPos.getY()) + ") distToCollector=" +
            String::valueOf(Math::sqrt(dxc * dxc + dyc * dyc)));

        // Leg 3a (arrival/landing): findPath(hollow cell0 -> outside cell0) will
        // NOT route through the portal graph (both cell 0 -> direct into the wall).
        // Re-ENTER a cell first (target IS a cell -> findPath routes through the
        // hollow portal into the building); leg 3b then egresses out to the world.
        if (diagnosticReturnReady) {
            diagnosticReturnIssued = true;
            cellEgressSuppressed = false;
            logCellNavDiag(agent.get(), "DIAG_REENTER_REQUEST from=" +
                CellNavDiagLog::fmtPos(agent.get()) + " toCell=" +
                CellNavDiagLog::fmtPos(diagnosticWorldPos, diagnosticLocalPos,
                    diagnosticCell.get()));
            moveToInterior(diagnosticWorldPos, diagnosticLocalPos,
                diagnosticCell.get());
        }
        return;
    }

    if (diagnosticReturnIssued && !diagnosticFinalIssued) {
        // Leg 3b: back inside a cell -> egress (enabled) out to the world.
        diagnosticFinalIssued = true;
        logCellNavDiag(agent.get(), "DIAG_REENTER_COMPLETE " +
            CellNavDiagLog::fmtPos(agent.get()) + " -> LANDING_EXIT_REQUEST to=" +
            CellNavDiagLog::fmtPos(diagnosticReturnWorldPos,
                diagnosticReturnWorldPos, nullptr));
        moveTo(diagnosticReturnWorldPos, diagnosticReturnWorldPos, nullptr);
        return;
    }

    if (diagnosticFinalIssued) {
        float dxr = agent->getWorldPosition().getX() -
            diagnosticReturnWorldPos.getX();
        float dyr = agent->getWorldPosition().getY() -
            diagnosticReturnWorldPos.getY();
        logCellNavDiag(agent.get(), "DIAG_LANDING_EXIT_COMPLETE final=" +
            CellNavDiagLog::fmtPos(agent.get()) + " outsideTarget=(" +
            String::valueOf(diagnosticReturnWorldPos.getX()) + "," +
            String::valueOf(diagnosticReturnWorldPos.getY()) + ") distToOutside=" +
            String::valueOf(Math::sqrt(dxr * dxr + dyr * dyr)));
    }
}

void SimCellNavDiagController::onPathFailed() {
    if (isStructureTraversalFeatureEnabled() && isTraversalActive()) {
        SimPlayerController::onPathFailed();
        return;
    }

    state = WAITING;
    logCellNavDiag(agent.get(), "PATH_FAILED diagnostic_abort current=" +
        CellNavDiagLog::fmtPos(agent.get()));
}

// ========================================================
// STRUCTURE-TRAVERSAL SCENARIO CONTROLLER
// ========================================================

SimTraversalTestController::SimTraversalTestController(AiAgent* aiAgent)
        : SimPlayerController(aiAgent) {
    setLoggingName("SimTraversalTestController");
}

SimTraversalTestController::~SimTraversalTestController() {
}

void SimTraversalTestController::startSimLoop() {
    if (agent == nullptr)
        return;

    state = WAITING;
    SimPlayerManager::instance()->startStructureTraversalTestBot(
        agent->getObjectID());
}

void SimTraversalTestController::onArrived() {
    if (agent != nullptr)
        SimPlayerManager::instance()->notifyStructureTraversalTestArrived(
            agent->getObjectID());
}

void SimTraversalTestController::onPathFailed() {
    if (agent != nullptr)
        SimPlayerManager::instance()->notifyStructureTraversalTestPathFailed(
            agent->getObjectID(), "controller_path_failed");
    SimPlayerController::onPathFailed();
}

void SimTraversalTestController::onPathFound(Vector<WorldCoordinates>* path,
        bool pathUsesNavmesh, bool pathIsOverland) {
    if (harnessForcePathFailure) {
        // Deliver the failure exactly where SimPathFindTask delivers a null
        // findPath result, so the bounded-failure scenario exercises the real
        // onPathTaskFailed -> onPathFailed path (and its state cleanup).
        harnessForcePathFailure = false;
        if (path != nullptr)
            delete path;
        StructureTraversalDiagLog::write(
            "SCENARIO_FORCED_PATH_FAILURE agent=" + String::valueOf(
                agent == nullptr ? 0 : agent->getObjectID()));
        onPathTaskFailed(pathUsesNavmesh);
        return;
    }

    SimPlayerController::onPathFound(path, pathUsesNavmesh, pathIsOverland);
}

void SimTraversalTestController::onTick() {
    if (agent != nullptr)
        SimPlayerManager::instance()->notifyStructureTraversalTestTick(
            agent->getObjectID());
}

void SimTraversalTestController::issueResolvedStep(
        const StructureTraversalTestStep& step, Vector3 targetWorld,
        Vector3 targetLocal, CellObject* targetCell) {
    if (step.op == "enter") {
        enterStructure(targetWorld, targetLocal, targetCell);
    } else if (step.op == "exit") {
        Vector3 destination(step.destination.x, step.destination.y,
            step.destination.z);
        if (destination.getX() == 0.f && destination.getY() == 0.f &&
                destination.getZ() == 0.f)
            destination = targetWorld;
        exitStructure(destination);
    } else if (step.op == "moveTo") {
        moveTo(Vector3(step.destination.x, step.destination.y,
            step.destination.z));
    }
}

void SimTraversalTestController::applyHarnessCombatDisplacement(
        const String& zoneName, const StructureTraversalTestPoint& point) {
    if (agent == nullptr || agent->getZone() == nullptr)
        return;

    ZoneServer* zoneServer = ServerCore::getZoneServer();
    Zone* zone = zoneServer == nullptr ? nullptr :
        zoneServer->getZone(zoneName);
    if (zone == nullptr)
        return;

    {
        Locker locker(agent);
        CellObject* parentCell = nullptr;
        if (point.cellOid != 0) {
            ManagedReference<SceneObject*> parentObject =
                zone->getZoneServer()->getObject(point.cellOid);
            parentCell = parentObject == nullptr ? nullptr :
                parentObject.castTo<CellObject*>();
        }
        agent->setMovementState(AiAgent::OBLIVIOUS);
        agent->clearPatrolPoints();
        agent->clearSavedPatrolPoints();
        agent->clearCurrentPath();

        // Do not call prepareForRelocation here: the intent and traversal
        // generation are deliberately part of the combat test.
        advanceWorkLoopGeneration("harnessCombatDisplacement");
        agent->switchZone(zoneName, point.x, point.z, point.y, point.cellOid);
        agent->setHomeLocation(point.x, point.z, point.y, parentCell);
    }

    // The jump above is intentional; it is the new watchdog baseline.
    traversalLastAppliedWorldPosition = agent->getWorldPosition();
    traversalWatchdogPositionInitialized = true;
}

bool SimTraversalTestController::acceptFoundPathHook(const Vector3& pathEnd) {
    // The base invariants already ran; this adds only the harness's own
    // complete-path measuring instrument.
    SimPlayerManager* manager = SimPlayerManager::instance();

    // Default-off: without the gate this is the base behaviour exactly.
    if (manager == nullptr ||
            !manager->isStructureTraversalRequireCompletePath())
        return true;

    float tolerance = manager->
        getStructureTraversalCompletePathToleranceMeters();
    float endDistance = pathEnd.distanceTo(destination);

    if (endDistance <= tolerance)
        return true;

    // Rejecting routes this through the existing bounded onPathFailed retry
    // rather than walking a route that cannot reach the target and then waiting
    // for an arrival that will never happen.
    StructureTraversalDiagLog::write(
        "ST_PATH result=rejected reason=incomplete_path agent=" +
        String::valueOf(agent == nullptr ? 0 : agent->getObjectID()) +
        " generation=" + String::valueOf(traversalGeneration) +
        " endDist=" + String::valueOf(endDistance) +
        " tolerance=" + String::valueOf(tolerance) +
        " pathEnd=(" + String::valueOf(pathEnd.getX()) + "," +
        String::valueOf(pathEnd.getY()) + "," +
        String::valueOf(pathEnd.getZ()) + ")" +
        " destination=(" + String::valueOf(destination.getX()) + "," +
        String::valueOf(destination.getY()) + "," +
        String::valueOf(destination.getZ()) + ")");

    return false;
}

bool SimTraversalTestController::usesNavmeshHybridMovement() const {
    SimPlayerManager* manager = SimPlayerManager::instance();

    return manager != nullptr &&
        manager->isStructureTraversalUseNavmeshHybrid();
}

bool SimTraversalTestController::isHarnessOutdoorsClearFor(
        uint64 buildingOid) const {
    if (agent == nullptr)
        return false;

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    if (parent != nullptr && parent->isCellObject())
        return false;

    // Fail CLOSED. Every reason the building cannot be checked -- no OID, no
    // zone, a dead OID, a non-building object -- used to report "outdoors",
    // which turns the hollow half of the harness exit assertion into a pass
    // for the one bot state it exists to catch.
    if (buildingOid == 0 || agent->getZone() == nullptr)
        return false;

    ManagedReference<SceneObject*> object = agent->getZone()->getZoneServer()->
        getObject(buildingOid);
    BuildingObject* building = object == nullptr ? nullptr :
        object->asBuildingObject();

    if (building == nullptr)
        return false;

    return !isWithinOwningBuildingHollowAt(agent->getWorldPosition(), building);
}

String SimTraversalTestController::describeHarnessOutdoorsStateFor(
        uint64 buildingOid) const {
    if (agent == nullptr)
        return String("scenarioBuilding=? state=agent_missing");

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    bool inCell = parent != nullptr && parent->isCellObject();
    Vector3 pos = agent->getWorldPosition();
    bool inHollow = false;
    float missDistance = -1.f;

    if (buildingOid != 0 && agent->getZone() != nullptr) {
        ManagedReference<SceneObject*> object = agent->getZone()->
            getZoneServer()->getObject(buildingOid);
        BuildingObject* building = object == nullptr ? nullptr :
            object->asBuildingObject();

        if (building != nullptr) {
            inHollow = isWithinOwningBuildingHollowAt(pos, building);
            missDistance = getOwningBuildingHollowMissDistance(pos, building);
        }
    }

    return String("scenarioBuilding=") + String::valueOf(buildingOid) +
        " inCell=" + String::valueOf(inCell ? 1 : 0) +
        " inHollowOfScenarioBuilding=" + String::valueOf(inHollow ? 1 : 0) +
        " hollowMissDistance=" + String::valueOf(missDistance) +
        " pos=(" + String::valueOf(pos.getX()) + "," +
        String::valueOf(pos.getY()) + "," + String::valueOf(pos.getZ()) + ")";
}

String SimTraversalTestController::describeHarnessOutdoorsState() const {
    if (agent == nullptr)
        return String("parentCell=agent_missing inHollow=? pos=?");

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    bool inCell = parent != nullptr && parent->isCellObject();
    bool inHollow = isWithinOwningBuildingHollow();
    Vector3 pos = agent->getWorldPosition();

    return String("parentCell=") +
        (inCell ? String::valueOf(parent->getObjectID()) : String("none")) +
        " inCell=" + String::valueOf(inCell ? 1 : 0) +
        " inHollow=" + String::valueOf(inHollow ? 1 : 0) +
        " pos=(" + String::valueOf(pos.getX()) + "," +
        String::valueOf(pos.getY()) + "," + String::valueOf(pos.getZ()) + ")" +
        " owningBuilding=" +
        String::valueOf(structureTraversalIntent.owningBuildingOid);
}

bool SimTraversalTestController::isHarnessOutdoorsClear() const {
    if (agent == nullptr)
        return false;

    ManagedReference<SceneObject*> parent = agent->getParent().get();
    return (parent == nullptr || !parent->isCellObject()) &&
        !isWithinOwningBuildingHollow();
}

// ========================================================
// SIM MINER CONTROLLER
// ========================================================

SimMinerController::SimMinerController(AiAgent* aiAgent) : SimMinerController(aiAgent, SimMinerConfig()) {
}

SimMinerController::SimMinerController(AiAgent* aiAgent, const SimMinerConfig& minerConfig) : SimPlayerController(aiAgent) {
    retryCount = 0;
    config = minerConfig;
    intelligentAssignmentPending = false;
    intelligentAssignmentActive = false;
    intelligentSampleActive = false;
    intelligentAssignmentStationed = false;
    intelligentLogActivationLifecycle = true;
    intelligentQueuedDuringSample = false;
    intelligentQueuedAtMs = 0;
    intelligentAssignmentGenerationId = 0;
    intelligentActivationSnapshotId = 0;
    intelligentTargetDensity = 0.f;
    intelligentAssignmentExpiresAtMs = 0;
    intelligentFinalApproachAttempts = 0;
    intelligentLastApproachDistance = 0.f;
    setLoggingName("SimMinerController");
}

SimMinerController::~SimMinerController() {
}

void SimMinerController::prepareForInterplanetaryTravelDeparture() {
    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager != nullptr && manager->isTicketCollectorTravelEnabled())
        dismountIfMounted("ticketCollectorDeparture");
    else
        maybeMountForTravel(travelDeparturePosition);
}

void SimMinerController::prepareForTicketCollectorEntry(
        const String& reason) {
    dismountIfMounted(reason);
}

void SimMinerController::prepareForInterplanetaryTravelBoarding(
        const String&) {
    dismountIfMounted("boardShuttle");
}

void SimMinerController::onInterplanetaryTravelBoarded(const String& fromZone,
        const String& destZone, const String& starport, const String& reason) {
    // Miner-only telemetry: this writes planetDispatch's boarded counter and
    // last-boarded fields. Hunters deliberately do not reach it.
    SimPlayerManager* manager = SimPlayerManager::instance();
    uint64 minerID = agent == nullptr ? 0 : agent->getObjectID();
    if (manager != nullptr && minerID != 0)
        manager->recordInterplanetaryTravelBoarded(
            minerID, fromZone, destZone, starport, reason);
}

void SimMinerController::onInterplanetaryTravelFinished(bool success,
        const String&, const String& reason) {
    // Invalid/busy entry attempts never activated the travel state and must
    // remain no-ops, exactly as before the state machine was lifted.
    if (!interplanetaryTravelActive)
        return;

    // The old board path cleared only local state when the controller had
    // already disappeared: no manager notification and no work-loop re-entry,
    // but still a full local reset (which advances the work-loop generation and
    // so invalidates any in-flight sample/arrival task). Retain that terminal
    // behavior exactly while routing it through the shared hook.
    if (!success && agent == nullptr && reason == "controllerUnavailable") {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    if (!success && reason != "invalidDestination") {
        uint64 minerID = agent == nullptr ? 0 : agent->getObjectID();
        SimPlayerManager* manager = SimPlayerManager::instance();
        if (manager != nullptr && minerID != 0)
            manager->clearMinerIntelligentTargetAssignmentFromController(
                minerID, reason);
    }

    resetIntelligentAssignmentForRecovery();
}

void SimMinerController::startSimLoop() {
    String activationResult;

    // P.4.5b: while traveling, the run to the ticket collector is driven by
    // moveTo()/checkArrival(); the normal decision loop must not clobber it.
    if (interplanetaryTravelActive) {
        state = WAITING;
        logLegacyLoopSuppressed("interplanetaryTravelActive");
        return;
    }

    if (intelligentAssignmentPending && beginIntelligentTargetAssignment(activationResult)) {
        uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
        SimPlayerManager::instance()->recordIntelligentMinerLoopStarted(
            sourceObjectID, "pendingAssignment");
        return;
    }

    if (intelligentAssignmentStationed) {
        state = WAITING;
        logLegacyLoopSuppressed("stationedLifecycleActive");
        return;
    }

    if (intelligentAssignmentActive || intelligentSampleActive) {
        logLegacyLoopSuppressed("intelligentAssignmentActive");
        return;
    }

    if (SimPlayerManager::instance()->isIntelligentMinerWorkLoopOwnerEnabled() &&
            !SimPlayerManager::instance()->isLegacyConceptualMinerLoopAllowed()) {
        state = WAITING;
        logLegacyLoopSuppressed("waitingForIntelligentAssignment");
        return;
    }

    advanceWorkLoopGeneration("legacyLoopStarted");
    state = DECIDING;
    String res = pickRandomResource();
    targetResource = res;
    uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
    SimPlayerManager::instance()->recordLegacyMinerLoopStarted(
        sourceObjectID, "conceptualFallbackAllowed");
    logStateTransition("SimMinerLegacyLoopStarted: selected conceptual resource [" + res + "]");
    performSurvey();
}

String SimMinerController::pickRandomResource() {
    if (config.resources.size() == 0) {
        int roll = System::random(4);
        if (roll == 0) return "iron";
        if (roll == 1) return "gas";
        if (roll == 2) return "water";
        return "copper";
    }

    if (config.resources.size() == 1)
        return config.resources.get(0);

    int index = System::random(config.resources.size() - 1);
    return config.resources.get(index);
}

String SimMinerController::getSimStateName(SimState simState) const {
    switch (simState) {
    case IDLE:
        return "idle";
    case DECIDING:
        return "deciding";
    case SURVEYING:
        return "surveying";
    case CALCULATING_PATH:
        return "calculating_path";
    case PERFORMING_ACTION:
        return "performing_action";
    case MOVING:
        return "moving";
    case SAMPLING:
        return "sampling";
    case WAITING:
        return "waiting";
    default:
        return "unknown";
    }
}

void SimMinerController::performSurvey() {
    if (agent == nullptr) return;

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySurveyBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    state = SURVEYING;
    logStateTransition("SimMinerLegacySurveyStarted resource=" + targetResource);

    agent->setMovementState(AiAgent::OBLIVIOUS);
    if (agent->getPosture() != CreaturePosture::UPRIGHT) {
        agent->setPosture(CreaturePosture::UPRIGHT, true);
    }
    agent->doAnimation("manipulate_high"); 

    Reference<SimBehaviorTask*> task =
        new SimBehaviorTask(this, SimBehaviorTask::FINISH_SURVEY,
            getWorkLoopGeneration());
    task->schedule(config.surveyDurationMs);
}

void SimMinerController::finishSurvey() {
    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySurveyFinishBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    logStateTransition("SimMinerLegacySurveyFinished resource=" + targetResource);
    goToResource(targetResource);
}

// P.4.4b mounted travel. Deploy+mount a real swoop (proven P.4.4a manager
// plumbing) when the upcoming leg is long enough, and ride it at the vehicle's
// own run speed the same way a mounted player does (CreatureObject::getRunSpeed
// returns the vehicle's speed for riders; AiAgent::findNextPosition reads the
// raw member, so we copy the value explicitly and restore it on dismount).
// LOCKING: never call the manager mount/dismount functions with the agent
// locked — they take their own agent+vehicle crosslocks (see the 2026-07-02
// deadlock postmortem in docs/npc-mount-and-player-dot-plan.md).
void SimMinerController::maybeMountForTravel(const Vector3& target) {
    if (mountedForTravel)
        return;

    ManagedReference<AiAgent*> strongAgent = agent;
    if (strongAgent == nullptr)
        return;

    SimPlayerManager* manager = SimPlayerManager::instance();
    if (!manager->isMountedTravelEnabled())
        return;

    Vector3 pos;
    float baseRunSpeed = 0.f;
    {
        Locker lock(strongAgent);
        if (strongAgent->isRidingMount() || strongAgent->getParent().get() != nullptr)
            return;
        pos = strongAgent->getWorldPosition();
        baseRunSpeed = strongAgent->getRunSpeed();
    }

    float minLeg = (float)manager->getMountedTravelMinLegMeters();
    float dx = pos.getX() - target.getX();
    float dy = pos.getY() - target.getY();
    if ((dx * dx + dy * dy) < minLeg * minLeg)
        return;

    String result;
    if (!manager->deployAndMountMinerVehicle(strongAgent->getObjectID(), result)) {
        logStateTransition("SimMinerMountedTravel mountFailed result=" + result +
            "; continuing on foot");
        return;
    }

    float vehicleSpeed = 0.f;
    {
        Locker lock(strongAgent);
        ManagedReference<SceneObject*> parent = strongAgent->getParent().get();
        if (parent != nullptr && parent->isVehicleObject()) {
            CreatureObject* vehicle = parent->asCreatureObject();
            if (vehicle != nullptr)
                vehicleSpeed = vehicle->getRunSpeed();
        }
        if (vehicleSpeed > 0.f) {
            preMountRunSpeed = baseRunSpeed;
            strongAgent->setRunSpeed(vehicleSpeed);
        }
    }

    mountedForTravel = true;
    manager->recordMountedTravelLegStarted();
    logStateTransition("SimMinerMountedTravel mounted speed=" +
        String::valueOf(vehicleSpeed) + " legMeters=" +
        String::valueOf(Math::sqrt(dx * dx + dy * dy)));
}

void SimMinerController::dismountIfMounted(const String& reason) {
    if (!mountedForTravel)
        return;

    mountedForTravel = false;

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent != nullptr && preMountRunSpeed > 0.f) {
        Locker lock(strongAgent);
        strongAgent->setRunSpeed(preMountRunSpeed);
    }
    preMountRunSpeed = 0.f;

    if (strongAgent == nullptr)
        return;

    String result;
    SimPlayerManager::instance()->dismountAndStoreMinerVehicle(
        strongAgent->getObjectID(), result);
    logStateTransition("SimMinerMountedTravel dismounted reason=" + reason +
        " result=" + result);
}

void SimMinerController::goToResource(const String& resourceName) {
    if (agent == nullptr) return;

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacyMoveBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    Zone* zone = agent->getZone();
    if (zone == nullptr) return;

    Vector3 currentPos = agent->getWorldPosition();
    Vector3 targetPos;
    bool usedFallback = false;

    if (!pickDestinationInNavMesh(zone, currentPos, targetPos, config.minSearchRadius, config.maxSearchRadius)) {
        float angle = System::random(360) * (M_PI / 180.0f);
        float dist = (float)config.fallbackRadius;
        targetPos.setX(currentPos.getX() + (dist * cos(angle)));
        targetPos.setY(currentPos.getY() + (dist * sin(angle)));

        if (!zone->isWithinBoundaries(targetPos)) {
            // Near an edge, bias the fallback toward the planet center instead
            // of allowing the conceptual loop to wander beyond terrain bounds.
            float currentDistance = Math::sqrt(
                currentPos.getX() * currentPos.getX() +
                currentPos.getY() * currentPos.getY());

            if (currentDistance > 0.f) {
                targetPos.setX(currentPos.getX() -
                    currentPos.getX() / currentDistance * dist);
                targetPos.setY(currentPos.getY() -
                    currentPos.getY() / currentDistance * dist);
            }
        }

        if (!zone->isWithinBoundaries(targetPos)) {
            logStateTransition("SimMinerLegacyMoveBlocked: no in-bounds fallback destination for [" +
                resourceName + "]; retrying loop");
            onPathFailed();
            return;
        }

        targetPos.setZ(zone->getHeight(targetPos.getX(), targetPos.getY()));
        usedFallback = true;
    }

    String destinationSource = usedFallback ? "fallback" : "navmesh";
    logStateTransition("SimMinerLegacyMoveStarted resource=" + resourceName + " destinationSource=" + destinationSource + " target=" + targetPos.toString());
    rePathAttempts = 0;
    maybeMountForTravel(targetPos);
    moveTo(targetPos);
}

bool SimPlayerController::isAtTicketCollector() const {
    ManagedReference<AiAgent*> strongAgent = agent;
    if (strongAgent == nullptr || !ticketCollectorFound)
        return false;

    Locker agentLocker(strongAgent);
    if (strongAgent->getWorldPosition().distanceTo(ticketCollectorWorld) >
            travelBoardRadius)
        return false;

    ManagedReference<SceneObject*> parent = strongAgent->getParent().get();
    if (ticketCollectorCell != nullptr)
        return parent != nullptr && parent->isCellObject() &&
            parent->getObjectID() == ticketCollectorCell->getObjectID();

    // The proven starport collector is cell 0/rootParent 0. A cell-less bot
    // at the collector is therefore in the hollow/outdoor containment; a bot
    // still parented to an interior cell is not boardable.
    return parent == nullptr || !parent->isCellObject();
}

bool SimPlayerController::canRetryTicketApproach() const {
    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager == nullptr)
        return false;

    if (ticketApproachAttempts >= manager->getTicketCollectorApproachAttempts())
        return false;

    return travelStartedAtMs == 0 ||
        System::getMiliTime() <= travelStartedAtMs +
            (uint64)manager->getTicketCollectorApproachTtlSeconds() * 1000;
}

void SimPlayerController::retryTicketApproach(const String& reason) {
    if (canRetryTicketApproach()) {
        beginTicketCollectorDepartureApproach(reason);
        return;
    }

    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager != nullptr && manager->isTicketCollectorFallbackToBoardFromNear())
        boardInterplanetaryShuttle("collectorUnreachable");
    else
        cancelTicketCollectorTravel("collectorApproachExhausted");
}

void SimPlayerController::beginTicketCollectorDepartureApproach(
        const String& reason) {
    if (!interplanetaryTravelActive || agent == nullptr)
        return;

    if (ticketApproachAttempts >=
            SimPlayerManager::instance()->getTicketCollectorApproachAttempts() ||
            (travelStartedAtMs != 0 && System::getMiliTime() >
                travelStartedAtMs + (uint64)SimPlayerManager::instance()->
                    getTicketCollectorApproachTtlSeconds() * 1000)) {
        retryTicketApproach(reason + ":exhausted");
        return;
    }

    ticketApproachAttempts++;

    ManagedReference<AiAgent*> strongAgent = agent;
    ManagedReference<Zone*> zone;
    Vector3 currentWorld;
    {
        Locker agentLocker(strongAgent);
        zone = strongAgent->getZone();
        currentWorld = strongAgent->getWorldPosition();
    }

    if (zone == nullptr) {
        retryTicketApproach("missingZone");
        return;
    }

    if (!ticketCollectorFound) {
        if (!SimPlayerManager::instance()->resolveNearestTicketCollector(
                zone, travelDeparturePosition, ticketCollectorWorld,
                ticketCollectorLocal, ticketCollectorCell, ticketCollectorOid)) {
            retryTicketApproach("collectorNotFound");
            return;
        }
        ticketCollectorFound = true;
    }

    if (isAtTicketCollector()) {
        ticketTravelPhase = TICKET_DEPARTURE_COLLECTOR;
        boardInterplanetaryShuttle("collectorReached");
        return;
    }

    Vector3 interiorWorld;
    Vector3 interiorLocal;
    ManagedReference<CellObject*> interiorCell;
    SimPlayerManager::StarportInteriorWaypointResult result =
        SimPlayerManager::instance()->resolveStarportInteriorWaypoint(
            zone, travelDeparturePosition, currentWorld, interiorWorld,
            interiorLocal, interiorCell);

    if (result == SimPlayerManager::STARPORT_RESOLVE_FAILED) {
        retryTicketApproach("interiorResolveFailed");
        return;
    }

    if (result == SimPlayerManager::STARPORT_WAYPOINT_FOUND) {
        // The derived controller tears down any vehicle before entering a
        // starport cell; the base owns the path choreography.
        TravelDiagLog::event("DEPART_INTERIOR", agent == nullptr ? 0 :
            agent->getObjectID(), "reason=" + reason +
            " attempts=" + String::valueOf(ticketApproachAttempts) +
            " hybridActive=" + String::valueOf(isHybridMovementActive()) +
            " cell=" + String::valueOf(interiorCell == nullptr ? 0 :
                interiorCell->getObjectID()) +
            " " + TravelDiagLog::fmtVec("interiorWorld", interiorWorld) +
            " " + TravelDiagLog::fmtVec("interiorLocal", interiorLocal));
        prepareForTicketCollectorEntry("ticketCollectorBeforeInterior");
        ticketTravelPhase = TICKET_DEPARTURE_ENTRY;
        moveToInterior(interiorWorld, interiorLocal, interiorCell.get());
        return;
    }

    ticketTravelPhase = TICKET_DEPARTURE_COLLECTOR;
    cellEgressSuppressed = false;
    moveTo(ticketCollectorWorld, ticketCollectorWorld,
        ticketCollectorCell.get());
}

// Delayed re-drive of the ticket-collector arrival exit after a transient
// STARPORT_RESOLVE_FAILED, so the resolver miss is retried with a real interval
// (bounded by attempts/TTL) instead of recursing and burning all attempts at once.
class TicketArrivalRetryTask : public Task {
    WeakReference<SimPlayerController*> controller;
    String reason;
public:
    TicketArrivalRetryTask(SimPlayerController* ctrl, const String& r)
        : controller(ctrl), reason(r) {}
    void run() override {
        Reference<SimPlayerController*> strong = controller.get();
        if (strong != nullptr)
            strong->beginTicketCollectorArrivalExit(reason);
    }
};

void SimPlayerController::beginTicketCollectorArrivalExit(const String& reason) {
    if (!interplanetaryTravelActive || agent == nullptr)
        return;

    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager == nullptr) {
        cancelTicketCollectorTravel("managerUnavailable");
        return;
    }

    if (ticketApproachAttempts >= manager->getTicketCollectorApproachAttempts() ||
            (travelStartedAtMs != 0 && System::getMiliTime() >
                travelStartedAtMs + (uint64)manager->getTicketCollectorApproachTtlSeconds() * 1000)) {
        cancelTicketCollectorTravel("arrivalExitResolveExhausted");
        return;
    }
    ticketApproachAttempts++;

    ManagedReference<AiAgent*> strongAgent = agent;
    ManagedReference<Zone*> zone;
    Vector3 currentWorld;
    {
        Locker agentLocker(strongAgent);
        zone = strongAgent->getZone();
        currentWorld = strongAgent->getWorldPosition();
    }

    if (zone == nullptr) {
        cancelTicketCollectorTravel("arrivalExitMissingZone");
        return;
    }

    Vector3 interiorWorld;
    Vector3 interiorLocal;
    ManagedReference<CellObject*> interiorCell;
    SimPlayerManager::StarportInteriorWaypointResult result =
        manager->resolveStarportInteriorWaypoint(
            zone, ticketArrivalOutdoor, currentWorld, interiorWorld,
            interiorLocal, interiorCell);

    if (result == SimPlayerManager::STARPORT_RESOLVE_FAILED) {
        // Transient resolver miss: retry after a real interval (do NOT recurse
        // synchronously, which would exhaust attempts instantly). Bounded by the
        // attempts/TTL check at the top of this method.
        Reference<TicketArrivalRetryTask*> task =
            new TicketArrivalRetryTask(this, reason + ":retry");
        task->schedule(2000);
        return;
    }

    clearCellEgressState();
    if (result == SimPlayerManager::STARPORT_WAYPOINT_FOUND) {
        ticketTravelPhase = TICKET_ARRIVAL_REENTER;
        moveToInterior(interiorWorld, interiorLocal, interiorCell.get());
        return;
    }

    ticketTravelPhase = TICKET_ARRIVAL_EGRESS;
    moveTo(ticketArrivalOutdoor);
}

void SimPlayerController::cancelTicketCollectorTravel(const String& reason) {
    TravelDiagLog::event("CANCEL", agent == nullptr ? 0 : agent->getObjectID(),
        "reason=" + reason + " phase=" +
        String::valueOf((int)ticketTravelPhase) + " attempts=" +
        String::valueOf(ticketApproachAttempts));
    Logger::console.info(String("SimMinerTicketCollectorTravelCancelled miner=") +
        String::valueOf(agent == nullptr ? 0 : agent->getObjectID()) +
        " reason=" + reason, true);

    // If we are cancelling an ARRIVAL exit the miner is stranded at the destination
    // hollow: reposition it to the OUTDOOR arrival (a safe switchZone, same as the
    // board reposition) so normal recovery does not resume mining wedged inside the
    // enclosed hollow. Departure-side cancels are already outdoors and skip this.
    if ((ticketTravelPhase == TICKET_ARRIVAL_REENTER ||
            ticketTravelPhase == TICKET_ARRIVAL_EGRESS) && agent != nullptr &&
            (ticketArrivalOutdoor.getX() != 0.f ||
             ticketArrivalOutdoor.getY() != 0.f)) {
        // Invalidate any in-flight path/arrival tasks and tear down stale movement
        // BEFORE the reposition, matching the board-path choreography (stale paths
        // have won this race live).
        prepareForRelocation("arrivalExitRecovery");
        ManagedReference<AiAgent*> strongAgent = agent;
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();
        if (zone != nullptr) {
            strongAgent->setMovementState(AiAgent::OBLIVIOUS);
            strongAgent->clearPatrolPoints();
            strongAgent->clearSavedPatrolPoints();
            strongAgent->clearCurrentPath();
            strongAgent->switchZone(zone->getZoneName(),
                ticketArrivalOutdoor.getX(), ticketArrivalOutdoor.getZ(),
                ticketArrivalOutdoor.getY(), 0);
            strongAgent->setHomeLocation(ticketArrivalOutdoor.getX(),
                ticketArrivalOutdoor.getZ(), ticketArrivalOutdoor.getY(), nullptr);
        }
    }

    ticketTravelPhase = TICKET_TRAVEL_NONE;
    clearCellEgressState();
    String destZone = travelDestinationZone;
    onInterplanetaryTravelFinished(false, destZone, reason);
    clearInterplanetaryTravelState();
}

void SimPlayerController::completeTicketCollectorTravel() {
    TravelDiagLog::event("COMPLETE", agent == nullptr ? 0 :
        agent->getObjectID(), "destZone=" + travelDestinationZone);
    clearCellEgressState();
    ticketTravelPhase = TICKET_TRAVEL_NONE;
    String destZone = travelDestinationZone;
    onInterplanetaryTravelFinished(true, destZone, "arrived");
    clearInterplanetaryTravelState();
}

void SimPlayerController::clearInterplanetaryTravelState() {
    interplanetaryTravelActive = false;
    travelDestinationZone = "";
    travelDeparturePosition = Vector3(0, 0, 0);
    travelDestinationArrival = Vector3(0, 0, 0);
    travelDestinationStarport = "";
    travelStartedAtMs = 0;
    ticketTravelPhase = TICKET_TRAVEL_NONE;
    ticketCollectorWorld = Vector3(0, 0, 0);
    ticketCollectorLocal = Vector3(0, 0, 0);
    ticketCollectorCell = nullptr;
    ticketCollectorOid = 0;
    ticketCollectorFound = false;
    ticketArrivalCollectorFound = false;
    ticketArrivalOutdoor = Vector3(0, 0, 0);
    ticketApproachAttempts = 0;
    clearCellEgressState();
}

bool SimPlayerController::handleInterplanetaryTravelArrival() {
    if (!interplanetaryTravelActive)
        return false;

    TravelDiagLog::event("ARRIVED", agent == nullptr ? 0 :
        agent->getObjectID(), "phase=" + String::valueOf((int)ticketTravelPhase) +
        " atCollector=" + String::valueOf(isAtTicketCollector()) +
        " collectorFound=" + String::valueOf(ticketCollectorFound));

    if (ticketTravelPhase == TICKET_DEPARTURE_ENTRY) {
        cellEgressSuppressed = true;
        ticketTravelPhase = TICKET_DEPARTURE_COLLECTOR;
        moveTo(ticketCollectorWorld, ticketCollectorWorld,
            ticketCollectorCell.get());
        return true;
    }

    if (ticketTravelPhase == TICKET_DEPARTURE_COLLECTOR) {
        if (isAtTicketCollector())
            boardInterplanetaryShuttle("collectorReached");
        else
            retryTicketApproach("collectorArrivalOutsideGate");
        return true;
    }

    if (ticketTravelPhase == TICKET_ARRIVAL_REENTER) {
        clearCellEgressState();
        ticketTravelPhase = TICKET_ARRIVAL_EGRESS;
        moveTo(ticketArrivalOutdoor);
        return true;
    }

    if (ticketTravelPhase == TICKET_ARRIVAL_EGRESS) {
        bool outdoors = false;
        {
            Locker agentLocker(agent);
            ManagedReference<SceneObject*> parent = agent->getParent().get();
            outdoors = parent == nullptr || !parent->isCellObject();
        }

        if (outdoors)
            completeTicketCollectorTravel();
        else
            beginTicketCollectorArrivalExit("stillInside");
        return true;
    }

    // Preserve the legacy immediate-board path when collector travel is off.
    boardInterplanetaryShuttle("arrived");
    return true;
}

bool SimPlayerController::handleInterplanetaryTravelPathFailed() {
    if (!interplanetaryTravelActive)
        return false;

    TravelDiagLog::event("PATH_FAILED", agent == nullptr ? 0 :
        agent->getObjectID(), "phase=" + String::valueOf((int)ticketTravelPhase) +
        " attempts=" + String::valueOf(ticketApproachAttempts));

    if (ticketTravelPhase == TICKET_ARRIVAL_REENTER ||
            ticketTravelPhase == TICKET_ARRIVAL_EGRESS) {
        beginTicketCollectorArrivalExit("arrivalPathFailed");
        return true;
    }

    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager != nullptr && manager->isTicketCollectorTravelEnabled())
        retryTicketApproach("pathFailed");
    else
        boardInterplanetaryShuttle("stuckFallback");

    return true;
}

void SimMinerController::onArrived() {
    if (handleInterplanetaryTravelArrival())
        return;

    if (intelligentAssignmentActive) {
        // P.4.5c final approach: a long off-navmesh walk can terminate short of
        // the true target (path exhausted / patrol point popped early), which
        // used to station the miner hundreds of meters away and churn it via
        // recovery. Re-path directly toward the target to close the gap -- bounded
        // by leg count and by requiring real progress each leg so a genuinely
        // unreachable target can't loop. Stationing within arrivalRadius is fine
        // (planet-wide resource; ~10-15 m short is acceptable).
        if (agent != nullptr) {
            static const int kMaxFinalApproachLegs = 8;
            static const float kMinApproachProgressMeters = 5.f;

            float arrivalRadius =
                SimPlayerManager::instance()->getMinerIntelligentArrivalRadiusMeters();
            Vector3 pos = agent->getWorldPosition();
            float dx = pos.getX() - intelligentTargetPosition.getX();
            float dy = pos.getY() - intelligentTargetPosition.getY();
            float distToTarget = Math::sqrt(dx * dx + dy * dy);

            bool madeProgress = intelligentFinalApproachAttempts == 0 ||
                distToTarget <=
                    intelligentLastApproachDistance - kMinApproachProgressMeters;

            if (distToTarget > arrivalRadius &&
                    intelligentFinalApproachAttempts < kMaxFinalApproachLegs &&
                    madeProgress) {
                intelligentFinalApproachAttempts++;
                intelligentLastApproachDistance = distToTarget;
                logIntelligentTargetArrival("final_approach");
                Logger::console.info(
                    String("SimMinerFinalApproach miner=") +
                    String::valueOf(agent->getObjectID()) +
                    " leg=" + String::valueOf(intelligentFinalApproachAttempts) +
                    " distanceToTarget=" +
                        String::valueOf(Math::getPrecision(distToTarget, 1)) +
                    " arrivalRadius=" +
                        String::valueOf(Math::getPrecision(arrivalRadius, 1)),
                    true);
                moveTo(intelligentTargetPosition);
                return;
            }
        }

        uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
        if (sourceObjectID != 0)
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                sourceObjectID, "sampleStarted");
        logIntelligentTargetArrival("sample_started");
        performIntelligentSample();
        return;
    }

    if (intelligentAssignmentPending || intelligentAssignmentStationed ||
            intelligentSampleActive) {
        logLegacyLoopSuppressed("legacyArrivalBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    logStateTransition("SimMinerLegacyMoveArrived resource=" + targetResource);
    performSample();
}

void SimMinerController::onPathFailed() {
    if (isStructureTraversalFeatureEnabled() && isTraversalActive()) {
        SimPlayerController::onPathFailed();
        return;
    }

    if (handleInterplanetaryTravelPathFailed())
        return;

    // P.4.4b: park the swoop before any failure handling/reassignment.
    dismountIfMounted("pathFailed");

    if (intelligentAssignmentActive || intelligentAssignmentPending) {
        logIntelligentTargetActivation("fallback", "pathFailed");
        uint64 sourceObjectID = agent != nullptr ? agent->getObjectID() : 0;
        if (sourceObjectID != 0)
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                sourceObjectID, "failed", "pathFailed");
        clearLocalIntelligentTargetAssignment();

        if (sourceObjectID != 0)
            SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(sourceObjectID, "pathFailed");
    }

    logStateTransition("SimMiner: Path failed; retrying loop for [" + targetResource + "]");
    SimPlayerController::onPathFailed();
}

bool SimMinerController::shouldContinueArrivalChecks() const {
    return !intelligentAssignmentStationed;
}

bool SimMinerController::shouldRepathWhenStuck() const {
    // P.4.5b: while traveling to a starport the path is a straight off-navmesh
    // line to the ticket collector; re-pathing reproduces it, so skip re-path and
    // let the watchdog escalate to onPathFailed() -> board-anyway fallback.
    if (interplanetaryTravelActive)
        return false;

    // For a directOverland assignment the path is a straight terrain-following
    // line; re-pathing reproduces the same line, so skip re-path and let the
    // watchdog give up immediately so the planner can reassign.
    if (intelligentAssignmentActive &&
            intelligentActivationPathTrustStatus == "directOverland")
        return false;

    return true;
}

void SimMinerController::resetIntelligentAssignmentForRecovery() {
    // Manager-initiated recovery (e.g. a stationed miner whose assignment was
    // reassigned far away and could not be reached). Drop all local intelligent
    // state (including stationed), tidy posture/patrol, and re-enter the work
    // loop so the planner can assign a fresh target the miner will actually
    // travel to. clearLocalIntelligentTargetAssignment advances the work-loop
    // generation, invalidating any in-flight stationed-sample/arrival tasks.
    // P.4.4b: a recovered miner must never keep (or leak) a swoop.
    if (isStructureTraversalFeatureEnabled() && isTraversalActive())
        clearStructureTraversalState("miner_recovery_reset");
    dismountIfMounted("recoveryReset");
    clearCellEgressState();
    clearLocalIntelligentTargetAssignment();

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent != nullptr) {
        Locker locker(strongAgent);
        strongAgent->clearPatrolPoints();
        if (strongAgent->getPosture() != CreaturePosture::UPRIGHT)
            strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    }

    startSimLoop();
}

bool SimPlayerController::beginInterplanetaryTravel(
        const String& destZone,
        const Vector3& departurePos,
        const Vector3& destArrivalPos,
        const String& destStarportName,
        float boardRadius,
        String& travelResult) {
    travelResult = "fallback";
    // A rejected new-trip request is a terminal path for the request, not for
    // an already-running journey. Keep the active journey visible to neither
    // the miner policy callback nor any future derived controller.
    auto notifyStartFailure = [this, &destZone](const String& reason) {
        bool wasActive = interplanetaryTravelActive;
        interplanetaryTravelActive = false;
        onInterplanetaryTravelFinished(false, destZone, reason);
        interplanetaryTravelActive = wasActive;
    };

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        travelResult = "controllerUnavailable";
        TravelDiagLog::event("BEGIN_REJECT", 0, "reason=" + travelResult);
        notifyStartFailure(travelResult);
        return false;
    }

    if (destZone.isEmpty()) {
        travelResult = "invalidDestination";
        TravelDiagLog::event("BEGIN_REJECT", strongAgent->getObjectID(),
            "reason=" + travelResult);
        notifyStartFailure(travelResult);
        return false;
    }

    if (!canBeginInterplanetaryTravel()) {
        travelResult = "controllerBusy";
        TravelDiagLog::event("BEGIN_REJECT", strongAgent->getObjectID(),
            "reason=" + travelResult);
        notifyStartFailure(travelResult);
        return false;
    }

    String validationFailure;
    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();

        if (zone == nullptr) {
            travelResult = "missingZone";
            validationFailure = travelResult;
        } else if (zone->getZoneName() == destZone) {
            travelResult = "alreadyOnPlanet";
            validationFailure = travelResult;
        } else if (strongAgent->isDead() || strongAgent->isIncapacitated() ||
                strongAgent->isInCombat()) {
            travelResult = "controllerStateNotSafe";
            validationFailure = travelResult;
        }
    }

    if (!validationFailure.isEmpty()) {
        TravelDiagLog::event("BEGIN_REJECT", strongAgent->getObjectID(),
            "reason=" + validationFailure);
        notifyStartFailure(validationFailure);
        return false;
    }

    interplanetaryTravelActive = true;
    travelDestinationZone = destZone;
    travelDeparturePosition = departurePos;
    travelDestinationArrival = destArrivalPos;
    travelDestinationStarport = destStarportName;
    travelStartedAtMs = System::getMiliTime();
    SimPlayerManager* manager = SimPlayerManager::instance();
    bool collectorTravel = manager != nullptr &&
        manager->isTicketCollectorTravelEnabled();
    travelBoardRadius = collectorTravel ?
        manager->getTicketCollectorBoardRadiusMeters() :
        (boardRadius > 0.f ? boardRadius : 20.f);
    ticketTravelPhase = collectorTravel ? TICKET_DEPARTURE_RESOLVE :
        TICKET_TRAVEL_NONE;
    ticketCollectorFound = false;
    ticketArrivalCollectorFound = false;
    ticketCollectorWorld = Vector3(0, 0, 0);
    ticketCollectorLocal = Vector3(0, 0, 0);
    ticketCollectorCell = nullptr;
    ticketCollectorOid = 0;
    ticketArrivalOutdoor = destArrivalPos;
    ticketApproachAttempts = 0;

    travelResult = "traveling";

    uint64 sourceObjectID = strongAgent->getObjectID();
    Logger::console.info(
        String("SimMinerInterplanetaryTravelStarted miner=") +
        String::valueOf(sourceObjectID) +
        " destZone=" + destZone +
        " destStarport=" +
            (destStarportName.isEmpty() ? String("none") : destStarportName) +
        " departure=(" +
            String::valueOf(Math::getPrecision(departurePos.getX(), 1)) + "," +
            String::valueOf(Math::getPrecision(departurePos.getY(), 1)) + ")",
        true);

    TravelDiagLog::event("BEGIN_OK", strongAgent->getObjectID(),
        "destZone=" + destZone + " starport=" +
        (destStarportName.isEmpty() ? String("none") : destStarportName) +
        " collectorTravel=" + String::valueOf(collectorTravel) +
        " hybrid=" + String::valueOf(usesNavmeshHybridMovement()) +
        " " + TravelDiagLog::fmtVec("departure", departurePos) +
        " " + TravelDiagLog::fmtVec("arrival", destArrivalPos));

    if (collectorTravel) {
        // Dismount before the first cell-aware operation. The approach itself
        // is deliberately on foot so a rider can never enter a POB cell.
        prepareForInterplanetaryTravelDeparture();
        beginTicketCollectorDepartureApproach("travelStarted");
    } else {
        // Existing P.4.5b behavior, byte-for-byte while the new gate is off.
        prepareForInterplanetaryTravelDeparture();
        moveTo(departurePos);
    }
    return true;
}

void SimPlayerController::boardInterplanetaryShuttle(const String& reason) {
    TravelDiagLog::event("BOARD", agent == nullptr ? 0 : agent->getObjectID(),
        "reason=" + reason + " destZone=" + travelDestinationZone);
    prepareForInterplanetaryTravelBoarding(reason);

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        onInterplanetaryTravelFinished(false, travelDestinationZone,
            "controllerUnavailable");
        clearInterplanetaryTravelState();
        return;
    }

    String destZone = travelDestinationZone;
    Vector3 arrival = travelDestinationArrival;
    String starport = travelDestinationStarport;
    uint64 minerID = strongAgent->getObjectID();

    if (destZone.isEmpty()) {
        onInterplanetaryTravelFinished(false, destZone, "invalidDestination");
        clearInterplanetaryTravelState();
        return;
    }

    String fromZone = "unknown";
    Vector3 landing = arrival;
    ticketArrivalCollectorFound = false;

    SimPlayerManager* manager = SimPlayerManager::instance();
    if (manager != nullptr && manager->isTicketCollectorTravelEnabled()) {
        ZoneServer* zoneServer = ServerCore::getZoneServer();
        Zone* destinationZone = zoneServer == nullptr ? nullptr :
            zoneServer->getZone(destZone);
        Vector3 collectorLocal;
        ManagedReference<CellObject*> collectorCell;
        uint64 collectorOid = 0;
        if (manager->resolveNearestTicketCollector(destinationZone, arrival,
                landing, collectorLocal, collectorCell, collectorOid)) {
            ticketArrivalCollectorFound = true;
        }
    }

    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();
        if (zone != nullptr)
            fromZone = zone->getZoneName();

        // "Board the shuttle": switchZone params are (terrain, X, Z=height, Y=north,
        // parentID=0 outdoor). Same safe reposition as P.4.5a station travel; the
        // OUTDOOR arrival means we never enter the un-navmeshed starport interior.
        strongAgent->switchZone(destZone, landing.getX(), landing.getZ(),
            landing.getY(), 0);
        // Anchor the leash on the new planet so a stale home location on the old
        // planet can't pull the miner. The next assignment's move resets it again.
        strongAgent->setHomeLocation(landing.getX(), landing.getZ(),
            landing.getY(), nullptr);
    }

    Logger::console.info(
        String("SimMinerInterplanetaryTravelBoarded miner=") +
        String::valueOf(minerID) +
        " fromZone=" + fromZone +
        " toZone=" + destZone +
        " starport=" + (starport.isEmpty() ? String("none") : starport) +
        " reason=" + reason,
        true);

    // Boarding telemetry is per-controller-kind policy, not shared mechanics:
    // recordInterplanetaryTravelBoarded writes MINER planet-dispatch counters
    // and last-boarded fields, so a hunter or buff trip running through this
    // same base path would corrupt miner telemetry. Each controller reports
    // its own.
    onInterplanetaryTravelBoarded(fromZone, destZone, starport, reason);

    if (manager != nullptr && manager->isTicketCollectorTravelEnabled() &&
            ticketArrivalCollectorFound) {
        travelStartedAtMs = System::getMiliTime();
        ticketApproachAttempts = 0;
        ticketArrivalOutdoor = arrival;
        beginTicketCollectorArrivalExit("boarded");
    } else {
        // Existing arrival behavior: clear travel state and re-acquire on the
        // destination planet immediately.
        onInterplanetaryTravelFinished(true, destZone, "arrived");
        clearInterplanetaryTravelState();
    }
}

void SimMinerController::performSample() {
    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySampleBlockedByIntelligentLifecycle");
        startSimLoop();
        return;
    }

    // P.4.4b: park the swoop before kneeling to sample.
    dismountIfMounted("legacySample");

    state = SAMPLING;
    logStateTransition("SimMinerLegacySampleStarted resource=" + targetResource);

    agent->clearPatrolPoints(); 
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample"); 
    
    Reference<SimBehaviorTask*> task =
        new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE,
            getWorkLoopGeneration());
    task->schedule(config.sampleDurationMs);
}

void SimMinerController::finishSample() {
    if (intelligentSampleActive) {
        finishIntelligentSample();
        return;
    }

    ManagedReference<AiAgent*> strongAgent = agent;
    if (strongAgent == nullptr)
        return;

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentAssignmentStationed) {
        logLegacyLoopSuppressed("legacySampleFinishBlockedByIntelligentLifecycle");
        strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
        strongAgent->doAnimation("stop_sample");
        startSimLoop();
        return;
    }

    String completedResource = targetResource;
    int yieldAmount = 0;
    bool logYield = false;
    bool recordYield = prepareConceptualYield(completedResource, yieldAmount, logYield);
    uint64 sourceObjectID = strongAgent->getObjectID();

    logStateTransition("SimMinerLegacySampleFinished resource=" + completedResource);
    strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    strongAgent->doAnimation("stop_sample");
    startSimLoop();

    // Keep conceptual accounting outside the completed sample's agent work.
    if (recordYield) {
        SimPlayerManager::instance()->recordConceptualMinerYield(
            completedResource, yieldAmount, sourceObjectID, logYield);
    }
}

bool SimMinerController::requestIntelligentTargetAssignment(
        const String& profileKey,
        const String& resourceName,
        const String& resourceType,
        const String& targetZone,
        const Vector3& targetPosition,
        float density,
        uint64 expiresAtMs,
        uint64 assignmentGenerationId,
        const String& targetHash,
        uint64 activationSnapshotId,
        const String& activationPathValidationStatus,
        const String& activationPathTrustStatus,
        bool logActivationLifecycle,
        String& activationResult) {
    activationResult = "fallback";

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        activationResult = "controllerUnavailable";
        return false;
    }

    if (profileKey.isEmpty() || resourceName.isEmpty() ||
            resourceType.isEmpty() || targetZone.isEmpty()) {
        activationResult = "invalidAssignment";
        return false;
    }

    uint64 now = System::getMiliTime();

    if (expiresAtMs > 0 && now > expiresAtMs) {
        activationResult = "assignmentExpired";
        return false;
    }

    String currentZoneName;

    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();

        if (zone == nullptr) {
            activationResult = "missingZone";
            return false;
        }

        currentZoneName = zone->getZoneName();

        if (currentZoneName != targetZone) {
            activationResult = "wrongPlanet";
            return false;
        }

        if (strongAgent->isDead()) {
            activationResult = "dead";
            return false;
        }

        if (strongAgent->isIncapacitated()) {
            activationResult = "incapacitated";
            return false;
        }

        if (strongAgent->isInCombat()) {
            activationResult = "combat";
            return false;
        }

        if (!zone->isWithinBoundaries(targetPosition)) {
            activationResult = "targetOutOfBounds";
            return false;
        }
    }

    // P.4.5b: a traveling miner is busy (running to the shuttle); the manager must
    // not hand it a normal same-planet target mid-trip.
    if (interplanetaryTravelActive) {
        activationResult = "controllerBusy";
        return false;
    }

    if (intelligentAssignmentPending || intelligentAssignmentActive ||
            intelligentSampleActive || intelligentAssignmentStationed) {
        bool sameAssignment =
            intelligentAssignmentGenerationId == assignmentGenerationId &&
            !targetHash.isEmpty() &&
            intelligentTargetHash == targetHash;

        if (!sameAssignment) {
            activationResult = "controllerBusy";
            return false;
        }

        intelligentLogActivationLifecycle = logActivationLifecycle;
        activationResult = "alreadyActive";

        if (intelligentSampleActive) {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "sampleStarted", activationResult);
        } else if (intelligentAssignmentStationed) {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "stationed", activationResult);
        } else if (intelligentAssignmentActive) {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "activationStarted", activationResult);
        } else {
            SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
                strongAgent->getObjectID(), "queued", activationResult);
        }

        return true;
    }

    intelligentProfileKey = profileKey;
    intelligentResourceName = resourceName;
    intelligentResourceType = resourceType;
    intelligentTargetZone = targetZone;
    intelligentTargetPosition = targetPosition;
    intelligentTargetDensity = density;
    intelligentAssignmentExpiresAtMs = expiresAtMs;
    intelligentAssignmentGenerationId = assignmentGenerationId;
    intelligentTargetHash = targetHash;
    intelligentActivationSnapshotId = activationSnapshotId;
    intelligentActivationPathValidationStatus = activationPathValidationStatus;
    intelligentActivationPathTrustStatus = activationPathTrustStatus;
    intelligentLogActivationLifecycle = logActivationLifecycle;
    intelligentQueuedState = getSimStateName(state);
    intelligentQueuedDuringSample =
        state == SAMPLING || state == PERFORMING_ACTION;
    intelligentQueuedAtMs = now;
    intelligentAssignmentPending = true;
    advanceWorkLoopGeneration("intelligentAssignmentAccepted");

    activationResult = "queued";
    logIntelligentTargetActivation("queued");
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        strongAgent->getObjectID(), "queued", activationResult);

    return true;
}

bool SimMinerController::beginIntelligentTargetAssignment(String& activationResult) {
    activationResult = "fallback";

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        activationResult = "controllerUnavailable";
        clearLocalIntelligentTargetAssignment();
        return false;
    }

    uint64 now = System::getMiliTime();
    uint64 sourceObjectID = strongAgent->getObjectID();

    if (intelligentAssignmentExpiresAtMs > 0 &&
            now > intelligentAssignmentExpiresAtMs) {
        activationResult = "assignmentExpired";
        logIntelligentTargetActivation("fallback", activationResult);
        SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
            sourceObjectID, "failed", activationResult);
        clearLocalIntelligentTargetAssignment();
        SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(sourceObjectID, activationResult);
        return false;
    }

    bool activationSafe = true;

    {
        Locker agentLocker(strongAgent);
        Zone* zone = strongAgent->getZone();

        if (zone == nullptr) {
            activationResult = "missingZone";
            activationSafe = false;
        } else if (zone->getZoneName() != intelligentTargetZone) {
            activationResult = "wrongPlanet";
            activationSafe = false;
        } else if (strongAgent->isDead() || strongAgent->isIncapacitated() ||
                strongAgent->isInCombat()) {
            activationResult = "controllerStateNotSafe";
            activationSafe = false;
        } else if (!zone->isWithinBoundaries(intelligentTargetPosition)) {
            activationResult = "targetOutOfBounds";
            activationSafe = false;
        }
    }

    if (!activationSafe) {
        logIntelligentTargetActivation("fallback", activationResult);
        SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
            sourceObjectID, "failed", activationResult);
        clearLocalIntelligentTargetAssignment();
        SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(sourceObjectID, activationResult);
        return false;
    }

    intelligentAssignmentPending = false;
    intelligentAssignmentActive = true;
    intelligentSampleActive = false;
    intelligentAssignmentStationed = false;

    targetResource = !intelligentResourceType.isEmpty() ?
        intelligentResourceType : intelligentResourceName;

    activationResult = "started";
    logIntelligentTargetActivation("started");
    Logger::console.info(
        String("SimMinerIntelligentMoveStarted miner=") +
        String::valueOf(sourceObjectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash) +
        " targetResource=" +
            (intelligentResourceName.isEmpty() ?
                String("none") : intelligentResourceName) +
        " targetType=" +
            (intelligentResourceType.isEmpty() ?
                String("none") : intelligentResourceType) +
        " targetZone=" +
            (intelligentTargetZone.isEmpty() ?
                String("none") : intelligentTargetZone),
        true);
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "activationStarted", activationResult);

    strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    strongAgent->doAnimation("stop_sample");
    rePathAttempts = 0;
    intelligentFinalApproachAttempts = 0;
    intelligentLastApproachDistance = 0.f;
    maybeMountForTravel(intelligentTargetPosition);
    moveTo(intelligentTargetPosition);
    return true;
}

void SimMinerController::performIntelligentSample() {
    state = SAMPLING;
    intelligentSampleActive = true;

    if (agent == nullptr) {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    agent->clearPatrolPoints();
    agent->setMovementState(AiAgent::OBLIVIOUS);
    agent->setPosture(CreaturePosture::CROUCHED, true);
    agent->doAnimation("sample");

    Reference<SimBehaviorTask*> task =
        new SimBehaviorTask(this, SimBehaviorTask::FINISH_SAMPLE,
            getWorkLoopGeneration());
    task->schedule(SimPlayerManager::getGameDerivedStationedSampleResultDelayMs());
}

void SimMinerController::startStationedSample() {
    if (!intelligentAssignmentStationed)
        return;

    // P.4.4b: the ride ends where the work starts — park the swoop before the
    // first stationed sample (idempotent; later sample ticks no-op).
    dismountIfMounted("stationed");

    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    intelligentAssignmentStationed = false;
    intelligentAssignmentActive = true;
    intelligentSampleActive = false;
    advanceWorkLoopGeneration("stationedSampleStarted");

    uint64 sourceObjectID = strongAgent->getObjectID();
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "sampleStarted", "stationedRepeat");
    logIntelligentTargetArrival("stationed_sample_started");
    Logger::console.info(
        String("SimMinerStationedSampleStarted miner=") +
        String::valueOf(sourceObjectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash),
        true);
    performIntelligentSample();
}

void SimMinerController::finishIntelligentSample() {
    ManagedReference<AiAgent*> strongAgent = agent;

    if (strongAgent == nullptr) {
        clearLocalIntelligentTargetAssignment();
        return;
    }

    String completedResource = targetResource;
    int yieldAmount = 0;
    bool logYield = false;
    bool recordYield = prepareConceptualYield(completedResource, yieldAmount, logYield);
    uint64 sourceObjectID = strongAgent->getObjectID();

    logIntelligentTargetArrival("sample_finished");
    Logger::console.info(
        String("SimMinerStationedSampleFinished miner=") +
        String::valueOf(sourceObjectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash),
        true);
    SimPlayerManager::instance()->recordMinerIntelligentTargetAssignmentLifecycleFromController(
        sourceObjectID, "sampleFinished");
    strongAgent->setPosture(CreaturePosture::UPRIGHT, true);
    strongAgent->doAnimation("stop_sample");

    bool scheduleRepeatedSample = false;
    int repeatedSampleDelayMs = 0;
    String stationedReason;
    bool retainedStationed =
        SimPlayerManager::instance()->transitionMinerIntelligentAssignmentToStationed(
            sourceObjectID,
            recordYield ? yieldAmount : 0,
            scheduleRepeatedSample,
            repeatedSampleDelayMs,
            stationedReason);

    if (recordYield) {
        if (retainedStationed) {
            SimPlayerManager::instance()->recordSimulatedAcquisitionTransactionFromController(
                sourceObjectID, yieldAmount);
        }

        SimPlayerManager::instance()->recordIntelligentConceptualMinerYield(
            completedResource, yieldAmount, sourceObjectID, logYield);
    }

    if (retainedStationed) {
        intelligentAssignmentPending = false;
        intelligentAssignmentActive = false;
        intelligentSampleActive = false;
        intelligentAssignmentStationed = true;
        state = WAITING;
        advanceWorkLoopGeneration("stationed");

        if (scheduleRepeatedSample && repeatedSampleDelayMs > 0) {
            Reference<SimBehaviorTask*> task =
                new SimBehaviorTask(this,
                    SimBehaviorTask::START_STATIONED_SAMPLE,
                    getWorkLoopGeneration());
            task->schedule(repeatedSampleDelayMs);
        }

        return;
    }

    clearLocalIntelligentTargetAssignment();
    SimPlayerManager::instance()->clearMinerIntelligentTargetAssignmentFromController(
        sourceObjectID,
        stationedReason.isEmpty() ? String("sampleComplete") : stationedReason);
    startSimLoop();
}

void SimMinerController::clearLocalIntelligentTargetAssignment() {
    advanceWorkLoopGeneration("clearIntelligentAssignment");
    intelligentAssignmentPending = false;
    intelligentAssignmentActive = false;
    intelligentSampleActive = false;
    intelligentAssignmentStationed = false;
    intelligentProfileKey = "";
    intelligentQueuedDuringSample = false;
    intelligentQueuedAtMs = 0;
    intelligentAssignmentGenerationId = 0;
    intelligentActivationSnapshotId = 0;
    intelligentQueuedState = "";
    intelligentTargetHash = "";
    intelligentActivationPathValidationStatus = "";
    intelligentActivationPathTrustStatus = "";
    intelligentResourceName = "";
    intelligentResourceType = "";
    intelligentTargetZone = "";
    intelligentTargetPosition = Vector3(0, 0, 0);
    intelligentTargetDensity = 0.f;
    intelligentAssignmentExpiresAtMs = 0;
    // P.4.5b: also drop any in-flight travel so recovery that resets a traveling
    // miner cancels the trip cleanly (it re-acquires on its current planet).
    clearInterplanetaryTravelState();
}

void SimMinerController::logIntelligentTargetActivation(
        const String& action, const String& reason) const {
    if (!intelligentLogActivationLifecycle)
        return;

    uint64 objectID = agent != nullptr ? agent->getObjectID() : 0;

    String line = String("MinerIntelligentTargetActivation miner=") +
        String::valueOf(objectID) +
        " action=" + action +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash) +
        " activationSnapshotId=" +
            String::valueOf(intelligentActivationSnapshotId) +
        " selectedProfile=" +
            (intelligentProfileKey.isEmpty() ?
                String("none") : intelligentProfileKey) +
        " targetResource=" +
            (intelligentResourceName.isEmpty() ?
                String("none") : intelligentResourceName) +
        " targetType=" +
            (intelligentResourceType.isEmpty() ?
                String("none") : intelligentResourceType) +
        " targetZone=" +
            (intelligentTargetZone.isEmpty() ?
                String("none") : intelligentTargetZone) +
        " x=" +
            String::valueOf(Math::getPrecision(
                intelligentTargetPosition.getX(), 1)) +
        " y=" +
            String::valueOf(Math::getPrecision(
                intelligentTargetPosition.getY(), 1)) +
        " z=" +
            String::valueOf(Math::getPrecision(
                intelligentTargetPosition.getZ(), 1)) +
        " density=" +
            String::valueOf(Math::getPrecision(intelligentTargetDensity, 3)) +
        " pathValidationStatus=" +
            (intelligentActivationPathValidationStatus.isEmpty() ?
                String("valid") : intelligentActivationPathValidationStatus) +
        " pathTrustStatus=" +
            (intelligentActivationPathTrustStatus.isEmpty() ?
                String("verifiedPath") : intelligentActivationPathTrustStatus) +
        " queuedState=" +
            (intelligentQueuedState.isEmpty() ?
                String("none") : intelligentQueuedState) +
        " queuedDuringSample=" +
            (intelligentQueuedDuringSample ?
                String("true") : String("false")) +
        " previousSampleYieldMayFollow=" +
            (intelligentQueuedDuringSample ?
                String("true") : String("false"));

    if (intelligentQueuedAtMs > 0) {
        uint64 now = System::getMiliTime();
        uint64 queuedAgeSeconds = now > intelligentQueuedAtMs ?
            (now - intelligentQueuedAtMs) / 1000 : 0;
        line += " queuedAgeSeconds=" + String::valueOf(queuedAgeSeconds);
    }

    if (!reason.isEmpty())
        line += " fallbackReason=" + reason;

    line += " mode=limited";
    Logger::console.info(line, true);
}

void SimMinerController::logIntelligentTargetArrival(
        const String& arrivalResult) const {
    if (!intelligentLogActivationLifecycle)
        return;

    uint64 objectID = agent != nullptr ? agent->getObjectID() : 0;

    Logger::console.info(
        String("MinerIntelligentTargetArrival miner=") +
        String::valueOf(objectID) +
        " assignmentGenerationId=" +
            String::valueOf(intelligentAssignmentGenerationId) +
        " targetHash=" +
            (intelligentTargetHash.isEmpty() ?
                String("none") : intelligentTargetHash) +
        " activationSnapshotId=" +
            String::valueOf(intelligentActivationSnapshotId) +
        " selectedProfile=" +
            (intelligentProfileKey.isEmpty() ?
                String("none") : intelligentProfileKey) +
        " targetResource=" +
            (intelligentResourceName.isEmpty() ?
                String("none") : intelligentResourceName) +
        " targetType=" +
            (intelligentResourceType.isEmpty() ?
                String("none") : intelligentResourceType) +
        " arrivalResult=" + arrivalResult +
        " yieldMode=conceptual" +
        " conceptualResource=" +
            (targetResource.isEmpty() ? String("none") : targetResource) +
        " mode=limited",
        true);
}

bool SimMinerController::prepareConceptualYield(const String& completedResource, int& amount, bool& logYield) const {
    if (!config.yieldEnabled || completedResource.isEmpty())
        return false;

    if (intelligentAssignmentActive || intelligentSampleActive ||
            intelligentAssignmentStationed) {
        amount =
            SimPlayerManager::getGameDerivedStationedSampleYield(
                intelligentTargetDensity);
        logYield = config.logYield;
        return amount > 0;
    }

    int minAmount = config.minYieldAmount;
    int maxAmount = config.maxYieldAmount;

    if (minAmount <= 0 || maxAmount <= 0)
        return false;

    if (maxAmount < minAmount)
        maxAmount = minAmount;

    amount = minAmount;
    if (maxAmount > minAmount)
        amount += System::random(maxAmount - minAmount);

    logYield = config.logYield;
    return true;
}

void SimMinerController::logLegacyLoopSuppressed(const String& reason) const {
    uint64 objectID = agent != nullptr ? agent->getObjectID() : 0;

    SimPlayerManager::instance()->recordLegacyMinerLoopSuppressed(
        objectID,
        getSimStateName(state),
        intelligentAssignmentPending,
        intelligentAssignmentActive,
        intelligentAssignmentStationed,
        intelligentAssignmentGenerationId,
        intelligentTargetHash,
        reason);
}

void SimMinerController::onStaleWorkLoopTaskIgnored(
        const String& taskType, uint64 capturedGeneration,
        uint64 currentGeneration) {
    (void)taskType;
    (void)capturedGeneration;
    (void)currentGeneration;
}

void SimMinerController::logStateTransition(const String& message) const {
#ifdef DEBUG_SIMPVP
    Logger::console.info(message, true);
#else
    if (config.logStateTransitions)
        Logger::console.info(message, true);
#endif
}
