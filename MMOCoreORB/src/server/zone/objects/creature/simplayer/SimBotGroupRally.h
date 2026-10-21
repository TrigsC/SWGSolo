/*
 * SimBotGroupRally.h
 *
 * Pure policy for the P.10p G2 rally/directive line.  Parsing and movement
 * choices stay independent of the world, managers and locks; the manager and
 * controller feed live snapshots into these value-only functions.
 */

#ifndef SIMBOTGROUPRALLY_H_
#define SIMBOTGROUPRALLY_H_

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "SimBotGrouping.h"

namespace SimBotGroupRally {

struct PlaceRule {
	String key;
	String planet;
	String city;
	float x = 0.f;
	float y = 0.f;
	bool hasPoint = false;
	std::vector<String> aliases;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct LandmarkRule {
	String kind;
	std::vector<String> keywords;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct RallyVocabulary {
	std::vector<String> buff;
	std::vector<String> meet;
	std::vector<LandmarkRule> landmarks;
	std::vector<PlaceRule> places;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct DirectiveText {
	bool wantBuffs = false;
	bool hasMeet = false;
	String placeKey;
	String landmark;
	bool unknownPlaceWords = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct LeaderLine {
	SimBotGrouping::GroupCommand command;
	DirectiveText directive;
	bool isDirective = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline bool rallyStringEquals(const String& left, const String& right) {
	return left.trim().toLowerCase() == right.trim().toLowerCase();
}

inline bool isRallyFiller(const String& token) {
	return token == "out" || token == "front" || token == "of" ||
		token == "at" || token == "the" || token == "lets" ||
		token == "let" || token == "and" || token == "please" ||
		token == "to" || token == "me" || token == "get" ||
		token == "then" || token == "us" || token == "we" || token == "all" ||
		token == "everyone" || token == "guys" || token == "now";
}

inline std::vector<String> removeRallyFillers(
		const std::vector<String>& tokens) {
	std::vector<String> result;
	for (size_t i = 0; i < tokens.size(); ++i) {
		if (!isRallyFiller(tokens[i]))
			result.push_back(tokens[i]);
	}
	return result;
}

struct RallyAliasMatch {
	const PlaceRule* place = nullptr;
	size_t tokenCount = 0;
	int placeIndex = -1;
};

inline RallyAliasMatch findPlaceAlias(const std::vector<String>& tokens,
		const RallyVocabulary& vocabulary) {
	RallyAliasMatch result;
	const std::vector<String> filtered = removeRallyFillers(tokens);
	for (size_t placeIndex = 0; placeIndex < vocabulary.places.size();
			++placeIndex) {
		const PlaceRule& place = vocabulary.places[placeIndex];
		std::vector<String> aliases = place.aliases;
		if (!place.key.isEmpty())
			aliases.push_back(place.key);
		if (!place.city.isEmpty())
			// SimBotGrouping::tokenize treats the underscore as a separator, so
			// an internal city key is also a human-readable alias.
			aliases.push_back(place.city);
		for (size_t aliasIndex = 0; aliasIndex < aliases.size(); ++aliasIndex) {
			const std::vector<String> aliasTokens =
				SimBotGrouping::keywordTokens(aliases[aliasIndex]);
			if (aliasTokens.empty())
				continue;
			bool matched = false;
			for (size_t start = 0; start + aliasTokens.size() <= tokens.size();
					++start) {
				matched = true;
				for (size_t tokenIndex = 0; tokenIndex < aliasTokens.size();
						++tokenIndex) {
					if (tokens[start + tokenIndex] != aliasTokens[tokenIndex]) {
						matched = false;
						break;
					}
				}
				if (matched)
					break;
			}
			if (!matched) {
				for (size_t start = 0;
						start + aliasTokens.size() <= filtered.size(); ++start) {
					matched = true;
					for (size_t tokenIndex = 0;
							tokenIndex < aliasTokens.size(); ++tokenIndex) {
						if (filtered[start + tokenIndex] != aliasTokens[tokenIndex]) {
							matched = false;
							break;
						}
					}
					if (matched)
						break;
				}
			}
			if (matched && aliasTokens.size() > result.tokenCount) {
				result.place = &place;
				result.tokenCount = aliasTokens.size();
				result.placeIndex = static_cast<int>(placeIndex);
			}
		}
	}
	return result;
}

inline String findRallyLandmark(const std::vector<String>& tokens,
		const RallyVocabulary& vocabulary, bool includeLeader) {
	String bestKind;
	size_t bestLength = 0;
	for (size_t i = 0; i < vocabulary.landmarks.size(); ++i) {
		const LandmarkRule& landmark = vocabulary.landmarks[i];
		if (!includeLeader && rallyStringEquals(landmark.kind, "leader"))
			continue;
		for (size_t j = 0; j < landmark.keywords.size(); ++j) {
			const std::vector<String> keywordTokens =
				SimBotGrouping::keywordTokens(landmark.keywords[j]);
			bool found = false;
			for (size_t start = 0;
					start + keywordTokens.size() <= tokens.size(); ++start) {
				found = true;
				for (size_t k = 0; k < keywordTokens.size(); ++k) {
					if (tokens[start + k] != keywordTokens[k]) {
						found = false;
						break;
					}
				}
				if (found)
					break;
			}
			if (found && keywordTokens.size() > bestLength) {
				bestKind = landmark.kind;
				bestLength = keywordTokens.size();
			}
		}
	}
	return bestKind;
}

inline DirectiveText parseDirective(const String& text,
		const RallyVocabulary& vocabulary,
		const std::vector<String>& ignoredWords = std::vector<String>()) {
	DirectiveText result;
	const std::vector<String> tokens = SimBotGrouping::tokenize(text);
	result.wantBuffs = SimBotGrouping::containsKeyword(tokens, vocabulary.buff);
	result.hasMeet = SimBotGrouping::containsKeyword(tokens, vocabulary.meet);

	const RallyAliasMatch place = findPlaceAlias(tokens, vocabulary);
	if (place.place != nullptr)
		result.placeKey = place.place->key;

	// A concrete landmark beats the generic leader landmark, and a place alias
	// beats the leader phrase ("meet me at Mos Eisley").
	result.landmark = findRallyLandmark(tokens, vocabulary, false);

	// "meet at narnia" names a place we do not know; "let's get buffs and
	// group up" names none, which means back to the leader.  Only words left
	// after the meet phrase count, minus fillers, buff and meet words, leader
	// phrases ("me", "here", "my location") and the group's bot names.  This
	// runs before the leader landmark, so "meet me at narnia" stays unknown.
	if (result.hasMeet && result.placeKey.isEmpty() &&
			result.landmark.isEmpty()) {
		size_t meetEnd = tokens.size();
		for (size_t i = 0; i < vocabulary.meet.size(); ++i) {
			int first = -1;
			if (SimBotGrouping::hasTokenSequence(tokens, vocabulary.meet[i],
					&first)) {
				const size_t end = static_cast<size_t>(first) +
					SimBotGrouping::keywordTokens(vocabulary.meet[i]).size();
				meetEnd = std::min(meetEnd, end);
			}
		}
		// Whole spans of every known phrase (multi-word ones like "group up"
		// or "my location" included) are marked before single words are judged.
		std::vector<bool> known(tokens.size(), false);
		auto markPhrases = [&tokens, &known](const std::vector<String>& phrases) {
			for (size_t k = 0; k < phrases.size(); ++k) {
				const std::vector<String> phrase =
					SimBotGrouping::keywordTokens(phrases[k]);
				for (size_t start = 0; !phrase.empty() &&
						start + phrase.size() <= tokens.size(); ++start) {
					bool equal = true;
					for (size_t t = 0; t < phrase.size() && equal; ++t)
						equal = tokens[start + t] == phrase[t];
					for (size_t t = 0; equal && t < phrase.size(); ++t)
						known[start + t] = true;
				}
			}
		};
		markPhrases(vocabulary.buff);
		markPhrases(vocabulary.meet);
		for (size_t l = 0; l < vocabulary.landmarks.size(); ++l) {
			if (rallyStringEquals(vocabulary.landmarks[l].kind, "leader"))
				markPhrases(vocabulary.landmarks[l].keywords);
		}
		for (size_t i = meetEnd; i < tokens.size(); ++i) {
			if (known[i] || isRallyFiller(tokens[i]) || tokens[i] == "up" ||
					SimBotGrouping::containsString(ignoredWords, tokens[i]))
				continue;
			result.unknownPlaceWords = true;
			break;
		}
	}
	if (result.landmark.isEmpty() && result.placeKey.isEmpty() &&
			!result.unknownPlaceWords && (result.wantBuffs || result.hasMeet))
		result.landmark = "leader";
	return result;
}

inline LeaderLine parseLeaderLine(const String& text,
		const std::vector<SimBotGrouping::NamedBot>& botFirstNames,
		const SimBotGrouping::Vocabulary& groupingVocabulary,
		const RallyVocabulary& rallyVocabulary) {
	LeaderLine result;
	result.command = SimBotGrouping::parseGroupCommand(text, botFirstNames,
		groupingVocabulary, true);
	std::vector<String> nameWords;
	for (size_t i = 0; i < botFirstNames.size(); ++i) {
		const std::vector<String> parts = SimBotGrouping::keywordTokens(
			botFirstNames[i].firstName);
		nameWords.insert(nameWords.end(), parts.begin(), parts.end());
	}
	result.directive = parseDirective(text, rallyVocabulary, nameWords);
	result.isDirective = result.directive.wantBuffs ||
		result.directive.hasMeet;

	// LEAVE and FOLLOW are explicit commands and win even when a place is also
	// mentioned.  A directive wins over STATUS/READY; the manager distinguishes
	// it with isDirective because it needs the parsed place payload.
	if (result.command.command == SimBotGrouping::GROUP_COMMAND_LEAVE ||
			result.command.command == SimBotGrouping::GROUP_COMMAND_FOLLOW)
		result.isDirective = false;
	else if (result.isDirective)
		result.command.command = SimBotGrouping::GROUP_COMMAND_NONE;
	return result;
}

struct BuffCityCandidate {
	String city;
	String planet;
	int legsBotToHub = -1;
	int legsHubToMeet = -1;
	bool sameCityAsMeet = false;
	bool sameCityAsBot = false;
	bool samePlanetAsMeet = false;
	bool samePlanetAsBot = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct BuffCityInputs {
	String meetCity;
	String botCity;
	String meetPlanet;
	String botPlanet;
	std::vector<BuffCityCandidate> candidates;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct BuffCityChoice {
	int index = -1;
	String reason = "fallback";

	bool found() const { return index >= 0; }

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline bool candidateSameCityAsMeet(const BuffCityCandidate& candidate,
		const BuffCityInputs& inputs) {
	return candidate.sameCityAsMeet || (!inputs.meetCity.isEmpty() &&
		rallyStringEquals(candidate.city, inputs.meetCity));
}

inline bool candidateSameCityAsBot(const BuffCityCandidate& candidate,
		const BuffCityInputs& inputs) {
	return candidate.sameCityAsBot || (!inputs.botCity.isEmpty() &&
		rallyStringEquals(candidate.city, inputs.botCity));
}

inline int candidatePlanetRank(const BuffCityCandidate& candidate,
		const BuffCityInputs& inputs) {
	const bool sameMeetPlanet = candidate.samePlanetAsMeet ||
		(!inputs.meetPlanet.isEmpty() &&
			rallyStringEquals(candidate.planet, inputs.meetPlanet));
	if (sameMeetPlanet)
		return 0;
	const bool sameBotPlanet = candidate.samePlanetAsBot ||
		(!inputs.botPlanet.isEmpty() &&
			rallyStringEquals(candidate.planet, inputs.botPlanet));
	return sameBotPlanet ? 1 : 2;
}

inline BuffCityChoice chooseBuffCity(const BuffCityInputs& inputs) {
	BuffCityChoice result;
	for (size_t i = 0; i < inputs.candidates.size(); ++i) {
		if (candidateSameCityAsMeet(inputs.candidates[i], inputs)) {
			result.index = static_cast<int>(i);
			result.reason = "meet_city";
			return result;
		}
	}
	for (size_t i = 0; i < inputs.candidates.size(); ++i) {
		if (candidateSameCityAsBot(inputs.candidates[i], inputs)) {
			result.index = static_cast<int>(i);
			result.reason = "current_city";
			return result;
		}
	}

	int bestIndex = -1;
	int bestLegs = 0;
	int bestPlanetRank = 0;
	for (size_t i = 0; i < inputs.candidates.size(); ++i) {
		const BuffCityCandidate& candidate = inputs.candidates[i];
		if (candidate.legsBotToHub < 0 || candidate.legsHubToMeet < 0)
			continue;
		const int totalLegs = candidate.legsBotToHub + candidate.legsHubToMeet;
		const int planetRank = candidatePlanetRank(candidate, inputs);
		if (bestIndex < 0 || totalLegs < bestLegs ||
				(totalLegs == bestLegs && planetRank < bestPlanetRank)) {
			bestIndex = static_cast<int>(i);
			bestLegs = totalLegs;
			bestPlanetRank = planetRank;
		}
	}
	if (bestIndex >= 0) {
		result.index = bestIndex;
		result.reason = "on_the_way";
	}
	return result;
}

enum DirectiveStage {
	PLANNING = 0,
	BUFF_TRAVEL,
	BUFFING,
	RALLY_TRAVEL,
	RALLY_HOLD,
	DONE
};

enum DirectiveOutcome {
	ARRIVAL = 0,
	BUFF_DONE,
	RALLY_ARRIVAL,
	RELEASED,
	FAILED
};

inline DirectiveStage firstStage(bool wantBuffs, bool buffCityIsCurrent,
		bool needsBuffs) {
	if (!wantBuffs || !needsBuffs)
		return RALLY_TRAVEL;
	return buffCityIsCurrent ? BUFFING : BUFF_TRAVEL;
}

inline DirectiveStage nextStage(DirectiveStage stage,
		DirectiveOutcome outcome) {
	switch (stage) {
	case BUFF_TRAVEL:
		return outcome == ARRIVAL ? BUFFING : stage;
	case BUFFING:
		return outcome == BUFF_DONE || outcome == FAILED ? RALLY_TRAVEL : stage;
	case RALLY_TRAVEL:
		return outcome == ARRIVAL || outcome == RALLY_ARRIVAL ? RALLY_HOLD :
			stage;
	case RALLY_HOLD:
		return outcome == RELEASED ? DONE : stage;
	default:
		return stage;
	}
}

inline DirectiveStage nextStage(DirectiveStage stage, const String& outcome) {
	const String normalized = outcome.trim().toLowerCase();
	if (normalized == "arrival" || normalized == "arrived")
		return nextStage(stage, ARRIVAL);
	if (normalized == "buff_done" || normalized == "buff_complete" ||
			normalized == "buffed")
		return nextStage(stage, BUFF_DONE);
	if (normalized == "rally_arrival" || normalized == "rally_arrived")
		return nextStage(stage, RALLY_ARRIVAL);
	if (normalized == "released" || normalized == "follow" ||
			normalized == "leader_arrived")
		return nextStage(stage, RELEASED);
	if (normalized == "failed" || normalized == "fallback")
		return nextStage(stage, FAILED);
	return stage;
}

struct HoldVerdict {
	bool release = false;
	String reason = "keep";

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline HoldVerdict judgeHoldRelease(bool leaderSamePlanet,
		float leaderDistanceToPoint, float releaseMeters, bool followCommand,
		uint64 holdStartedMs, uint64 nowMs, int maxHoldMinutes) {
	if (followCommand)
		return {true, "follow_command"};
	const float radius = std::max(0.f, releaseMeters);
	if (leaderSamePlanet && leaderDistanceToPoint >= 0.f &&
			leaderDistanceToPoint <= radius)
		return {true, "leader_arrived"};
	if (maxHoldMinutes > 0 && holdStartedMs != 0 && nowMs >= holdStartedMs +
			static_cast<uint64>(maxHoldMinutes) * 60ULL * 1000ULL)
		return {true, "hold_timeout"};
	return {false, "keep"};
}

enum SamePlanetMode {
	WALK = 0,
	SHUTTLE
};

inline SamePlanetMode samePlanetMode(float directMeters,
		float botToPadMeters, float padToTargetMeters, float minSavingsMeters,
		bool shuttleSuppressed) {
	if (shuttleSuppressed || minSavingsMeters <= 0.f)
		return WALK;
	const float direct = std::max(0.f, directMeters);
	const float shuttle = std::max(0.f, botToPadMeters) +
		std::max(0.f, padToTargetMeters) + minSavingsMeters;
	return shuttle < direct ? SHUTTLE : WALK;
}

struct RebuffVisit {
	bool notifiedThisVisit = false;
	uint64 outsideSinceMs = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct RebuffVisitStep {
	RebuffVisit visit;
	bool notify = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline RebuffVisitStep stepRebuffVisit(const RebuffVisit& current,
		bool inCityRegion, bool buffsLow, uint64 nowMs,
		int visitResetSeconds) {
	RebuffVisitStep result;
	result.visit = current;
	if (inCityRegion) {
		result.visit.outsideSinceMs = 0;
		if (buffsLow && !result.visit.notifiedThisVisit) {
			result.visit.notifiedThisVisit = true;
			result.notify = true;
		}
		return result;
	}

	if (result.visit.outsideSinceMs == 0)
		result.visit.outsideSinceMs = nowMs;
	if (visitResetSeconds > 0 && nowMs >= result.visit.outsideSinceMs +
			static_cast<uint64>(visitResetSeconds) * 1000ULL)
		result.visit.notifiedThisVisit = false;
	return result;
}

struct RallySlotPolicy {
	float rallyMinMeters = 3.f;
	float rallyMaxMeters = 8.f;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline Vector3 rallySlot(uint64 identityId, const Vector3& point, int botIndex,
		const RallySlotPolicy& policy) {
	SimBotGrouping::FollowPolicy followPolicy;
	followPolicy.followMinMeters = std::max(0.f, policy.rallyMinMeters);
	followPolicy.followMaxMeters = std::max(followPolicy.followMinMeters,
		policy.rallyMaxMeters);
	return SimBotGrouping::followSlot(identityId, point, 0.f, botIndex,
		followPolicy);
}

} // namespace SimBotGroupRally

#endif // SIMBOTGROUPRALLY_H_
