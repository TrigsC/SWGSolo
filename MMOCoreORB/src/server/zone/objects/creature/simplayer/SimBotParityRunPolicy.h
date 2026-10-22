/*
 * SimBotParityRunPolicy.h
 *
 * Pure policy for the scoped PlayerBot parity runner.  The runner owns all
 * live state and cleanup; this header only decides which result follows, when
 * a wall-clock cap applies, and how a failure is classified.
 */

#ifndef SIMBOTPARITYRUNPOLICY_H_
#define SIMBOTPARITYRUNPOLICY_H_

#include <algorithm>
#include <vector>

#include "engine/engine.h"

namespace SimBotParityRunPolicy {

inline bool inScope(const String& scenarioName,
		const std::vector<String>& onlyScenarios) {
	if (onlyScenarios.empty())
		return true;

	const String wanted = scenarioName.trim();
	for (size_t i = 0; i < onlyScenarios.size(); ++i) {
		if (wanted == onlyScenarios[i].trim())
			return true;
	}
	return false;
}

inline int nextIndexAfterResult(int currentIndex, const String& status,
		bool continueOnFailure) {
	if (currentIndex < 0)
		return currentIndex;

	const String normalized = status.trim().toLowerCase();
	if (normalized == "fail" && !continueOnFailure)
		return currentIndex;
	return currentIndex + 1;
}

inline bool runCapReached(uint64 startedAtMs, uint64 nowMs,
		int maxRunMinutes) {
	if (startedAtMs == 0 || maxRunMinutes <= 0 || nowMs <= startedAtMs)
		return false;

	const uint64 capMs = static_cast<uint64>(maxRunMinutes) * 60ULL * 1000ULL;
	return nowMs - startedAtMs >= capMs;
}

struct FailureInputs {
	bool noEligibleBorrowedBot = false;
	bool standInInvalid = false;
	bool borrowedBotDied = false;
	bool killerCaptured = false;
	bool killerIsScenarioTarget = false;
};

inline String classifyParityFailure(const FailureInputs& inputs) {
	if (inputs.noEligibleBorrowedBot || inputs.standInInvalid)
		return "environment";
	if (inputs.borrowedBotDied &&
			(!inputs.killerCaptured || !inputs.killerIsScenarioTarget))
		return "environment";
	return "scope";
}

} // namespace SimBotParityRunPolicy

#endif /* SIMBOTPARITYRUNPOLICY_H_ */
