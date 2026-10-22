/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef UPDATENEARESTMISSIONFORGROUPTASK_H_
#define UPDATENEARESTMISSIONFORGROUPTASK_H_

#include "engine/engine.h"

namespace server {
namespace zone {
namespace objects {
	namespace mission {
		class MissionObject;
	}

	namespace player {
		class PlayerObject;
	}

namespace group {
	class GroupObject;

namespace tasks {

class UpdateNearestMissionForGroupTask : public Task, public Logger {
	WeakReference<GroupObject*> groupRef;
	unsigned int planetCRC;

public:
	UpdateNearestMissionForGroupTask(GroupObject* group, const unsigned int planetCRC);
	void run();

private:
	float calculateManhattanDistanceToPosition(const Vector3& position,
		const Vector3& missionPosition);

	void setPlayersNearestMissionForGroupWaypoint(
		server::zone::objects::player::PlayerObject* ghost,
		const Vector3& position, uint32 planetCRC);
};

} // namespace tasks
} // namespace group
} // namespace objects
} // namespace zone
} // namespace server

using namespace server::zone::objects::group::tasks;

#endif /* UPDATENEARESTMISSIONFORGROUPTASK_H_ */
