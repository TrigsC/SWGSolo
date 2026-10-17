/*
 * Pure weapon-loadout policy for roster PlayerBots.
 *
 * This header deliberately contains no object, database, or lock access.  The
 * manager snapshots real weapon rows and feeds those snapshots into these
 * deterministic decisions.
 */
#ifndef SIMBOTLOADOUT_H_
#define SIMBOTLOADOUT_H_

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <vector>

#include "engine/engine.h"

namespace SimBotLoadout {

static const int STARTER = 0;
static const int VAULT = 1;

struct WeaponStats {
	String templateString;
	uint32 templateCrc = 0;
	String xpType;
	int gameObjectType = 0;
	float minDamage = 0.f;
	float maxDamage = 0.f;
	float attackSpeed = 1.f;
	float woundsRatio = 0.f;
	int pointBlankAccuracy = 0;
	int idealAccuracy = 0;
	int maxRangeAccuracy = 0;
	int pointBlankRange = 0;
	int idealRange = 0;
	int maxRange = 0;
	int healthAttackCost = 0;
	int actionAttackCost = 0;
	int mindAttackCost = 0;
	Vector<String> certificationsRequired;
	uint64 objectId = 0;

	// Engine containers require these; the stats are never serialized.
	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct LoadoutChoice {
	int source = STARTER;
	uint64 objectId = 0;
	WeaponStats stats;
	float score = 0.f;
	String desiredXpType;
	bool templateComplete = false;
	// True when a better candidate lost only to the minUpgradePercent margin.
	bool heldByHysteresis = false;
};

enum UpgradeReason {
	BUY,
	NOT_BETTER,
	UNCERTIFIED,
	WRONG_CLASS,
	OVER_BUDGET,
	UNAFFORDABLE
};

inline float scoreWeapon(const WeaponStats& weapon) {
	return ((weapon.minDamage + weapon.maxDamage) * 0.5f) /
		std::max(weapon.attackSpeed, 0.1f);
}

inline bool contains(const Vector<String>& values, const String& value) {
	for (int i = 0; i < values.size(); ++i) {
		if (values.get(i) == value)
			return true;
	}
	return false;
}

inline bool isCertified(const Vector<String>& required,
		const Vector<String>& held) {
	for (int i = 0; i < required.size(); ++i) {
		if (!contains(held, required.get(i)))
			return false;
	}
	return true;
}

inline bool preferred(const WeaponStats& left, const WeaponStats& right) {
	const float leftScore = scoreWeapon(left);
	const float rightScore = scoreWeapon(right);
	if (leftScore != rightScore)
		return leftScore > rightScore;
	if (left.maxRange != right.maxRange)
		return left.maxRange > right.maxRange;
	return left.objectId < right.objectId;
}

inline Vector<String> weaponClassCandidates(int gameObjectType) {
	Vector<String> result;
	switch (gameObjectType) {
	case 0x20001: // MELEEWEAPON
		result.add("combat_meleespecialize_unarmed");
		result.add("combat_meleespecialize_onehand");
		result.add("combat_meleespecialize_twohand");
		result.add("combat_meleespecialize_polearm");
		break;
	case 0x20007: // ONEHANDMELEEWEAPON
		result.add("combat_meleespecialize_onehand");
		break;
	case 0x20008: // TWOHANDMELEEWEAPON
		result.add("combat_meleespecialize_twohand");
		break;
	case 0x20009: // POLEARM
		result.add("combat_meleespecialize_polearm");
		break;
	case 0x2000A: // PISTOL
		result.add("combat_rangedspecialize_pistol");
		break;
	case 0x2000B: // CARBINE
		result.add("combat_rangedspecialize_carbine");
		break;
	case 0x2000C: // RIFLE
		result.add("combat_rangedspecialize_rifle");
		break;
	default:
		break;
	}
	return result;
}

inline bool classMatches(const WeaponStats& weapon, const String& desiredXpType,
		bool templateComplete) {
	return templateComplete || desiredXpType.isEmpty() ||
		weapon.xpType == desiredXpType;
}

inline LoadoutChoice chooseWeapon(const std::vector<WeaponStats>& candidates,
		const String& desiredXpType, bool templateComplete,
		const Vector<String>& certsHeld, const WeaponStats& starter,
		uint64 currentSourceOid, float minUpgradePercent) {
	LoadoutChoice result;
	result.source = STARTER;
	result.stats = starter;
	result.score = scoreWeapon(starter);
	result.desiredXpType = desiredXpType;
	result.templateComplete = templateComplete;

	const WeaponStats* best = nullptr;
	const WeaponStats* current = nullptr;
	for (size_t i = 0; i < candidates.size(); ++i) {
		const WeaponStats& candidate = candidates[i];
		if (!classMatches(candidate, desiredXpType, templateComplete))
			continue;
		if (!isCertified(candidate.certificationsRequired, certsHeld))
			continue;
		if (candidate.objectId == currentSourceOid)
			current = &candidate;
		if (best == nullptr || preferred(candidate, *best))
			best = &candidate;
	}

	if (best == nullptr || !preferred(*best, starter))
		return result;

	// Hysteresis against whatever the body holds now: the still-valid vault
	// weapon, or the starter when there is none.
	if (current == nullptr || current->objectId != best->objectId) {
		const float currentScore = current == nullptr ? result.score :
			scoreWeapon(*current);
		const float threshold = std::max(0.f, currentScore) *
			(1.f + std::max(0.f, minUpgradePercent) / 100.f);
		if (scoreWeapon(*best) < threshold) {
			result.heldByHysteresis = true;
			if (current == nullptr)
				return result;
			result.source = VAULT;
			result.objectId = current->objectId;
			result.stats = *current;
			result.score = scoreWeapon(*current);
			return result;
		}
	}

	result.source = VAULT;
	result.objectId = best->objectId;
	result.stats = *best;
	result.score = scoreWeapon(*best);
	return result;
}

inline std::vector<uint64_t> protectedSet(
		const std::vector<WeaponStats>& candidates, uint64_t equippedOid,
		const Vector<String>& planXpTypes, const Vector<String>& certsHeld,
		const Vector<String>& certsPlanned) {
	Vector<String> usableCerts = certsHeld;
	for (int i = 0; i < certsPlanned.size(); ++i)
		if (!contains(usableCerts, certsPlanned.get(i)))
			usableCerts.add(certsPlanned.get(i));

	std::vector<uint64_t> result;
	if (equippedOid != 0)
		result.push_back(equippedOid);
	for (int xpIndex = 0; xpIndex < planXpTypes.size(); ++xpIndex) {
		const String& xpType = planXpTypes.get(xpIndex);
		const WeaponStats* best = nullptr;
		for (size_t i = 0; i < candidates.size(); ++i) {
			const WeaponStats& candidate = candidates[i];
			if (candidate.xpType != xpType ||
					!isCertified(candidate.certificationsRequired, usableCerts))
				continue;
			if (best == nullptr || preferred(candidate, *best))
				best = &candidate;
		}
		if (best != nullptr && std::find(result.begin(), result.end(),
				best->objectId) == result.end())
			result.push_back(best->objectId);
	}
	return result;
}

inline UpgradeReason upgradeDecision(const WeaponStats& listing, int price,
		float currentScore, int64_t bank, int64_t reserved,
		int64_t reserveCredits, float spendFraction, int maxPrice,
		float minUpgradePercent, const Vector<String>& certsHeld,
		const String& desiredXpType, bool templateComplete) {
	if (!classMatches(listing, desiredXpType, templateComplete))
		return WRONG_CLASS;
	if (!isCertified(listing.certificationsRequired, certsHeld))
		return UNCERTIFIED;
	const float requiredScore = std::max(0.f, currentScore) *
		(1.f + std::max(0.f, minUpgradePercent) / 100.f);
	if (scoreWeapon(listing) < requiredScore)
		return NOT_BETTER;
	const int64_t spendable = bank - reserved - reserveCredits;
	if (spendable <= 0 || price > spendable)
		return UNAFFORDABLE;
	const int64_t fractionCeiling = static_cast<int64_t>(std::floor(
		std::max<int64_t>(0, spendable) *
		std::max(0.f, std::min(1.f, spendFraction))));
	const int64_t ceiling = std::min<int64_t>(std::max(0, maxPrice),
		fractionCeiling);
	return price > ceiling ? OVER_BUDGET : BUY;
}

} // namespace SimBotLoadout

#endif // SIMBOTLOADOUT_H_
