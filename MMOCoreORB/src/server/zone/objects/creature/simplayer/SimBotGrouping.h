/*
 * SimBotGrouping.h
 *
 * Pure policy for the PlayerBot grouping line.  The header deliberately does
 * not access game objects, managers or locks.  SimPlayerManager snapshots the
 * live values and feeds them into these decisions.
 */

#ifndef SIMBOTGROUPING_H_
#define SIMBOTGROUPING_H_

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <vector>

#include "engine/engine.h"

namespace SimBotGrouping {

struct RoleRule {
	String name;
	std::vector<String> keywords;
	String skill;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct GearRule {
	String name;
	std::vector<String> keywords;
	String armorTemplateContains;
	int minPieces = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct TierRule {
	String name;
	std::vector<String> keywords;
	bool requireTemplateComplete = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct Vocabulary {
	std::vector<String> requestPhrases;
	std::vector<String> botWords;
	std::vector<String> numberWords;
	std::vector<RoleRule> roles;
	std::vector<GearRule> gear;
	std::vector<TierRule> tiers;
	std::vector<String> status;
	std::vector<String> leave;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct LfbRequest {
	bool isRequest = false;
	int requestedCount = 0;
	int liveOffers = 0;
	String requesterPlanet;
	Vector3 requesterPosition;
	std::vector<String> requiredRoles;
	std::vector<String> requiredGear;
	std::vector<String> requiredTiers;
	std::vector<String> matchedKeywords;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

enum AvailabilityClass {
	FREE = 0,
	BUSY = 1
};

struct Availability {
	AvailabilityClass cls = BUSY;
	String reason = "unknown";

	bool isFree() const { return cls == FREE; }

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

// These values mirror SimHunterController's current enums without making this
// policy header depend on the controller or its engine types.  Group phases are
// reserved here so policy tests can cover the eventual appended values.
enum HuntPhaseCode {
	PHASE_IDLE_HOME = 0,
	PHASE_ANNOUNCE_JOB = 1,
	PHASE_BUFF_UP = 2,
	PHASE_RELOCATING = 3,
	PHASE_BUFF_TRIP = 4,
	PHASE_TRAVEL_OUT = 5,
	PHASE_TRAVEL_TO_TERMINAL = 6,
	PHASE_ACCEPT_MISSION = 7,
	PHASE_TRAVEL_TO_MISSION = 8,
	PHASE_TRAVEL_TO_LAIR = 9,
	PHASE_ENGAGING_LAIR = 10,
	PHASE_AWAITING_WORLD = 11,
	PHASE_HUNTING = 12,
	PHASE_RETREATING = 13,
	PHASE_HEALING = 14,
	PHASE_TRAVEL_HOME = 15,
	PHASE_DELIVER = 16,
	PHASE_MISSION_CLEANUP = 17,
	PHASE_CLONE_HOME = 18,
	PHASE_DONE = 19,
	PHASE_BAZAAR_VISIT = 20,
	PHASE_GROUP_WAIT = 21,
	PHASE_GROUP_FOLLOW = 22,
	PHASE_GROUP_TRAVEL = 23
};

enum EarlyGamePhaseCode {
	EARLY_LEAVE_CITY = 0,
	EARLY_SELECT_TARGET = 1,
	EARLY_ENGAGE = 2,
	EARLY_RECOVER = 3,
	EARLY_TRAIN = 4
};

inline Availability classifyAvailability(int phaseCode, int earlyGamePhaseCode,
		bool inCombat, bool dead, bool interplanetaryTravelActive,
		bool lootStepActive);

inline Availability classifyAvailability(int phaseCode, int earlyGamePhaseCode,
		bool inCombat, bool dead, bool interplanetaryTravelActive,
		bool lootStepActive, bool earlyGameTargetHeld,
		bool earlyGameActive = false) {
	if (dead)
		return {BUSY, "dead"};
	if (inCombat)
		return {BUSY, "in_combat"};
	if (interplanetaryTravelActive)
		return {BUSY, "interplanetary_travel"};
	if (lootStepActive)
		return {BUSY, "loot_step"};

	// A novice is available between fights.  Engage is the only early-game
	// phase that is busy, and a packed snapshot can distinguish an engage that
	// still owns a target from one that is only waiting for the next scan.
	if (earlyGamePhaseCode == EARLY_ENGAGE && earlyGameTargetHeld)
		return {BUSY, "early_game_engage"};
	// The early game runs under the HUNTING phase, so the phase switch below
	// would call every grinding novice busy. Owner decision 2026-10-19: a
	// grinding novice is free between fights (combat was rejected above).
	if (earlyGameActive)
		return {FREE, "early_game_between_fights"};

	switch (phaseCode) {
	case PHASE_IDLE_HOME:
	case PHASE_ANNOUNCE_JOB:
	case PHASE_BUFF_UP:
	case PHASE_TRAVEL_TO_TERMINAL:
	case PHASE_BAZAAR_VISIT:
	case PHASE_TRAVEL_HOME:
	case PHASE_DELIVER:
	case PHASE_MISSION_CLEANUP:
	case PHASE_CLONE_HOME:
	case PHASE_DONE:
		return {FREE, "free"};
	case PHASE_RELOCATING:
		return {BUSY, "relocating"};
	case PHASE_BUFF_TRIP:
		return {BUSY, "buff_trip"};
	case PHASE_TRAVEL_OUT:
		return {BUSY, "travel_out"};
	case PHASE_ACCEPT_MISSION:
		return {BUSY, "accept_mission"};
	case PHASE_TRAVEL_TO_MISSION:
		return {BUSY, "travel_to_mission"};
	case PHASE_TRAVEL_TO_LAIR:
		return {BUSY, "travel_to_lair"};
	case PHASE_ENGAGING_LAIR:
		return {BUSY, "engaging_lair"};
	case PHASE_AWAITING_WORLD:
		return {BUSY, "awaiting_world"};
	case PHASE_HUNTING:
		return {BUSY, "hunting"};
	case PHASE_RETREATING:
		return {BUSY, "retreating"};
	case PHASE_HEALING:
		return {BUSY, "healing"};
	case PHASE_GROUP_WAIT:
	case PHASE_GROUP_FOLLOW:
	case PHASE_GROUP_TRAVEL:
		return {BUSY, "grouped"};
	default:
		return {BUSY, "unknown_phase"};
	}
}

inline Availability classifyAvailability(int phaseCode, int earlyGamePhaseCode,
		bool inCombat, bool dead, bool interplanetaryTravelActive,
		bool lootStepActive) {
	return classifyAvailability(phaseCode, earlyGamePhaseCode, inCombat, dead,
			interplanetaryTravelActive, lootStepActive,
			earlyGamePhaseCode == EARLY_ENGAGE);
}

struct AvailabilitySnapshot {
	int phaseCode = PHASE_IDLE_HOME;
	int earlyGamePhaseCode = EARLY_LEAVE_CITY;
	bool inCombat = false;
	bool dead = false;
	bool interplanetaryTravelActive = false;
	bool lootStepActive = false;
	bool earlyGameTargetHeld = false;
	bool earlyGameActive = false;

	uint64 pack() const {
		uint64 value = static_cast<uint64>(phaseCode & 0xff);
		value |= static_cast<uint64>(earlyGamePhaseCode & 0xff) << 8;
		if (inCombat)
			value |= 1ULL << 16;
		if (dead)
			value |= 1ULL << 17;
		if (interplanetaryTravelActive)
			value |= 1ULL << 18;
		if (lootStepActive)
			value |= 1ULL << 19;
		if (earlyGameTargetHeld)
			value |= 1ULL << 20;
		if (earlyGameActive)
			value |= 1ULL << 21;
		return value;
	}

	static AvailabilitySnapshot unpack(uint64 value) {
		AvailabilitySnapshot result;
		result.phaseCode = static_cast<int>(value & 0xff);
		result.earlyGamePhaseCode = static_cast<int>((value >> 8) & 0xff);
		result.inCombat = (value & (1ULL << 16)) != 0;
		result.dead = (value & (1ULL << 17)) != 0;
		result.interplanetaryTravelActive = (value & (1ULL << 18)) != 0;
		result.lootStepActive = (value & (1ULL << 19)) != 0;
		result.earlyGameTargetHeld = (value & (1ULL << 20)) != 0;
		result.earlyGameActive = (value & (1ULL << 21)) != 0;
		return result;
	}

	Availability classify() const {
		return classifyAvailability(phaseCode, earlyGamePhaseCode, inCombat,
				dead, interplanetaryTravelActive, lootStepActive,
				earlyGameTargetHeld, earlyGameActive);
	}

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline uint64 packAvailabilitySnapshot(const AvailabilitySnapshot& snapshot) {
	return snapshot.pack();
}

inline AvailabilitySnapshot unpackAvailabilitySnapshot(uint64 value) {
	return AvailabilitySnapshot::unpack(value);
}

struct Candidate {
	uint64 identityId = 0;
	String firstName;
	String planet;
	Vector3 position;
	Availability availability;
	// -1 means use the room-wide policy.  Harnesses may override this for one
	// roster identity without changing the production availability policy.
	int availabilityChancePercent = -1;
	bool templateComplete = false;
	std::vector<String> trainedSkills;
	std::vector<String> wornArmorTemplates;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct ResponderPolicy {
	Vocabulary vocabulary;
	bool enabled = true;
	int defaultReplies = 2;
	int maxRepliesCap = 10;
	int replyDelayMinSeconds = 3;
	int replyDelayMaxSeconds = 12;
	int minReplyGapSeconds = 2;
	int availabilityChancePercent = 80;
	int maxGroupedBots = 10;
	int maxBotsPerLeader = 9;
	String requesterPlanet;
	Vector3 requesterPosition;
	int groupedTotal = 0;
	int leaderGroupedTotal = 0;
};

typedef ResponderPolicy Policy;

struct ResponderPick {
	uint64 identityId = 0;
	int delayMs = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline std::vector<String> tokenize(const String& text) {
	const int length = std::min(256, text.length());
	std::string normalized;
	normalized.reserve(static_cast<size_t>(length));
	for (int i = 0; i < length; ++i) {
		const unsigned char character = static_cast<unsigned char>(text.charAt(i));
		if (std::isalnum(character))
			normalized.push_back(static_cast<char>(std::tolower(character)));
		else
			normalized.push_back(' ');
	}

	std::vector<String> result;
	std::string current;
	for (size_t i = 0; i < normalized.size(); ++i) {
		if (normalized[i] == ' ') {
			if (!current.empty()) {
				result.push_back(String(current));
				current.clear();
			}
		} else {
			current.push_back(normalized[i]);
		}
	}
	if (!current.empty())
		result.push_back(String(current));
	return result;
}

inline std::vector<String> keywordTokens(const String& keyword) {
	return tokenize(keyword);
}

inline bool hasTokenSequence(const std::vector<String>& tokens,
		const String& phrase, int* firstIndex = nullptr) {
	const std::vector<String> phraseTokens = keywordTokens(phrase);
	if (phraseTokens.empty() || phraseTokens.size() > tokens.size())
		return false;
	for (size_t i = 0; i + phraseTokens.size() <= tokens.size(); ++i) {
		bool matches = true;
		for (size_t j = 0; j < phraseTokens.size(); ++j) {
			if (tokens[i + j] != phraseTokens[j]) {
				matches = false;
				break;
			}
		}
		if (matches) {
			if (firstIndex != nullptr)
				*firstIndex = static_cast<int>(i);
			return true;
		}
	}
	return false;
}

inline bool containsString(const std::vector<String>& values,
		const String& value) {
	for (size_t i = 0; i < values.size(); ++i) {
		if (values[i] == value)
			return true;
	}
	return false;
}

inline bool containsKeyword(const std::vector<String>& tokens,
		const std::vector<String>& keywords, String* matched = nullptr) {
	for (size_t i = 0; i < keywords.size(); ++i) {
		if (hasTokenSequence(tokens, keywords[i])) {
			if (matched != nullptr)
				*matched = keywords[i];
			return true;
		}
	}
	return false;
}

inline bool parsePositiveInt(const String& token, int& value) {
	const char* text = token.toCharArray();
	if (text == nullptr || *text == '\0')
		return false;
	char* end = nullptr;
	const long parsed = std::strtol(text, &end, 10);
	if (end == text || *end != '\0' || parsed <= 0)
		return false;
	value = parsed > 2147483647L ? 2147483647 : static_cast<int>(parsed);
	return true;
}

inline int numberWordValue(const String& token, const Vocabulary& vocabulary) {
	static const char* defaults[] = {"one", "two", "three", "four", "five",
		"six", "seven", "eight", "nine", "ten"};
	for (int i = 0; i < 10; ++i) {
		const String word = vocabulary.numberWords.size() > i &&
				!vocabulary.numberWords[i].isEmpty() ?
				vocabulary.numberWords[i].toLowerCase() : String(defaults[i]);
		if (token == word)
			return i + 1;
	}
	return 0;
}

inline int countNearBotWord(const std::vector<String>& tokens, int botIndex,
		const Vocabulary& vocabulary) {
	const int offsets[] = {-1, 1, -2, 2};
	for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
		const int index = botIndex + offsets[i];
		if (index < 0 || index >= static_cast<int>(tokens.size()))
			continue;
		int number = 0;
		if (parsePositiveInt(tokens[index], number))
			return number;
		number = numberWordValue(tokens[index], vocabulary);
		if (number > 0)
			return number;
	}
	return 0;
}

inline LfbRequest parseLfbRequest(const String& text,
		const Vocabulary& vocabulary) {
	LfbRequest result;
	const std::vector<String> tokens = tokenize(text);
	int botIndex = -1;
	String matchedBotWord;
	for (size_t i = 0; i < tokens.size() && botIndex < 0; ++i) {
		for (size_t j = 0; j < vocabulary.botWords.size(); ++j) {
			if (tokens[i] == vocabulary.botWords[j].toLowerCase()) {
				botIndex = static_cast<int>(i);
				matchedBotWord = vocabulary.botWords[j];
				break;
			}
		}
	}
	if (botIndex < 0)
		return result;

	String requestPhrase;
	for (size_t i = 0; i < vocabulary.requestPhrases.size(); ++i) {
		if (hasTokenSequence(tokens, vocabulary.requestPhrases[i])) {
			requestPhrase = vocabulary.requestPhrases[i];
			break;
		}
	}
	if (requestPhrase.isEmpty())
		return result;

	result.isRequest = true;
	result.requestedCount = countNearBotWord(tokens, botIndex, vocabulary);
	result.matchedKeywords.push_back(requestPhrase);
	result.matchedKeywords.push_back(matchedBotWord);

	for (size_t i = 0; i < vocabulary.roles.size(); ++i) {
		String matched;
		if (containsKeyword(tokens, vocabulary.roles[i].keywords, &matched)) {
			result.requiredRoles.push_back(vocabulary.roles[i].name);
			if (!containsString(result.matchedKeywords, matched))
				result.matchedKeywords.push_back(matched);
		}
	}
	for (size_t i = 0; i < vocabulary.gear.size(); ++i) {
		String matched;
		if (containsKeyword(tokens, vocabulary.gear[i].keywords, &matched)) {
			result.requiredGear.push_back(vocabulary.gear[i].name);
			if (!containsString(result.matchedKeywords, matched))
				result.matchedKeywords.push_back(matched);
		}
	}
	for (size_t i = 0; i < vocabulary.tiers.size(); ++i) {
		String matched;
		if (containsKeyword(tokens, vocabulary.tiers[i].keywords, &matched)) {
			result.requiredTiers.push_back(vocabulary.tiers[i].name);
			if (!containsString(result.matchedKeywords, matched))
				result.matchedKeywords.push_back(matched);
		}
	}
	return result;
}

inline const RoleRule* roleRule(const Vocabulary& vocabulary,
		const String& name) {
	for (size_t i = 0; i < vocabulary.roles.size(); ++i)
		if (vocabulary.roles[i].name == name)
			return &vocabulary.roles[i];
	return nullptr;
}

inline const GearRule* gearRule(const Vocabulary& vocabulary,
		const String& name) {
	for (size_t i = 0; i < vocabulary.gear.size(); ++i)
		if (vocabulary.gear[i].name == name)
			return &vocabulary.gear[i];
	return nullptr;
}

inline const TierRule* tierRule(const Vocabulary& vocabulary,
		const String& name) {
	for (size_t i = 0; i < vocabulary.tiers.size(); ++i)
		if (vocabulary.tiers[i].name == name)
			return &vocabulary.tiers[i];
	return nullptr;
}

inline bool candidateMatches(const Candidate& candidate,
		const LfbRequest& request, const Vocabulary& vocabulary) {
	for (size_t i = 0; i < request.requiredRoles.size(); ++i) {
		const RoleRule* role = roleRule(vocabulary, request.requiredRoles[i]);
		if (role == nullptr || role->skill.isEmpty() ||
				!containsString(candidate.trainedSkills, role->skill))
			return false;
	}
	for (size_t i = 0; i < request.requiredGear.size(); ++i) {
		const GearRule* gear = gearRule(vocabulary, request.requiredGear[i]);
		if (gear == nullptr)
			return false;
		int pieces = 0;
		const String needle = gear->armorTemplateContains.toLowerCase();
		for (size_t j = 0; j < candidate.wornArmorTemplates.size(); ++j) {
			if (candidate.wornArmorTemplates[j].toLowerCase().contains(needle))
				++pieces;
		}
		if (pieces < gear->minPieces)
			return false;
	}
	for (size_t i = 0; i < request.requiredTiers.size(); ++i) {
		const TierRule* tier = tierRule(vocabulary, request.requiredTiers[i]);
		if (tier == nullptr || (tier->requireTemplateComplete &&
				!candidate.templateComplete))
			return false;
	}
	return true;
}

template<typename Rng>
inline std::vector<ResponderPick> chooseResponders(
		const std::vector<Candidate>& candidates, const LfbRequest& request,
		const ResponderPolicy& policy, Rng& rng) {
	struct RankedCandidate {
		const Candidate* candidate;
		uint64 tieBreaker;
	};
	std::vector<RankedCandidate> eligible;
	for (size_t i = 0; i < candidates.size(); ++i) {
		const Candidate& candidate = candidates[i];
		if (candidate.identityId == 0 || !candidate.availability.isFree() ||
				!candidateMatches(candidate, request, policy.vocabulary))
			continue;
		const int configuredChance = candidate.availabilityChancePercent >= 0 ?
			candidate.availabilityChancePercent : policy.availabilityChancePercent;
		const int chance = std::max(0, std::min(100, configuredChance));
		if (chance == 0)
			continue;
		if (chance < 100 && std::uniform_int_distribution<int>(0, 99)(rng) >= chance)
			continue;
		eligible.push_back({&candidate, 0});
	}

	for (size_t i = 0; i < eligible.size(); ++i)
		eligible[i].tieBreaker = std::uniform_int_distribution<uint64>(0,
				std::numeric_limits<uint64>::max())(rng);
	std::sort(eligible.begin(), eligible.end(), [&](const RankedCandidate& left,
			const RankedCandidate& right) {
		const bool leftSame = left.candidate->planet == policy.requesterPlanet;
		const bool rightSame = right.candidate->planet == policy.requesterPlanet;
		if (leftSame != rightSame)
			return leftSame;
		const float leftDistance = left.candidate->position.squaredDistanceTo2d(
				policy.requesterPosition);
		const float rightDistance = right.candidate->position.squaredDistanceTo2d(
				policy.requesterPosition);
		if (leftDistance != rightDistance)
			return leftDistance < rightDistance;
		return left.tieBreaker < right.tieBreaker;
	});

	int requested = request.requestedCount > 0 ? request.requestedCount :
			policy.defaultReplies;
	requested = std::min(requested, policy.maxRepliesCap);
	requested = std::max(0, requested - std::max(0, request.liveOffers));
	requested = std::min(requested, static_cast<int>(eligible.size()));
	std::vector<ResponderPick> result;
	const int delayMin = std::max(0, policy.replyDelayMinSeconds);
	const int delayMax = std::max(delayMin, policy.replyDelayMaxSeconds);
	for (int i = 0; i < requested; ++i) {
		ResponderPick pick;
		pick.identityId = eligible[i].candidate->identityId;
		pick.delayMs = std::uniform_int_distribution<int>(delayMin * 1000,
				delayMax * 1000)(rng);
		result.push_back(pick);
	}
	std::sort(result.begin(), result.end(), [](const ResponderPick& left,
			const ResponderPick& right) {
		if (left.delayMs != right.delayMs)
			return left.delayMs < right.delayMs;
		return left.identityId < right.identityId;
	});
	const int gapMs = std::max(0, policy.minReplyGapSeconds) * 1000;
	for (size_t i = 1; i < result.size(); ++i)
		result[i].delayMs = std::max(result[i].delayMs,
				result[i - 1].delayMs + gapMs);
	return result;
}

struct InviteOffer {
	uint64 identityId = 0;
	uint64 playerOid = 0;
	uint64 createdAtMs = 0;
	uint64 expiresAtMs = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

enum InviteDecision {
	ACCEPT = 0,
	DECLINE = 1,
	INVITE_ACCEPT = ACCEPT,
	INVITE_DECLINE = DECLINE
};

struct InviteVerdict {
	InviteDecision decision = INVITE_DECLINE;
	String reason = "no_offer";

	bool accepted() const { return decision == INVITE_ACCEPT; }

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline InviteVerdict judgeInvite(const InviteOffer& offer, uint64 inviterOid,
		uint64 nowMs, bool botGrouped, int groupedTotal, int leaderBotCount,
		const ResponderPolicy& policy) {
	InviteVerdict result;
	if (!policy.enabled)
		result.reason = "gate_off";
	else if (offer.identityId == 0 || offer.playerOid == 0)
		result.reason = "no_offer";
	else if (offer.playerOid != inviterOid)
		result.reason = "wrong_player";
	else if (offer.expiresAtMs != 0 && nowMs >= offer.expiresAtMs)
		result.reason = "expired";
	else if (botGrouped)
		result.reason = "already_grouped";
	else if (policy.maxGroupedBots >= 0 && groupedTotal >= policy.maxGroupedBots)
		result.reason = "global_cap";
	else if (policy.maxBotsPerLeader >= 0 &&
			leaderBotCount >= policy.maxBotsPerLeader)
		result.reason = "leader_cap";
	else {
		result.decision = INVITE_ACCEPT;
		result.reason = "accepted";
	}
	return result;
}

struct FollowPolicy {
	float followMinMeters = 4.f;
	float followMaxMeters = 10.f;
	float repathMoveMeters = 5.f;
	float routeDistanceMeters = 1500.f;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline uint64 followHash(uint64 identityId, int botIndex) {
	uint64 value = identityId + 0x9e3779b97f4a7c15ULL +
			static_cast<uint64>(botIndex + 1) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

inline Vector3 followSlot(uint64 identityId, const Vector3& leaderPos,
		float leaderHeading, int botIndex, const FollowPolicy& policy) {
	const float minRadius = std::max(0.f, policy.followMinMeters);
	const float maxRadius = std::max(minRadius, policy.followMaxMeters);
	const uint64 hash = followHash(identityId, botIndex);
	const float unit = static_cast<float>(hash % 1000000ULL) / 1000000.f;
	const float angleUnit = static_cast<float>((hash >> 20) % 1000000ULL) /
			1000000.f;
	const float radius = minRadius + (maxRadius - minRadius) * unit;
	const float offsetRadians = (-100.f + 200.f * angleUnit) *
			0.017453292519943295f;
	const float angle = leaderHeading + 3.14159265358979323846f + offsetRadians;
	return Vector3(leaderPos.getX() + std::cos(angle) * radius,
			leaderPos.getY() + std::sin(angle) * radius, leaderPos.getZ());
}

inline bool needsRepath(const Vector3& botPos, const Vector3& slotPos,
		const Vector3& lastPathTarget, const FollowPolicy& policy) {
	const float maxDistance = std::max(0.f, policy.followMaxMeters);
	const float targetMove = std::max(0.f, policy.repathMoveMeters);
	return botPos.distanceTo2d(slotPos) > maxDistance ||
			lastPathTarget.distanceTo2d(slotPos) > targetMove;
}

enum TravelMode {
	HOLD = 0,
	WALK,
	WAIT_OUTSIDE,
	ROUTE,
	TRAVEL_HOLD = HOLD,
	TRAVEL_WALK = WALK,
	TRAVEL_WAIT_OUTSIDE = WAIT_OUTSIDE,
	TRAVEL_ROUTE = ROUTE
};

inline TravelMode travelMode(bool sameZone, float distance,
		bool leaderIndoors, const FollowPolicy& policy) {
	if (!sameZone)
		return TRAVEL_ROUTE;
	if (leaderIndoors)
		return TRAVEL_WAIT_OUTSIDE;
	if (distance > std::max(0.f, policy.routeDistanceMeters))
		return TRAVEL_ROUTE;
	if (distance <= std::max(0.f, policy.followMaxMeters))
		return TRAVEL_HOLD;
	return TRAVEL_WALK;
}

struct NamedBot {
	uint64 identityId = 0;
	String firstName;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

enum GroupCommandType {
	NONE = 0,
	STATUS,
	LEAVE,
	GROUP_COMMAND_NONE = NONE,
	GROUP_COMMAND_STATUS = STATUS,
	GROUP_COMMAND_LEAVE = LEAVE
};

struct GroupCommand {
	GroupCommandType command = GROUP_COMMAND_NONE;
	bool all = true;
	std::vector<uint64> targetIdentityIds;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline GroupCommand parseGroupCommand(const String& text,
		const std::vector<NamedBot>& botFirstNames,
		const Vocabulary& vocabulary) {
	GroupCommand result;
	const std::vector<String> tokens = tokenize(text);
	String matched;
	if (containsKeyword(tokens, vocabulary.leave, &matched))
		result.command = GROUP_COMMAND_LEAVE;
	else if (containsKeyword(tokens, vocabulary.status, &matched))
		result.command = GROUP_COMMAND_STATUS;
	if (result.command == GROUP_COMMAND_NONE)
		return result;

	for (size_t i = 0; i < botFirstNames.size(); ++i) {
		const std::vector<String> nameTokens = keywordTokens(
				botFirstNames[i].firstName);
		if (nameTokens.empty() || nameTokens.size() > tokens.size())
			continue;
		bool found = false;
		for (size_t start = 0; start + nameTokens.size() <= tokens.size() &&
				!found; ++start) {
			bool equal = true;
			for (size_t j = 0; j < nameTokens.size(); ++j) {
				if (tokens[start + j] != nameTokens[j]) {
					equal = false;
					break;
				}
			}
			found = equal;
		}
		if (found) {
			bool duplicate = false;
			for (size_t j = 0; j < result.targetIdentityIds.size(); ++j)
				duplicate |= result.targetIdentityIds[j] == botFirstNames[i].identityId;
			if (!duplicate)
				result.targetIdentityIds.push_back(botFirstNames[i].identityId);
		}
	}
	result.all = result.targetIdentityIds.empty();
	return result;
}

struct AssistVerdict {
	bool assist = false;
	String reason = "neutral";

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline AssistVerdict judgeAssist(bool attackerIsPlayer,
		bool attackerIsSimBot, int attackerFaction) {
	if (attackerIsPlayer)
		return {false, "player"};
	if (attackerIsSimBot)
		return {false, "sim_bot"};
	if (attackerFaction != 0)
		return {false, "faction_fight"};
	return {true, "neutral"};
}

struct ReleaseInputs {
	bool botInGroup = true;
	bool groupDisbanded = false;
	bool recordedLeaderInGroup = true;
	bool recordedLeaderIsLeader = true;
	bool playerMemberPresent = true;
	bool leaderOnline = true;
	bool deadRejoin = false;
	bool leaveCommand = false;
	bool gateEnabled = true;
	bool retired = false;
	uint64 leaderOfflineSinceMs = 0;
	uint64 nowMs = 0;
	int leaderOfflineGraceSeconds = 120;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

struct ReleaseVerdict {
	bool release = false;
	String reason = "keep";

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline ReleaseVerdict judgeRelease(const ReleaseInputs& inputs) {
	if (!inputs.gateEnabled)
		return {true, "gate_off"};
	if (inputs.retired)
		return {true, "retired"};
	if (inputs.leaveCommand)
		return {true, "leave_command"};
	// Membership first: a kicked bot's own group pointer is gone, so the leader
	// fields describe the recorded group, not one the bot is in. A surviving
	// recorded group means the bot was removed (kicked); a dissolved one means
	// the group ended (disbanded).
	if (!inputs.botInGroup && !inputs.deadRejoin)
		return {true, inputs.groupDisbanded ? "disbanded" : "kicked"};
	if (inputs.groupDisbanded && !inputs.deadRejoin)
		return {true, "disbanded"};
	if (!inputs.deadRejoin &&
			(!inputs.recordedLeaderInGroup || !inputs.recordedLeaderIsLeader))
		return {true, "leader_left"};
	if (!inputs.playerMemberPresent && !inputs.deadRejoin)
		return {true, "no_players"};
	if (!inputs.leaderOnline && inputs.leaderOfflineSinceMs != 0 &&
			inputs.nowMs >= inputs.leaderOfflineSinceMs +
				static_cast<uint64>(std::max(0, inputs.leaderOfflineGraceSeconds)) * 1000)
		return {true, "leader_offline"};
	return {false, "keep"};
}

enum RejoinDecision {
	REJOIN = 0,
	WAIT = 1,
	RELEASE = 2,
	REJOIN_WAIT = WAIT,
	REJOIN_RELEASE = RELEASE
};

struct RejoinVerdict {
	RejoinDecision decision = REJOIN_WAIT;
	String reason = "leader_offline";

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

inline RejoinVerdict judgeRejoin(bool leaderOnline, bool leaderHasGroup,
		bool leaderIsLeader) {
	if (!leaderOnline)
		return {REJOIN_WAIT, "leader_offline"};
	if (!leaderHasGroup || leaderIsLeader)
		return {REJOIN, "rejoin"};
	return {REJOIN_RELEASE, "leader_regrouped"};
}

} // namespace SimBotGrouping

#endif // SIMBOTGROUPING_H_
