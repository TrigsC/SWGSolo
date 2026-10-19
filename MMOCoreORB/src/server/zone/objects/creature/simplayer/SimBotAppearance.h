/*
 * Pure deterministic appearance selection for roster PlayerBots.
 *
 * The sim layer owns the application of these choices. This header only
 * selects stable entries and seeded feature values, so it has no object or
 * lock dependencies.
 */
#ifndef SIMBOTAPPEARANCE_H_
#define SIMBOTAPPEARANCE_H_

#include <stdint.h>
#include <vector>

#include "engine/engine.h"

namespace SimBotAppearance {

static const uint64 BODY_SALT = 0x53494d424f54424fULL;
static const uint64 OUTFIT_SALT = 0x53494d4f55544654ULL;
static const uint64 FEATURE_SALT = 0x53494d4645415452ULL;

inline uint64 splitmix64(uint64 value) {
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

inline uint64 keyHash(const String& key) {
	return static_cast<uint64>(key.hashCode());
}

inline int seededIndex(uint64 identityId, int count, uint64 salt) {
	if (count <= 0)
		return -1;
	return static_cast<int>(splitmix64(identityId ^ salt) %
		static_cast<uint64>(count));
}

inline bool seededBit(uint64 identityId, uint64 salt) {
	return (splitmix64(identityId ^ salt) & 1ULL) != 0;
}

inline int chooseRendezvous(uint64 identityId,
		const std::vector<String>& keys, uint64 salt) {
	int winner = -1;
	uint64 winnerScore = 0;
	for (size_t i = 0; i < keys.size(); ++i) {
		const uint64 score = splitmix64(identityId ^ keyHash(keys[i]) ^ salt);
		if (winner < 0 || score > winnerScore ||
				(score == winnerScore && keys[winner].compareTo(keys[i]) > 0)) {
			winner = static_cast<int>(i);
			winnerScore = score;
		}
	}
	return winner;
}

inline int chooseBody(uint64 identityId,
		const std::vector<String>& bodies) {
	return chooseRendezvous(identityId, bodies, BODY_SALT);
}

inline int chooseOutfitVariant(uint64 identityId,
		const std::vector<String>& variants) {
	return chooseRendezvous(identityId, variants, OUTFIT_SALT);
}

inline int chooseFeatureValue(uint64 identityId, int count, uint64 salt) {
	return seededIndex(identityId, count, FEATURE_SALT ^ salt);
}

inline bool chooseMorphSide(uint64 identityId, uint64 salt) {
	return seededBit(identityId, FEATURE_SALT ^ salt);
}

inline int chooseScalePercent(uint64 identityId, int minPercent,
		int maxPercent) {
	if (maxPercent <= minPercent)
		return minPercent;
	return minPercent + chooseFeatureValue(identityId,
		maxPercent - minPercent + 1, 0x5343414c45ULL);
}

} // namespace SimBotAppearance

#endif // SIMBOTAPPEARANCE_H_
