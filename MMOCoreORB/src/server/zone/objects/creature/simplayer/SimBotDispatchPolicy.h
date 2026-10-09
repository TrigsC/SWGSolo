/*
 * SimBotDispatchPolicy.h
 *
 * Pure helpers for progression-first mission dispatch (F_0.9.8b), kept free of
 * SkillManager, the lair index and every lock so they can be asserted directly
 * in core3tests.
 *
 * Owner rule 2026-10-09: a bot whose training template (its WHOLE plan, across
 * professions) is not complete puts reaching it first. It takes the missions it
 * will actually fight, chosen for XP, and market demand only breaks ties. A bot
 * with a complete template keeps the resource-driven market dispatch.
 *
 * "Will actually fight" is the rule combat already applies in
 * SimHunterController::scanForTarget: a solo bot without an elite box avoids
 * PACK creatures. Dispatch evaluating the SAME rule is what stops it handing a
 * bot a lair that combat then refuses -- 61 percent of abandons before this.
 *
 * "For XP" is measured against the tier of the weapon the bot is GRINDING, not
 * its overall level. A Master Teras Kasi bot starting a rifle has high defenses
 * but rifle damage, so it goes back to low-level lairs until the rifle catches up.
 */

#ifndef SIMBOTDISPATCHPOLICY_H_
#define SIMBOTDISPATCHPOLICY_H_

#include <cstring>

#include "engine/engine.h"

namespace SimBotDispatchPolicy {

// A pack lair is one where ANY weighted mobile is a PACK creature. Combat
// avoids per creature, so a single PACK wave member is enough to abandon.
inline bool isLairFightable(bool hasPackMobile, bool avoidPacks) {
	return !(hasPackMobile && avoidPacks);
}

// One fightable (planet, lair) option for a progression-mode bot.
struct ProgressionCandidate {
	String lairTemplate;
	String planet;
	int minDifficulty = 1;
	// pressure * yield / time for a live demand signal this lair can serve, or
	// 0 when it serves none -- the bot still goes, for the XP.
	float demandScore = 0.f;
	bool samePlanet = false;
};

// True when `a` should be dispatched ahead of `b` for a bot grinding at
// `grindTier`. Order, owner-ranked:
//   1. XP fit. Every lair whose minDifficulty <= grindTier beats every lair
//      above it. Among the at-or-below lairs the HARDEST wins (most XP the bot
//      can safely take); among the above lairs the EASIEST wins. The caller's
//      level window (grindTier + 5) still bounds both sides.
//   2. Demand: the higher market score.
//   3. No relocation: a lair on the bot's current planet.
//   4. Template name, then planet, alphabetically, so the choice is
//      deterministic.
inline bool progressionCandidateBefore(const ProgressionCandidate& a,
		const ProgressionCandidate& b, int grindTier) {
	const bool aAtOrBelow = a.minDifficulty <= grindTier;
	const bool bAtOrBelow = b.minDifficulty <= grindTier;
	if (aAtOrBelow != bAtOrBelow)
		return aAtOrBelow;
	if (a.minDifficulty != b.minDifficulty)
		return aAtOrBelow ? a.minDifficulty > b.minDifficulty :
			a.minDifficulty < b.minDifficulty;
	if (a.demandScore != b.demandScore)
		return a.demandScore > b.demandScore;
	if (a.samePlanet != b.samePlanet)
		return a.samePlanet;
	// strcmp, not String::compareTo: engine3's compareTo is strcmp(other, this),
	// the reverse of what its name suggests.
	int nameOrder = strcmp(a.lairTemplate.toCharArray(),
		b.lairTemplate.toCharArray());
	if (nameOrder != 0)
		return nameOrder < 0;
	return strcmp(a.planet.toCharArray(), b.planet.toCharArray()) < 0;
}

// Whether a template is complete, from the XP type of every UNTRAINED box in
// the plan (an empty entry marks a box whose skill could not be resolved,
// which always blocks). Complete means each remaining box is funded by a
// deferred XP type -- one kills cannot earn, such as medical or crafting.
inline bool isTemplateComplete(const Vector<String>& untrainedXpTypes,
		const Vector<String>& deferredXpTypes) {
	for (int i = 0; i < untrainedXpTypes.size(); ++i) {
		const String& xpType = untrainedXpTypes.get(i);
		if (xpType.isEmpty() || !deferredXpTypes.contains(xpType))
			return false;
	}
	return true;
}

// The XP stream whose weapon sets the grind tier: the weapon in hand, because
// that is what deals the damage; else the stream the next training box funds,
// before the first equip has been recorded.
inline String chooseGrindXpType(const String& heldWeaponXpType,
		const String& nextStepXpType, bool nextStepValid) {
	if (!heldWeaponXpType.isEmpty())
		return heldWeaponXpType;
	return nextStepValid ? nextStepXpType : String();
}

// Grind-fallback window, clamped so a typo cannot pin a bot in the grind loop
// for days or flap it every tick.
inline int normalizeGrindFallbackMinutes(int minutes) {
	if (minutes < 5)
		return 5;
	if (minutes > 240)
		return 240;
	return minutes;
}

}

#endif /* SIMBOTDISPATCHPOLICY_H_ */
