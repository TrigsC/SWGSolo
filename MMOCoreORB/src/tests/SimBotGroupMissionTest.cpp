#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotGroupMission.h"

namespace {

using namespace SimBotGroupMission;

TEST(SimBotGroupMissionTest, ParsesGrabCountCreatureDirectionAndAddress) {
	MissionVocabulary vocabulary;
	vocabulary.maxGrabCount = 5;
	MissionRequest request = parseMissionRequest(
		"Paki grab 9 kreetle missions to the SW", vocabulary, {"paki"},
		{"kreetle"});
	EXPECT_EQ(request.command, MISSION_GRAB);
	EXPECT_EQ(request.count, 5);
	EXPECT_EQ(request.creatureWord, "kreetle");
	EXPECT_TRUE(request.hasBearing);
	EXPECT_EQ(request.bearingDeg, 225);
	EXPECT_EQ(request.addressedBot, "paki");
	EXPECT_FALSE(request.unknownCreature);
}

TEST(SimBotGroupMissionTest, ParsesRunSkipAndUnknownCreature) {
	MissionVocabulary vocabulary;
	MissionRequest run = parseMissionRequest("next mission", vocabulary);
	EXPECT_EQ(run.command, MISSION_RUN);

	MissionRequest skip = parseMissionRequest("skip this one", vocabulary);
	EXPECT_EQ(skip.command, MISSION_SKIP);

	MissionRequest unknown = parseMissionRequest("grab a bantha mission",
		vocabulary, {}, {"kreetle"});
	EXPECT_EQ(unknown.command, MISSION_GRAB);
	EXPECT_EQ(unknown.creatureWord, "bantha");
	EXPECT_TRUE(unknown.unknownCreature);
}

TEST(SimBotGroupMissionTest, CreatureMatchingIsPluralTolerant) {
	EXPECT_TRUE(lairMatchesCreature("kreetle_lair",
		{"Kreetles"}, "kreetle"));
	EXPECT_TRUE(lairMatchesCreature("object/mobile/nightsister_lair.iff",
		{"Nightsister"}, "nightsisters"));
	EXPECT_FALSE(lairMatchesCreature("kreetle_lair", {"gackle_bat"},
		"bantha"));
}

TEST(SimBotGroupMissionTest, BearingWrapsAtNorthAndUsesTolerance) {
	EXPECT_TRUE(bearingWithin(350, 10, 20));
	EXPECT_TRUE(bearingWithin(45, 0));
	EXPECT_FALSE(bearingWithin(90, 0));
}

TEST(SimBotGroupMissionTest, AssignsAddressedBotThenNearestCapacity) {
	std::vector<GrabCandidate> candidates = {
		{7, "far", 100.f, 2},
		{9, "paki", 500.f, 1},
		{11, "near", 10.f, 2}};
	GrabAssignment assignment = assignGrab(10, candidates, "PAKI");
	ASSERT_EQ(assignment.assignments.size(), 3U);
	EXPECT_EQ(assignment.assignments[0].identityId, 9U);
	EXPECT_EQ(assignment.assignments[0].count, 1);
	EXPECT_EQ(assignment.assignments[1].identityId, 11U);
	EXPECT_EQ(assignment.assignments[1].count, 2);
	EXPECT_EQ(assignment.assignments[2].identityId, 7U);
	EXPECT_EQ(assignment.assignments[2].count, 2);
	EXPECT_EQ(assignment.unassigned, 5);
}

TEST(SimBotGroupMissionTest, PicksNearestSamePlanetAndKeepsTieOrder) {
	std::vector<MissionEntryView> entries = {
		{1, 10, Vector3(10.f, 0.f, 0.f), false, false},
		{2, 10, Vector3(-10.f, 0.f, 0.f), false, false},
		{3, 11, Vector3(0.f, 0.f, 0.f), false, false},
		{4, 10, Vector3(0.f, 0.f, 0.f), true, false}};
	EXPECT_EQ(pickNextMission(entries, 10, Vector3(0.f, 0.f, 0.f)), 0);
	EXPECT_EQ(pickNextMission(entries, 12, Vector3()), -1);
}

TEST(SimBotGroupMissionTest, SplitsUsingMemberCountAndDropsOutOfRange) {
	std::vector<SplitMember> members = {
		{true, 100, true}, {false, 200, true}, {false, 300, false}};
	RewardSplit split = splitReward(100, 1, members);
	EXPECT_EQ(split.divisor, 3);
	EXPECT_EQ(split.share, 33);
	EXPECT_EQ(split.paid.size(), 2U);
	EXPECT_EQ(split.outOfRange.size(), 1U);
}

TEST(SimBotGroupMissionTest, ProducesCompassAndRoundedDistance) {
	Vector3 origin(0.f, 0.f, 0.f);
	EXPECT_EQ(compassWord(origin, Vector3(10.f, 10.f, 0.f)), "NE");
	EXPECT_EQ(compassWord(origin, Vector3(-10.f, 0.f, 0.f)), "W");
	EXPECT_EQ(roundedMeters(origin, Vector3(3.f, 4.f, 0.f)), 5);
}

} // namespace

// Owner phrase from the 2026-10-22 in-game test.
TEST(SimBotGroupMissionTest, ParsesOwnerWestPhrase) {
	SimBotGroupMission::MissionVocabulary vocabulary;
	SimBotGroupMission::MissionRequest request =
		SimBotGroupMission::parseMissionRequest(
			"Grab 2 Bol missions to the West", vocabulary, {"rocla"}, {"bol"});
	EXPECT_EQ(request.command, SimBotGroupMission::MISSION_GRAB);
	EXPECT_EQ(request.count, 2);
	EXPECT_EQ(request.creatureWord, "bol");
	EXPECT_TRUE(request.hasBearing);
	EXPECT_EQ(request.bearingDeg, 270);
	SimBotGroupMission::MissionRequest lairs =
		SimBotGroupMission::parseMissionRequest(
			"Grab 2 Bol lairs to the South East", vocabulary, {}, {"bol"});
	EXPECT_EQ(lairs.command, SimBotGroupMission::MISSION_GRAB);
	EXPECT_TRUE(lairs.hasBearing);
	EXPECT_EQ(lairs.bearingDeg, 135);
}
