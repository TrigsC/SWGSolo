/*
 * CreatureHarvestYield.h
 *
 * The stock creature harvest quantity is shared with the roster PlayerBot
 * path.  Keeping this value-only seam out of the generated CreatureManager
 * avoids an IDL fan-out and gives both callers one source of truth.
 */

#ifndef CREATUREHARVESTYIELD_H_
#define CREATUREHARVESTYIELD_H_

#include <algorithm>
#include <cmath>

namespace CreatureHarvestYield {

struct Result {
	int quantity = 0;
	const char* densityBand = "creature_quality_skinny";
};

inline Result calculate(float corpseMax, int harvestingMod, float density,
		float groupModifier, bool corpseHasParent) {
	Result result;
	int quantity = static_cast<int>(corpseMax *
		static_cast<float>(harvestingMod / 100.0f));
	quantity = std::max(quantity, 3);

	if (density > 0.75f) {
		quantity = static_cast<int>(quantity * 1.25f);
		result.densityBand = "creature_quality_fat";
	} else if (density > 0.50f) {
		quantity = static_cast<int>(quantity * 1.00f);
		result.densityBand = "creature_quality_medium";
	} else if (density > 0.25f) {
		quantity = static_cast<int>(quantity * 0.75f);
		result.densityBand = "creature_quality_scrawny";
	} else {
		quantity = static_cast<int>(quantity * 0.50f);
		result.densityBand = "creature_quality_skinny";
	}

	quantity = static_cast<int>(quantity * groupModifier);
	if (corpseHasParent)
		quantity = 1;

	result.quantity = quantity;
	return result;
}

inline int scoutExperience(int level, float speciesModifier,
		float buffMultiplier, float localMultiplier, float globalMultiplier) {
	const int base = level * 5 + 19;
	return static_cast<int>(static_cast<float>(base) * speciesModifier *
		buffMultiplier * localMultiplier * globalMultiplier);
}

inline int scoutExperience(int level, float globalMultiplier) {
	return scoutExperience(level, 1.f, 1.f, 1.f, globalMultiplier);
}

} // namespace CreatureHarvestYield

#endif // CREATUREHARVESTYIELD_H_
