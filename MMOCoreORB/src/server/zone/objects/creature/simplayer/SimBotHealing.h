/*
 * SimBotHealing.h
 *
 * Pure policy for the P.10p G5 healing line.  The manager and controller
 * snapshot live objects and feed these value-only decisions; this header does
 * not own engine objects, locks or configuration state.
 */

#ifndef SIMBOTHEALING_H_
#define SIMBOTHEALING_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

#include "SimBotGrouping.h"

namespace SimBotHealing {

struct HealVocabulary {
	std::vector<String> heal;
	std::vector<String> self;
	std::vector<String> everyone;

	HealVocabulary() {
		heal = {"heal", "stim", "patch"};
		self = {"me", "myself"};
		everyone = {"everyone", "us", "group", "all"};
	}

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

enum HealCommand {
	HEAL_NONE = 0,
	HEAL_TARGET
};

enum PatientKind {
	SPEAKER = 0,
	NAMED_BOT,
	NAMED_PLAYER,
	EVERYONE
};

struct HealRequest {
	HealCommand command = HEAL_NONE;
	std::vector<uint64> healerIdentityIds;
	PatientKind patientKind = SPEAKER;
	String patientName;

	bool isHeal() const { return command == HEAL_TARGET; }

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline bool containsPhrase(const std::vector<String>& tokens,
		const std::vector<String>& phrases, int* firstIndex = nullptr,
		int* phraseLength = nullptr) {
	for (size_t i = 0; i < phrases.size(); ++i) {
		int index = -1;
		if (SimBotGrouping::hasTokenSequence(tokens, phrases[i], &index)) {
			if (firstIndex != nullptr)
				*firstIndex = index;
			if (phraseLength != nullptr)
				*phraseLength = static_cast<int>(
					SimBotGrouping::keywordTokens(phrases[i]).size());
			return true;
		}
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

inline bool isRallyLine(const std::vector<String>& tokens) {
	static const std::vector<String> rally = {"rally", "regroup", "gather",
		"meet", "meet up", "group up", "head to", "go to", "follow"};
	return SimBotGrouping::containsKeyword(tokens, rally);
}

inline bool sameNameAt(const std::vector<String>& tokens, int start,
		const std::vector<String>& nameTokens) {
	if (start < 0 || nameTokens.empty() ||
		start + static_cast<int>(nameTokens.size()) >
			static_cast<int>(tokens.size()))
		return false;
	for (size_t i = 0; i < nameTokens.size(); ++i) {
		if (tokens[start + static_cast<int>(i)] != nameTokens[i])
			return false;
	}
	return true;
}

inline int findNameBefore(const std::vector<String>& tokens,
		const String& name, int beforeIndex) {
	const std::vector<String> nameTokens = SimBotGrouping::keywordTokens(name);
	for (int i = 0; i < beforeIndex; ++i) {
		if (sameNameAt(tokens, i, nameTokens) &&
			i + static_cast<int>(nameTokens.size()) <= beforeIndex)
			return i;
	}
	return -1;
}

inline int findNameAtOrAfter(const std::vector<String>& tokens,
		const String& name, int startIndex) {
	const std::vector<String> nameTokens = SimBotGrouping::keywordTokens(name);
	for (int i = std::max(0, startIndex); i < static_cast<int>(tokens.size()); ++i) {
		if (sameNameAt(tokens, i, nameTokens))
			return i;
	}
	return -1;
}

inline String joinTokens(const std::vector<String>& tokens, int start) {
	String result;
	for (int i = std::max(0, start); i < static_cast<int>(tokens.size()); ++i) {
		if (tokens[i] == "please" || tokens[i] == "now")
			continue;
		if (!result.isEmpty())
			result += " ";
		result += tokens[i];
	}
	return result;
}

inline HealRequest parseHealCommand(const String& text,
		const HealVocabulary& vocabulary,
		const std::vector<SimBotGrouping::NamedBot>& namedBots =
			std::vector<SimBotGrouping::NamedBot>(),
		bool speakerIsLeader = true) {
	HealRequest result;
	const std::vector<String> tokens = SimBotGrouping::tokenize(text);
	if (tokens.empty() || isMissionGrabLine(tokens) || isRallyLine(tokens))
		return result;

	int healIndex = -1;
	int healLength = 0;
	if (!containsPhrase(tokens, vocabulary.heal, &healIndex, &healLength))
		return result;

	result.command = HEAL_TARGET;
	for (size_t i = 0; i < namedBots.size(); ++i) {
		if (findNameBefore(tokens, namedBots[i].firstName, healIndex) < 0)
			continue;
		bool duplicate = false;
		for (size_t j = 0; j < result.healerIdentityIds.size(); ++j)
			duplicate |= result.healerIdentityIds[j] == namedBots[i].identityId;
		if (!duplicate)
			result.healerIdentityIds.push_back(namedBots[i].identityId);
	}

	const int patientStart = healIndex + healLength;
	int firstPatientToken = patientStart;
	while (firstPatientToken < static_cast<int>(tokens.size()) &&
			(tokens[firstPatientToken] == "please" ||
			 tokens[firstPatientToken] == "now"))
		++firstPatientToken;
	if (firstPatientToken >= static_cast<int>(tokens.size()))
		return result;

	int phraseIndex = -1;
	if (containsPhrase(std::vector<String>(tokens.begin() + firstPatientToken,
			tokens.end()), vocabulary.self, &phraseIndex)) {
		result.patientKind = SPEAKER;
		return result;
	}

	if (containsPhrase(std::vector<String>(tokens.begin() + firstPatientToken,
			tokens.end()), vocabulary.everyone, &phraseIndex)) {
		if (!speakerIsLeader)
			return HealRequest();
		result.patientKind = EVERYONE;
		return result;
	}

	for (size_t i = 0; i < namedBots.size(); ++i) {
		if (findNameAtOrAfter(tokens, namedBots[i].firstName,
				firstPatientToken) < 0)
			continue;
		result.patientKind = NAMED_BOT;
		result.patientName = namedBots[i].firstName;
		return result;
	}

	result.patientKind = NAMED_PLAYER;
	result.patientName = joinTokens(tokens, firstPatientToken);
	if (result.patientName.isEmpty())
		result.patientKind = SPEAKER;
	return result;
}

inline bool needsHeal(float healthFraction, float actionFraction,
		bool hasHealthOrActionDamage, float threshold) {
	if (!hasHealthOrActionDamage)
		return false;
	return std::min(healthFraction, actionFraction) <= threshold;
}

struct PlayerBotHealingPolicy {
	struct StimEntry {
		String templatePath;
		uint32 templateCrc = 0;
		int gameObjectType = 0;
		int minDoses = 20;
		float maxPricePerDose = 0.f;
		// The template's base medicineUse; restock never buys a tier the bot
		// cannot use.
		int medicineUse = 0;

		bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
		bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
	};

	bool playerHealsBots = true;
	bool botHeals = true;
	bool selfHeal = true;
	float groupHealThreshold = 0.70f;
	float selfHealThreshold = 0.60f;
	float selfCriticalFraction = 0.35f;
	float maxApproachMeters = 32.f;
	float applyRangeMeters = 6.f;
	int approachTimeoutSeconds = 20;
	int applyTimeoutSeconds = 8;
	bool bazaarRestock = true;
	int minDoses = 20;
	float restockSpendFraction = 0.25f;
	int restockScanIntervalMinutes = 30;
	int maxRestockLoadsPerVisit = 12;
	Vector<StimEntry> items;
	String signature;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct PatientCandidate {
	uint64 identityId = 0;
	uint64 objectId = 0;
	bool isSelf = false;
	float healthFraction = 1.f;
	float actionFraction = 1.f;
	bool hasHealthDamage = false;
	bool hasActionDamage = false;
	float distanceMeters = -1.f;
	bool healable = true;
	bool losClear = true;
	bool inCombat = false;
	bool orderedTarget = false;

	float lowestFraction() const {
		return std::min(healthFraction, actionFraction);
	}

	bool hasDamage() const {
		return hasHealthDamage || hasActionDamage;
	}

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct PatientChoice {
	int index = -1;
	String reason = "nothing";
	// The member a no-patient reason is about (refused, out of range, out of
	// sight); -1 when the reason names nobody.
	int reasonIndex = -1;

	bool found() const { return index >= 0; }

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline PatientChoice chooseHealPatient(
		const std::vector<PatientCandidate>& candidates,
		const PlayerBotHealingPolicy& policy = PlayerBotHealingPolicy()) {
	PatientChoice result;
	bool sawDamage = false;
	int outOfRangeIndex = -1;
	int refusedIndex = -1;
	int blockedIndex = -1;
	bool sawHealableNeed = false;
	int bestIndex = -1;
	float bestFraction = std::numeric_limits<float>::max();
	// A named order is about its patient alone: an unhurt Towe is "no_damage",
	// never a reason to heal whoever else is hurt.
	bool hasOrderedTarget = false;
	for (size_t i = 0; i < candidates.size(); ++i)
		hasOrderedTarget = hasOrderedTarget || candidates[i].orderedTarget;

	for (size_t i = 0; i < candidates.size(); ++i) {
		const PatientCandidate& candidate = candidates[i];
		if (hasOrderedTarget && !candidate.orderedTarget)
			continue;
		if (!candidate.hasDamage())
			continue;
		sawDamage = true;

		// Range, refusal and sight only explain a miss for a member who needs a
		// heal: a scratched puller out at 40 m is not "unreachable".
		const float threshold = candidate.isSelf ? policy.selfHealThreshold :
			policy.groupHealThreshold;
		if (!candidate.orderedTarget && !needsHeal(candidate.healthFraction,
				candidate.actionFraction, true, threshold))
			continue;

		if (candidate.distanceMeters >= 0.f &&
				candidate.distanceMeters > std::max(0.f, policy.maxApproachMeters)) {
			if (outOfRangeIndex < 0)
				outOfRangeIndex = static_cast<int>(i);
			continue;
		}

		if (!candidate.healable) {
			if (refusedIndex < 0)
				refusedIndex = static_cast<int>(i);
			continue;
		}
		sawHealableNeed = true;
		if (!candidate.losClear) {
			if (blockedIndex < 0)
				blockedIndex = static_cast<int>(i);
			continue;
		}

		if (candidate.orderedTarget) {
			result.index = static_cast<int>(i);
			result.reason = "selected";
			return result;
		}

		const float fraction = candidate.lowestFraction();
		if (bestIndex < 0 || fraction < bestFraction ||
				(fraction == bestFraction && candidate.isSelf &&
					candidate.lowestFraction() <= policy.selfCriticalFraction)) {
			bestIndex = static_cast<int>(i);
			bestFraction = fraction;
		}
	}

	if (bestIndex >= 0) {
		result.index = bestIndex;
		result.reason = "selected";
		return result;
	}
	if (!sawDamage) {
		result.reason = "no_damage";
	} else if (!sawHealableNeed && refusedIndex >= 0) {
		result.reason = "faction";
		result.reasonIndex = refusedIndex;
	} else if (outOfRangeIndex >= 0) {
		result.reason = "out_of_range";
		result.reasonIndex = outOfRangeIndex;
	} else if (blockedIndex >= 0) {
		result.reason = "unreachable";
		result.reasonIndex = blockedIndex;
	}
	return result;
}

struct StimStack {
	uint64 objectId = 0;
	uint32 templateCrc = 0;
	int doses = 0;
	int medicineUseRequired = 0;
	int effectiveness = 0;
	bool ranged = false;
	bool pet = false;
	bool droid = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline int chooseStim(const std::vector<StimStack>& stacks, int medicineUse) {
	int best = -1;
	for (size_t i = 0; i < stacks.size(); ++i) {
		const StimStack& stack = stacks[i];
		if (stack.objectId == 0 || stack.doses <= 0 || stack.ranged || stack.pet ||
				stack.droid || stack.medicineUseRequired > medicineUse)
			continue;
		if (best < 0 || stack.effectiveness > stacks[best].effectiveness ||
				(stack.effectiveness == stacks[best].effectiveness &&
					stack.doses < stacks[best].doses))
			best = static_cast<int>(i);
	}
	return best;
}

// Doses a bot can actually use: melee stims within its healing_ability.
inline int usableStimDoses(const std::vector<StimStack>& stacks,
		int healingAbility) {
	int doses = 0;
	for (size_t i = 0; i < stacks.size(); ++i) {
		const StimStack& stack = stacks[i];
		if (stack.doses > 0 && !stack.ranged && !stack.pet && !stack.droid &&
				stack.medicineUseRequired <= healingAbility)
			doses += stack.doses;
	}
	return doses;
}

// Restock is one need for all usable stims, not one per template: usable
// doses are pooled, and any pending usable purchase blocks the next one.
// Returns the indices of the usable entries to shop for (empty = no need).
inline std::vector<int> stimRestockNeeds(
		const Vector<PlayerBotHealingPolicy::StimEntry>& entries,
		const std::vector<StimStack>& stacks, int healingAbility,
		const std::vector<uint32>& pendingTemplateCrcs, int minDoses) {
	std::vector<int> usable;
	if (healingAbility <= 0)
		return usable;
	for (int i = 0; i < entries.size(); ++i) {
		if (entries.get(i).medicineUse > healingAbility)
			continue;
		if (std::find(pendingTemplateCrcs.begin(), pendingTemplateCrcs.end(),
				entries.get(i).templateCrc) != pendingTemplateCrcs.end())
			return std::vector<int>();
		usable.push_back(i);
	}
	if (usableStimDoses(stacks, healingAbility) >= minDoses)
		usable.clear();
	return usable;
}

struct HealerCandidate {
	uint64 identityId = 0;
	bool hasUsableStim = false;
	bool cooldownReady = false;
	bool isMedic = true;
	float distanceMeters = -1.f;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline int chooseHealer(const std::vector<HealerCandidate>& candidates) {
	int result = -1;
	float bestDistance = std::numeric_limits<float>::max();
	for (size_t i = 0; i < candidates.size(); ++i) {
		const HealerCandidate& candidate = candidates[i];
		if (candidate.identityId == 0 || !candidate.isMedic ||
				!candidate.hasUsableStim || !candidate.cooldownReady)
			continue;
		const float distance = candidate.distanceMeters < 0.f ?
			std::numeric_limits<float>::max() : candidate.distanceMeters;
		if (result < 0 || distance < bestDistance) {
			result = static_cast<int>(i);
			bestDistance = distance;
		}
	}
	return result;
}

enum HealStage {
	APPROACH = 0,
	APPLY,
	DONE,
	ABORT
};

struct HealState {
	HealStage stage = APPROACH;
	bool started = false;
	uint64 startedAtMs = 0;
	uint64 phaseStartedAtMs = 0;
	String reason;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct HealInputs {
	uint64 nowMs = 0;
	bool withinApplyRange = false;
	bool patientDead = false;
	bool patientLeft = false;
	bool unreachable = false;
	bool refused = false;
	bool applied = false;
	bool noDamage = false;
	uint64 approachTimeoutMs = 20000;
	uint64 applyTimeoutMs = 8000;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline HealState healStep(const HealState& current, const HealInputs& inputs) {
	HealState result = current;
	if (current.stage == DONE || current.stage == ABORT)
		return result;

	if (!result.started) {
		result.started = true;
		result.startedAtMs = inputs.nowMs;
		result.phaseStartedAtMs = inputs.nowMs;
	}

	if (inputs.patientDead) {
		result.stage = ABORT;
		result.reason = "patient_dead";
		return result;
	}
	if (inputs.patientLeft) {
		result.stage = ABORT;
		result.reason = "patient_left";
		return result;
	}
	if (inputs.refused) {
		result.stage = ABORT;
		result.reason = "refused";
		return result;
	}

	switch (current.stage) {
	case APPROACH:
		if (inputs.unreachable) {
			result.stage = ABORT;
			result.reason = "unreachable";
		} else if (inputs.withinApplyRange) {
			result.stage = APPLY;
			result.phaseStartedAtMs = inputs.nowMs;
		} else if (inputs.approachTimeoutMs > 0 && inputs.nowMs >=
				result.phaseStartedAtMs + inputs.approachTimeoutMs) {
			result.stage = ABORT;
			result.reason = "unreachable";
		}
		break;
	case APPLY:
		if (inputs.applied) {
			result.stage = DONE;
			result.reason = "applied";
		} else if (inputs.noDamage) {
			result.stage = DONE;
			result.reason = "no_damage";
		} else if (inputs.applyTimeoutMs > 0 && inputs.nowMs >=
				result.phaseStartedAtMs + inputs.applyTimeoutMs) {
			result.stage = ABORT;
			result.reason = "timeout";
		}
		break;
	default:
		break;
	}
	return result;
}

// FactionStatus values are kept as integers in the engine (ONLEAVE=0,
// COVERT=1, OVERT=2).  This wrapper pins the stock decision table in pure
// tests while the live CreatureObject implementation remains authoritative.
enum HealFactionStatus {
	FACTION_ONLEAVE = 0,
	FACTION_COVERT = 1,
	FACTION_OVERT = 2
};

inline bool stockHealFactionVerdict(bool useCovertOvert, int targetFaction,
		int targetStatus, int healerFaction, int healerStatus,
		bool targetIsPlayer, bool targetHasGcwTef) {
	if (useCovertOvert) {
		if (targetIsPlayer && targetFaction != healerFaction &&
				(targetStatus == FACTION_OVERT || targetHasGcwTef))
			return false;
		return true;
	}

	if (targetFaction != healerFaction && targetStatus != FACTION_ONLEAVE)
		return false;
	if (targetStatus == FACTION_OVERT && healerStatus != FACTION_OVERT)
		return false;
	if (targetStatus != FACTION_ONLEAVE && healerStatus == FACTION_ONLEAVE)
		return false;
	return true;
}

} // namespace SimBotHealing

#endif // SIMBOTHEALING_H_
