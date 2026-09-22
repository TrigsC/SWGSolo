/*
 * SimBotMissionGraduation.h
 *
 * Pure helper for the F_0.9.8 mission-graduation rule: a planned PlayerBot may
 * take missions once it has trained box N in ONE line of a listed profession
 * (the 3xxx/4xxx shorthand, owner decision 2026-09-22).
 */

#ifndef SIMBOTMISSIONGRADUATION_H_
#define SIMBOTMISSIONGRADUATION_H_

#include "engine/engine.h"

namespace SimBotMissionGraduation {

// Deepest line box trained in any listed profession: "combat_brawler_unarmed_04"
// yields 4. SWG line boxes end in "_0N"; novice and master boxes do not, so they
// never count. A prefix matches only at a "_" boundary, so "combat_brawler"
// never matches a hypothetical "combat_brawlerx_*".
inline int maxLineDepth(const Vector<String>& trainedSkills,
		const Vector<String>& professionPrefixes) {
	int deepest = 0;
	for (int i = 0; i < trainedSkills.size(); ++i) {
		const String& skill = trainedSkills.get(i);
		bool listed = false;
		for (int p = 0; p < professionPrefixes.size() && !listed; ++p)
			listed = skill.beginsWith(professionPrefixes.get(p) + "_");
		if (!listed)
			continue;
		int sep = skill.lastIndexOf("_");
		if (sep < 0 || skill.length() - sep != 3 || skill.charAt(sep + 1) != '0')
			continue;
		char digit = skill.charAt(sep + 2);
		if (digit < '1' || digit > '9')
			continue;
		deepest = Math::max(deepest, digit - '0');
	}
	return deepest;
}

}

#endif /* SIMBOTMISSIONGRADUATION_H_ */
