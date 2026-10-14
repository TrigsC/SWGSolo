/*
 * SimBotLootStep.h
 * Pure decisions used by the PlayerBot loot sub-step.
 */

#ifndef SIMBOTLOOTSTEP_H_
#define SIMBOTLOOTSTEP_H_

#include <cstdint>
#include <vector>

namespace SimBotLootStep {

struct ClaimCandidate {
	std::uint64_t corpseOid = 0;
	float x = 0.f;
	float y = 0.f;
	std::uint64_t expiresAtMs = 0;
};

enum ExitReason {
	EXIT_NONE = 0,
	EXIT_NEW_ATTACKER,
	EXIT_CLAIM_EXPIRED,
	EXIT_CORPSE_GONE,
	EXIT_TIMEOUT,
	EXIT_BOT_DEAD
};

inline bool isExpired(std::uint64_t nowMs, std::uint64_t expiresAtMs) {
	return expiresAtMs != 0 && nowMs >= expiresAtMs;
}

inline int selectNearest(const std::vector<ClaimCandidate>& claims,
		float x, float y, float maxDetourMeters, std::uint64_t nowMs) {
	if (maxDetourMeters < 0.f)
		return -1;

	const float maxDistanceSquared = maxDetourMeters * maxDetourMeters;
	int selected = -1;
	float selectedDistanceSquared = 0.f;
	for (int i = 0; i < static_cast<int>(claims.size()); ++i) {
		const ClaimCandidate& candidate = claims[i];
		if (candidate.corpseOid == 0 ||
				isExpired(nowMs, candidate.expiresAtMs))
			continue;

		const float dx = candidate.x - x;
		const float dy = candidate.y - y;
		const float distanceSquared = dx * dx + dy * dy;
		if (distanceSquared > maxDistanceSquared)
			continue;

		if (selected < 0 || distanceSquared < selectedDistanceSquared) {
			selected = i;
			selectedDistanceSquared = distanceSquared;
		}
	}

	return selected;
}

inline ExitReason decideExit(bool botAlive, bool newAttacker,
		bool corpsePresent, std::uint64_t nowMs,
		std::uint64_t claimExpiresAtMs, std::uint64_t stepDeadlineMs) {
	if (!botAlive)
		return EXIT_BOT_DEAD;
	if (newAttacker)
		return EXIT_NEW_ATTACKER;
	if (!corpsePresent)
		return EXIT_CORPSE_GONE;
	if (isExpired(nowMs, claimExpiresAtMs))
		return EXIT_CLAIM_EXPIRED;
	if (stepDeadlineMs != 0 && nowMs >= stepDeadlineMs)
		return EXIT_TIMEOUT;
	return EXIT_NONE;
}

}

#endif /* SIMBOTLOOTSTEP_H_ */
