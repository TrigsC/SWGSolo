#include "UpdateNearestMissionForGroupTask.h"
#include "server/zone/objects/group/GroupObject.h"
#include "server/zone/objects/mission/MissionObject.h"
#include "server/zone/objects/scene/SceneObject.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "server/zone/objects/waypoint/WaypointObject.h"
#include "server/zone/ZoneServer.h"
#include "server/zone/Zone.h"
#include "server/zone/objects/creature/simplayer/SimPlayerManager.h"

// #define DEBUG_GROUPS

UpdateNearestMissionForGroupTask::UpdateNearestMissionForGroupTask(GroupObject* group, const unsigned int planetCRC) : Task() {
	setLoggingName("UpdateNearestMissionForGroupTask");

	this->groupRef = group;
	this->planetCRC = planetCRC;
}

void UpdateNearestMissionForGroupTask::run() {
	Reference<GroupObject*> group = groupRef.get();
	if (group == nullptr) {
		return;
	}

#ifdef DEBUG_GROUPS
	info(true) << "UpdateNearestMissionForGroupTask called for Group ID: " << group->getObjectID();
#endif

	Locker locker(group);

	// Filter the group by planet.
	Vector<Reference<CreatureObject*> > groupMembersOnPlanet;
	for (int i = 0; i < group->getGroupSize(); i++) {
		Reference<CreatureObject*> groupMember = group->getGroupMember(i);

		if (groupMember != nullptr && groupMember->isPlayerCreature()) {
			Zone* zone = groupMember->getZone();

			if (zone != nullptr && zone->getZoneCRC() == planetCRC) {
				groupMembersOnPlanet.add(groupMember);
			}
		}
	}

	if (groupMembersOnPlanet.size() == 0) {
		return;
	}

	// Find the average location for all members on the planet.
	float totalX = 0;
	float totalY = 0;
	float totalZ = 0;

	for (int i = 0; i < groupMembersOnPlanet.size(); i++) {
		if (groupMembersOnPlanet.get(i) != nullptr) {
			CreatureObject* groupMember = groupMembersOnPlanet.get(i);
			Vector3 groupMemberPostion = groupMember->getWorldPosition();
			totalX += groupMemberPostion.getX();
			totalY += groupMemberPostion.getY();
			totalZ += groupMemberPostion.getZ();
		}
	}

	Vector3 averageGroupMemberLocation = Vector3(totalX / groupMembersOnPlanet.size(), totalY / groupMembersOnPlanet.size(), totalZ / groupMembersOnPlanet.size());

	// Find all of the missions for the group members on the planet.
	Vector<Reference<MissionObject*> > missionsOnPlanet;

	for (int i = 0; i < groupMembersOnPlanet.size(); i++) {
		CreatureObject* groupMember = groupMembersOnPlanet.get(i);

		if (groupMember != nullptr) {
			SceneObject* datapad = groupMember->getSlottedObject("datapad");
			if (datapad == nullptr) {
				continue;
			}

			int numberOfMissionsForMember = 0;

			Locker clocker(groupMember, group);

			for (int k = 0; k < datapad->getContainerObjectsSize(); k++) {
				if (datapad->getContainerObject(k) != nullptr && datapad->getContainerObject(k)->isMissionObject()) {
					numberOfMissionsForMember++;
					Reference<MissionObject*> mission = datapad->getContainerObject(k).castTo<MissionObject*>();

					if (mission->getWaypointToMission() != nullptr && mission->getWaypointToMission()->getPlanetCRC() == planetCRC) {
						missionsOnPlanet.add(mission);
					}
				}
			}

			if (numberOfMissionsForMember >= 2) {
				continue;
			}
		}
	}

	Vector<uint64> skippedPlayerMissionOids;
	Vector<PlayerBotGroupMissionWaypointTarget>
		botMissionTargets;
	SimPlayerManager::instance()->getGroupBotMissionTargets(
		group->getObjectID(), planetCRC, botMissionTargets,
		skippedPlayerMissionOids);
	for (int i = missionsOnPlanet.size() - 1; i >= 0; --i) {
		MissionObject* mission = missionsOnPlanet.get(i);
		if (mission != nullptr && skippedPlayerMissionOids.contains(
				mission->getObjectID()))
			missionsOnPlanet.remove(i);
	}

	// Finally find the closest mission.
	Reference<MissionObject*> nearestMission = nullptr;
	Vector3 nearestPosition;
	bool nearestIsBotMission = false;
	uint64 nearestPlanId = 0;
	uint64 nearestEntryId = 0;
	float shortestDistanceSoFar = std::numeric_limits<float>::max();

	for (int i = 0; i < missionsOnPlanet.size(); ++i) {
		MissionObject* mission = missionsOnPlanet.get(i);
		if (mission == nullptr || mission->getWaypointToMission() == nullptr)
			continue;
		Vector3 position(mission->getWaypointToMission()->getWorldPositionX(),
			mission->getWaypointToMission()->getWorldPositionY(),
			mission->getWaypointToMission()->getWorldPositionZ());
		float distance = calculateManhattanDistanceToPosition(
			averageGroupMemberLocation, position);
		if (distance < shortestDistanceSoFar) {
			shortestDistanceSoFar = distance;
			nearestMission = mission;
			nearestPosition = position;
			nearestIsBotMission = false;
		}
	}
	for (int i = 0; i < botMissionTargets.size(); ++i) {
		const PlayerBotGroupMissionWaypointTarget& target =
			botMissionTargets.get(i);
		float distance = calculateManhattanDistanceToPosition(
			averageGroupMemberLocation, target.position);
		if (distance < shortestDistanceSoFar) {
			shortestDistanceSoFar = distance;
			nearestMission = nullptr;
			nearestPosition = target.position;
			nearestIsBotMission = true;
			nearestPlanId = target.planId;
			nearestEntryId = target.entryId;
		}
	}

	PlayerBotGroupMissionWaypointWinner winner;
	winner.none = shortestDistanceSoFar == std::numeric_limits<float>::max();
	winner.planetCRC = planetCRC;
	winner.leaderPlanetCRC = 0;
	if (groupMembersOnPlanet.size() > 0 &&
			groupMembersOnPlanet.get(0)->getZone() != nullptr)
		winner.planet = groupMembersOnPlanet.get(0)->getZone()->getZoneName();
	if (group->getLeader() != nullptr && group->getLeader()->getZone() != nullptr) {
		winner.leaderPlanetCRC = group->getLeader()->getZone()->getZoneCRC();
		winner.planet = group->getLeader()->getZone()->getZoneName();
	}
	winner.position = nearestPosition;
	if (!winner.none) {
		if (nearestIsBotMission) {
			winner.kind = PLAYER_BOT_GROUP_MISSION_BOT_OFFER;
			winner.planId = nearestPlanId;
			winner.entryId = nearestEntryId;
		} else {
			winner.kind = nearestMission != nullptr && nearestMission->getTypeCRC() ==
				MissionTypes::DESTROY ? PLAYER_BOT_GROUP_MISSION_PLAYER_DESTROY :
				PLAYER_BOT_GROUP_MISSION_PLAYER_OTHER;
			winner.missionOid = nearestMission == nullptr ? 0 :
				nearestMission->getObjectID();
			if (nearestMission != nullptr) {
				ManagedReference<CreatureObject*> missionOwner =
					nearestMission->getParentRecursively(
						SceneObjectType::PLAYERCREATURE).castTo<CreatureObject*>();
				winner.ownerOid = missionOwner == nullptr ? 0 :
					missionOwner->getObjectID();
			}
		}
	}
	SimPlayerManager::instance()->publishGroupNearestMission(
		group->getObjectID(), winner);

	// If a mission was found, add the nearest mission waypoint for all members. Otherwise if a mission
	// was not found then remove the nearest mission waypoint for all members.
	for (int i = 0; i < groupMembersOnPlanet.size(); i++) {
		CreatureObject* groupMember = groupMembersOnPlanet.get(i);

		Locker clocker(groupMember, group);

		if (groupMember->getPlayerObject() != nullptr) {
			PlayerObject* ghost = groupMember->getPlayerObject();

			setPlayersNearestMissionForGroupWaypoint(ghost, nearestPosition,
				winner.none ? 0 : planetCRC);
		}
	}
}

float UpdateNearestMissionForGroupTask::calculateManhattanDistanceToPosition(
		const Vector3& position, const Vector3& missionPosition) {
	// The Manhattan distance will give us a "good enough" value for comparing the relative distance
	// to two missions.  It can fail if two missions are very close to each other but that's fine for
	// the purposes of this waypoint.
	return fabs(position.getX() - missionPosition.getX()) +
		fabs(position.getY() - missionPosition.getY());
}

void UpdateNearestMissionForGroupTask::setPlayersNearestMissionForGroupWaypoint(
		PlayerObject* ghost, const Vector3& position, uint32 crc) {
	if (ghost == nullptr) {
		return;
	}

	if (crc == 0) {
		ghost->removeWaypointBySpecialType(WaypointObject::SPECIALTYPE_NEARESTMISSIONFORGROUP, true);
	} else {
		ManagedReference<WaypointObject*> waypoint = ghost->getWaypointBySpecialType(WaypointObject::SPECIALTYPE_NEARESTMISSIONFORGROUP);
		if (waypoint == nullptr) {
			ZoneServer* zoneServer = ghost->getZoneServer();
			waypoint = zoneServer->createObject(0xc456e788, 1).castTo<WaypointObject*>();
		}

		Locker locker(waypoint);

		waypoint->setCustomObjectName("@group:groupwaypoint", false); // Nearest mission for group
		waypoint->setSpecialTypeID(WaypointObject::SPECIALTYPE_NEARESTMISSIONFORGROUP);
		waypoint->setPlanetCRC(crc);
		waypoint->setPosition(position.getX(), position.getZ(), position.getY());
		waypoint->setColor(WaypointObject::COLOR_YELLOW);
		waypoint->setActive(true);

		ghost->addWaypoint(waypoint, false);
	}
}