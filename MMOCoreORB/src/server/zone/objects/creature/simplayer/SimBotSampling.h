/*
 * SimBotSampling.h
 *
 * Pure sampling policy.  Engine-facing code converts SurveyTool constants,
 * resource classes and object IDs to these value types before calling here.
 */

#ifndef SIMBOTSAMPLING_H_
#define SIMBOTSAMPLING_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

namespace SimBotSampling {

enum SurveyToolClass {
	SOLAR = 1,
	CHEMICAL = 2,
	FLORA = 3,
	GAS = 4,
	GEOTHERMAL = 5,
	MINERAL = 6,
	WATER = 7,
	WIND = 8,
	FUSION = 9,
	INORGANIC = 10
};

inline std::string lower(const std::string& value) {
	std::string result = value;
	for (size_t i = 0; i < result.size(); ++i) {
		if (result[i] >= 'A' && result[i] <= 'Z')
			result[i] = static_cast<char>(result[i] - 'A' + 'a');
	}
	return result;
}

inline const char* toolClassName(int toolClass) {
	switch (toolClass) {
	case MINERAL: return "mineral";
	case CHEMICAL: return "chemical";
	case FLORA: return "flora";
	case GAS: return "gas";
	case WATER: return "water";
	case WIND: return "wind";
	case SOLAR: return "solar";
	case GEOTHERMAL: return "geothermal";
	case FUSION: return "fusion";
	case INORGANIC: return "inorganic";
	default: return "";
	}
}

inline int toolClassFromName(const std::string& value) {
	const std::string name = lower(value);
	for (int toolClass = SOLAR; toolClass <= INORGANIC; ++toolClass) {
		if (name == toolClassName(toolClass))
			return toolClass;
	}
	return 0;
}

// Stock survey tool templates (bin/scripts/object/tangible/survey_tool).
// Geothermal and fusion have no player survey tool, so they have no template.
inline const char* toolTemplate(int toolClass) {
	switch (toolClass) {
	case MINERAL: return "object/tangible/survey_tool/survey_tool_mineral.iff";
	case CHEMICAL: return "object/tangible/survey_tool/survey_tool_liquid.iff";
	case FLORA: return "object/tangible/survey_tool/survey_tool_lumber.iff";
	case GAS: return "object/tangible/survey_tool/survey_tool_gas.iff";
	case WATER: return "object/tangible/survey_tool/survey_tool_moisture.iff";
	case WIND: return "object/tangible/survey_tool/survey_tool_wind.iff";
	case SOLAR: return "object/tangible/survey_tool/survey_tool_solar.iff";
	case INORGANIC: return "object/tangible/survey_tool/survey_tool_inorganic.iff";
	default: return "";
	}
}

// Mirrors ResourceSpawner::sendSurvey: a tool lists spawns of its own type,
// and an inorganic tool also lists every inorganic spawn.
inline bool isSampleable(int spawnToolClass, bool spawnIsInorganic,
		int heldToolClass) {
	if (heldToolClass == 0 || toolTemplate(heldToolClass)[0] == '\0')
		return false;
	return (spawnToolClass != 0 && spawnToolClass == heldToolClass) ||
		(heldToolClass == INORGANIC && spawnIsInorganic);
}

inline std::string pricingClass(const std::string& resourceClass) {
	return lower(resourceClass);
}

inline uint64_t pricePerUnit(const std::map<std::string, uint64_t>& prices,
		const std::string& resourceClass, uint64_t defaultPrice) {
	const std::string key = pricingClass(resourceClass);
	std::map<std::string, uint64_t>::const_iterator it = prices.find(key);
	return it == prices.end() ? defaultPrice : it->second;
}

inline bool isStaleStack(uint64_t units, uint64_t sellThreshold,
		bool inShift, float qualityFactor, float keepQualityFactor) {
	return units < sellThreshold && !inShift && qualityFactor < keepQualityFactor;
}

struct StaleStack {
	uint64_t objectId = 0;
	uint64_t units = 0;
	float qualityFactor = 0.f;
};

inline bool staleStackComesBefore(const StaleStack& left,
		const StaleStack& right) {
	if (left.qualityFactor != right.qualityFactor)
		return left.qualityFactor < right.qualityFactor;
	if (left.units != right.units)
		return left.units < right.units;
	return left.objectId < right.objectId;
}

} // namespace SimBotSampling

#endif // SIMBOTSAMPLING_H_
