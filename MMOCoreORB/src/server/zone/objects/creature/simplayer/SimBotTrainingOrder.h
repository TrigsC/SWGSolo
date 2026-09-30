/*
 * SimBotTrainingOrder.h
 *
 * Pure helpers for two training-plan rules, kept free of SkillManager and of
 * any lock so they can be asserted directly in core3tests.
 *
 *   lineRank          -- where a box's LINE sits in a plan's declared priority.
 *   pickParallelStep  -- which box to pursue when lines on different XP types
 *                        advance independently.
 *
 * Owner rule 2026-09-30, for elite grinding: a player works the lines in a
 * deliberate order (Teras Kasi: Speed to 4 first, then Balance Conditioning,
 * then Meditation), and a line funded by a DIFFERENT XP type is trained
 * separately rather than waiting its turn. The default plan order is
 * cheapest-box-first, which for Teras Kasi put the one combat_general line
 * first -- the scarce XP type -- and stalled bots holding 370k unused unarmed
 * XP behind a 60k combat_general box they could not yet afford.
 */

#ifndef SIMBOTTRAININGORDER_H_
#define SIMBOTTRAININGORDER_H_

#include <vector>

#include "engine/engine.h"

namespace SimBotTrainingOrder {

// Index of the first priority entry that this skill's line belongs to, matched
// only at a "_" boundary so "combat_unarmed_speed" never claims a hypothetical
// "combat_unarmed_speedy_01". A skill on no listed line ranks AFTER every listed
// line (returns priority.size()); an empty list ranks everything equally, which
// leaves the caller's existing cost-then-name order entirely in charge.
inline int lineRank(const String& skill, const Vector<String>& priority) {
	for (int i = 0; i < priority.size(); ++i) {
		if (skill.beginsWith(priority.get(i) + "_"))
			return i;
	}
	return priority.size();
}

// One untrained box, in plan order.
struct TrainingCandidate {
	String xpType;
	// Untrained with every prerequisite trained.
	bool frontier = false;
	// Its XP and skill points are available now.
	bool affordable = false;
};

// Parallel ACROSS XP streams, strict priority WITHIN each one.
//
// Each XP type is its own stream, and a stream's HEAD is its first frontier box
// in plan order. Only heads are eligible. Among the heads, the first affordable
// one is chosen; if none is affordable, the first head is -- that is the box the
// bot should be earning toward, and it keeps the declared priority in charge of
// what is displayed and which weapon is equipped to fund it.
//
// Restricting to heads is what keeps the owner's order intact. An earlier
// revision took ANY affordable frontier box, so once Speed 1 was trained a
// cheaper Balance 1 on the same unarmed XP could jump ahead of a Speed 2 not yet
// affordable -- breaking "Speed to 4 first". Two boxes on the same XP type
// compete for the same pool, so the later one must wait; a box on a DIFFERENT
// XP type spends a different pool and may proceed. That is exactly the owner's
// rule that Teras Kasi's combat-XP tree is "trained separately".
//
// Returns -1 when no stream has a frontier box.
//
// Selection is advisory only. trainPlayerBotSkill re-validates prerequisites,
// preclusions, the 250-point budget and XP before writing anything, so a wrong
// pick here is refused rather than persisted.
inline int pickParallelStep(const std::vector<TrainingCandidate>& candidates) {
	Vector<String> seenStreams;
	int firstHead = -1;
	for (size_t i = 0; i < candidates.size(); ++i) {
		const TrainingCandidate& candidate = candidates[i];
		// A non-frontier box is not a stream head, and it does NOT claim its
		// stream: a later frontier box on the same XP type can still lead it.
		if (!candidate.frontier)
			continue;
		if (seenStreams.contains(candidate.xpType))
			continue;
		seenStreams.add(candidate.xpType);
		if (candidate.affordable)
			return static_cast<int>(i);
		if (firstHead < 0)
			firstHead = static_cast<int>(i);
	}
	return firstHead;
}
}

#endif /* SIMBOTTRAININGORDER_H_ */
