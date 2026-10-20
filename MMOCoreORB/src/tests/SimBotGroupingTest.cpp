#include "gtest/gtest.h"

#include <random>

#include "server/zone/objects/creature/simplayer/SimBotGrouping.h"

namespace {

using namespace SimBotGrouping;

Vocabulary vocabulary() {
	Vocabulary result;
	result.requestPhrases = {"lfb", "looking for", "lf", "need", "want",
		"anyone"};
	result.botWords = {"bot", "bots"};
	RoleRule tkm;
	tkm.name = "tkm";
	tkm.keywords = {"tkm", "teras kasi"};
	tkm.skill = "combat_unarmed_master";
	result.roles.push_back(tkm);
	GearRule composite;
	composite.name = "composite";
	composite.keywords = {"composite"};
	composite.armorTemplateContains = "armor_composite";
	composite.minPieces = 9;
	result.gear.push_back(composite);
	TierRule dwb;
	dwb.name = "dwb";
	dwb.keywords = {"dwb", "death watch bunker"};
	dwb.requireTemplateComplete = true;
	result.tiers.push_back(dwb);
	result.status = {"status", "where", "where are you", "eta"};
	result.leave = {"leave", "disband bots", "bye"};
	return result;
}

Candidate candidate(uint64 id, const char* planet, float x) {
	Candidate result;
	result.identityId = id;
	result.planet = planet;
	result.position = Vector3(x, 0.f, 0.f);
	result.availability = {FREE, "free"};
	return result;
}

TEST(SimBotGrouping, ParsesRequestsFiltersAndNumberWords) {
	Vocabulary vocab = vocabulary();
	LfbRequest request = parseLfbRequest(
			"LF 3 TKM bots with composite for DWB!", vocab);
	ASSERT_TRUE(request.isRequest);
	EXPECT_EQ(request.requestedCount, 3);
	ASSERT_EQ(request.requiredRoles.size(), 1U);
	EXPECT_EQ(request.requiredRoles[0], "tkm");
	ASSERT_EQ(request.requiredGear.size(), 1U);
	EXPECT_EQ(request.requiredGear[0], "composite");
	ASSERT_EQ(request.requiredTiers.size(), 1U);
	EXPECT_EQ(request.requiredTiers[0], "dwb");

	request = parseLfbRequest("looking for two bots", vocab);
	EXPECT_TRUE(request.isRequest);
	EXPECT_EQ(request.requestedCount, 2);

	request = parseLfbRequest("I am looking for a healer", vocab);
	EXPECT_FALSE(request.isRequest);
}

TEST(SimBotGrouping, AvailabilityCoversEveryHunterPhase) {
	const int freePhases[] = {PHASE_IDLE_HOME, PHASE_ANNOUNCE_JOB,
		PHASE_BUFF_UP, PHASE_TRAVEL_TO_TERMINAL, PHASE_BAZAAR_VISIT,
		PHASE_TRAVEL_HOME, PHASE_DELIVER, PHASE_MISSION_CLEANUP,
		PHASE_CLONE_HOME, PHASE_DONE};
	for (size_t i = 0; i < sizeof(freePhases) / sizeof(freePhases[0]); ++i)
		EXPECT_EQ(classifyAvailability(freePhases[i], EARLY_SELECT_TARGET, false,
				false, false, false).cls, FREE);

	const int busyPhases[] = {PHASE_RELOCATING, PHASE_BUFF_TRIP,
		PHASE_TRAVEL_OUT, PHASE_ACCEPT_MISSION, PHASE_TRAVEL_TO_MISSION,
		PHASE_TRAVEL_TO_LAIR, PHASE_ENGAGING_LAIR, PHASE_AWAITING_WORLD,
		PHASE_HUNTING, PHASE_RETREATING, PHASE_HEALING};
	for (size_t i = 0; i < sizeof(busyPhases) / sizeof(busyPhases[0]); ++i)
		EXPECT_EQ(classifyAvailability(busyPhases[i], EARLY_SELECT_TARGET, false,
				false, false, false).cls, BUSY);

	EXPECT_EQ(classifyAvailability(PHASE_IDLE_HOME, EARLY_ENGAGE, false, false,
			false, false).cls, BUSY);
	EXPECT_EQ(classifyAvailability(PHASE_IDLE_HOME, EARLY_SELECT_TARGET, true,
			false, false, false).reason, "in_combat");
	EXPECT_EQ(classifyAvailability(PHASE_IDLE_HOME, EARLY_SELECT_TARGET, false,
			true, false, false).reason, "dead");
}

TEST(SimBotGrouping, GrindingNoviceIsFreeBetweenFights) {
	// The early game runs under HUNTING; only an engage holding a target or
	// combat makes the novice busy.
	EXPECT_TRUE(classifyAvailability(PHASE_HUNTING, EARLY_SELECT_TARGET, false,
		false, false, false, false, true).isFree());
	EXPECT_FALSE(classifyAvailability(PHASE_HUNTING, EARLY_ENGAGE, false,
		false, false, false, true, true).isFree());
	EXPECT_FALSE(classifyAvailability(PHASE_HUNTING, EARLY_RECOVER, true,
		false, false, false, false, true).isFree());
	EXPECT_FALSE(classifyAvailability(PHASE_HUNTING, EARLY_LEAVE_CITY, false,
		false, false, false, false, false).isFree());
	AvailabilitySnapshot snapshot;
	snapshot.phaseCode = PHASE_HUNTING;
	snapshot.earlyGameActive = true;
	EXPECT_TRUE(AvailabilitySnapshot::unpack(snapshot.pack()).earlyGameActive);
	EXPECT_TRUE(AvailabilitySnapshot::unpack(snapshot.pack()).classify().isFree());
}

TEST(SimBotGrouping, AvailabilitySnapshotRoundTripsPackedFlags) {
	AvailabilitySnapshot original;
	original.phaseCode = PHASE_TRAVEL_TO_LAIR;
	original.earlyGamePhaseCode = EARLY_ENGAGE;
	original.inCombat = true;
	original.dead = false;
	original.interplanetaryTravelActive = true;
	original.lootStepActive = true;
	original.earlyGameTargetHeld = true;
	AvailabilitySnapshot unpacked = AvailabilitySnapshot::unpack(original.pack());
	EXPECT_EQ(unpacked.phaseCode, original.phaseCode);
	EXPECT_EQ(unpacked.earlyGamePhaseCode, original.earlyGamePhaseCode);
	EXPECT_EQ(unpacked.inCombat, original.inCombat);
	EXPECT_EQ(unpacked.dead, original.dead);
	EXPECT_EQ(unpacked.interplanetaryTravelActive,
			original.interplanetaryTravelActive);
	EXPECT_EQ(unpacked.lootStepActive, original.lootStepActive);
	EXPECT_EQ(unpacked.earlyGameTargetHeld, original.earlyGameTargetHeld);
}

TEST(SimBotGrouping, ChoosesMatchingRespondersWithRankingAndSpacing) {
	LfbRequest request = parseLfbRequest("LF TKM bot", vocabulary());
	request.requesterPlanet = "tatooine";
	request.requesterPosition = Vector3(0.f, 0.f, 0.f);
	std::vector<Candidate> candidates;
	Candidate near = candidate(1, "tatooine", 10.f);
	near.trainedSkills.push_back("combat_unarmed_master");
	Candidate far = candidate(2, "tatooine", 100.f);
	far.trainedSkills.push_back("combat_unarmed_master");
	Candidate remote = candidate(3, "naboo", 1.f);
	remote.trainedSkills.push_back("combat_unarmed_master");
	candidates.push_back(near);
	candidates.push_back(far);
	candidates.push_back(remote);

	ResponderPolicy policy;
	policy.vocabulary = vocabulary();
	policy.defaultReplies = 3;
	policy.maxRepliesCap = 2;
	policy.availabilityChancePercent = 100;
	policy.replyDelayMinSeconds = 3;
	policy.replyDelayMaxSeconds = 3;
	policy.minReplyGapSeconds = 2;
	policy.requesterPlanet = request.requesterPlanet;
	policy.requesterPosition = request.requesterPosition;
	std::mt19937 rng(7);
	std::vector<ResponderPick> picks = chooseResponders(candidates, request,
			policy, rng);
	ASSERT_EQ(picks.size(), 2U);
	EXPECT_EQ(picks[0].identityId, 1U);
	EXPECT_GE(picks[1].delayMs - picks[0].delayMs, 2000);
}

TEST(SimBotGrouping, AppliesRoleGearAndTierFilters) {
	LfbRequest request = parseLfbRequest("LF TKM bot with composite DWB",
			vocabulary());
	Candidate c = candidate(1, "tatooine", 0.f);
	c.trainedSkills.push_back("combat_unarmed_master");
	c.templateComplete = true;
	for (int i = 0; i < 9; ++i)
		c.wornArmorTemplates.push_back("object/armor/armor_composite_" +
				String::valueOf(i));
	ResponderPolicy policy;
	policy.vocabulary = vocabulary();
	EXPECT_TRUE(candidateMatches(c, request, policy.vocabulary));
	c.templateComplete = false;
	EXPECT_FALSE(candidateMatches(c, request, policy.vocabulary));
}

TEST(SimBotGrouping, JudgesInviteVerdicts) {
	ResponderPolicy policy;
	InviteOffer offer;
	offer.identityId = 1;
	offer.playerOid = 20;
	offer.expiresAtMs = 100;

	InviteOffer emptyOffer;
	EXPECT_EQ(judgeInvite(emptyOffer, 20, 50, false, 0, 0, policy).reason,
			"no_offer");
	EXPECT_EQ(judgeInvite(offer, 20, 50, false, 0, 0, policy).decision,
			INVITE_ACCEPT);
	EXPECT_EQ(judgeInvite(offer, 21, 50, false, 0, 0, policy).reason,
			"wrong_player");
	EXPECT_EQ(judgeInvite(offer, 20, 100, false, 0, 0, policy).reason,
			"expired");
	EXPECT_EQ(judgeInvite(offer, 20, 50, true, 0, 0, policy).reason,
			"already_grouped");
	policy.maxGroupedBots = 1;
	EXPECT_EQ(judgeInvite(offer, 20, 50, false, 1, 0, policy).reason,
			"global_cap");
	policy.maxGroupedBots = 10;
	policy.maxBotsPerLeader = 1;
	EXPECT_EQ(judgeInvite(offer, 20, 50, false, 0, 1, policy).reason,
			"leader_cap");
	policy.enabled = false;
	EXPECT_EQ(judgeInvite(offer, 20, 50, false, 0, 0, policy).reason,
			"gate_off");
}

TEST(SimBotGrouping, FollowSlotsAreStableSpreadAndRepathBounded) {
	FollowPolicy policy;
	Vector3 leader(100.f, 200.f, 5.f);
	Vector3 first = followSlot(42, leader, 0.f, 0, policy);
	EXPECT_EQ(first.getX(), followSlot(42, leader, 0.f, 0, policy).getX());
	EXPECT_NE(first.getX(), followSlot(43, leader, 0.f, 0, policy).getX());
	EXPECT_TRUE(needsRepath(leader, first, Vector3(), policy));
	EXPECT_FALSE(needsRepath(first, first, first, policy));
}

TEST(SimBotGrouping, SelectsTravelMode) {
	FollowPolicy policy;
	EXPECT_EQ(travelMode(true, 5.f, false, policy), TRAVEL_HOLD);
	EXPECT_EQ(travelMode(true, 50.f, false, policy), TRAVEL_WALK);
	EXPECT_EQ(travelMode(true, 50.f, true, policy), TRAVEL_WAIT_OUTSIDE);
	EXPECT_EQ(travelMode(false, 5.f, false, policy), TRAVEL_ROUTE);
	EXPECT_EQ(travelMode(true, 1501.f, false, policy), TRAVEL_ROUTE);
}

TEST(SimBotGrouping, ParsesGroupCommandsAndNames) {
	Vocabulary vocab = vocabulary();
	std::vector<NamedBot> names{{1, "Paki"}, {2, "Mira"}};
	GroupCommand command = parseGroupCommand("Paki leave", names, vocab);
	EXPECT_EQ(command.command, GROUP_COMMAND_LEAVE);
	EXPECT_FALSE(command.all);
	ASSERT_EQ(command.targetIdentityIds.size(), 1U);
	EXPECT_EQ(command.targetIdentityIds[0], 1U);

	command = parseGroupCommand("status", names, vocab);
	EXPECT_EQ(command.command, GROUP_COMMAND_STATUS);
	EXPECT_TRUE(command.all);
}

TEST(SimBotGrouping, JudgesAssistRules) {
	EXPECT_EQ(judgeAssist(true, false, 0).reason, "player");
	EXPECT_EQ(judgeAssist(false, true, 0).reason, "sim_bot");
	EXPECT_EQ(judgeAssist(false, false, 2).reason, "faction_fight");
	EXPECT_TRUE(judgeAssist(false, false, 0).assist);
}

TEST(SimBotGrouping, JudgesReleaseAndDeathRejoinExceptions) {
	ReleaseInputs inputs;
	inputs.botInGroup = false;
	EXPECT_EQ(judgeRelease(inputs).reason, "kicked");
	// Membership is judged before the leader fields: a kicked bot no longer
	// sees the leader, which must not read as leader_left.
	inputs.recordedLeaderInGroup = false;
	EXPECT_EQ(judgeRelease(inputs).reason, "kicked");
	inputs.groupDisbanded = true;
	EXPECT_EQ(judgeRelease(inputs).reason, "disbanded");
	inputs.groupDisbanded = false;
	inputs.recordedLeaderInGroup = true;
	inputs.botInGroup = true;
	inputs.groupDisbanded = true;
	EXPECT_EQ(judgeRelease(inputs).reason, "disbanded");
	inputs.groupDisbanded = false;
	inputs.recordedLeaderInGroup = false;
	EXPECT_EQ(judgeRelease(inputs).reason, "leader_left");
	inputs.recordedLeaderInGroup = true;
	inputs.recordedLeaderIsLeader = false;
	EXPECT_EQ(judgeRelease(inputs).reason, "leader_left");
	inputs.recordedLeaderIsLeader = true;
	inputs.playerMemberPresent = false;
	EXPECT_EQ(judgeRelease(inputs).reason, "no_players");
	inputs.playerMemberPresent = true;
	inputs.leaveCommand = true;
	EXPECT_EQ(judgeRelease(inputs).reason, "leave_command");
	inputs.leaveCommand = false;
	inputs.retired = true;
	EXPECT_EQ(judgeRelease(inputs).reason, "retired");
	inputs.retired = false;
	EXPECT_EQ(judgeRelease(inputs).release, false);
	inputs.deadRejoin = true;
	inputs.botInGroup = false;
	inputs.groupDisbanded = true;
	EXPECT_FALSE(judgeRelease(inputs).release);
	inputs.playerMemberPresent = false;
	EXPECT_FALSE(judgeRelease(inputs).release);
	inputs.leaderOnline = false;
	inputs.leaderOfflineSinceMs = 1000;
	inputs.nowMs = 121001;
	EXPECT_EQ(judgeRelease(inputs).reason, "leader_offline");

	inputs.gateEnabled = false;
	EXPECT_EQ(judgeRelease(inputs).reason, "gate_off");
}

TEST(SimBotGrouping, JudgesRejoin) {
	EXPECT_EQ(judgeRejoin(false, true, true).decision, REJOIN_WAIT);
	EXPECT_EQ(judgeRejoin(true, false, false).decision, REJOIN);
	EXPECT_EQ(judgeRejoin(true, true, true).decision, REJOIN);
	EXPECT_EQ(judgeRejoin(true, true, false).reason, "leader_regrouped");
}

} // namespace
