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
#include <cctype>
#include <cstdint>
#include <cstdlib>
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

} // namespace SimBotGroupRally

// G3 keeps the mission request value types beside the G2 parser so
// LeaderLine can carry them without a circular include.  The standalone
// SimBotGroupMission.h header includes this file and provides the remaining
// mission policies.
namespace SimBotGroupMission {

struct DirectionRule {
	String word;
	int bearingDeg = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

struct MissionVocabulary {
	std::vector<String> grab;
	std::vector<String> missions;
	std::vector<String> run;
	std::vector<String> skip;
	std::vector<DirectionRule> directions;
	std::vector<String> numbers;
	int maxGrabCount = 20;

	MissionVocabulary() {
		grab = {"grab", "get", "take", "pick up"};
		missions = {"mission", "missions", "mish", "lair", "lairs"};
		run = {"lets do missions", "do the missions", "next mission"};
		skip = {"skip", "skip this one"};
		numbers = {"one", "two", "three", "four", "five"};
		const char* words[] = {"n", "ne", "e", "se", "s", "sw", "w", "nw",
			"north", "northeast", "east", "southeast", "south", "southwest",
			"west", "northwest"};
		const int bearings[] = {0, 45, 90, 135, 180, 225, 270, 315,
			0, 45, 90, 135, 180, 225, 270, 315};
		for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
			directions.push_back({String(words[i]), bearings[i]});
	}

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

enum MissionCommand {
	MISSION_NONE = 0,
	MISSION_GRAB,
	MISSION_RUN,
	MISSION_SKIP
};

struct MissionRequest {
	MissionCommand command = MISSION_NONE;
	int count = 1;
	String creatureWord;
	int bearingDeg = 0;
	bool hasBearing = false;
	String addressedBot;
	bool unknownCreature = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) const { return true; }
};

inline bool missionHasToken(const std::vector<String>& values,
		const String& token) {
	for (size_t i = 0; i < values.size(); ++i) {
		if (token == values[i].trim().toLowerCase())
			return true;
	}
	return false;
}

inline bool missionIsNumber(const String& token, const MissionVocabulary& vocab,
		int& value) {
	const char* raw = token.toCharArray();
	if (raw != nullptr && *raw != '\0') {
		char* end = nullptr;
		const long parsed = std::strtol(raw, &end, 10);
		if (end != raw && *end == '\0' && parsed > 0) {
			value = parsed > 2147483647L ? 2147483647 :
				static_cast<int>(parsed);
			return true;
		}
	}
	for (size_t i = 0; i < vocab.numbers.size(); ++i) {
		if (token == vocab.numbers[i].trim().toLowerCase()) {
			value = static_cast<int>(i) + 1;
			return true;
		}
	}
	return false;
}

inline bool missionIsDirection(const String& token,
		const MissionVocabulary& vocab, int& bearing) {
	for (size_t i = 0; i < vocab.directions.size(); ++i) {
		if (token == vocab.directions[i].word.trim().toLowerCase()) {
			bearing = ((vocab.directions[i].bearingDeg % 360) + 360) % 360;
			return true;
		}
	}
	return false;
}

inline bool missionIsFiller(const String& token,
		const MissionVocabulary& vocab) {
	if (token == "out" || token == "front" || token == "of" ||
			token == "at" || token == "the" || token == "lets" ||
			token == "let" || token == "and" || token == "please" ||
			token == "to" || token == "me" || token == "then" ||
			token == "us" || token == "we" || token == "all" ||
			token == "everyone" || token == "guys" || token == "now" ||
			token == "a" || token == "an" || token == "this" ||
			token == "one" || token == "here" || token == "around" ||
			token == "there")
		return true;
	if (missionHasToken(vocab.grab, token) ||
			missionHasToken(vocab.missions, token) ||
			missionHasToken(vocab.skip, token))
		return true;
	int value = 0;
	return missionIsNumber(token, vocab, value) ||
		missionIsDirection(token, vocab, value);
}

inline MissionRequest parseMissionRequest(const String& text,
		const MissionVocabulary& vocab,
		const std::vector<String>& nameWords = std::vector<String>(),
		const std::vector<String>& creatureWords = std::vector<String>()) {
	MissionRequest result;
	const std::vector<String> tokens = SimBotGrouping::tokenize(text);
	const bool hasGrab = SimBotGrouping::containsKeyword(tokens, vocab.grab);
	const bool hasMission = SimBotGrouping::containsKeyword(tokens, vocab.missions);
	if (hasGrab && hasMission)
		result.command = MISSION_GRAB;
	else if (SimBotGrouping::containsKeyword(tokens, vocab.run))
		result.command = MISSION_RUN;
	else if (SimBotGrouping::containsKeyword(tokens, vocab.skip))
		result.command = MISSION_SKIP;
	else
		return result;

	int parsedCount = 1;
	for (size_t i = 0; i < tokens.size(); ++i) {
		int value = 0;
		if (missionIsNumber(tokens[i], vocab, value)) {
			parsedCount = value;
			break;
		}
	}
	const int maxCount = std::max(1, vocab.maxGrabCount);
	result.count = std::max(1, std::min(maxCount, parsedCount));

	for (size_t i = 0; i < tokens.size(); ++i) {
		const String token = tokens[i].trim().toLowerCase();
		if (missionHasToken(nameWords, token)) {
			if (result.addressedBot.isEmpty())
				result.addressedBot = token;
			continue;
		}
		int bearing = 0;
		if (missionIsDirection(token, vocab, bearing)) {
			// "south east" written as two words is SE: a north/south word
			// followed by an east/west word combines into the intercardinal.
			int nextBearing = 0;
			if ((bearing == 0 || bearing == 180) && i + 1 < tokens.size() &&
					missionIsDirection(tokens[i + 1].trim().toLowerCase(), vocab,
						nextBearing) && (nextBearing == 90 || nextBearing == 270)) {
				bearing = bearing == 0 ? (nextBearing == 90 ? 45 : 315) :
					(nextBearing == 90 ? 135 : 225);
				++i;
			}
			if (!result.hasBearing) {
				result.bearingDeg = bearing;
				result.hasBearing = true;
			}
			continue;
		}
		if (missionIsFiller(token, vocab))
			continue;

		bool knownCreature = false;
		for (size_t creatureIndex = 0; creatureIndex < creatureWords.size();
				++creatureIndex) {
			const String known = creatureWords[creatureIndex].trim().toLowerCase();
			const String tokenSingular = token.length() > 1 &&
				token.charAt(token.length() - 1) == 's' ?
				token.subString(0, token.length() - 1) : token;
			const String knownSingular = known.length() > 1 &&
				known.charAt(known.length() - 1) == 's' ?
				known.subString(0, known.length() - 1) : known;
			if (token == known || tokenSingular == knownSingular) {
				knownCreature = true;
				break;
			}
		}
		if (result.creatureWord.isEmpty())
			result.creatureWord = token;
		if (!knownCreature)
			result.unknownCreature = true;
	}
	if (result.command != MISSION_GRAB) {
		result.creatureWord = String();
		result.unknownCreature = false;
	}
	return result;
}

} // namespace SimBotGroupMission

namespace SimBotGroupRally {

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
	SimBotGroupMission::MissionRequest mission;
	bool isMission = false;

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
		const RallyVocabulary& rallyVocabulary,
		const SimBotGroupMission::MissionVocabulary& missionVocabulary =
			SimBotGroupMission::MissionVocabulary()) {
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
	result.mission = SimBotGroupMission::parseMissionRequest(text,
		missionVocabulary, nameWords);
	result.isMission = result.mission.command !=
		SimBotGroupMission::MISSION_NONE;

	// LEAVE and FOLLOW are explicit commands and win even when a place or
	// mission is also mentioned.  Mission commands then win over G2's
	// STATUS/READY and directive parsing.  A directive wins over STATUS/READY;
	// the manager distinguishes it with isDirective because it needs the parsed
	// place payload.
	if (result.command.command == SimBotGrouping::GROUP_COMMAND_LEAVE ||
			result.command.command == SimBotGrouping::GROUP_COMMAND_FOLLOW)
		result.isMission = false;
	if (result.command.command == SimBotGrouping::GROUP_COMMAND_LEAVE ||
			result.command.command == SimBotGrouping::GROUP_COMMAND_FOLLOW)
		result.isDirective = false;
	else if (result.isMission) {
		result.command.command = SimBotGrouping::GROUP_COMMAND_NONE;
		result.isDirective = false;
	}
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
	MISSION_GRAB_TRAVEL,
	MISSION_GRAB,
	MISSION_WAIT,
	MISSION_TRAVEL,
	MISSION_HOLD,
	MISSION_FIGHT,
	DONE
};

enum DirectiveOutcome {
	ARRIVAL = 0,
	BUFF_DONE,
	RALLY_ARRIVAL,
	RELEASED,
	FAILED,
	GRAB_DONE,
	MISSION_ARRIVAL,
	MISSION_DONE,
	MISSION_ABANDONED
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
	case MISSION_GRAB_TRAVEL:
		return outcome == ARRIVAL ? MISSION_GRAB : stage;
	case MISSION_GRAB:
		return outcome == GRAB_DONE ? MISSION_WAIT : stage;
	case MISSION_TRAVEL:
		return outcome == MISSION_ARRIVAL ? MISSION_HOLD : stage;
	case MISSION_HOLD:
		return outcome == RELEASED ? MISSION_FIGHT : stage;
	case MISSION_FIGHT:
		return outcome == MISSION_DONE || outcome == MISSION_ABANDONED ?
			MISSION_WAIT : stage;
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
	if (normalized == "grab_done" || normalized == "grab_complete")
		return nextStage(stage, GRAB_DONE);
	if (normalized == "mission_arrival" || normalized == "mission_arrived")
		return nextStage(stage, MISSION_ARRIVAL);
	if (normalized == "mission_done" || normalized == "mission_complete")
		return nextStage(stage, MISSION_DONE);
	if (normalized == "mission_abandoned" || normalized == "abandoned")
		return nextStage(stage, MISSION_ABANDONED);
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
