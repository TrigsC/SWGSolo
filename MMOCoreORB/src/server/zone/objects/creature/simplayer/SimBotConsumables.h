/*
 * Pure food-and-drink policy for roster PlayerBots.
 *
 * This header deliberately does not access game objects, managers, or locks.
 * SimPlayerManager snapshots those values and feeds them into these
 * deterministic decisions.
 */
#ifndef SIMBOTCONSUMABLES_H_
#define SIMBOTCONSUMABLES_H_

#include <algorithm>
#include <stdint.h>
#include <limits>
#include <vector>

#include "engine/engine.h"

namespace SimBotConsumables {

static const int kStomachMax = 100;
static const uint64 kDigestIntervalMs = 18000;

struct StomachSide {
	int fill = 0;
	uint64 updatedAtMs = 0;
};

struct Stomach {
	StomachSide food;
	StomachSide drink;
};

struct DigestResult {
	int fill = 0;
	uint64 updatedAtMs = 0;
};

inline DigestResult digest(int fill, uint64 updatedAtMs, uint64 nowMs) {
	DigestResult result;
	result.fill = std::max(0, std::min(kStomachMax, fill));
	result.updatedAtMs = updatedAtMs;

	if (result.fill == 0 || nowMs <= result.updatedAtMs) {
		if (result.fill == 0)
			result.updatedAtMs = nowMs;
		return result;
	}

	const uint64 intervals = (nowMs - result.updatedAtMs) / kDigestIntervalMs;
	if (intervals == 0)
		return result;

	if (intervals >= static_cast<uint64>(result.fill)) {
		result.fill = 0;
		result.updatedAtMs = nowMs;
		return result;
	}

	result.fill -= static_cast<int>(intervals);
	result.updatedAtMs += intervals * kDigestIntervalMs;
	return result;
}

inline DigestResult digest(const StomachSide& side, uint64 nowMs) {
	return digest(side.fill, side.updatedAtMs, nowMs);
}

inline uint64 msUntilRoom(int fill, uint64 updatedAtMs, uint64 nowMs,
		int needed) {
	if (needed <= 0 || fill + needed <= kStomachMax)
		return 0;

	DigestResult materialized = digest(fill, updatedAtMs, nowMs);
	if (materialized.fill + needed <= kStomachMax)
		return 0;

	const int overflow = materialized.fill + needed - kStomachMax;
	const uint64 nextDigest = materialized.updatedAtMs + kDigestIntervalMs;
	return static_cast<uint64>(std::max(0, overflow - 1)) *
			kDigestIntervalMs + (nextDigest > nowMs ? nextDigest - nowMs : 0);
}

struct ConsumableEntry {
	uint32 templateCrc = 0;
	uint32 buffCrc = 0;
	bool isDrink = false;
	// The template's game object type: bazaar listings are indexed by it.
	int gameObjectType = 0;
	int minDoses = 10;
	float maxPricePerDose = 0.f;
	String templatePath;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct HeldStack {
	uint64 objectId = 0;
	uint32 templateCrc = 0;
	int doses = 0;
	int filling = 0;
};

struct Policy {
	int minRecheckSeconds = 30;
	int noDoseRetrySeconds = 300;
};

struct Choice {
	int entryIndex = -1;
	ConsumableEntry entry;
	HeldStack stack;
};

struct Blocked {
	int entryIndex = -1;
	String reason;
	int filling = 0;
	bool isDrink = false;
	uint64 buffRemainingMs = 0;
};

struct Decision {
	std::vector<Choice> choices;
	std::vector<Blocked> blocked;
	Stomach stomach;
};

inline int findStack(const ConsumableEntry& entry,
		const std::vector<HeldStack>& stacks) {
	int selected = -1;
	for (size_t i = 0; i < stacks.size(); ++i) {
		if (stacks[i].templateCrc != entry.templateCrc || stacks[i].doses <= 0)
			continue;

		if (selected < 0 || stacks[i].doses < stacks[selected].doses ||
				(stacks[i].doses == stacks[selected].doses &&
				stacks[i].objectId < stacks[selected].objectId))
			selected = static_cast<int>(i);
	}
	return selected;
}

inline Decision chooseConsumptions(const std::vector<ConsumableEntry>& entries,
		const std::vector<HeldStack>& stacks,
		const std::vector<bool>& buffActive,
		const std::vector<uint64_t>& buffRemainingMs,
		const Stomach& stomach, uint64 nowMs, const Policy& policy) {
	(void) policy;
	Decision result;
	DigestResult food = digest(stomach.food, nowMs);
	DigestResult drink = digest(stomach.drink, nowMs);
	result.stomach.food = {food.fill, food.updatedAtMs};
	result.stomach.drink = {drink.fill, drink.updatedAtMs};

	for (size_t i = 0; i < entries.size(); ++i) {
		const ConsumableEntry& entry = entries[i];
		if (i < buffActive.size() && buffActive[i]) {
			Blocked blocked;
			blocked.entryIndex = static_cast<int>(i);
			blocked.reason = "buff_active";
			blocked.isDrink = entry.isDrink;
			if (i < buffRemainingMs.size())
				blocked.buffRemainingMs = buffRemainingMs[i];
			result.blocked.push_back(blocked);
			continue;
		}

		const int stackIndex = findStack(entry, stacks);
		if (stackIndex < 0) {
			Blocked blocked;
			blocked.entryIndex = static_cast<int>(i);
			blocked.reason = "no_doses";
			blocked.isDrink = entry.isDrink;
			result.blocked.push_back(blocked);
			continue;
		}

		const HeldStack& stack = stacks[stackIndex];
		const int currentFill = entry.isDrink ? result.stomach.drink.fill :
				result.stomach.food.fill;
		if (currentFill + stack.filling > kStomachMax) {
			Blocked blocked;
			blocked.entryIndex = static_cast<int>(i);
			blocked.reason = "stomach_full";
			blocked.filling = stack.filling;
			blocked.isDrink = entry.isDrink;
			result.blocked.push_back(blocked);
			continue;
		}

		Choice choice;
		choice.entryIndex = static_cast<int>(i);
		choice.entry = entry;
		choice.stack = stack;
		result.choices.push_back(choice);
		if (entry.isDrink)
			result.stomach.drink.fill += stack.filling;
		else
			result.stomach.food.fill += stack.filling;
	}

	return result;
}

inline uint64 nextCheckAtMs(const std::vector<Blocked>& blocked,
		const std::vector<uint64_t>& buffRemainingMsAfterApply,
		const Stomach& stomachAfter, int failedApplications, uint64 nowMs,
		const Policy& policy) {
	const uint64 minRecheckMs = static_cast<uint64>(std::max(1,
			policy.minRecheckSeconds)) * 1000;
	uint64 soonest = std::numeric_limits<uint64>::max();

	for (size_t i = 0; i < buffRemainingMsAfterApply.size(); ++i) {
		if (buffRemainingMsAfterApply[i] > 0)
			soonest = std::min(soonest, nowMs + buffRemainingMsAfterApply[i]);
	}

	for (const Blocked& item : blocked) {
		if (item.reason == "buff_active" && item.buffRemainingMs > 0) {
			soonest = std::min(soonest, nowMs + item.buffRemainingMs);
		} else if (item.reason == "stomach_full") {
			const StomachSide& side = item.isDrink ? stomachAfter.drink :
					stomachAfter.food;
			const uint64 wait = msUntilRoom(side.fill, side.updatedAtMs, nowMs,
					item.filling);
			soonest = std::min(soonest, nowMs + wait);
		} else if (item.reason == "no_doses") {
			soonest = std::min(soonest, nowMs + static_cast<uint64>(std::max(1,
					policy.noDoseRetrySeconds)) * 1000);
		}
	}

	if (failedApplications > 0)
		soonest = std::min(soonest, nowMs + minRecheckMs);
	const uint64 floor = nowMs + minRecheckMs;
	if (soonest == std::numeric_limits<uint64>::max())
		return floor;
	return std::max(soonest, floor);
}

inline bool restockNeed(int heldDoses, int minDoses,
		bool pendingForTemplate) {
	return !pendingForTemplate && heldDoses < minDoses;
}

inline float pricePerDose(int price, int doses) {
	if (doses <= 0)
		return std::numeric_limits<float>::infinity();
	return static_cast<float>(price) / static_cast<float>(doses);
}

inline bool acceptsListing(int price, int doses, float maxPricePerDose,
		int64 ceiling) {
	return price > 0 && doses > 0 && price <= ceiling &&
			pricePerDose(price, doses) <= maxPricePerDose;
}

/*
 * The stock AI accuracy with the food buff taken out, plus the buff.
 * AiAgent::getSkillMod returns the creature's skill-mod total when it is
 * nonzero and only otherwise its fallback (template statistic, then
 * 100 + 2 x level for accuracy), so a buff hides the fallback: when the total
 * minus the buff is zero, the stock value is the fallback again.
 */
inline int foodAdjustedAiAccuracy(int totalAccuracyMod, int buffAccuracy,
		int fallbackAccuracy, float chanceHit, int weaponTypeAccuracy) {
	int nonFoodAccuracy = totalAccuracyMod - buffAccuracy;
	if (buffAccuracy != 0 && nonFoodAccuracy == 0)
		nonFoodAccuracy = fallbackAccuracy;
	int result = nonFoodAccuracy > 0 ? nonFoodAccuracy + weaponTypeAccuracy :
			static_cast<int>(chanceHit * 100.f);
	return result + buffAccuracy;
}

} // namespace SimBotConsumables

#endif // SIMBOTCONSUMABLES_H_
