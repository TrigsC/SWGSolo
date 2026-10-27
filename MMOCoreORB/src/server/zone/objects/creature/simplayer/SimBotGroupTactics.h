/*
 * SimBotGroupTactics.h
 *
 * Pure policy for the P.10p G4 group-tactics line.  The manager snapshots
 * world state and owns the orders; these value-only helpers own parsing and
 * transition decisions.
 */

#ifndef SIMBOTGROUPTACTICS_H_
#define SIMBOTGROUPTACTICS_H_

#include <algorithm>
#include <cstdint>
#include <vector>

#include "SimBotGrouping.h"

namespace SimBotGroupTactics {

struct TacticVocabulary {
	std::vector<String> pull;
	std::vector<String> spinPoll;
	std::vector<String> spinOn;
	std::vector<String> spinOff;
	std::vector<String> eat;
	std::vector<String> backOff;
	std::vector<String> lairAttack;

	TacticVocabulary() {
		pull = {"pull"};
		spinPoll = {"who has spin", "who can spin", "has spin", "have spin"};
		spinOn = {"spin the lair", "spin on", "lets spin"};
		spinOff = {"no spin", "spin off", "stop spin", "stop spinning"};
		eat = {"take food", "take foods", "eat"};
		backOff = {"back off", "off the lair", "hold"};
		lairAttack = {"attack the lair", "attack lair", "hit the lair"};
	}

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

enum TacticCommand {
	TACTIC_NONE = 0,
	TACTIC_PULL,
	TACTIC_SPIN_POLL,
	TACTIC_SPIN_ON,
	TACTIC_SPIN_OFF,
	TACTIC_EAT,
	TACTIC_BACK_OFF,
	// "attack the lair": spin mode on and the go signal in one command.
	TACTIC_SPIN_GO
};

struct TacticRequest {
	TacticCommand command = TACTIC_NONE;
	bool all = true;
	std::vector<uint64> targetIdentityIds;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline bool containsPhrase(const std::vector<String>& tokens,
		const std::vector<String>& phrases) {
	for (size_t i = 0; i < phrases.size(); ++i) {
		if (SimBotGrouping::hasTokenSequence(tokens, phrases[i]))
			return true;
	}
	return false;
}

inline bool isMissionGrabLine(const std::vector<String>& tokens) {
	static const std::vector<String> grab = {"grab", "get", "take", "pick up"};
	static const std::vector<String> missions = {"mission", "missions", "mish",
		"lair", "lairs"};
	return SimBotGrouping::containsKeyword(tokens, grab) &&
		SimBotGrouping::containsKeyword(tokens, missions);
}

inline TacticRequest parseTacticCommand(const String& text,
		const TacticVocabulary& vocabulary,
		const std::vector<SimBotGrouping::NamedBot>& namedBots =
			std::vector<SimBotGrouping::NamedBot>()) {
	TacticRequest result;
	const std::vector<String> tokens = SimBotGrouping::tokenize(text);
	if (isMissionGrabLine(tokens))
		return result;

	const std::vector<String> filtered = SimBotGrouping::removeRallyFillers(tokens);
	const auto matches = [&filtered, &tokens](const std::vector<String>& phrases) {
		return containsPhrase(tokens, phrases) || containsPhrase(filtered, phrases);
	};

	if (matches(vocabulary.spinOff))
		result.command = TACTIC_SPIN_OFF;
	else if (matches(vocabulary.lairAttack))
		result.command = TACTIC_SPIN_GO;
	else if (matches(vocabulary.spinOn))
		result.command = TACTIC_SPIN_ON;
	else if (matches(vocabulary.spinPoll))
		result.command = TACTIC_SPIN_POLL;
	else if (matches(vocabulary.backOff))
		result.command = TACTIC_BACK_OFF;
	else if (matches(vocabulary.pull))
		result.command = TACTIC_PULL;
	else if (matches(vocabulary.eat))
		result.command = TACTIC_EAT;
	else
		return result;

	SimBotGrouping::Vocabulary emptyVocabulary;
	const SimBotGrouping::GroupCommand addressed =
		SimBotGrouping::parseGroupCommand(text, namedBots, emptyVocabulary, true);
	result.all = addressed.all;
	result.targetIdentityIds = addressed.targetIdentityIds;
	return result;
}

struct PullCandidate {
	float distanceToAnchor = -1.f;
	bool inCombat = false;
	bool isPack = false;
	bool attackable = true;
	bool attackerIsPlayer = false;
	bool attackerIsSimBot = false;
	int attackerFaction = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct PullTargetChoice {
	int index = -1;
	String reason = "nothing";

	bool found() const { return index >= 0; }

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline PullTargetChoice choosePullTarget(
		const std::vector<PullCandidate>& candidates) {
	PullTargetChoice result;
	bool sawBusy = false;
	bool sawFaction = false;
	for (size_t i = 0; i < candidates.size(); ++i) {
		const PullCandidate& candidate = candidates[i];
		if (candidate.inCombat) {
			sawBusy = true;
			continue;
		}
		if (!candidate.attackable)
			continue;
		const SimBotGrouping::AssistVerdict assist = SimBotGrouping::judgeAssist(
				candidate.attackerIsPlayer, candidate.attackerIsSimBot,
				candidate.attackerFaction);
		if (!assist.assist) {
			if (assist.reason == "faction_fight")
				sawFaction = true;
			continue;
		}
		// Non-pack first, then nearest; an unknown (negative) distance ranks last.
		if (result.index < 0) {
			result.index = static_cast<int>(i);
			continue;
		}
		const PullCandidate& best = candidates[result.index];
		if (best.isPack != candidate.isPack) {
			if (!candidate.isPack)
				result.index = static_cast<int>(i);
			continue;
		}
		if (candidate.distanceToAnchor >= 0.f && (best.distanceToAnchor < 0.f ||
				candidate.distanceToAnchor < best.distanceToAnchor))
			result.index = static_cast<int>(i);
	}
	if (result.index >= 0) {
		result.reason = "selected";
		return result;
	}
	if (sawFaction)
		result.reason = "faction";
	else if (sawBusy)
		result.reason = "all_busy";
	return result;
}

enum PullStage {
	APPROACH = 0,
	TAG,
	RETURN,
	DONE,
	ABORT
};

struct PullState {
	PullStage stage = APPROACH;
	uint64 startedAtMs = 0;
	uint64 taggedAtMs = 0;
	String reason;
	String line;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct PullInputs {
	uint64 nowMs = 0;
	bool withinEngageRange = false;
	bool targetAimedAtBot = false;
	bool targetDamagedByBot = false;
	bool targetDead = false;
	bool targetChangedPlanet = false;
	bool botDead = false;
	bool withinFollowMax = false;
	uint64 tagTimeoutMs = 8000;
	uint64 pullTimeoutMs = 45000;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline PullState pullStep(const PullState& current, const PullInputs& inputs) {
	PullState result = current;
	if (current.stage == DONE || current.stage == ABORT)
		return result;
	if (inputs.targetDead || inputs.targetChangedPlanet || inputs.botDead) {
		result.stage = ABORT;
		result.reason = inputs.botDead ? "bot_dead" :
			(inputs.targetDead ? "target_dead" : "planet_changed");
		result.line = "pullFailed";
		return result;
	}
	if (result.startedAtMs == 0)
		result.startedAtMs = inputs.nowMs;
	switch (current.stage) {
	case APPROACH:
		if (inputs.withinEngageRange) {
			result.stage = TAG;
			result.taggedAtMs = inputs.nowMs;
		}
		else if (inputs.pullTimeoutMs > 0 && inputs.nowMs >=
				result.startedAtMs + inputs.pullTimeoutMs) {
			result.stage = ABORT;
			result.reason = "unreachable";
			result.line = "pullFailed";
		}
		break;
	case TAG:
		if (inputs.targetAimedAtBot || inputs.targetDamagedByBot) {
			result.stage = RETURN;
			result.taggedAtMs = inputs.nowMs;
		}
		else if (inputs.tagTimeoutMs > 0 && inputs.nowMs >=
				((current.taggedAtMs != 0 ? current.taggedAtMs :
					current.startedAtMs) + inputs.tagTimeoutMs)) {
			result.stage = ABORT;
			result.reason = "tag_unconfirmed";
			result.line = "pullFailed";
		}
		break;
	case RETURN:
		if (inputs.withinFollowMax) {
			result.stage = DONE;
			result.reason = "returned";
			result.line = "pullBack";
		}
		// The walk back has its own budget, from the confirmed tag: a long
		// approach must not eat the return time.
		else if (inputs.pullTimeoutMs > 0 && inputs.nowMs >=
				((current.taggedAtMs != 0 ? current.taggedAtMs :
					current.startedAtMs) + inputs.pullTimeoutMs)) {
			result.stage = ABORT;
			result.reason = "timeout";
			result.line = "pullFailed";
		}
		break;
	default:
		break;
	}
	return result;
}

struct SpinPollAnswer {
	String lineKey = "spinNo";
	String ability;
	int hamPercent = 0;
	int ham = 0;
	int wornArmorPieces = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline SpinPollAnswer spinPollAnswer(bool hasAreaAbility, bool buffsComplete,
		float hamFraction, int wornArmorPieces) {
	SpinPollAnswer result;
	result.hamPercent = std::max(0, std::min(100,
		static_cast<int>(hamFraction * 100.f + 0.5f)));
	result.ham = result.hamPercent;
	result.wornArmorPieces = wornArmorPieces;
	if (!hasAreaAbility)
		result.lineKey = "spinNo";
	else if (buffsComplete)
		result.lineKey = "spinYes";
	else
		result.lineKey = "spinYesNoBuffs";
	return result;
}

inline SpinPollAnswer spinPollAnswer(bool hasAreaAbility,
		const String& ability, bool buffsComplete, float hamFraction,
		int wornArmorPieces) {
	SpinPollAnswer result = spinPollAnswer(hasAreaAbility, buffsComplete,
			hamFraction, wornArmorPieces);
	result.ability = ability;
	return result;
}

// Owner rule (client pass 2026-10-23): bots never charge a quiet lair just
// because the leader is near it. They go in when the lair is aggro (it took
// damage, or one of its creatures is fighting) or the leader said "attack the
// lair", and only while the leader is within startMeters of it.
inline bool spinShouldEngage(bool spinMode, bool hasAreaAbility,
		uint64 suppressedLairOid, uint64 lairOid, float leaderDistanceToLair,
		bool lairAggro, bool goOrdered, float startMeters) {
	return spinMode && hasAreaAbility && lairOid != 0 &&
		suppressedLairOid != lairOid && (lairAggro || goOrdered) &&
		leaderDistanceToLair >= 0.f &&
		leaderDistanceToLair <= std::max(0.f, startMeters);
}

enum TacticOrder {
	ORDER_NONE = 0,
	ORDER_PULL,
	ORDER_SPIN_POLL,
	ORDER_EAT
};

} // namespace SimBotGroupTactics

#endif // SIMBOTGROUPTACTICS_H_
