/*
 * PathFinderManager.cpp
 *
 *  Created on: 02/03/2011
 *      Author: victor
 */

#include "PathFinderManager.h"
#include "server/zone/objects/building/BuildingObject.h"
#include "server/zone/objects/ship/PobShipObject.h"
#include "server/zone/objects/cell/CellObject.h"
#include "templates/SharedObjectTemplate.h"
#include "templates/appearance/PortalLayout.h"
#include "templates/appearance/FloorMesh.h"
#include "templates/appearance/PathGraph.h"
#include "server/zone/Zone.h"
#include "server/zone/objects/creature/simplayer/SimPlayerManager.h"
#include "server/zone/objects/creature/simplayer/StructureTraversalDiagLog.h"

#include "CollisionManager.h"
#include "engine/util/u3d/Funnel.h"
#include "engine/util/u3d/Segment.h"
#include "pathfinding/recast/DetourCommon.h"

// #define DEBUG_PATHING

const static constexpr int MAX_QUERY_NODES = 2048 * 2;

void destroyNavMeshQuery(void* value) {
	dtFreeNavMeshQuery(reinterpret_cast<dtNavMeshQuery*>(value));
}

PathFinderManager::PathFinderManager() : Logger("PathFinderManager"), m_navQuery(destroyNavMeshQuery) {
	setFileLogger("log/pathfinder.log", true, true);
	setLogToConsole(false);
	setGlobalLogging(false);
	setLogSynchronized(true);
	setLogJSON(ConfigManager::instance()->getPathfinderLogJSON());
	setRotateLogSizeMB(ConfigManager::instance()->getRotateLogSizeMB());

	m_filter.setIncludeFlags(SAMPLE_POLYFLAGS_ALL ^ (SAMPLE_POLYFLAGS_DISABLED));
	m_filter.setExcludeFlags(0);
	m_filter.setAreaCost(SAMPLE_POLYAREA_GROUND, 1.0f);
	m_filter.setAreaCost(SAMPLE_POLYAREA_WATER, 15.0f);
	m_filter.setAreaCost(SAMPLE_POLYAREA_ROAD, 1.0f);
	m_filter.setAreaCost(SAMPLE_POLYAREA_DOOR, 1.0f);
	m_filter.setAreaCost(SAMPLE_POLYAREA_GRASS, 2.0f);
	m_filter.setAreaCost(SAMPLE_POLYAREA_JUMP, 1.5f);

	m_spawnFilter.setIncludeFlags(SAMPLE_POLYFLAGS_ALL ^ (SAMPLE_POLYFLAGS_DISABLED | SAMPLE_POLYFLAGS_SWIM));
	m_spawnFilter.setAreaCost(SAMPLE_POLYAREA_GROUND, 1.0f);
	m_spawnFilter.setExcludeFlags(0);

	setLogging(true);
}

Vector<WorldCoordinates>* PathFinderManager::findPath(const WorldCoordinates& pointA, const WorldCoordinates& pointB, Zone *zone) {
#ifdef PLATFORM_WIN
#undef isnan
#endif

	if (std::isnan(pointA.getX()) || std::isnan(pointA.getY()) || std::isnan(pointA.getZ())) {
		return nullptr;
	}

	if (std::isnan(pointB.getX()) || std::isnan(pointB.getY()) || std::isnan(pointB.getZ())) {
		return nullptr;
	}

	auto cellA = pointA.getCell();
	auto cellB = pointB.getCell();

	if (cellA == nullptr && cellB == nullptr) { // world -> world
		return findPathFromWorldToWorld(pointA, pointB, zone);
	} else if (cellA != nullptr && cellB == nullptr) { // cell -> world
		return findPathFromCellToWorld(pointA, pointB, zone);
	} else if (cellA == nullptr && cellB != nullptr) { // world -> cell
		return findPathFromWorldToCell(pointA, pointB, zone);
	} else { // cell -> cell, the only left option
		return findPathWithinCell(pointA, pointB);
	}

	return nullptr;
}

void PathFinderManager::filterPastPoints(Vector<WorldCoordinates>* path, SceneObject* object) {
	Vector3 thisWorldPosition = object->getWorldPosition();
	Vector3 thiswP = thisWorldPosition;

	thiswP.setZ(0);

#ifdef DEBUG_PATHING
	for (int i = 0; i < path->size(); ++i) {
		WorldCoordinates coord = path->get(i);

		info(true) << "Filter Past Points initial path point #" << i << " X: " << coord.getX() << " Z: " << coord.getZ() << " Y: " << coord.getY();

		if (coord.getCell() == nullptr) {
			info(true) << " -- Cell is nullptr --";
		} else {
			info(true) << "Cell ID: " << coord.getCell()->getObjectID();
		}
	}
#endif

	int i = 2;

	while (i < path->size()) {
		WorldCoordinates coord1 = path->get(i - 1);
		WorldCoordinates coord2 = path->get(i);

		if (path->size() > 2) {
			if (coord1 == coord2) {
				WorldCoordinates point = path->get(i - 1);

#ifdef DEBUG_PATHING
				info(true) << "Removing Path Point @ 1 -- X = " << point.getX() << " Z: " << point.getZ() << " Y: " << point.getY();
#endif
				path->remove(i - 1);
				continue;
			}

			Vector3 initial(coord1.getX(), coord1.getZ(), coord1.getY());
			Vector3 end(coord2.getX(), coord2.getZ(), coord2.getY());

			if (initial == end) {
				WorldCoordinates point2 = path->get(i - 1);

#ifdef DEBUG_PATHING
				info(true) << "Removing Path Point @ 2 -- X = " << point2.getX() << " Z: " << point2.getZ() << " Y: " << point2.getY();
#endif
				path->remove(i - 1);
				continue;
			}

			end.setZ(0);

			Vector3 start = coord1.getWorldPosition();
			start.setZ(0);

			Segment sgm(start, end);
			Vector3 closestP = sgm.getClosestPointTo(thiswP);

			if (closestP.distanceTo(thiswP) <= FLT_EPSILON) {
				for (int j = i - 1; j > 0; --j) {
					WorldCoordinates point3 = path->get(j);

#ifdef DEBUG_PATHING
					info(true) << "Removing Path Point @ 3 -- X = " << point3.getX() << " Z: " << point3.getZ() << " Y: " << point3.getY();
#endif
					path->remove(j);
				}

				continue;
			}
		}

		i++;
	}

#ifdef DEBUG_PATHING
	info(true) << "filterPastPoints Complete -- End Path Size = " << path->size();
#endif
}

bool pointInSphere(const Vector3 &point, const Sphere& sphere) {
	return (point-sphere.getCenter()).length() < sphere.getRadius();
}

static AtomicLong totalTime;

void PathFinderManager::getNavMeshCollisions(SortedVector<NavCollision*> *collisions,
											 const SortedVector<ManagedReference<NavArea*>> *areas,
											 const Vector3& start, const Vector3& end) {
	Vector3 dir = (end-start);
	float maxT = dir.normalize();

	for (const ManagedReference<NavArea*>& area : *areas) {
		Zone* zone = area->getZone();

		if (zone == nullptr) {
			String name = area->getMeshName();
			error() << "Null zone on navmesh area " << name << " in getNavMeshCollisions";
			continue;
		}

		const AABB* bounds = area->getMeshBounds();

		const Vector3& bPos = bounds->center();
		Vector3 sPos(bPos.getX(), bPos.getZ(), 0);
		sPos.setZ(CollisionManager::getWorldFloorCollision(sPos.getX(), sPos.getY(), zone, false));
		const float radius = bounds->extents()[bounds->longestAxis()] * .975f;
		float radiusSq = radius*radius;

		//http://www.scratchapixel.com/code.php?id=10&origin=/lessons/3d-basic-rendering/minimal-ray-tracer-rendering-simple-shapes
		Vector3 L = sPos - start;
		float tca = L.dotProduct(dir);
		if (tca < 0) continue;
		float d2 = L.dotProduct(L) - tca * tca;
		if (d2 > radiusSq) continue;
		float thc = sqrt(radiusSq - d2);
		float t1 = tca - thc;
		float t2 = tca + thc;

		if (fabs(t1 - t2) > 0.1f && t1 > 0 && t1 < maxT)
			collisions->put(new NavCollision(start + (dir * t1), t1, area));

		if (t2 > 0 && t2 < maxT)
			collisions->put(new NavCollision(start + (dir * t2), t2, area));
	}
}

dtNavMeshQuery* PathFinderManager::getNavQuery() {
	dtNavMeshQuery* query = m_navQuery.get();

	if (query == nullptr) {
		query = dtAllocNavMeshQuery();
		m_navQuery.set(query);
	}

	return query;
}

bool PathFinderManager::getRecastPath(const Vector3& start, const Vector3& end, NavArea* area, Vector<WorldCoordinates>* path, float& len, bool allowPartial) {
	const Vector3 startPosition(start.getX(), start.getZ(), -start.getY());
	const Vector3 targetPosition(end.getX(), end.getZ(), -end.getY());
	const float* startPosAsFloat = startPosition.toFloatArray();
	const float* tarPosAsFloat = targetPosition.toFloatArray();
	const static float extents[3] = {8, 8, 3};
	dtPolyRef startPoly;
	dtPolyRef endPoly;

	Vector3 areaPos = area->getPosition();
	Zone* zone = area->getZone();

	if (zone == nullptr)
		return false;

	areaPos.setZ(area->getAreaTerrainHeight());

	dtNavMeshQuery* query = getNavQuery();

	ReadLocker rLocker(area);

	RecastNavMesh* navMesh = area->getNavMesh();

	if (navMesh == nullptr || !navMesh->isLoaded())
		return false;

	// We need to flip the Y/Z axis and negate Z to put it in recasts model space
	const Sphere sphere(Vector3(areaPos.getX(), areaPos.getZ(), -areaPos.getY()), area->getRadius());

	query->init(navMesh->getNavMesh(), MAX_QUERY_NODES);

	if (pointInSphere(targetPosition, sphere) || pointInSphere(startPosition, sphere)) {
		Vector3 polyStart;
		Vector3 polyEnd;
		int numPolys;
		const static constexpr int MAX_POLYS = 2048;

		dtPolyRef polyPath[MAX_POLYS];
		int status = 0;

		if (!((status = query->findNearestPoly(startPosAsFloat, extents, &m_filter, &startPoly, polyStart.toFloatArray())) & DT_SUCCESS))
			return false;

		if (!((status = query->findNearestPoly(tarPosAsFloat, extents, &m_filter, &endPoly, polyEnd.toFloatArray())) & DT_SUCCESS))
			return false;

		if (!((status = query->findPath(startPoly, endPoly, polyStart.toFloatArray(), polyEnd.toFloatArray(), &m_filter, polyPath, &numPolys, MAX_POLYS)) & DT_SUCCESS))
			return false;

#ifdef DEBUG_PATHING
		info("findPath result: 0x" + String::hexvalueOf(status), true);
#endif

		if ((status & DT_PARTIAL_RESULT) && !allowPartial)
			return false;

		if (path == nullptr)
			return true;

		if (numPolys) {
			// In case of partial path, make sure the end point is clamped to the last polygon.
			float epos[3];
			dtVcopy(epos, polyEnd.toFloatArray());
			if (polyPath[numPolys - 1] != endPoly) {
#ifdef DEBUG_PATHING
				info("Poly mismatch: Expected: " + String::hexvalueOf((int64)endPoly) + " actual: " + String::hexvalueOf((int64)polyPath[numPolys-1]), true);
#endif
				if (allowPartial)
					query->closestPointOnPoly(polyPath[numPolys - 1], tarPosAsFloat, polyEnd.toFloatArray(), 0);
				else
					return false;
			}

			static constexpr int MAX_PATH_POINTS = 256;

			float pathPoints[MAX_PATH_POINTS][3];
			int numPoints = 0;
			int pathOptions = DT_STRAIGHTPATH_ALL_CROSSINGS;

			status = query->findStraightPath(polyStart.toFloatArray(), polyEnd.toFloatArray(),
									polyPath, numPolys,
									(float*) pathPoints, nullptr, nullptr,
									&numPoints, MAX_PATH_POINTS, pathOptions);
#ifdef DEBUG_PATHING
			info("findStraightPath result: 0x" + String::hexvalueOf(status), true);
			info("number of points = " + String::valueOf(numPoints), true);
#endif
			if (numPoints > 0) {
				for (int i = 0; i < numPoints; i++) {
					//info("PathFind Point : " + point.toString(), true);
					len += pathPoints[i][0] * pathPoints[i][0] + pathPoints[i][2] * pathPoints[i][2];
					path->add(WorldCoordinates(Vector3(pathPoints[i][0], -pathPoints[i][2], pathPoints[i][1]), nullptr));
				}
			}
		}
	}

	return true;
}

Vector<WorldCoordinates>* PathFinderManager::findPathFromWorldToWorld(const WorldCoordinates& pointA, const Vector<WorldCoordinates>& endPoints, Zone* zone, bool allowPartial) {
	Vector<WorldCoordinates>* finalpath = new Vector<WorldCoordinates>();
	float finalLengthSq = FLT_MAX;

#ifdef PROFILE_PATHING
	Timer t;
	t.start();
#endif

	for (const WorldCoordinates& pointB : endPoints) {
		const Vector3& startTemp = pointA.getPoint();
		const Vector3& targetTemp = pointB.getPoint();

		SortedVector<ManagedReference<NavArea*> > areas;

		Vector3 mid = startTemp + ((targetTemp-startTemp) * 0.5f);

		zone->getInRangeNavMeshes(mid.getX(), mid.getY(), &areas, true);

		SortedVector<NavCollision*> collisions;

		getNavMeshCollisions(&collisions, &areas, pointA.getWorldPosition(), pointB.getWorldPosition());

		// Collisions are sorted by distance from the start of the line. This is done so that we can chain our path from
		// one navmesh to another if a path spans multiple meshes.
		Vector<WorldCoordinates> *path = new Vector<WorldCoordinates>();
		float len = 0.0f;

		try {
			int collisionSize = collisions.size();

			if (collisionSize == 1) { // we're entering/exiting a navmesh
				NavCollision* collision = collisions.get(0);
				NavArea *area = collision->getNavArea();
				Vector3 position = collision->getPosition();
				position.setZ(CollisionManager::getWorldFloorCollision(position.getX(), position.getY(), zone, true));

				if (area->containsPoint(startTemp.getX(), startTemp.getY())) {
					if (!getRecastPath(startTemp, position, area, path, len, allowPartial)) { // exiting navmesh
						delete collision;
						if (path != nullptr) delete path;
						continue;
					}

					path->add(pointB);

				} else {
					path->add(pointA);

					if (!getRecastPath(position, targetTemp, area, path, len, allowPartial)) { // entering navmesh
						delete collision;
						if (path != nullptr) delete path;
						continue;
					}
				}

				if (len > 0 && len < finalLengthSq) {
					if (finalpath)
						delete finalpath;

					finalLengthSq = len;
					finalpath = path;
					path = nullptr;
				}
			} else if (collisionSize == 0) { // we're already inside a navmesh (or there are no navmeshes around)
				for (int i = 0; i < areas.size(); i++) {
					if (!getRecastPath(startTemp, targetTemp, areas.get(i), path, len, allowPartial)) {
						continue;
					}

					if (len > 0 && len < finalLengthSq) {
						if (finalpath)
							delete finalpath;

						finalLengthSq = len;
						finalpath = path;
						path = new Vector<WorldCoordinates>();
					}
				}
			} else if (collisionSize == 2) { // we're crossing over a mesh or dealing with multiple meshes
				NavCollision* collision1 = collisions.get(0);
				NavArea *area1 = collision1->getNavArea();
				NavCollision* collision2 = collisions.get(1);
				NavArea *area2 = collision2->getNavArea();

				if (area1 == area2) { // crossing same mesh
					path->add(pointA);

					Vector3 position1 = collision1->getPosition();
					position1.setZ(CollisionManager::getWorldFloorCollision(position1.getX(), position1.getY(), zone, false));
					Vector3 position2 = collision2->getPosition();
					position2.setZ(CollisionManager::getWorldFloorCollision(position2.getX(), position2.getY(), zone, false));

					if (!getRecastPath(position1, position2, area1, path, len, allowPartial)) {
						delete collision1;
						delete collision2;
						if (path != nullptr) delete path;
						continue;
					}

					path->add(pointB);

					if (len > 0 && len < finalLengthSq) {
						if (finalpath)
							delete finalpath;

						finalLengthSq = len;
						finalpath = path;
						path = nullptr;
					}
				} else { // TODO: handle multiple meshes

				}
			} else { // TODO: handle multiple meshes

			}
		} catch (...) {
			error("Unhandled pathing exception");

			for (int i=collisions.size()-1; i>=0; i--) {
				NavCollision *collision = collisions.remove(i);
				delete collision;
			}

			delete path;
			path = nullptr;
		}

		for (int i=collisions.size()-1; i>=0; i--) {
			NavCollision *collision = collisions.remove(i);
			delete collision;
		}

		if (path != nullptr)
			delete path;
	}

	if (finalpath && finalpath->size() < 2) { // path could not be evaluated, just return the start/end position
		finalpath->removeAll();
		finalpath->add(pointA);
		finalpath->add(endPoints.get(0));
	}

#ifdef PROFILE_PATHING
	t.stop();
	totalTime.add(t.getElapsedTime());
	info("Spent " + String::valueOf(totalTime.get()) + " in recast", true);
#endif
	return finalpath;
}

Vector<WorldCoordinates>* PathFinderManager::findPathFromWorldToWorld(const WorldCoordinates& pointA, const WorldCoordinates& pointB, Zone* zone) {
	Vector<WorldCoordinates> temp;
	temp.add(pointB);
	return findPathFromWorldToWorld(pointA, temp, zone, true);
}

Vector<WorldCoordinates>* PathFinderManager::findPathFromWorldToCell(const WorldCoordinates& pointA, const WorldCoordinates& pointB, Zone *zone) {
	CellObject* targetCell = pointB.getCell();

	if (targetCell == nullptr)
		return nullptr;

	ManagedReference<BuildingObject*> building = dynamic_cast<BuildingObject*>(targetCell->getParent().get().get());

	if (building == nullptr) {
		String zoneName = zone == nullptr ? "unknown" : zone->getZoneName();

		error() << "building == nullptr in PathFinderManager::findPathFromWorldToCell from " << pointA << " to " << pointB << " in zone " << zoneName;

		return nullptr;
	}

	SharedObjectTemplate* templateObject = building->getObjectTemplate();

	if (templateObject == nullptr)
		return nullptr;

	const PortalLayout* portalLayout = templateObject->getPortalLayout();

	if (portalLayout == nullptr)
		return nullptr;

	//find nearest entrance
	const FloorMesh* exteriorFloorMesh = portalLayout->getFloorMesh(0); // get outside layout
	const FloorMesh* interiorFloorMesh = portalLayout->getFloorMesh(targetCell->getCellNumber());

	if (exteriorFloorMesh == nullptr || interiorFloorMesh == nullptr)
		return nullptr;

	const PathGraph* exteriorPathGraph = exteriorFloorMesh->getPathGraph();

	if (exteriorPathGraph == nullptr)
		return nullptr;

	Vector<WorldCoordinates>* path = new Vector<WorldCoordinates>(5, 1);
	path->add(pointA);

	Vector3 transformedPosition = transformToModelSpace(pointA.getPoint(), building);
	const PathNode* exteriorNode = exteriorPathGraph->findNearestGlobalNode(transformedPosition);

	if (exteriorNode == nullptr) {
		String zoneName = zone == nullptr ? "unknown" : zone->getZoneName();

		error() << "nullptr exterior node for building " << templateObject->getFullTemplateString()
				<< " from " << pointA << " to " << pointB << " in zone " << zoneName;

		delete path;
		return nullptr;
	}

	const TriangleNode* nearestInteriorNodeTriangle = CollisionManager::getTriangle(pointB.getPoint(), interiorFloorMesh);

	if (nearestInteriorNodeTriangle == nullptr) {
		String zoneName = zone == nullptr ? "unknown" : zone->getZoneName();

		error() << "nearest node triangle is nullptr for building " << templateObject->getFullTemplateString() << " from " << pointA << " to " << pointB << " in zone " << zoneName;

		delete path;
		return nullptr;
	}

	const PathNode* nearestInteriorNode = CollisionManager::findNearestPathNode(nearestInteriorNodeTriangle, interiorFloorMesh, pointB.getPoint());

	if (nearestInteriorNode == nullptr) {
		String zoneName = zone == nullptr ? "unknown" : zone->getZoneName();

		error() << "nearest node is nullptr for building " << templateObject->getFullTemplateString() << " from World Coordinate A -- X: " << pointA.getX() << " Z: " << pointA.getZ() << " Y: " << pointA.getZ() << " to World Coordinate B -- X: " << pointB.getX() << " Z: " << pointB.getZ() << " Y: " << pointB.getY()
				<< " in zone " << zoneName;

		delete path;
		return nullptr;
	}

	// find graph from outside to appropriate cell
	Vector<const PathNode*>* pathToCell = portalLayout->getPath(exteriorNode, nearestInteriorNode);

	if (pathToCell == nullptr) {
		String zoneName = zone == nullptr ? "unknown" : zone->getZoneName();

		error() << "getPath from " << exteriorNode << " to " << nearestInteriorNode << " is nullptr for building " << templateObject->getFullTemplateString() << " from " << pointA << " to " << pointB << " in zone " << zoneName;

		delete path;
		return nullptr;
	}

#ifdef DEBUG_PATHING
	printf("Pathing - worldToCell Called -- ");
	printf(" Initial Path Size = %i \n", path->size());
#endif

	for (int i = 0; i < pathToCell->size(); ++i) {
		const PathNode* pathNode = pathToCell->get(i);
		const PathGraph* pathGraph = pathNode->getPathGraph();

		const FloorMesh* floorMesh = pathGraph->getFloorMesh();
		int cellID = floorMesh->getCellID();

		//info("cellID:" + String::valueOf(cellID), true);

		if (cellID == 0) { // we are still outside
			WorldCoordinates coord(pathNode->getPosition(), targetCell);

			WorldCoordinates point(coord.getWorldPosition(), nullptr);

#ifdef DEBUG_PATHING
			printf("Adding Path Node with Cell ID = 0 , X = %f ,", point.getX());
			printf("Y = %f \n", point.getY());
#endif

			path->add(point);
		} else { // we are inside the building
			CellObject* pathCell = building->getCell(cellID);

#ifdef DEBUG_PATHING
			printf("Adding Path Node with Cell ID = %i, ", cellID);
			printf(" X = %f ,", pathNode->getPosition().getX());
			printf("Y = %f \n", pathNode->getPosition().getY());
#endif

			WorldCoordinates point(pathNode->getPosition(), pathCell);

			path->add(point);
		}
	}

	delete pathToCell;
	pathToCell = nullptr;

	// path from cell path node to destination point
	Vector<const Triangle*>* trianglePath = nullptr;

	int res = getFloorPath(path->get(path->size() - 1).getPoint(), pointB.getPoint(), interiorFloorMesh, trianglePath);

	if (res != -1 && trianglePath != nullptr) {
		addTriangleNodeEdges(path->get(path->size() - 1).getPoint(), pointB.getPoint(), trianglePath, path, targetCell);
	}

	if (trianglePath != nullptr) {
		delete trianglePath;
	}

	path->add(pointB);

#ifdef DEBUG_PATHING
	printf(" FINAL PATH POINTS VECTOR - worldToCell: \n");

	for (int i = 0; i < path->size(); ++i) {
		WorldCoordinates coord = path->get(i);

		printf("Final Path Point #%i - ", i);
		printf(" X = %f,", coord.getX());
		printf("Y = %f", coord.getY());
		if (coord.getCell() == nullptr) {
			printf(" -- Cell is nullptr --");
		}

		printf("\n");
	}
#endif

	return path;
}

const FloorMesh* PathFinderManager::getFloorMesh(CellObject* cell) {
	auto building1 = (cell->getParent().get().castTo<BuildingObject*>());

	SharedObjectTemplate* templateObject = building1->getObjectTemplate();

	if (templateObject == nullptr) {
		return nullptr;
	}

	const PortalLayout* portalLayout = templateObject->getPortalLayout();

	if (portalLayout == nullptr) {
		return nullptr;
	}

	const FloorMesh* floorMesh1 = portalLayout->getFloorMesh(cell->getCellNumber());

	return floorMesh1;
}

int PathFinderManager::getFloorPath(const Vector3& pointA, const Vector3& pointB, const FloorMesh* floor, Vector<const Triangle*>*& nodes) {
	/*Vector3 objectPos = pointA;
	Vector3 targetPos = pointB;

	StringBuffer objPos;
	objPos << "returning path point x:" << objectPos.getX() << " z:" << objectPos.getZ() << " y:" << objectPos.getY();
	info(objPos.toString(), true);

	//switching y<->z for client coords
	objectPos.set(objectPos.getX(), objectPos.getY(), objectPos.getZ());
	targetPos.set(targetPos.getX(), targetPos.getY(), targetPos.getZ());*/

	//TriangleNode* objectFloor = floor->findNearestTriangle(objectPos);

	const TriangleNode* objectFloor = CollisionManager::getTriangle(pointA, floor);

	/*Vector3 objectFloorVector = objectFloor->getBarycenter();
	StringBuffer objectFoorBar;
	objectFoorBar << "nearest object floor point x:" << objectFloorVector.getX() << " z:" << objectFloorVector.getY() << " y:" << objectFloorVector.getZ();
	info(objectFoorBar.toString(), true);*/

	//TriangleNode* targetFloor = floor->findNearestTriangle(targetPos);
	const TriangleNode* targetFloor = CollisionManager::getTriangle(pointB, floor);

	/*Vector3 targetFloorVector = targetFloor->getBarycenter();
	StringBuffer targetFoorBar;
	targetFoorBar << "nearest target floor point x:" << targetFloorVector.getX() << " z:" << targetFloorVector.getY() << " y:" << targetFloorVector.getZ();
	info(targetFoorBar.toString(), true);*/

	nodes = nullptr;

	if (objectFloor == targetFloor) { // we are on the same triangle, returning pointB
		return -1;
	} else if (objectFloor == nullptr || targetFloor == nullptr)
		return 1;

	nodes = TriangulationAStarAlgorithm::search(pointA, pointB, objectFloor, targetFloor);

	if (nodes == nullptr)
		return 1;

	return 0;
}

Vector3 PathFinderManager::transformToModelSpace(const Vector3& point, SceneObject* building) {
	// we need to move world position into model space
	Vector3 switched(point.getX(), point.getZ(), point.getY());
	Matrix4 translationMatrix;
	translationMatrix.setTranslation(-building->getPositionX(), -building->getPositionZ(), -building->getPositionY());

	float rad = -building->getDirection()->getRadians();
	float cosRad = cos(rad);
	float sinRad = sin(rad);

	Matrix3 rot;
	rot[0][0] = cosRad;
	rot[0][2] = -sinRad;
	rot[1][1] = 1;
	rot[2][0] = sinRad;
	rot[2][2] = cosRad;

	Matrix4 rotateMatrix;
	rotateMatrix.setRotationMatrix(rot);

	Matrix4 modelMatrix;
	modelMatrix = translationMatrix * rotateMatrix;

	Vector3 transformedPosition = switched * modelMatrix;

	transformedPosition.set(transformedPosition.getX(), transformedPosition.getY(), transformedPosition.getZ());

	return transformedPosition;
}

// Diagnostics helper for the F_0.8.0 egress repair ladder. Only ever called
// from inside the structure-traversal feature gate.
static String describeStructureTraversalPathNode(const char* label,
		const PathNode* node) {
	if (node == nullptr)
		return String(label) + "=null";

	Vector3 position = node->getPosition();

	return String(label) + "=" + String::valueOf(node->getID()) +
		"(global=" + String::valueOf(node->getGlobalGraphNodeID()) +
		" pos=(" + String::valueOf(position.getX()) + "," +
		String::valueOf(position.getY()) + "," +
		String::valueOf(position.getZ()) + "))";
}

Vector<WorldCoordinates>* PathFinderManager::findPathFromCellToWorld(const WorldCoordinates& pointA, const WorldCoordinates& pointB, Zone *zone) {
	Vector<WorldCoordinates>* path = new Vector<WorldCoordinates>(5, 1);

	if (path == nullptr)
		return nullptr;

	path->add(pointA);

	CellObject* ourCell = pointA.getCell();
	ManagedReference<BuildingObject*> building = cast<BuildingObject*>( ourCell->getParent().get().get());
	int ourCellID = ourCell->getCellNumber();
	SharedObjectTemplate* templateObject = ourCell->getParent().get()->getObjectTemplate();

	if (templateObject == nullptr) {
		delete path;
		return nullptr;
	}

	const PortalLayout* portalLayout = templateObject->getPortalLayout();

	if (portalLayout == nullptr) {
		delete path;
		return nullptr;
	}

	const FloorMesh* sourceFloorMesh = portalLayout->getFloorMesh(ourCellID);

	if (sourceFloorMesh == nullptr) {
		delete path;
		return nullptr;
	}

	const PathGraph* sourcePathGraph = sourceFloorMesh->getPathGraph();

	if (sourcePathGraph == nullptr) {
		delete path;
		return nullptr;
	}

	const FloorMesh* exteriorFloorMesh = portalLayout->getFloorMesh(0);

	if (exteriorFloorMesh == nullptr) {
		delete path;
		return nullptr;
	}

	const PathGraph* exteriorPathGraph = exteriorFloorMesh->getPathGraph();

	if (exteriorPathGraph == nullptr) {
		delete path;
		return nullptr;
	}

	// we need to move world position into model space
	Vector3 transformedPosition = transformToModelSpace(pointB.getPoint(), building);

	//find exit node in our cell
	//PathNode* exitNode = sourcePathGraph->findNearestNode(pointA.getPoint());
	const TriangleNode* nearestTargetNodeTriangle = CollisionManager::getTriangle(pointA.getPoint(), sourceFloorMesh);

	if (nearestTargetNodeTriangle == nullptr) {
		delete path;
		return nullptr;
	}

	const PathNode* exitNode = CollisionManager::findNearestPathNode(nearestTargetNodeTriangle, sourceFloorMesh, transformedPosition);//targetPathGraph->findNearestNode(pointB.getPoint());

	if (exitNode == nullptr) {
		delete path;
		return nullptr;
	}

	//find exterior node
	const PathNode* exteriorNode = exteriorPathGraph->findNearestGlobalNode(transformedPosition);

	if (exteriorNode == nullptr) {
		delete path;
		return nullptr;
	}

	//find path to the exit
	Vector<const PathNode*>* exitPath = portalLayout->getPath(exitNode, exteriorNode);

	if (exitPath == nullptr) {
		String zoneName = zone == nullptr ? "unknown" : zone->getZoneName();

		// The legacy selection and interior-rooted search above are deliberately
		// untouched. The foundation repair is entered only after that attempt has
		// failed, and only while the new feature gate is enabled.
		if (SimPlayerManager::instance()->isStructureTraversalEnabled()) {
			const PathNode* legacyExitNode = exitNode;
			const PathNode* legacyExteriorNode = exteriorNode;
			bool repaired = false;

			SimPlayerManager::instance()->recordStructureTraversalPathfinderFallback();
			StructureTraversalDiagLog::write(
				"ST_PATH repair=legacy_failure building=" +
				String::valueOf(building->getObjectID()) + " cell=" +
				String::valueOf(ourCell->getObjectID()) + " from=" +
				StructureTraversalDiagLog::fmtPos(pointA) + " to=" +
				StructureTraversalDiagLog::fmtPos(pointB) + " zone=" + zoneName);

			// Topology snapshot: without this, an "all_failed" tells us nothing
			// about WHY the portal graph could not be crossed.
			StructureTraversalDiagLog::write(
				"ST_PATH repair=topology " +
				describeStructureTraversalPathNode("legacyExitNode",
					legacyExitNode) + " " +
				describeStructureTraversalPathNode("legacyExteriorNode",
					legacyExteriorNode) + " sourceGlobalNodes=" +
				String::valueOf(sourcePathGraph == nullptr ? -1 :
					sourcePathGraph->getGlobalNodes().size()) +
				" building=" + String::valueOf(building->getObjectID()) +
				" cell=" + String::valueOf(ourCell->getObjectID()));

			// Retry 1: re-hint the interior exit from the agent's cell-local
			// position, not the requested outdoor door position.
			const PathNode* repairedExitNode =
				CollisionManager::findNearestPathNode(nearestTargetNodeTriangle,
					sourceFloorMesh, pointA.getPoint());
			if (repairedExitNode != nullptr) {
				exitPath = portalLayout->getPath(repairedExitNode,
					legacyExteriorNode);
				if (exitPath != nullptr) {
					exitNode = repairedExitNode;
					repaired = true;
					StructureTraversalDiagLog::write(
						"ST_PATH repair=interior_rehint result=success nodes=" +
						String::valueOf(exitPath->size()) + " building=" +
						String::valueOf(building->getObjectID()) + " cell=" +
						String::valueOf(ourCell->getObjectID()));
				}
			}

			if (!repaired)
				StructureTraversalDiagLog::write(
					"ST_PATH repair=interior_rehint result=failed reason=" +
					String(repairedExitNode == nullptr ?
						"no_nearest_path_node" : "no_portal_path") + " " +
					describeStructureTraversalPathNode("rehintExitNode",
						repairedExitNode) + " " +
					describeStructureTraversalPathNode("exteriorNode",
						legacyExteriorNode) + " building=" +
					String::valueOf(building->getObjectID()) + " cell=" +
					String::valueOf(ourCell->getObjectID()));

			// Retry 2: choose the exterior node whose global graph ID is exposed
			// by the source cell, nearest to the requested door. Kept in scope
			// so Retry 3 can reverse the CORRECTED pair, not the legacy one.
			const PathNode* linkedExteriorNode = nullptr;
			if (!repaired) {
				float linkedDistance = 160000000.f;
				Vector<const PathNode*> sourceGlobalNodes =
					sourcePathGraph->getGlobalNodes();

				for (int i = 0; i < sourceGlobalNodes.size(); ++i) {
					const PathNode* sourceGlobalNode = sourceGlobalNodes.get(i);
					if (sourceGlobalNode == nullptr)
						continue;

					const PathNode* candidate = exteriorFloorMesh->getGlobalNode(
						sourceGlobalNode->getGlobalGraphNodeID());
					if (candidate == nullptr)
						continue;

					float candidateDistance = candidate->getPosition().squaredDistanceTo(
						transformedPosition);
					if (linkedExteriorNode == nullptr ||
						candidateDistance < linkedDistance) {
						linkedExteriorNode = candidate;
						linkedDistance = candidateDistance;
					}
				}

				if (linkedExteriorNode != nullptr) {
					const PathNode* linkedExitNode = repairedExitNode == nullptr ?
						legacyExitNode : repairedExitNode;
					exitPath = portalLayout->getPath(linkedExitNode,
						linkedExteriorNode);
					if (exitPath != nullptr) {
						exitNode = linkedExitNode;
						exteriorNode = linkedExteriorNode;
						repaired = true;
						StructureTraversalDiagLog::write(
							"ST_PATH repair=linked_exterior result=success nodes=" +
							String::valueOf(exitPath->size()) + " globalNode=" +
							String::valueOf(linkedExteriorNode->getGlobalGraphNodeID()) +
							" building=" + String::valueOf(building->getObjectID()) +
							" cell=" + String::valueOf(ourCell->getObjectID()));
					}
				}

				if (!repaired)
					StructureTraversalDiagLog::write(
						"ST_PATH repair=linked_exterior result=failed reason=" +
						String(linkedExteriorNode == nullptr ?
							"no_linked_exterior_node" : "no_portal_path") +
						" candidatesScanned=" +
						String::valueOf(sourceGlobalNodes.size()) + " " +
						describeStructureTraversalPathNode("linkedExteriorNode",
							linkedExteriorNode) + " building=" +
						String::valueOf(building->getObjectID()) + " cell=" +
						String::valueOf(ourCell->getObjectID()));
			}

			// Retry 3: the entry direction is known to work for these layouts.
			// Reverse it and feed it through the existing downstream processing.
			// Try the CORRECTED node pair first — reversing the legacy pair is
			// pointless when Retry 1/2 established that those nodes are the
			// disconnected ones — then fall back to the legacy pair.
			if (!repaired) {
				const PathNode* reverseExitNode = repairedExitNode == nullptr ?
					legacyExitNode : repairedExitNode;
				const PathNode* reverseExteriorNode =
					linkedExteriorNode == nullptr ? legacyExteriorNode :
					linkedExteriorNode;

				Vector<const PathNode*>* entryPath = portalLayout->getPath(
					reverseExteriorNode, reverseExitNode);
				if (entryPath == nullptr && (reverseExitNode != legacyExitNode ||
						reverseExteriorNode != legacyExteriorNode)) {
					reverseExitNode = legacyExitNode;
					reverseExteriorNode = legacyExteriorNode;
					entryPath = portalLayout->getPath(reverseExteriorNode,
						reverseExitNode);
				}

				if (entryPath != nullptr) {
					exitPath = new Vector<const PathNode*>(entryPath->size(), 1);
					for (int i = entryPath->size() - 1; i >= 0; --i)
						exitPath->add(entryPath->get(i));
					delete entryPath;
					exitNode = reverseExitNode;
					exteriorNode = reverseExteriorNode;
					repaired = true;
					StructureTraversalDiagLog::write(
						"ST_PATH repair=reversed_entry result=success nodes=" +
						String::valueOf(exitPath->size()) + " building=" +
						String::valueOf(building->getObjectID()) + " cell=" +
						String::valueOf(ourCell->getObjectID()));
				} else {
					// Both directions are unreachable across the portal graph
					// for every node pair the ladder can construct.
					StructureTraversalDiagLog::write(
						"ST_PATH repair=reversed_entry result=failed "
						"reason=no_portal_path_either_direction "
						"legacyPairAlsoTried=" +
						String::valueOf(reverseExitNode == legacyExitNode &&
							reverseExteriorNode == legacyExteriorNode) + " " +
						describeStructureTraversalPathNode("reverseExitNode",
							reverseExitNode) + " " +
						describeStructureTraversalPathNode(
							"reverseExteriorNode", reverseExteriorNode) +
						" building=" + String::valueOf(building->getObjectID()) +
						" cell=" + String::valueOf(ourCell->getObjectID()));
				}
			}

			// Retry 4: start A* at the source cell's global nodes and allow the
			// portal graph to traverse intermediate cell graphs before reaching
			// the exterior. Retry 2 only tests a direct shared-global-ID pair;
			// this intentionally does not require such a pair.
			if (!repaired) {
				Vector<const PathNode*> remainingGlobalNodes =
					sourcePathGraph->getGlobalNodes();
				int candidatesTotal = remainingGlobalNodes.size();
				const int candidateCap = 16;
				int candidatesTried = 0;
				const PathNode* bestExteriorNode = linkedExteriorNode != nullptr ?
					linkedExteriorNode : legacyExteriorNode;

				while (remainingGlobalNodes.size() > 0 &&
						candidatesTried < candidateCap) {
					int nearestIndex = 0;
					float nearestDistance = remainingGlobalNodes.get(0)->
						getPosition().squaredDistanceTo(transformedPosition);

					for (int i = 1; i < remainingGlobalNodes.size(); ++i) {
						float candidateDistance = remainingGlobalNodes.get(i)->
							getPosition().squaredDistanceTo(transformedPosition);
						if (candidateDistance < nearestDistance) {
							nearestIndex = i;
							nearestDistance = candidateDistance;
						}
					}

					const PathNode* sourceGlobalNode =
						remainingGlobalNodes.get(nearestIndex);
					remainingGlobalNodes.remove(nearestIndex);

					// Skip outright rather than half-guarding: getPath() would
					// dereference a null start node, and the nearest-scan above
					// already dereferences to measure distance.
					if (sourceGlobalNode == nullptr)
						continue;

					candidatesTried++;

					// getNeighbors() mixes BOTH kinds of edge: ordinary
					// same-floor ones from PathGraph::connectNodes, and the
					// cross-floor links from connectFloorMeshGraphs. Only the
					// latter constitute portal connectivity, so count them
					// separately -- a node with three local edges and no portal
					// link would otherwise read as connected and lead the next
					// diagnosis to exactly the wrong conclusion.
					const Vector<PathNode*>* neighbors =
						sourceGlobalNode->getNeighbors();
					int neighborCount = neighbors == nullptr ? 0 :
						neighbors->size();
					int crossGraphChildren = 0;

					for (int n = 0; n < neighborCount; ++n) {
						const PathNode* neighbor = neighbors->get(n);

						if (neighbor != nullptr && neighbor->getPathGraph() !=
								sourceGlobalNode->getPathGraph())
							++crossGraphChildren;
					}

					// The bot must be able to WALK to this node inside its own
					// cell. Downstream, a failed floor route is silently ignored
					// and exitNode is appended anyway, which would emit a
					// straight in-cell segment that can cross walls -- the exact
					// clipping this project is trying to eliminate. The stock
					// selector filters through the same triangle-floor route.
					Vector<const Triangle*>* candidateFloorPath = nullptr;
					int floorResult = getFloorPath(pointA.getPoint(),
						sourceGlobalNode->getPosition(), sourceFloorMesh,
						candidateFloorPath);
					// getFloorPath's contract: -1 = SAME TRIANGLE (reachable by
					// a direct segment, nodes left null), 0 = reachable via the
					// returned triangle path, 1 = genuinely unreachable. Only 1
					// rejects. The `res != -1` idiom used further down guards
					// whether INTERMEDIATE edges are needed, which is a
					// different question -- reusing it here would discard the
					// most common close-range case as unreachable.
					bool floorReachable = floorResult == -1 ||
						(floorResult == 0 && candidateFloorPath != nullptr);

					if (candidateFloorPath != nullptr)
						delete candidateFloorPath;

					Vector<const PathNode*>* candidatePath =
						(!floorReachable || bestExteriorNode == nullptr) ?
						nullptr : portalLayout->getPath(sourceGlobalNode,
						bestExteriorNode);
					String candidatePathNodes = candidatePath == nullptr ?
						String("none") : String::valueOf(candidatePath->size());

					StructureTraversalDiagLog::write(
						"ST_PATH repair=global_multihop candidate globalId=" +
							String::valueOf(
								sourceGlobalNode->getGlobalGraphNodeID()) +
						" crossGraphChildren=" +
						String::valueOf(crossGraphChildren) + " neighborCount=" +
						String::valueOf(neighborCount) + " floorReachable=" +
						String::valueOf(floorReachable ? 1 : 0) +
						" floorResult=" + String::valueOf(floorResult) +
						" pathNodes=" +
						candidatePathNodes);

					if (candidatePath != nullptr) {
						exitPath = candidatePath;
						exitNode = sourceGlobalNode;
						exteriorNode = bestExteriorNode;
						repaired = true;
						break;
					}
				}

				StructureTraversalDiagLog::write(
					"ST_PATH repair=global_multihop result=" +
					String(repaired ? "success" : "failed") + " nodes=" +
					String::valueOf(exitPath == nullptr ? 0 : exitPath->size()) +
					" globalNode=" + String::valueOf(repaired && exitNode != nullptr ?
						exitNode->getGlobalGraphNodeID() : -1) +
					" candidatesTried=" + String::valueOf(candidatesTried) +
					" candidatesTotal=" + String::valueOf(candidatesTotal) +
					" building=" + String::valueOf(building->getObjectID()) +
					" cell=" + String::valueOf(ourCell->getObjectID()));
			}

			// Retry 5: the source cell's portal geometry is authoritative when
			// the path graph has no edge into the exterior graph. Walk to the
			// nearest reachable outside portal on the source floor, then emit its
			// model-space doorway and the same doorway transformed to world space.
			if (!repaired) {
				const CellProperty* sourceCellProperty =
					portalLayout->getCellProperty(ourCellID);
				int portalsExamined = 0;
				int outsidePortals = 0;
				int reachablePortals = 0;
				const CellPortal* selectedPortal = nullptr;
				Vector3 selectedDoorPoint;
				float selectedDistance = 0.f;
				int selectedFloorResult = 1;
				Vector<const Triangle*>* selectedTrianglePath = nullptr;

				if (sourceCellProperty != nullptr) {
					for (int i = 0; i < sourceCellProperty->getNumberOfPortals();
							++i) {
						++portalsExamined;
						const CellPortal* portal = sourceCellProperty->getPortal(i);
						int targetCellIndex = portal == nullptr ? -1 :
							portal->getTargetCellIndex();
						int geometryIndex = portal == nullptr ? -1 :
							portal->getGeometryIndex();
						int floorResult = 1;
						bool reachable = false;
						bool doorResolved = false;
						Vector3 modelDoorPoint;
						Vector3 doorPoint;
						float distanceFromBot = 0.f;
						Vector<const Triangle*>* trianglePath = nullptr;

						// Resolve geometry ONLY for portals that lead outside.
						// getPortalBounds() indexes portalGeometry without a
						// bounds check (it throws on a bad index), so there is no
						// reason to touch it for portals we are about to discard.
						if (portal != nullptr && targetCellIndex <= 1) {
							++outsidePortals;

							// Portal geometry is Y-UP MODEL space: .Y is height,
							// .Z is north. Path-graph node positions and
							// pointA.getPoint() are already cell-local
							// (x, north, height). These are DIFFERENT frames --
							// verified against stock
							// BuildingObjectImplementation::computeExteriorPortalWorldPoint,
							// which drops to the floor by subtracting the Y
							// extent and then swaps components 1 and 2.
							// center() is also the doorway's VERTICAL MIDDLE, so
							// aiming at it would target roughly chest height
							// rather than the floor the bot walks on.
							const AABB& doorBounds =
								portalLayout->getPortalBounds(geometryIndex);
							modelDoorPoint = doorBounds.center() -
								Vector3(0, doorBounds.extents().getY(), 0);
							doorPoint = Vector3(modelDoorPoint.getX(),
								modelDoorPoint.getZ(), modelDoorPoint.getY());
							doorResolved = true;
							distanceFromBot = pointA.getPoint().distanceTo(
								doorPoint);
							floorResult = getFloorPath(pointA.getPoint(), doorPoint,
								sourceFloorMesh, trianglePath);
							reachable = floorResult == -1 ||
								(floorResult == 0 && trianglePath != nullptr);
							if (reachable)
								++reachablePortals;
						}
						StructureTraversalDiagLog::write(
							"ST_PATH repair=portal_geometry portal index=" +
							String::valueOf(i) + " targetCell=" +
							String::valueOf(targetCellIndex) + " geometryIndex=" +
							String::valueOf(geometryIndex) + " doorModel=" +
							(doorResolved ? (String::valueOf(
								modelDoorPoint.getX()) + "," +
								String::valueOf(modelDoorPoint.getY()) + "," +
								String::valueOf(modelDoorPoint.getZ())) :
								String("not_outside")) + " doorCellLocal=" +
							(doorResolved ? (String::valueOf(doorPoint.getX()) +
								"," + String::valueOf(doorPoint.getY()) + "," +
								String::valueOf(doorPoint.getZ())) :
								String("not_outside")) + " floorResult=" +
							String::valueOf(floorResult) + " reachable=" +
							String::valueOf(reachable ? 1 : 0) +
							" distFromBot=" + (doorResolved ?
								String::valueOf(distanceFromBot) :
								String("n/a")));

						if (reachable && (selectedPortal == nullptr ||
								distanceFromBot < selectedDistance)) {
							if (selectedTrianglePath != nullptr)
								delete selectedTrianglePath;

							selectedPortal = portal;
							selectedDoorPoint = doorPoint;
							selectedDistance = distanceFromBot;
							selectedFloorResult = floorResult;
							selectedTrianglePath = trianglePath;
							trianglePath = nullptr;
						}

						if (trianglePath != nullptr)
							delete trianglePath;
					}
				}

				if (selectedPortal != nullptr) {
					if (selectedFloorResult != -1 &&
							selectedTrianglePath != nullptr) {
						addTriangleNodeEdges(pointA.getPoint(), selectedDoorPoint,
							selectedTrianglePath, path, ourCell);
					}

					if (selectedTrianglePath != nullptr)
						delete selectedTrianglePath;

					path->add(WorldCoordinates(selectedDoorPoint, ourCell));
					WorldCoordinates doorInCell(selectedDoorPoint, ourCell);
					path->add(WorldCoordinates(doorInCell.getWorldPosition(),
						nullptr));

					StructureTraversalDiagLog::write(
						"ST_PATH repair=portal_geometry result=success reason=ok "
						"portalsExamined=" + String::valueOf(portalsExamined) +
						" outsidePortals=" + String::valueOf(outsidePortals) +
						" reachable=" + String::valueOf(reachablePortals) +
						" nodes=" + String::valueOf(path->size()) + " building=" +
						String::valueOf(building->getObjectID()) + " cell=" +
						String::valueOf(ourCell->getObjectID()));

					// findPathFromCellToWorld is general pathfinding, not just
					// SimPlayer egress: returning at the doorway would hand every
					// other caller a truncated route REPORTED AS SUCCESS. Append
					// the outdoor leg to the actually-requested pointB, mirroring
					// the normal tail below.
					if (path->size()) {
						Vector<WorldCoordinates>* outdoorLeg =
							findPathFromWorldToWorld(
								path->get(path->size() - 1), pointB, zone);

						if (outdoorLeg != nullptr) {
							path->addAll(*outdoorLeg);
							delete outdoorLeg;
						}
					} else {
						path->add(pointB);
					}

					// !repaired means Retries 1-4 returned no path, so exitPath is
					// null here. Retry 5 owns only the existing path vector and
					// returns it directly; the downstream exitPath cleanup is skipped.
					return path;
				}

				String failureReason = outsidePortals == 0 ?
					String("no_outside_portal") : String("none_reachable");
				StructureTraversalDiagLog::write(
					"ST_PATH repair=portal_geometry result=failed reason=" +
					failureReason + " portalsExamined=" +
					String::valueOf(portalsExamined) + " outsidePortals=" +
					String::valueOf(outsidePortals) + " reachable=" +
					String::valueOf(reachablePortals) + " nodes=0 building=" +
					String::valueOf(building->getObjectID()) + " cell=" +
					String::valueOf(ourCell->getObjectID()));
			}

			// Retry 6: the doorway graph can cross several interior cells even
			// when the path graphs cannot reach the exterior graph. BFS the raw
			// portal links so the exact geometry used for every cell handoff is
			// retained, then route each segment on that cell's floor mesh.
			if (!repaired) {
				const int maxCellsVisited = 32;
				const int maxCellHops = 8;
				const int cellPropertyCount =
					portalLayout->getCellProperties().size();
				const int pathSizeOnEntry = path->size();
				Vector<int> pendingCells;
				SortedVector<int> visitedCells;
				Vector<int> parentCells;
				Vector<int> parentPortalGeometry;
				Vector<int> cellDepth;

				for (int i = 0; i < cellPropertyCount; ++i) {
					parentCells.add(-1);
					parentPortalGeometry.add(-1);
					cellDepth.add(-1);
				}

				bool bfsFound = false;
				int exitCellIndex = -1;
				int exitPortalGeometry = -1;
				int exitPortalTargetCell = -1;
				// Streamed, not collected. A cell can expose SEVERAL world
				// portals, so a list capped at the cell budget would silently
				// drop eligible exits -- including, on this building, the one
				// nearest the destination. Evaluating each candidate as it is
				// found keeps memory O(1) and drops nothing.
				int exitCandidateCount = 0;
				int bestExitCell = -1;
				int bestExitGeometry = -1;
				int bestExitTargetCell = -1;
				int bestExitDepth = 0;
				float bestExitDistance = 0.f;

				if (ourCellID >= 0 && ourCellID < cellPropertyCount) {
					visitedCells.setNoDuplicateInsertPlan();
					visitedCells.put(ourCellID);
					pendingCells.add(ourCellID);
					cellDepth.set(ourCellID, 0);
				}

				// Telemetry only. Hitting a limit must NOT stop the search:
				// the exit portal may be later in this same cell's portal list,
				// or in a cell already queued and still within budget. Only the
				// over-budget NEIGHBOUR is skipped.
				bool budgetHit = false;
				for (int pendingIndex = 0;
						pendingIndex < pendingCells.size();
						++pendingIndex) {
					int currentCellIndex = pendingCells.get(pendingIndex);
					const CellProperty* currentProperty =
						portalLayout->getCellProperty(currentCellIndex);
					if (currentProperty == nullptr)
						continue;

					int currentDepth = cellDepth.get(currentCellIndex);
					for (int portalIndex = 0;
							portalIndex < currentProperty->getNumberOfPortals();
							++portalIndex) {
						const CellPortal* portal =
							currentProperty->getPortal(portalIndex);
						if (portal == nullptr)
							continue;

						int targetCellIndex = portal->getTargetCellIndex();
						if (targetCellIndex <= 1) {
							++exitCandidateCount;

							int candidateGeometry = portal->getGeometryIndex();
							const AABB& candidateBounds =
								portalLayout->getPortalBounds(candidateGeometry);
							Vector3 candidateDoorModel =
								candidateBounds.center() -
								Vector3(0,
									candidateBounds.extents().getY(), 0);
							float candidateDistance =
								candidateDoorModel.distanceTo(
									transformedPosition);

							StructureTraversalDiagLog::write(
								"ST_PATH repair=multicell exitCandidate cell=" +
								String::valueOf(currentCellIndex) +
								" geometryIndex=" +
								String::valueOf(candidateGeometry) + " depth=" +
								String::valueOf(currentDepth) + " door=" +
								String::valueOf(candidateDoorModel.getX()) +
								"," +
								String::valueOf(candidateDoorModel.getY()) +
								"," +
								String::valueOf(candidateDoorModel.getZ()) +
								" distToTarget=" +
								String::valueOf(candidateDistance));

							// Nearest the destination wins; shorter chain breaks
							// ties.
							if (bestExitCell < 0 ||
									candidateDistance < bestExitDistance ||
									(candidateDistance == bestExitDistance &&
										currentDepth < bestExitDepth)) {
								bestExitCell = currentCellIndex;
								bestExitGeometry = candidateGeometry;
								bestExitTargetCell = targetCellIndex;
								bestExitDepth = currentDepth;
								bestExitDistance = candidateDistance;
							}

							continue;
						}

						if (targetCellIndex < 0 ||
								targetCellIndex >= cellPropertyCount ||
								visitedCells.contains(targetCellIndex))
							continue;

						// The final outside portal is also a portal hop, so a
						// child at this depth would exceed the total hop budget.
						// Skip THIS neighbour and keep scanning -- aborting here
						// would discard already-queued cells and the remaining
						// portals of this cell, any of which may hold an exit
						// that is still inside the budget.
						if (visitedCells.size() >= maxCellsVisited ||
								currentDepth >= maxCellHops - 1) {
							budgetHit = true;
							continue;
						}

						visitedCells.put(targetCellIndex);
						pendingCells.add(targetCellIndex);
						parentCells.set(targetCellIndex, currentCellIndex);
						parentPortalGeometry.set(targetCellIndex,
							portal->getGeometryIndex());
						cellDepth.set(targetCellIndex, currentDepth + 1);
					}
				}

				bfsFound = bestExitCell >= 0;

				if (bfsFound) {
					exitCellIndex = bestExitCell;
					exitPortalGeometry = bestExitGeometry;
					exitPortalTargetCell = bestExitTargetCell;

					StructureTraversalDiagLog::write(
						"ST_PATH repair=multicell exitChoice cell=" +
						String::valueOf(exitCellIndex) + " geometryIndex=" +
						String::valueOf(exitPortalGeometry) + " depth=" +
						String::valueOf(bestExitDepth) + " distToTarget=" +
						String::valueOf(bestExitDistance) + " candidates=" +
						String::valueOf(exitCandidateCount) +
						" rejectedFarther=" +
						String::valueOf(exitCandidateCount - 1) + " building=" +
						String::valueOf(building->getObjectID()) + " cell=" +
						String::valueOf(ourCell->getObjectID()));
				}

				Vector<int> reversedChain;
				Vector<int> chainCells;
				if (bfsFound) {
					int chainCellIndex = exitCellIndex;
					while (chainCellIndex >= 0) {
						reversedChain.add(chainCellIndex);
						if (chainCellIndex == ourCellID)
							break;
						chainCellIndex = parentCells.get(chainCellIndex);
					}

					if (reversedChain.size() == 0 ||
							reversedChain.get(reversedChain.size() - 1) != ourCellID) {
						bfsFound = false;
						exitCellIndex = -1;
						exitPortalGeometry = -1;
						exitPortalTargetCell = -1;
					} else {
						for (int i = reversedChain.size() - 1; i >= 0; --i)
							chainCells.add(reversedChain.get(i));
					}
				}

				StructureTraversalDiagLog::write(
					"ST_PATH repair=multicell bfs result=" +
					String(bfsFound ? "found" : "not_found") +
					" cellsVisited=" + String::valueOf(visitedCells.size()) +
					" budgetHit=" + String::valueOf(budgetHit ? 1 : 0) +
					" chainLen=" + String::valueOf(chainCells.size()) +
					" exitCell=" + String::valueOf(exitCellIndex) +
					" exitPortalGeom=" +
					String::valueOf(exitPortalGeometry) + " building=" +
					String::valueOf(building->getObjectID()) + " cell=" +
					String::valueOf(ourCell->getObjectID()));

				int failedHop = -1;
				int hopsCompleted = 0;
				if (!bfsFound) {
					while (path->size() > pathSizeOnEntry)
						path->remove(path->size() - 1);

					StructureTraversalDiagLog::write(
						String("ST_PATH repair=multicell result=failed reason=") +
						(budgetHit ? "no_exit_cell_within_budget" :
							"no_exit_cell_in_building") + " hops=0 nodes=" +
						String::valueOf(path->size()) + " building=" +
						String::valueOf(building->getObjectID()) + " cell=" +
						String::valueOf(ourCell->getObjectID()));
				} else {
					Vector3 currentPoint = pointA.getPoint();
					int currentCellIndex = ourCellID;
					CellObject* currentCell = ourCell;
					const FloorMesh* currentFloorMesh = sourceFloorMesh;
					bool routeReachable = true;

					for (int chainIndex = 1;
							chainIndex < chainCells.size(); ++chainIndex) {
						int nextCellIndex = chainCells.get(chainIndex);
						int geometryIndex =
							parentPortalGeometry.get(nextCellIndex);
						const AABB& doorBounds =
							portalLayout->getPortalBounds(geometryIndex);
						Vector3 doorModel = doorBounds.center() -
							Vector3(0, doorBounds.extents().getY(), 0);
						Vector3 doorCellLocal(doorModel.getX(), doorModel.getZ(),
							doorModel.getY());
						float segmentLength = currentPoint.distanceTo(doorCellLocal);
						Vector<const Triangle*>* trianglePath = nullptr;
						int floorResult = 1;
						if (currentFloorMesh != nullptr)
							floorResult = getFloorPath(currentPoint, doorCellLocal,
								currentFloorMesh, trianglePath);
						bool reachable = floorResult == -1 ||
							(floorResult == 0 && trianglePath != nullptr);
						CellObject* nextCell = building->getCell(nextCellIndex);
						const FloorMesh* nextFloorMesh = nextCell == nullptr ?
							nullptr : portalLayout->getFloorMesh(nextCellIndex);
						if (nextCell == nullptr || nextFloorMesh == nullptr)
							reachable = false;

						StructureTraversalDiagLog::write(
							"ST_PATH repair=multicell hop=" +
							String::valueOf(hopsCompleted) + " fromCell=" +
							String::valueOf(currentCellIndex) + " toCell=" +
							String::valueOf(nextCellIndex) + " geometryIndex=" +
							String::valueOf(geometryIndex) + " doorModel=" +
							String::valueOf(doorModel.getX()) + "," +
							String::valueOf(doorModel.getY()) + "," +
							String::valueOf(doorModel.getZ()) + " doorCellLocal=" +
							String::valueOf(doorCellLocal.getX()) + "," +
							String::valueOf(doorCellLocal.getY()) + "," +
							String::valueOf(doorCellLocal.getZ()) + " floorResult=" +
							String::valueOf(floorResult) + " reachable=" +
							String::valueOf(reachable ? 1 : 0) + " segLen=" +
							String::valueOf(segmentLength));

						if (!reachable) {
							if (trianglePath != nullptr)
								delete trianglePath;
							routeReachable = false;
							failedHop = hopsCompleted;
							break;
						}

						if (floorResult == 0 && trianglePath != nullptr)
							addTriangleNodeEdges(currentPoint, doorCellLocal,
								trianglePath, path, currentCell);

						if (trianglePath != nullptr)
							delete trianglePath;

						path->add(WorldCoordinates(doorCellLocal, currentCell));
						path->add(WorldCoordinates(doorCellLocal, nextCell));
						currentPoint = doorCellLocal;
						currentCell = nextCell;
						currentFloorMesh = nextFloorMesh;
						currentCellIndex = nextCellIndex;
						++hopsCompleted;
					}

					if (routeReachable) {
						int finalHop = hopsCompleted;
						const AABB& exitDoorBounds =
							portalLayout->getPortalBounds(exitPortalGeometry);
						Vector3 exitDoorModel = exitDoorBounds.center() -
							Vector3(0, exitDoorBounds.extents().getY(), 0);
						Vector3 exitDoorCellLocal(exitDoorModel.getX(),
							exitDoorModel.getZ(), exitDoorModel.getY());
						float segmentLength = currentPoint.distanceTo(
							exitDoorCellLocal);
						Vector<const Triangle*>* trianglePath = nullptr;
						int floorResult = 1;
						if (currentFloorMesh != nullptr)
							floorResult = getFloorPath(currentPoint,
								exitDoorCellLocal, currentFloorMesh, trianglePath);
						bool reachable = floorResult == -1 ||
							(floorResult == 0 && trianglePath != nullptr);

						StructureTraversalDiagLog::write(
							"ST_PATH repair=multicell hop=" +
							String::valueOf(finalHop) + " fromCell=" +
							String::valueOf(currentCellIndex) + " toCell=" +
							String::valueOf(exitPortalTargetCell) +
							" geometryIndex=" +
							String::valueOf(exitPortalGeometry) + " doorModel=" +
							String::valueOf(exitDoorModel.getX()) + "," +
							String::valueOf(exitDoorModel.getY()) + "," +
							String::valueOf(exitDoorModel.getZ()) +
							" doorCellLocal=" +
							String::valueOf(exitDoorCellLocal.getX()) + "," +
							String::valueOf(exitDoorCellLocal.getY()) + "," +
							String::valueOf(exitDoorCellLocal.getZ()) +
							" floorResult=" + String::valueOf(floorResult) +
							" reachable=" + String::valueOf(reachable ? 1 : 0) +
							" segLen=" + String::valueOf(segmentLength));

						if (!reachable) {
							if (trianglePath != nullptr)
								delete trianglePath;
							routeReachable = false;
							failedHop = finalHop;
						} else {
							if (floorResult == 0 && trianglePath != nullptr)
								addTriangleNodeEdges(currentPoint,
									exitDoorCellLocal, trianglePath, path, currentCell);

							if (trianglePath != nullptr)
								delete trianglePath;

							path->add(WorldCoordinates(exitDoorCellLocal,
								currentCell));
							WorldCoordinates eIn(exitDoorCellLocal, currentCell);
							path->add(WorldCoordinates(eIn.getWorldPosition(),
								nullptr));
							++hopsCompleted;
						}
					}

					if (routeReachable) {
						if (path->size()) {
							Vector<WorldCoordinates>* outdoorLeg =
								findPathFromWorldToWorld(
									path->get(path->size() - 1), pointB, zone);

							if (outdoorLeg != nullptr) {
								path->addAll(*outdoorLeg);
								delete outdoorLeg;
							}
						} else {
							path->add(pointB);
						}

						StructureTraversalDiagLog::write(
							"ST_PATH repair=multicell result=success reason=ok hops=" +
							String::valueOf(hopsCompleted) + " nodes=" +
							String::valueOf(path->size()) + " building=" +
							String::valueOf(building->getObjectID()) + " cell=" +
							String::valueOf(ourCell->getObjectID()));
						return path;
					}
				}

				if (bfsFound) {
					while (path->size() > pathSizeOnEntry)
						path->remove(path->size() - 1);

					StructureTraversalDiagLog::write(
						"ST_PATH repair=multicell result=failed reason="
						"hop_unreachable hops=" + String::valueOf(
							failedHop < 0 ? hopsCompleted : failedHop + 1) +
						" nodes=" + String::valueOf(path->size()) + " building=" +
						String::valueOf(building->getObjectID()) + " cell=" +
						String::valueOf(ourCell->getObjectID()));
				}
			}

			if (!repaired)
				StructureTraversalDiagLog::write(
					"ST_PATH repair=all_failed building=" +
					String::valueOf(building->getObjectID()) + " cell=" +
					String::valueOf(ourCell->getObjectID()) + " " +
					describeStructureTraversalPathNode("finalExitNode",
						exitNode) + " " +
					describeStructureTraversalPathNode("finalExteriorNode",
						exteriorNode) + " zone=" + zoneName);
		}

		if (exitPath == nullptr) {
			error() << "getPath from " << exitNode << " to " << exteriorNode << " exitpath is nullptr for building " << templateObject->getFullTemplateString() << " from " << pointA << " to " << pointB << " in zone " << zoneName;

			delete path;
			return nullptr;
		}
	}

	//find triangle path to exitNode
	Vector<const Triangle*>* trianglePath = nullptr;

	int res = getFloorPath(pointA.getPoint(), exitNode->getPosition(), sourceFloorMesh, trianglePath);

	if (res != -1 && trianglePath != nullptr) {
		addTriangleNodeEdges(pointA.getPoint(), exitNode->getPosition(), trianglePath, path, ourCell);
	}

	if (trianglePath != nullptr) {
		delete trianglePath;
	}

	path->add(WorldCoordinates(exitNode->getPosition(), ourCell));

	// Populate cell traversing
	for (int i = 0; i < exitPath->size(); ++i) {
		const PathNode* pathNode = exitPath->get(i);
		const PathGraph* pathGraph = pathNode->getPathGraph();

		const FloorMesh* floorMesh = pathGraph->getFloorMesh();

		int cellID = floorMesh->getCellID();

		 // We are outside
		if (cellID == 0) {
			WorldCoordinates coord(pathNode->getPosition(), ourCell);

			path->add(WorldCoordinates(coord.getWorldPosition(), nullptr));
		 // We are inside the building
		} else {
			CellObject* pathCell = building->getCell(cellID);

			path->add(WorldCoordinates(pathNode->getPosition(), pathCell));
		}
	}

	delete exitPath;
	exitPath = nullptr;

	if (path->size()) {
		Vector<WorldCoordinates>* newPath = findPathFromWorldToWorld(path->get(path->size()-1), pointB, zone);

		if (newPath != nullptr) {
			path->addAll(*newPath);
			delete newPath;
		}
	} else {
		path->add(pointB);
	}

	return path;
}

void PathFinderManager::addTriangleNodeEdges(const Vector3& source, const Vector3& goal, Vector<const Triangle*>* trianglePath,
		Vector<WorldCoordinates>* path, CellObject* cell) {
	Vector3 startPoint = Vector3(source.getX(), source.getZ(), source.getY());
	Vector3 goalPoint = Vector3(goal.getX(), goal.getZ(), goal.getY());

	Vector<Vector3>* funnelPath = Funnel::funnel(startPoint, goalPoint, trianglePath);

	/*info("found funnel size: " + String::valueOf(funnelPath->size()));
	info("triangle number: " + String::valueOf(trianglePath->size()));

	StringBuffer objectFoorBarSource;
	objectFoorBarSource << "funnel source point x:" << source.getX() << " z:" << source.getZ() << " y:" << source.getY();
	info(objectFoorBarSource.toString(), true);*/

	for (int i = 1; i < funnelPath->size() - 1; ++i) { //without the start and the goal
		/*Vector3 worldPosition = path->get(i);

		StringBuffer objectFoorBar;
		objectFoorBar << "funnel node point x:" << worldPosition.getX() << " z:" << worldPosition.getZ() << " y:" << worldPosition.getY();
		info(objectFoorBar.toString(), true);*/

		Vector3 pathPoint = funnelPath->get(i);

		//switch y<->x
		pathPoint.set(pathPoint.getX(), pathPoint.getY(), pathPoint.getZ());

		/*StringBuffer objectFoorBar;
		objectFoorBar << "funnel node point x:" << pathPoint.getX() << " z:" << pathPoint.getZ() << " y:" << pathPoint.getY();
		info(objectFoorBar.toString(), true);*/

		path->add(WorldCoordinates(pathPoint, cell));
	}

	/*StringBuffer objectFoorBarGoal;
	objectFoorBarGoal << "funnel goal point x:" << goal.getX() << " z:" << goal.getZ() << " y:" << goal.getY();
	info(objectFoorBarGoal.toString(), true);*/

	delete funnelPath;



	/*for (int i = 0; i < trianglePath->size(); ++i) {
		TriangleNode* node = trianglePath->get(i);

		//if (!node->isEdge()) // adding only edge nodes
			//continue;

		Vector3 pathBary = node->getBarycenter();

		//switch y<->x
		pathBary.set(pathBary.getX(), pathBary.getY(), pathBary.getZ());

		path->add(WorldCoordinates(pathBary, cell));
	}*/
}

Vector<WorldCoordinates>* PathFinderManager::findPathFromCellToDifferentCell(const WorldCoordinates& pointA, const WorldCoordinates& pointB) {
#ifdef DEBUG_PATHING
	info (true) << "PathFinderManager::findPathFromCellToDifferentCell -- called";
#endif

	auto ourCell = pointA.getCell();
	auto targetCell = pointB.getCell();

	if (ourCell == nullptr || targetCell == nullptr) {
		return nullptr;
	}

	int ourCellIndex = ourCell->getCellNumber();
	int targetCellIndex = targetCell->getCellNumber();

	ManagedReference<TangibleObject*> rootParent1 = cast<TangibleObject*>(ourCell->getParent().get().get());
	ManagedReference<TangibleObject*> rootParent2 = cast<TangibleObject*>(targetCell->getParent().get().get());

	if (rootParent1 == nullptr || rootParent2 == nullptr) {
		return nullptr;
	}

	 // TODO: implement path finding between 2 buildings
	 if (rootParent1 != rootParent2) {
		error() << __FUNCTION__ << " - no implementation for pathfinding between two separate root parents";
		return nullptr;
	 }

	auto templateObject = rootParent1->getObjectTemplate();

	if (templateObject == nullptr) {
		return nullptr;
	}

	const auto portalLayout = templateObject->getPortalLayout();

	if (portalLayout == nullptr) {
		return nullptr;
	}

	const auto floorMesh1 = portalLayout->getFloorMesh(ourCellIndex);
	const auto floorMesh2 = portalLayout->getFloorMesh(targetCellIndex);

	if (floorMesh1 == nullptr || floorMesh2 == nullptr) {
		return nullptr;
	}

	if (floorMesh2->getCellID() != targetCellIndex) {
		error() << __FUNCTION__ << " - floorMesh2 cellID != targetCellID";
		return nullptr;
	}

	// info(true) << "Current Cell Index: " << ourCellIndex <<  " Target Cell Index:" << targetCellIndex;

	const auto pathGraph1 = floorMesh1->getPathGraph();
	const auto pathGraph2 = floorMesh2->getPathGraph();

	if (pathGraph1 == nullptr || pathGraph2 == nullptr) {
		error() << __FUNCTION__ << " - PathGraph for target cell is null";
		return nullptr;
	}

	Vector<WorldCoordinates>* path = new Vector<WorldCoordinates>(5, 1);

	// Add initial point to path
	path->add(pointA);

	const auto nearestSourceNodeTriangle = CollisionManager::getTriangle(pointA.getPoint(), floorMesh1);

	if (nearestSourceNodeTriangle == nullptr) {
		delete path;
		path = nullptr;

		return nullptr;
	}

	const auto source = CollisionManager::findNearestPathNode(nearestSourceNodeTriangle, floorMesh1, pointA.getPoint());

	if (source == nullptr) {
		delete path;
		path = nullptr;

		return nullptr;
	}

	const auto nearestTargetNodeTriangle = CollisionManager::getTriangle(pointB.getPoint(), floorMesh2);

	if (nearestTargetNodeTriangle == nullptr) {
		delete path;
		path = nullptr;

		return nullptr;
	}

	const auto target = CollisionManager::findNearestPathNode(nearestTargetNodeTriangle, floorMesh2, pointB.getPoint());

	if (target == nullptr) {
		delete path;
		path = nullptr;

		return nullptr;
	}

	Vector<const PathNode*>* nodes = portalLayout->getPath(source, target);

	if (nodes == nullptr) {
		error() << __FUNCTION__ << "Could not find path from " << source << " to " << target << " in building: " << templateObject->getFullTemplateString();

		delete path;
		path = nullptr;

		return nullptr;
	}

	// FIXME (dannuic): Sometimes nodes only have one entry.... why?
	if (nodes->size() == 1) {
		auto zone = rootParent1->getZone();
		String zoneName = zone == nullptr ? "unknown" : zone->getZoneName();

		error() << __FUNCTION__ << "getPath from " << source << " to " << target << " nodes->size() == 1 for building " << templateObject->getFullTemplateString() << " from " << pointA << " to " << pointB << " in zone " << zoneName;

		delete nodes;
		delete path;
		nodes = nullptr;
		path = nullptr;

		return nullptr;
	}

	// path from our position to path node
	Vector<const Triangle*>* trianglePath = nullptr;

	int res = getFloorPath(pointA.getPoint(), nodes->get(1)->getPosition(), floorMesh1, trianglePath);

	if (res != -1 && trianglePath != nullptr) {
		addTriangleNodeEdges(pointA.getPoint(), nodes->get(1)->getPosition(), trianglePath, path, ourCell);
	}

	if (trianglePath != nullptr) {
		delete trianglePath;
		trianglePath = nullptr;
	}

	// Source Cell Node, add as starting point of path
	WorldCoordinates sourceCellNode(source->getPosition(), ourCell);

	path->add(sourceCellNode);

	bool rootIsPob = rootParent1->isPobShip();

	// Traversing cells
	for (int i = 1; i < nodes->size(); ++i) {
		const PathNode* pathNode = nodes->get(i);
		const PathGraph* pathGraph = pathNode->getPathGraph();

		const FloorMesh* floorMesh = pathGraph->getFloorMesh();

		int cellIndex = floorMesh->getCellID();

		if (cellIndex == 0 || (rootIsPob && (cellIndex != ourCellIndex || cellIndex != targetCellIndex))) {
			// We should never have a cellIndex of 0 when moving cell to cell
			nodes->remove(i);
#ifdef DEBUG_PATHING
			info(true) << "Removing node with cellIndex = 0";
#endif
		} else {
			CellObject* pathCell = rootParent1->getCell(cellIndex);

			if (pathCell == nullptr) {
				continue;
			}

			WorldCoordinates coord(pathNode->getPosition(), pathCell);

#ifdef DEBUG_PATHING
			info(true) << "Adding Path Node with Cell ID = " << cellIndex << " X: " << coord.getX() << " Z: " << coord.getZ() << " Y: " << coord.getY();
#endif
			path->add(coord);

			if (i == nodes->size() - 1) {
				if (pathNode != target) {
					error() << __FUNCTION__ << "pathNode != target pathNode: " << pathNode->getID() << " target:" << target->getID();
				}

				if (pathCell != targetCell) {
					error() << "final cell not target cell";
				}
			}
		}
	}

	// Clean up nodes
	delete nodes;
	nodes = nullptr;

	// path from cell entrance to destination point
	trianglePath = nullptr;

	res = getFloorPath(path->get(path->size() - 1).getPoint(), pointB.getPoint(), floorMesh2, trianglePath);

	if (res != -1 && trianglePath != nullptr) {
		addTriangleNodeEdges(path->get(path->size() - 1).getPoint(), pointB.getPoint(), trianglePath, path, targetCell);
	}

	// Clean up the triangle path
	if (trianglePath != nullptr) {
		delete trianglePath;
	}

	// Add final ending pointB
	path->add(pointB);

#ifdef DEBUG_PATHING
	info(true) << "FINAL PATH POINTS cell to other cell:";

	for (int i = path->size() - 1; i >= 0; i--) {
		int forwardItter = (path->size() - 1) - i;
		WorldCoordinates coord = path->get(forwardItter);

		if (coord.getCell() == nullptr) {
			path->remove(i);
			continue;
		}

		info(true) << "Final Path Point -- X: " << coord.getX() << " Z: " << coord.getZ() << " Y: " << coord.getY() << " Cell ID: " << coord.getCell()->getObjectID();
	}
#endif

	return path;
}

Vector<WorldCoordinates>* PathFinderManager::findPathWithinCell(const WorldCoordinates& pointA, const WorldCoordinates& pointB) {
	auto ourCell = pointA.getCell();
	auto targetCell = pointB.getCell();

#ifdef DEBUG_PATHING
	info(true) << "findPathWithinCell called";
#endif // DEBUG_PATHING

	if (ourCell == nullptr || targetCell == nullptr) {
		return nullptr;
	}

	if (ourCell != targetCell) {
		return findPathFromCellToDifferentCell(pointA, pointB);
	}

	int ourCellID = ourCell->getCellNumber();

	auto rootParent = cast<TangibleObject*>(ourCell->getParent().get().get());

	if (rootParent == nullptr) {
		return nullptr;
	}

	if (rootParent->isBuildingObject()) {
		auto building = cast<BuildingObject*>(rootParent);

		if (building == nullptr) {
			return nullptr;
		}
	} else if (rootParent->isPobShip()) {
		auto pobShip = cast<PobShipObject*>(rootParent);

		if (pobShip == nullptr) {
			return nullptr;
		}
	} else {
		// Not a building or a POB Ship
		return nullptr;
	}

	SharedObjectTemplate* templateObject = rootParent->getObjectTemplate();

	if (templateObject == nullptr) {
		return nullptr;
	}

	const PortalLayout* portalLayout = templateObject->getPortalLayout();

	if (portalLayout == nullptr) {
		return nullptr;
	}

	const FloorMesh* floorMesh1 = portalLayout->getFloorMesh(ourCellID);

	if (floorMesh1 == nullptr) {
		return nullptr;
	}

	Vector<WorldCoordinates>* path = new Vector<WorldCoordinates>(5, 1);

	 // Add source point
	path->add(pointA);

#ifdef DEBUG_PATHING
	info(true) << "Origin and destination points are in the same cell. Need to Calculate triangle path using floorMesh for cellID: " << ourCellID;
#endif // DEBUG_PATHING

	Vector<const Triangle*>* trianglePath = nullptr;

	int res = getFloorPath(pointA.getPoint(), pointB.getPoint(), floorMesh1, trianglePath);

	// Points in the same triangle
	if (res == -1) {
		path->add(pointB);

		if (trianglePath != nullptr) {
			delete trianglePath;
			trianglePath = nullptr;
		}

		return path;
	}

	 // returning nullptr, no path found
	if (trianglePath == nullptr) {
		error() << __FUNCTION__ << " - path nullptr";

		delete path;

		return findPathFromCellToDifferentCell(pointA, pointB);
	}

#ifdef DEBUG_PATHIN
	info(true) << "Same Cell Path Found";
#endif // DEBUG_PATHING

	addTriangleNodeEdges(pointA.getPoint(), pointB.getPoint(), trianglePath, path, ourCell);

	delete trianglePath;
	trianglePath = nullptr;

	// Add Destination point
	path->add(pointB);

	return path;
}

float frand() {
	return System::getMTRand()->randExc();
}

bool PathFinderManager::getSpawnPointInArea(const Sphere& area, Zone *zone, Vector3& point, bool checkPath) {
	SortedVector<ManagedReference<NavArea*>> areas;
	float radius = area.getRadius();
	const Vector3& center = area.getCenter();
	Vector3 flipped(center.getX(), center.getZ(), -center.getY());
	const float extents[3] = {3, 5, 3};

	dtNavMeshQuery* query = getNavQuery();

	if (zone == nullptr)
		return false;

	zone->getInRangeNavMeshes(center.getX(), center.getY(), &areas, true);

	if (areas.size() == 0) {
		Vector3 temp((frand() * 2.0f) - 1.0f, (frand() * 2.0f) - 1.0f, 0);
		Vector3 result = temp * (frand() * radius);
		point = center + result;
		point.setZ(CollisionManager::getWorldFloorCollision(point.getX(), point.getY(), zone, false));
		return true;
	}

	for (const auto& navArea : areas) {
		Vector3 polyStart;
		dtPolyRef startPoly;
		dtPolyRef ref;
		int status = 0;
		float pt[3];

		RecastNavMesh *mesh = navArea->getNavMesh();
		if (mesh == nullptr)
			continue;

		ReadLocker rLocker(navArea);

		dtNavMesh *dtNavMesh = mesh->getNavMesh();
		if (dtNavMesh == nullptr)
			continue;

		query->init(dtNavMesh, MAX_QUERY_NODES);

		if (!((status = query->findNearestPoly(flipped.toFloatArray(), extents, &m_spawnFilter, &startPoly, polyStart.toFloatArray())) & DT_SUCCESS))
			continue;

		for (int i=0; i<50; i++) {
			try {
				if (!((status = query->findRandomPointAroundCircle(startPoly, polyStart.toFloatArray(), radius, &m_spawnFilter, frand, &ref, pt)) & DT_SUCCESS)) {
					continue;
				} else {
					point = Vector3(pt[0], -pt[2], CollisionManager::getWorldFloorCollision(pt[0], -pt[2], zone, false));

					Vector3 temp = point - center;
					float len = temp.length();
					if (len > radius) {
						float multiplier = (frand() * radius) / len;
						temp.setX(temp.getX() * multiplier);
						temp.setY(temp.getY() * multiplier);
						point = center + temp;
						radius = len;

						point.setZ(CollisionManager::getWorldFloorCollision(point.getX(), point.getY(), zone, false));
					}
				}

				if (checkPath && !getRecastPath(center, point, navArea, nullptr, radius, false)) {
					continue;
				}

				return true;
			} catch (Exception& exc) {
				error(exc.getMessage());
			}
		}
	}

	return false;
}
