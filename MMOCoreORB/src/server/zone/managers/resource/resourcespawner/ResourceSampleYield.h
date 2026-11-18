/*
 * ResourceSampleYield.h
 *
 * Value-only stock surveying arithmetic shared by the player survey path and
 * roster PlayerBot sampling.  The caller owns the random draws so a failed
 * stock branch consumes exactly the same sequence of random values.
 */

#ifndef RESOURCESAMPLEYIELD_H_
#define RESOURCESAMPLEYIELD_H_

namespace ResourceSampleYield {

enum Outcome {
	OK = 0,
	DENSITY_TOO_LOW,
	RATE_FAILED,
	TOO_FEW_UNITS
};

struct Input {
	float density = 0.f;
	int surveySkill = 0;
	int sampleRateMod = 0;
	int sampleSizeMod = 0;
	int samplingMultiplier = 1;
	bool gamble = false;
	int gambleRoll = 0;
	bool richLocationPresent = false;
	bool richRateBypass = false;
	bool richInRange = false;
	int roll100 = 0;
	int roll3 = 0;
};

struct Result {
	Outcome outcome = DENSITY_TOO_LOW;
	int units = 0;
	int maxUnits = 0;
	int xpcap = 40;
	int xp = 0;

	bool succeeded() const {
		return outcome == OK;
	}
};

inline bool densityPasses(const Input& input) {
	return input.density >= .10f &&
		(input.density * 100.f) >=
			(32 - ((input.surveySkill / 20) * 6));
}

inline float sampleRate(const Input& input) {
	return (input.surveySkill * input.density) + input.roll100 +
		input.sampleRateMod;
}

inline bool sampleRatePasses(const Input& input) {
	return input.gamble || input.richRateBypass || input.richLocationPresent ||
		sampleRate(input) >= 40.f;
}

inline Result calculate(const Input& input) {
	Result result;
	if (!densityPasses(input)) {
		result.outcome = DENSITY_TOO_LOW;
		return result;
	}

	if (!sampleRatePasses(input)) {
		result.outcome = RATE_FAILED;
		return result;
	}

	result.maxUnits = static_cast<int>(input.density * (25 + input.roll3));
	const float cityMultiplier = 1.f + input.sampleSizeMod / 100.f;
	result.units = static_cast<int>(result.maxUnits *
		(static_cast<float>(input.surveySkill) / 100.0f) *
		input.samplingMultiplier * cityMultiplier);

	if (input.gamble) {
		if (input.gambleRoll == 1)
			result.units *= 5;
		result.xpcap = 50;
	}

	if (input.richLocationPresent) {
		if (input.richInRange)
			result.units *= 5;
		result.xpcap = 50;
	}

	if (result.units < 2) {
		result.outcome = TOO_FEW_UNITS;
		return result;
	}

	result.outcome = OK;
	result.xp = static_cast<int>((static_cast<float>(result.units) /
		static_cast<float>(result.maxUnits)) * result.xpcap);
	return result;
}

} // namespace ResourceSampleYield

#endif // RESOURCESAMPLEYIELD_H_
