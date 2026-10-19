/*
 * Pure armor-loadout policy for roster PlayerBots.
 *
 * This header deliberately contains no object, database, or lock access.  The
 * manager snapshots real armor rows and feeds those snapshots into these
 * deterministic decisions.
 */
#ifndef SIMBOTARMORLOADOUT_H_
#define SIMBOTARMORLOADOUT_H_

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <vector>

#include "engine/engine.h"
#include "templates/faction/Factions.h"

namespace SimBotArmorLoadout {

enum DamageTypeIndex {
	KINETIC = 0,
	ENERGY,
	BLAST,
	STUN,
	HEAT,
	COLD,
	ACID,
	ELECTRICITY,
	LIGHTSABER,
	DAMAGE_TYPE_COUNT
};

enum BuffPool {
	STRENGTH = 1 << 0,
	CONSTITUTION = 1 << 1,
	QUICKNESS = 1 << 2,
	STAMINA = 1 << 3,
	FOCUS = 1 << 4,
	WILLPOWER = 1 << 5
};

enum ChoiceStatus {
	KEPT,
	NEW,
	HELD_BY_HYSTERESIS
};

enum UsableReason {
	OK,
	USABLE = OK,
	RACE,
	FACTION,
	CERT,
	NO_COVERAGE
};

enum ArmorUpgradeReason {
	BUY,
	NOT_BETTER,
	UNUSABLE,
	TOO_ENCUMBERING,
	OVER_BUDGET,
	UNAFFORDABLE
};

struct ArmorStats {
	String templateString;
	uint32 templateCrc = 0;
	uint64 objectId = 0;
	int gameObjectType = 0;

	// slotKey is the sorted, joined arrangement-descriptor slot list.  The
	// names are retained as well because greedy selection must detect overlap.
	String slotKey;
	Vector<String> slotNames;
	unsigned int hitLocationMask = 0;
	int armorRating = 0;
	float resists[DAMAGE_TYPE_COUNT] = {};
	unsigned int vulnerabilityMask = 0;

	int healthEncumbrance = 0;
	int actionEncumbrance = 0;
	int mindEncumbrance = 0;

	Vector<uint32> allowedRaces;
	uint32 faction = Factions::FACTIONNEUTRAL;
	Vector<String> certificationsRequired;
	int condition = 0;
	int maxCondition = 0;

	// Set by callers that already have a policy score, such as the listing
	// cache.  A zero value falls back to the default scoring config.
	float score = 0.f;

	// Engine containers require these; the stats are never serialized.
	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct ArmorScoringConfig {
	float damageTypeWeights[DAMAGE_TYPE_COUNT] = {
		0.5f, 0.2f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.05f, 0.f
	};
	int assumedArmorPiercing = 0;
};

struct ArmorChoicePiece {
	ArmorStats stats;
	float score = 0.f;
	ChoiceStatus status = NEW;
};

struct ArmorChoice {
	std::vector<ArmorChoicePiece> pieces;
};

inline float clampScore(float score) {
	return std::max(0.f, std::min(1.f, score));
}

inline bool contains(const Vector<uint32>& values, uint32 value) {
	for (int i = 0; i < values.size(); ++i) {
		if (values.get(i) == value)
			return true;
	}
	return false;
}

inline bool contains(const Vector<String>& values, const String& value) {
	for (int i = 0; i < values.size(); ++i) {
		if (values.get(i) == value)
			return true;
	}
	return false;
}

inline bool hasSlotOverlap(const ArmorStats& left, const ArmorStats& right) {
	if (left.slotNames.isEmpty() || right.slotNames.isEmpty())
		return !left.slotKey.isEmpty() && left.slotKey == right.slotKey;

	for (int i = 0; i < left.slotNames.size(); ++i) {
		if (contains(right.slotNames, left.slotNames.get(i)))
			return true;
	}

	return false;
}

inline bool hasSlotOverlap(const ArmorStats& stats,
		const Vector<String>& claimedSlots) {
	if (!stats.slotNames.isEmpty()) {
		for (int i = 0; i < stats.slotNames.size(); ++i) {
			if (contains(claimedSlots, stats.slotNames.get(i)))
				return true;
		}
		return false;
	}

	return !stats.slotKey.isEmpty() && contains(claimedSlots, stats.slotKey);
}

inline void addSlots(const ArmorStats& stats, Vector<String>& claimedSlots) {
	if (!stats.slotNames.isEmpty()) {
		for (int i = 0; i < stats.slotNames.size(); ++i) {
			if (!contains(claimedSlots, stats.slotNames.get(i)))
				claimedSlots.add(stats.slotNames.get(i));
		}
	} else if (!stats.slotKey.isEmpty() &&
			!contains(claimedSlots, stats.slotKey)) {
		claimedSlots.add(stats.slotKey);
	}
}

inline bool isUsable(const ArmorStats& stats, uint32 bodyRaceCrc,
		uint32 botFaction, const Vector<String>& trainedSkills,
		UsableReason* reason = nullptr) {
	if (!contains(stats.allowedRaces, bodyRaceCrc)) {
		if (reason != nullptr)
			*reason = RACE;
		return false;
	}

	if ((botFaction == Factions::FACTIONNEUTRAL &&
			stats.faction != Factions::FACTIONNEUTRAL) ||
			(botFaction != Factions::FACTIONNEUTRAL &&
			stats.faction != Factions::FACTIONNEUTRAL &&
			stats.faction != botFaction)) {
		if (reason != nullptr)
			*reason = FACTION;
		return false;
	}

	if (stats.certificationsRequired.size() > 0) {
		bool hasCertification = false;
		for (int i = 0; i < stats.certificationsRequired.size(); ++i) {
			const String& certification = stats.certificationsRequired.get(i);
			if (!certification.isEmpty() &&
					contains(trainedSkills, certification)) {
				hasCertification = true;
				break;
			}
		}

		if (!hasCertification) {
			if (reason != nullptr)
				*reason = CERT;
			return false;
		}
	}

	if (stats.hitLocationMask == 0) {
		if (reason != nullptr)
			*reason = NO_COVERAGE;
		return false;
	}

	if (reason != nullptr)
		*reason = OK;
	return true;
}

inline UsableReason usability(const ArmorStats& stats, uint32 bodyRaceCrc,
		uint32 botFaction, const Vector<String>& trainedSkills) {
	UsableReason reason = OK;
	isUsable(stats, bodyRaceCrc, botFaction, trainedSkills, &reason);
	return reason;
}

inline unsigned int damageTypeMask(int type) {
	switch (type) {
	case KINETIC:
		return 1;
	case ENERGY:
		return 2;
	case BLAST:
		return 4;
	case STUN:
		return 8;
	case HEAT:
		return 32;
	case COLD:
		return 64;
	case ACID:
		return 128;
	case ELECTRICITY:
		return 256;
	case LIGHTSABER:
		return 16;
	default:
		return 0;
	}
}

inline float scoreArmor(const ArmorStats& stats,
		const ArmorScoringConfig& config) {
	if (stats.hitLocationMask == 0)
		return 0.f;

	float weightedScore = 0.f;
	float totalWeight = 0.f;
	for (int type = KINETIC; type < LIGHTSABER; ++type) {
		const float weight = std::max(0.f, config.damageTypeWeights[type]);
		if (weight <= 0.f)
			continue;

		totalWeight += weight;
		float absorbed = 0.f;
		if ((stats.vulnerabilityMask & damageTypeMask(type)) == 0) {
			const float armorFactor = stats.armorRating >
					config.assumedArmorPiercing ?
					std::pow(0.5f, static_cast<float>(stats.armorRating -
						config.assumedArmorPiercing)) :
					std::pow(1.25f, static_cast<float>(
						config.assumedArmorPiercing - stats.armorRating));
			const float resistTerm = 1.f - (stats.resists[type] / 100.f);
			absorbed = 1.f - (armorFactor * resistTerm);
		}

		weightedScore += weight * clampScore(absorbed);
	}

	if (totalWeight <= 0.f)
		return 0.f;

	return clampScore(weightedScore / totalWeight);
}

inline int totalEncumbrance(const ArmorStats& stats) {
	return stats.healthEncumbrance + stats.actionEncumbrance +
		stats.mindEncumbrance;
}

// secondaries: strength, constitution, quickness, stamina, focus, willpower
// (current values). Mirrors PlayerManagerImplementation::checkEncumbrancies.
inline bool passesEncumbrance(const ArmorStats& stats,
		const int (&secondaries)[6]) {
	if (stats.healthEncumbrance <= 0 && stats.actionEncumbrance <= 0 &&
			stats.mindEncumbrance <= 0)
		return true;

	return stats.healthEncumbrance < secondaries[0] &&
		stats.healthEncumbrance < secondaries[1] &&
		stats.actionEncumbrance < secondaries[2] &&
		stats.actionEncumbrance < secondaries[3] &&
		stats.mindEncumbrance < secondaries[4] &&
		stats.mindEncumbrance < secondaries[5];
}

inline unsigned int requiredBuffPools(const ArmorStats& stats) {
	unsigned int pools = 0;
	if (stats.healthEncumbrance > 0)
		pools |= STRENGTH | CONSTITUTION;
	if (stats.actionEncumbrance > 0)
		pools |= QUICKNESS | STAMINA;
	if (stats.mindEncumbrance > 0)
		pools |= FOCUS | WILLPOWER;
	return pools;
}

inline bool containsOid(const std::vector<uint64_t>& oids, uint64 oid) {
	return std::find(oids.begin(), oids.end(), static_cast<uint64_t>(oid)) !=
		oids.end();
}

inline bool preferred(const ArmorStats& left, float leftScore,
		const ArmorStats& right, float rightScore) {
	if (leftScore != rightScore)
		return leftScore > rightScore;
	if (totalEncumbrance(left) != totalEncumbrance(right))
		return totalEncumbrance(left) < totalEncumbrance(right);
	return left.objectId < right.objectId;
}

inline ArmorChoice chooseArmorInternal(
		const std::vector<ArmorStats>& candidates,
		const std::vector<uint64_t>& currentWSourceOids,
		const ArmorScoringConfig& config, float minUpgradePercent,
		uint32 bodyRaceCrc, uint32 botFaction,
		const Vector<String>& trainedSkills, bool filterUsability) {
	struct RankedCandidate {
		const ArmorStats* stats;
		float score;
	};

	std::vector<RankedCandidate> ranked;
	for (size_t i = 0; i < candidates.size(); ++i) {
		const ArmorStats& candidate = candidates[i];
		if (filterUsability && !isUsable(candidate, bodyRaceCrc, botFaction,
				trainedSkills))
			continue;

		float score = scoreArmor(candidate, config);
		if (score <= 0.f)
			continue;
		ranked.push_back({&candidate, score});
	}

	std::sort(ranked.begin(), ranked.end(),
		[](const RankedCandidate& left, const RankedCandidate& right) {
			return preferred(*left.stats, left.score, *right.stats, right.score);
		});

	ArmorChoice result;
	Vector<String> claimedSlots;
	for (size_t i = 0; i < ranked.size(); ++i) {
		const ArmorStats& candidate = *ranked[i].stats;
		if (hasSlotOverlap(candidate, claimedSlots))
			continue;

		const bool isCurrent = containsOid(currentWSourceOids,
			candidate.objectId);
		// The worn piece on this slot key, looked up among the ranked (usable,
		// covering) candidates only: an unusable worn piece is never held.
		const ArmorStats* current = nullptr;
		float currentScore = 0.f;
		for (size_t rankedIndex = 0; rankedIndex < ranked.size(); ++rankedIndex) {
			const ArmorStats* rankedStats = ranked[rankedIndex].stats;
			if (rankedStats->slotKey == candidate.slotKey &&
					containsOid(currentWSourceOids, rankedStats->objectId)) {
				current = rankedStats;
				currentScore = ranked[rankedIndex].score;
				break;
			}
		}

		ArmorChoicePiece choice;
		choice.stats = candidate;
		choice.score = ranked[i].score;
		choice.status = isCurrent ? KEPT : NEW;

		if (!isCurrent && current != nullptr) {
			const float threshold = std::max(0.f, currentScore) *
				(1.f + std::max(0.f, minUpgradePercent) / 100.f);
			if (choice.score < threshold) {
				if (currentScore <= 0.f)
					continue;
				choice.stats = *current;
				choice.score = currentScore;
				choice.status = HELD_BY_HYSTERESIS;
			}
		}

		result.pieces.push_back(choice);
		addSlots(choice.stats, claimedSlots);
	}

	return result;
}

inline ArmorChoice chooseArmor(const std::vector<ArmorStats>& candidates,
		const std::vector<uint64_t>& currentWSourceOids,
		uint32 bodyRaceCrc, uint32 botFaction,
		const Vector<String>& trainedSkills,
		const ArmorScoringConfig& config, float minUpgradePercent) {
	return chooseArmorInternal(candidates, currentWSourceOids, config,
		minUpgradePercent, bodyRaceCrc, botFaction, trainedSkills, true);
}

// Convenience overload for callers that have already applied the usability
// filter to the candidate snapshot.
inline ArmorChoice chooseArmor(const std::vector<ArmorStats>& candidates,
		const std::vector<uint64_t>& currentWSourceOids,
		const ArmorScoringConfig& config, float minUpgradePercent) {
	Vector<String> noSkills;
	return chooseArmorInternal(candidates, currentWSourceOids, config,
		minUpgradePercent, 0, Factions::FACTIONNEUTRAL, noSkills, false);
}

inline std::vector<uint64_t> protectedSet(
		const std::vector<ArmorStats>& candidates,
		const std::vector<uint64_t>& wornOids, uint32 bodyRaceCrc,
		uint32 botFaction, const Vector<String>& trainedSkills,
		const ArmorScoringConfig& config) {
	std::vector<uint64_t> result = wornOids;
	std::vector<String> slotKeys;
	std::vector<const ArmorStats*> best;
	std::vector<float> bestScores;

	for (size_t i = 0; i < candidates.size(); ++i) {
		const ArmorStats& candidate = candidates[i];
		if (!isUsable(candidate, bodyRaceCrc, botFaction, trainedSkills) ||
				candidate.slotKey.isEmpty())
			continue;

		float candidateScore = scoreArmor(candidate, config);
		if (candidateScore <= 0.f)
			continue;

		int slotIndex = -1;
		for (size_t j = 0; j < slotKeys.size(); ++j) {
			if (slotKeys[j] == candidate.slotKey) {
				slotIndex = static_cast<int>(j);
				break;
			}
		}

		if (slotIndex < 0) {
			slotKeys.push_back(candidate.slotKey);
			best.push_back(&candidate);
			bestScores.push_back(candidateScore);
		} else if (preferred(candidate, candidateScore, *best[slotIndex],
				bestScores[slotIndex])) {
			best[slotIndex] = &candidate;
			bestScores[slotIndex] = candidateScore;
		}
	}

	for (size_t i = 0; i < best.size(); ++i) {
		if (!containsOid(result, best[i]->objectId))
			result.push_back(best[i]->objectId);
	}

	return result;
}

inline ArmorUpgradeReason upgradeDecision(const ArmorStats& listing,
		int price, float currentScoreForKey, int64_t bank, int64_t reserved,
		int64_t reserveCredits, float spendFraction, int maxPrice,
		float minUpgradePercent, UsableReason usabilityReason,
		int bodyHamBaseline, const ArmorScoringConfig& config) {
	if (usabilityReason != OK)
		return UNUSABLE;

	// Only a positive encumbrance can exceed what a buffed body can carry.
	if ((listing.healthEncumbrance > 0 &&
			listing.healthEncumbrance >= bodyHamBaseline) ||
			(listing.actionEncumbrance > 0 &&
			listing.actionEncumbrance >= bodyHamBaseline) ||
			(listing.mindEncumbrance > 0 &&
			listing.mindEncumbrance >= bodyHamBaseline))
		return TOO_ENCUMBERING;

	const float listingScore = listing.score > 0.f ? listing.score :
		scoreArmor(listing, config);
	const float requiredScore = std::max(0.f, currentScoreForKey) *
		(1.f + std::max(0.f, minUpgradePercent) / 100.f);
	if (listingScore < requiredScore)
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

inline ArmorUpgradeReason upgradeDecision(const ArmorStats& listing,
		int price, float currentScoreForKey, int64_t bank, int64_t reserved,
		int64_t reserveCredits, float spendFraction, int maxPrice,
		float minUpgradePercent, UsableReason usabilityReason,
		int bodyHamBaseline) {
	return upgradeDecision(listing, price, currentScoreForKey, bank, reserved,
		reserveCredits, spendFraction, maxPrice, minUpgradePercent,
		usabilityReason, bodyHamBaseline, ArmorScoringConfig());
}

} // namespace SimBotArmorLoadout

#endif // SIMBOTARMORLOADOUT_H_
