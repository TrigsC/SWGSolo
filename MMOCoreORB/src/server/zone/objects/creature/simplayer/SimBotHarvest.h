/*
 * SimBotHarvest.h
 *
 * Pure policy for the PlayerBot Scout harvesting path.  The manager owns
 * object, progression and configuration state; this header only operates on
 * value types so the family and pricing rules stay independent of the world.
 */

#ifndef SIMBOTHARVEST_H_
#define SIMBOTHARVEST_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace SimBotHarvest {

enum Family {
	HIDE = 0,
	MEAT = 1,
	BONE = 2,
	NONE = 3
};

static const int FAMILY_COUNT = 3;

inline const char* familyName(Family family) {
	switch (family) {
	case HIDE: return "hide";
	case MEAT: return "meat";
	case BONE: return "bone";
	default: return "none";
	}
}

inline std::string lowerHarvestType(const std::string& value) {
	std::string result = value;
	for (size_t i = 0; i < result.size(); ++i) {
		if (result[i] >= 'A' && result[i] <= 'Z')
			result[i] = static_cast<char>(result[i] - 'A' + 'a');
	}
	return result;
}

inline Family familyFromResourceType(const std::string& resourceType) {
	const std::string type = lowerHarvestType(resourceType);
	if (type.find("hide") != std::string::npos)
		return HIDE;
	if (type.find("meat") != std::string::npos)
		return MEAT;
	if (type.find("bone") != std::string::npos)
		return BONE;
	return NONE;
}

inline Family familyFromGameObjectType(int gameObjectType) {
	// SceneObjectType values are deliberately repeated here.  Keeping the
	// policy header engine-free avoids pulling the generated object layer into
	// pure policy tests.
	switch (gameObjectType) {
	case 0x40000A: return HIDE; // ORGANICHIDE
	case 0x400009: return MEAT; // ORGANICFOOD
	case 0x40000B: return BONE; // ORGANICSTRUCTURAL
	default: return NONE;
	}
}

inline Family familyFromResourceType(const char* resourceType) {
	return familyFromResourceType(resourceType == nullptr ? std::string() :
		std::string(resourceType));
}

inline Family chooseFamily(const uint64_t available[3],
		const uint64_t needUnits[3], const Family fallbackOrder[3]) {
	static const Family defaultOrder[3] = {HIDE, MEAT, BONE};
	const Family* order = fallbackOrder == nullptr ? defaultOrder : fallbackOrder;
	Family selected = NONE;
	uint64_t selectedNeed = 0;
	bool hasAvailableFamily = false;
	for (int orderIndex = 0; orderIndex < FAMILY_COUNT; ++orderIndex) {
		const Family family = order[orderIndex];
		const int familyIndex = static_cast<int>(family);
		if (familyIndex < 0 || familyIndex >= FAMILY_COUNT ||
				available[familyIndex] == 0)
			continue;
		hasAvailableFamily = true;
		// Equal need goes to the family this corpse yields most of; the
		// fallback order only breaks a tie on both.
		if (needUnits[familyIndex] > selectedNeed ||
				(needUnits[familyIndex] == selectedNeed && selected != NONE &&
				available[familyIndex] > available[static_cast<int>(selected)])) {
			selected = family;
			selectedNeed = needUnits[familyIndex];
		}
	}
	if (selected != NONE && selectedNeed > 0)
		return selected;

	// With no positive demand, preserve the stock-like deterministic fallback:
	// largest corpse family first, then hide -> meat -> bone for ties.
	uint64_t largest = 0;
	for (int orderIndex = 0; orderIndex < FAMILY_COUNT; ++orderIndex) {
		const Family family = order[orderIndex];
		const int familyIndex = static_cast<int>(family);
		if (familyIndex < 0 || familyIndex >= FAMILY_COUNT)
			continue;
		if (available[familyIndex] > largest) {
			largest = available[familyIndex];
			selected = family;
		}
	}
	if (hasAvailableFamily && selected != NONE && largest > 0)
		return selected;

	for (int i = 0; i < FAMILY_COUNT; ++i) {
		const Family family = order[i];
		const int familyIndex = static_cast<int>(family);
		if (familyIndex >= 0 && familyIndex < FAMILY_COUNT &&
				available[familyIndex] > 0)
			return family;
	}
	return NONE;
}

inline Family chooseFamily(const uint64_t available[3],
		const uint64_t needUnits[3]) {
	static const Family fallbackOrder[3] = {HIDE, MEAT, BONE};
	return chooseFamily(available, needUnits, fallbackOrder);
}

inline uint64_t needUnits(uint64_t target, uint64_t held, uint64_t listed) {
	const uint64_t maxValue = std::numeric_limits<uint64_t>::max();
	const uint64_t supply = held > maxValue - listed ? maxValue :
		held + listed;
	return target > supply ? target - supply : 0;
}

inline float resourceQualityFactor(const int attributes[], int attributeCount,
		float minQualityFactor) {
	if (attributeCount <= 0)
		return std::max(0.f, std::min(1.f, minQualityFactor));

	int total = 0;
	int present = 0;
	for (int i = 0; i < attributeCount; ++i) {
		// Spawn attributes run 1..1000; a 0 is an attribute the spawn lacks.
		if (attributes[i] <= 0)
			continue;
		total += attributes[i];
		++present;
	}
	if (present == 0)
		return std::max(0.f, std::min(1.f, minQualityFactor));

	const float mean = static_cast<float>(total) /
		static_cast<float>(present) / 1000.f;
	return std::max(std::max(0.f, minQualityFactor),
		std::min(1.f, mean));
}

struct HumanFloor {
	uint64_t price = 0;
	uint64_t units = 0;
	bool valid() const { return price > 0 && units > 0; }
};

struct PriceDecision {
	uint64_t price = 0;
	uint64_t base = 0;
	uint64_t floor = 0;
	bool skipFloorAboveMax = false;
};

inline PriceDecision priceResourceListing(uint64_t units,
		uint64_t pricePerUnit, float qualityFactor,
		const HumanFloor& humanFloor, uint64_t minPrice, uint64_t maxPrice) {
	PriceDecision result;
	if (units == 0 || maxPrice == 0)
		return result;

	qualityFactor = std::max(0.f, std::min(1.f, qualityFactor));
	const double base = static_cast<double>(units) *
		static_cast<double>(pricePerUnit) * static_cast<double>(qualityFactor);
	result.base = static_cast<uint64_t>(std::ceil(base));
	if (humanFloor.valid()) {
		const uint64_t maxValue = std::numeric_limits<uint64_t>::max();
		const uint64_t numerator = units > maxValue / humanFloor.price ?
			maxValue : units * humanFloor.price;
		const uint64_t rounded = numerator / humanFloor.units +
			(numerator % humanFloor.units == 0 ? 0 : 1);
		result.floor = rounded;
	}

	const uint64_t ask = std::max(result.base, std::max(result.floor,
		minPrice));
	if (ask > maxPrice) {
		result.skipFloorAboveMax = result.floor > maxPrice;
		return result;
	}
	result.price = ask;
	return result;
}

inline bool shouldSellStack(uint64_t units, uint64_t threshold,
		int maxListingsLeft) {
	return maxListingsLeft > 0 && threshold > 0 && units >= threshold;
}

inline uint64_t capDispatchSignal(uint64_t signalUnits, uint64_t need,
		uint64_t familyInbound) {
	const uint64_t remaining = need > familyInbound ?
		need - familyInbound : 0;
	return std::min(signalUnits, remaining);
}

} // namespace SimBotHarvest

#endif // SIMBOTHARVEST_H_
