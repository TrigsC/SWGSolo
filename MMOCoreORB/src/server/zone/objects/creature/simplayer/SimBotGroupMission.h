/*
 * SimBotGroupMission.h
 *
 * Pure policy for the P.10p G3 group-mission line.  Mission request parsing
 * and the G2 LeaderLine value types live in SimBotGroupRally.h so the include
 * direction remains one-way; all selection, assignment, reward and chat
 * geometry decisions are kept here and are independent of world objects.
 */

#ifndef SIMBOTGROUPMISSION_H_
#define SIMBOTGROUPMISSION_H_

#include <algorithm>
#include <cmath>
#include <vector>

#include "SimBotGroupRally.h"

namespace SimBotGroupMission {

inline String missionSingular(const String& value) {
	String result = value.trim().toLowerCase();
	if (result.length() > 1 && result.charAt(result.length() - 1) == 's')
		result = result.subString(0, result.length() - 1);
	return result;
}

inline bool lairMatchesCreature(const String& lairTemplate,
		const std::vector<String>& mobileNames, const String& word) {
	const String wanted = missionSingular(word);
	if (wanted.isEmpty())
		return false;
	const std::vector<String> templateTokens =
		SimBotGrouping::tokenize(lairTemplate);
	for (size_t i = 0; i < templateTokens.size(); ++i) {
		if (missionSingular(templateTokens[i]) == wanted)
			return true;
	}
	for (size_t i = 0; i < mobileNames.size(); ++i) {
		const std::vector<String> nameTokens =
			SimBotGrouping::tokenize(mobileNames[i]);
		for (size_t j = 0; j < nameTokens.size(); ++j) {
			if (missionSingular(nameTokens[j]) == wanted)
				return true;
		}
	}
	return false;
}

inline bool bearingWithin(int offerBearing, int wantedBearing,
		int toleranceDeg = 45) {
	int difference = std::abs((offerBearing - wantedBearing) % 360);
	if (difference > 180)
		difference = 360 - difference;
	return difference <= std::max(0, toleranceDeg);
}

struct GrabCandidate {
	uint64 identityId = 0;
	String firstName;
	float distanceToTerminal = 0.f;
	int remainingCapacity = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct GrabAssignmentEntry {
	uint64 identityId = 0;
	int count = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct GrabAssignment {
	std::vector<GrabAssignmentEntry> assignments;
	int unassigned = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline GrabAssignment assignGrab(int count,
		const std::vector<GrabCandidate>& candidates,
		const String& addressedBot = String()) {
	GrabAssignment result;
	int remaining = std::max(0, count);
	std::vector<size_t> order;
	order.reserve(candidates.size());

	const String addressed = addressedBot.trim().toLowerCase();
	for (size_t i = 0; i < candidates.size(); ++i) {
		if (!addressed.isEmpty() && candidates[i].firstName.trim().toLowerCase() ==
				addressed)
			order.push_back(i);
	}
	std::vector<size_t> others;
	for (size_t i = 0; i < candidates.size(); ++i) {
		bool alreadyAdded = false;
		for (size_t j = 0; j < order.size(); ++j)
			alreadyAdded |= order[j] == i;
		if (!alreadyAdded)
			others.push_back(i);
	}
	std::sort(others.begin(), others.end(), [&](size_t left, size_t right) {
		if (candidates[left].distanceToTerminal !=
				candidates[right].distanceToTerminal)
			return candidates[left].distanceToTerminal <
				candidates[right].distanceToTerminal;
		return candidates[left].identityId < candidates[right].identityId;
	});
	order.insert(order.end(), others.begin(), others.end());

	for (size_t i = 0; i < order.size() && remaining > 0; ++i) {
		const GrabCandidate& candidate = candidates[order[i]];
		const int assigned = std::min(remaining,
			std::max(0, candidate.remainingCapacity));
		if (assigned <= 0)
			continue;
		result.assignments.push_back({candidate.identityId, assigned});
		remaining -= assigned;
	}
	result.unassigned = remaining;
	return result;
}

struct MissionEntryView {
	uint64 entryId = 0;
	uint32 planet = 0;
	Vector3 pos;
	bool completed = false;
	bool abandoned = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline int pickNextMission(const std::vector<MissionEntryView>& entries,
		uint32 planetCrc, const Vector3& playerCentroid) {
	int selected = -1;
	float selectedDistance = 0.f;
	for (size_t i = 0; i < entries.size(); ++i) {
		const MissionEntryView& entry = entries[i];
		if (entry.completed || entry.abandoned || entry.planet != planetCrc)
			continue;
		const float distance = std::fabs(entry.pos.getX() -
			playerCentroid.getX()) + std::fabs(entry.pos.getY() -
			playerCentroid.getY());
		if (selected < 0 || distance < selectedDistance) {
			selected = static_cast<int>(i);
			selectedDistance = distance;
		}
	}
	return selected;
}

struct SplitMember {
	bool isPlayer = false;
	uint64 id = 0;
	bool inRange = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct RewardSplit {
	int divisor = 0;
	int share = 0;
	std::vector<SplitMember> paid;
	std::vector<SplitMember> outOfRange;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline RewardSplit splitReward(int totalCredits, int missionDivisor,
		const std::vector<SplitMember>& members) {
	RewardSplit result;
	result.divisor = std::max(std::max(0, missionDivisor),
		static_cast<int>(members.size()));
	result.share = result.divisor > 0 ? totalCredits / result.divisor : 0;
	for (size_t i = 0; i < members.size(); ++i) {
		if (members[i].inRange)
			result.paid.push_back(members[i]);
		else
			result.outOfRange.push_back(members[i]);
	}
	return result;
}

inline String compassWord(const Vector3& from, const Vector3& to) {
	const float dx = to.getX() - from.getX();
	const float dy = to.getY() - from.getY();
	if (std::fabs(dx) < 0.001f && std::fabs(dy) < 0.001f)
		return "N";
	const float bearing = std::atan2(dx, dy) * 180.f /
		static_cast<float>(3.14159265358979323846);
	float normalized = bearing < 0.f ? bearing + 360.f : bearing;
	const int sector = static_cast<int>(std::floor((normalized + 22.5f) / 45.f)) % 8;
	static const char* words[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
	return words[sector];
}

inline int roundedMeters(float distance) {
	return std::max(0, static_cast<int>(std::floor(distance + 0.5f)));
}

inline int roundedMeters(const Vector3& from, const Vector3& to) {
	const float dx = to.getX() - from.getX();
	const float dy = to.getY() - from.getY();
	return roundedMeters(std::sqrt(dx * dx + dy * dy));
}

} // namespace SimBotGroupMission

#endif /* SIMBOTGROUPMISSION_H_ */
