/*
 * SimBotAbilityTiers.h
 *
 * Pure helpers for resolving the highest trained tier of an ability chain.
 */

#ifndef SIMBOTABILITYTIERS_H_
#define SIMBOTABILITYTIERS_H_

#include "engine/engine.h"

struct SimBotAbilityTier {
	String chainKey;
	int tier = 0;
};

namespace SimBotAbilityTiers {

inline SimBotAbilityTier parseAbilityTier(const String& name) {
	SimBotAbilityTier result;
	String normalized = name.toLowerCase();
	int suffixStart = normalized.length();

	while (suffixStart > 0) {
		char digit = normalized.charAt(suffixStart - 1);
		if (digit < '0' || digit > '9')
			break;
		--suffixStart;
	}

	result.chainKey = normalized.subString(0, suffixStart);
	if (suffixStart == normalized.length())
		return result;

	for (int i = suffixStart; i < normalized.length(); ++i) {
		int digit = normalized.charAt(i) - '0';
		if (result.tier > (2147483647 - digit) / 10) {
			result.tier = 2147483647;
			break;
		}
		result.tier = result.tier * 10 + digit;
	}

	return result;
}

inline void collapseAbilityTiers(const Vector<String>& input,
		Vector<String>& kept, Vector<String>& superseded) {
	kept.removeAll();
	superseded.removeAll();

	// Keep one entry per chain, in first-seen chain order. Only a strictly
	// lower tier is "superseded"; a repeat of the same tier (including a case
	// variant of the same name) is the same ability and is dropped silently.
	VectorMap<String, int> chainIndexes;
	Vector<int> keptTiers;
	Vector<String> lowered;
	for (int i = 0; i < input.size(); ++i) {
		const String& command = input.get(i);
		SimBotAbilityTier parsed = parseAbilityTier(command);

		// A name made only of digits has no chain prefix; treat it as its own
		// chain rather than discarding a command the skill really grants.
		if (parsed.chainKey.isEmpty())
			parsed.chainKey = command.toLowerCase();

		if (!chainIndexes.contains(parsed.chainKey)) {
			chainIndexes.put(parsed.chainKey, kept.size());
			kept.add(command);
			keptTiers.add(parsed.tier);
			continue;
		}

		int index = chainIndexes.get(parsed.chainKey);
		if (parsed.tier > keptTiers.get(index)) {
			lowered.add(kept.get(index));
			kept.set(index, command);
			keptTiers.set(index, parsed.tier);
		} else if (parsed.tier < keptTiers.get(index)) {
			lowered.add(command);
		}
	}

	for (int i = 0; i < lowered.size(); ++i) {
		const String name = lowered.get(i).toLowerCase();
		bool duplicate = false;
		for (int j = 0; j < superseded.size() && !duplicate; ++j)
			duplicate = superseded.get(j).toLowerCase() == name;
		for (int j = 0; j < kept.size() && !duplicate; ++j)
			duplicate = kept.get(j).toLowerCase() == name;
		if (!duplicate)
			superseded.add(lowered.get(i));
	}
}

} // namespace SimBotAbilityTiers

#endif /* SIMBOTABILITYTIERS_H_ */
