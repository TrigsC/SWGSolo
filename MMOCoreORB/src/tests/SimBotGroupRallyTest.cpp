#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotGroupRally.h"

namespace {

using namespace SimBotGroupRally;

RallyVocabulary vocabulary() {
	RallyVocabulary result;
	result.buff = {"buff", "buffs", "buffed", "rebuff"};
	result.meet = {"meet", "meet up", "group up", "rally", "regroup",
		"gather", "head to", "go to"};
	result.landmarks = {
		{"cantina", {"cantina"}},
		{"starport", {"starport", "spaceport"}},
		{"shuttleport", {"shuttleport", "shuttle"}},
		{"medcenter", {"medcenter", "med center", "hospital"}},
		{"leader", {"me", "leader", "my location", "here"}}
	};
	PlaceRule mosEisley;
	mosEisley.key = "mos_eisley";
	mosEisley.planet = "tatooine";
	mosEisley.city = "mos_eisley";
	mosEisley.aliases = {"mos eisley", "eisley"};
	result.places.push_back(mosEisley);
	PlaceRule miningOutpost;
	miningOutpost.key = "mining_outpost";
	miningOutpost.planet = "dantooine";
	miningOutpost.city = "mining_outpost";
	miningOutpost.aliases = {"mining outpost", "dantooine mining outpost",
		"dantooine"};
	result.places.push_back(miningOutpost);
	return result;
}

TEST(SimBotGroupRallyTest, ParsesDirectivePlacesAndFiller) {
	DirectiveText directive = parseDirective(
		"Let's get buffs and group up out front of the Mos Eisley cantina",
		vocabulary());
	EXPECT_TRUE(directive.wantBuffs);
	EXPECT_TRUE(directive.hasMeet);
	EXPECT_EQ(directive.placeKey, "mos_eisley");
	EXPECT_EQ(directive.landmark, "cantina");
	EXPECT_FALSE(directive.unknownPlaceWords);

	directive = parseDirective("get buffs", vocabulary());
	EXPECT_TRUE(directive.wantBuffs);
	EXPECT_FALSE(directive.hasMeet);
	EXPECT_EQ(directive.landmark, "leader");
}

TEST(SimBotGroupRallyTest, LongestAliasBeatsShortAliasAndUnknownIsReported) {
	RallyVocabulary configured = vocabulary();
	PlaceRule shortAlias;
	shortAlias.key = "eisley_short";
	shortAlias.planet = "tatooine";
	shortAlias.aliases = {"eisley"};
	configured.places.push_back(shortAlias);
	DirectiveText directive = parseDirective("meet at Mos Eisley", configured);
	EXPECT_EQ(directive.placeKey, "mos_eisley");

	directive = parseDirective("meet at Narnia", configured);
	EXPECT_TRUE(directive.hasMeet);
	EXPECT_TRUE(directive.unknownPlaceWords);
	EXPECT_TRUE(directive.placeKey.isEmpty());

	// A meet phrase with no place is "back to the leader", not unknown.
	directive = parseDirective("lets get buffs and group up", configured);
	EXPECT_FALSE(directive.unknownPlaceWords);
	EXPECT_EQ(directive.landmark, "leader");
	directive = parseDirective("regroup", configured);
	EXPECT_FALSE(directive.unknownPlaceWords);
	EXPECT_EQ(directive.landmark, "leader");

	// "me" is filler before an unknown place, which stays unknown.
	directive = parseDirective("meet me at narnia", configured);
	EXPECT_TRUE(directive.unknownPlaceWords);
	directive = parseDirective("get buffs and meet me", configured);
	EXPECT_FALSE(directive.unknownPlaceWords);
	EXPECT_EQ(directive.landmark, "leader");
	directive = parseDirective("meet here", configured);
	EXPECT_FALSE(directive.unknownPlaceWords);
	EXPECT_EQ(directive.landmark, "leader");
	directive = parseDirective("regroup paki", configured, {"paki"});
	EXPECT_FALSE(directive.unknownPlaceWords);
	EXPECT_EQ(directive.landmark, "leader");
	// Chained multi-word meet phrases are known words, not a place.
	directive = parseDirective("meet me and group up", configured);
	EXPECT_FALSE(directive.unknownPlaceWords);
	EXPECT_EQ(directive.landmark, "leader");
	directive = parseDirective("rally, get buffed and meet up", configured);
	EXPECT_FALSE(directive.unknownPlaceWords);
	EXPECT_TRUE(directive.wantBuffs);

	// A place alias beats the leader word.
	directive = parseDirective("meet me at the mining outpost", configured);
	EXPECT_EQ(directive.placeKey, "mining_outpost");
	EXPECT_TRUE(directive.landmark.isEmpty());
}

TEST(SimBotGroupRallyTest, LeaderLinePrecedenceKeepsNameTargets) {
	SimBotGrouping::Vocabulary grouping;
	grouping.status = {"status", "where"};
	grouping.follow = {"follow", "follow me", "on me"};
	grouping.ready = {"ready", "ready up"};
	grouping.leave = {"leave", "bye"};
	std::vector<SimBotGrouping::NamedBot> names{{17, "Paki"}};

	LeaderLine line = parseLeaderLine("Paki get buffs and meet at Mos Eisley",
		names, grouping, vocabulary());
	EXPECT_TRUE(line.isDirective);
	EXPECT_EQ(line.command.command, SimBotGrouping::GROUP_COMMAND_NONE);
	ASSERT_EQ(line.command.targetIdentityIds.size(), 1U);
	EXPECT_EQ(line.command.targetIdentityIds[0], 17U);

	line = parseLeaderLine("follow me to Mos Eisley", names, grouping,
		vocabulary());
	EXPECT_FALSE(line.isDirective);
	EXPECT_EQ(line.command.command, SimBotGrouping::GROUP_COMMAND_FOLLOW);

	line = parseLeaderLine("leave and meet at Mos Eisley", names, grouping,
		vocabulary());
	EXPECT_FALSE(line.isDirective);
	EXPECT_EQ(line.command.command, SimBotGrouping::GROUP_COMMAND_LEAVE);
}

BuffCityCandidate hub(const char* city, int botToHub, int hubToMeet,
		const char* planet) {
	BuffCityCandidate result;
	result.city = city;
	result.planet = planet;
	result.legsBotToHub = botToHub;
	result.legsHubToMeet = hubToMeet;
	return result;
}

TEST(SimBotGroupRallyTest, ChoosesMeetCurrentThenShortestReachableHub) {
	BuffCityInputs inputs;
	inputs.meetCity = "mos_eisley";
	inputs.botCity = "bestine";
	inputs.meetPlanet = "tatooine";
	inputs.botPlanet = "tatooine";
	inputs.candidates = {
		hub("mos_eisley", 4, 3, "tatooine"),
		hub("bestine", 2, 8, "tatooine"),
		hub("coronet", 1, 1, "corellia")};

	BuffCityChoice choice = chooseBuffCity(inputs);
	EXPECT_EQ(choice.index, 0);
	EXPECT_EQ(choice.reason, "meet_city");

	// A meet city without buffers falls to the bot's current buffer city.
	inputs.meetCity = "keren";
	choice = chooseBuffCity(inputs);
	EXPECT_EQ(choice.index, 1);
	EXPECT_EQ(choice.reason, "current_city");

	// Neither city has buffers: fewest total legs wins.
	inputs.meetCity = "theed";
	inputs.botCity = "tyrena";
	choice = chooseBuffCity(inputs);
	EXPECT_EQ(choice.index, 2);
	EXPECT_EQ(choice.reason, "on_the_way");
}

TEST(SimBotGroupRallyTest, BuffCityTieBreaksByMeetPlanetAndSkipsUnreachable) {
	BuffCityInputs inputs;
	inputs.meetPlanet = "naboo";
	inputs.botPlanet = "tatooine";
	inputs.candidates = {
		hub("unreachable", -1, 2, "naboo"),
		hub("bot_hub", 1, 1, "tatooine"),
		hub("meet_hub", 1, 1, "naboo")};
	BuffCityChoice choice = chooseBuffCity(inputs);
	EXPECT_EQ(choice.index, 2);
	EXPECT_EQ(choice.reason, "on_the_way");

	inputs.candidates[1].legsBotToHub = -1;
	inputs.candidates[2].legsBotToHub = -1;
	choice = chooseBuffCity(inputs);
	EXPECT_EQ(choice.index, -1);
	EXPECT_EQ(choice.reason, "fallback");
}

TEST(SimBotGroupRallyTest, StagesAdvanceInDirectiveOrder) {
	EXPECT_EQ(firstStage(true, true, true), BUFFING);
	EXPECT_EQ(firstStage(true, false, true), BUFF_TRAVEL);
	EXPECT_EQ(firstStage(true, false, false), RALLY_TRAVEL);
	EXPECT_EQ(nextStage(BUFF_TRAVEL, ARRIVAL), BUFFING);
	EXPECT_EQ(nextStage(BUFFING, BUFF_DONE), RALLY_TRAVEL);
	EXPECT_EQ(nextStage(RALLY_TRAVEL, RALLY_ARRIVAL), RALLY_HOLD);
	EXPECT_EQ(nextStage(RALLY_HOLD, RELEASED), DONE);
}

TEST(SimBotGroupRallyTest, HoldReleaseHonoursArrivalFollowAndOptionalTimeout) {
	EXPECT_EQ(judgeHoldRelease(true, 29.f, 30.f, false, 1000, 2000, 0).reason,
		"leader_arrived");
	EXPECT_EQ(judgeHoldRelease(false, 0.f, 30.f, true, 1000, 2000, 0).reason,
		"follow_command");
	EXPECT_FALSE(judgeHoldRelease(false, 0.f, 30.f, false, 1000, 59000, 0).release);
	EXPECT_EQ(judgeHoldRelease(false, 0.f, 30.f, false, 1000, 61000, 1).reason,
		"hold_timeout");
}

TEST(SimBotGroupRallyTest, ShuttleRequiresSavingsAndHonoursSuppression) {
	EXPECT_EQ(samePlanetMode(2500.f, 500.f, 500.f, 1000.f, false), SHUTTLE);
	EXPECT_EQ(samePlanetMode(2500.f, 500.f, 500.f, 0.f, false), WALK);
	EXPECT_EQ(samePlanetMode(2500.f, 500.f, 500.f, 1000.f, true), WALK);
	EXPECT_EQ(samePlanetMode(2000.f, 500.f, 500.f, 1000.f, false), WALK);
}

TEST(SimBotGroupRallyTest, RebuffNoticeIsOncePerVisitAndResetsAfterLeaving) {
	RebuffVisit visit;
	RebuffVisitStep step = stepRebuffVisit(visit, true, true, 1000, 120);
	EXPECT_TRUE(step.notify);
	EXPECT_TRUE(step.visit.notifiedThisVisit);
	step = stepRebuffVisit(step.visit, true, true, 2000, 120);
	EXPECT_FALSE(step.notify);
	step = stepRebuffVisit(step.visit, false, false, 3000, 120);
	EXPECT_EQ(step.visit.outsideSinceMs, 3000U);
	step = stepRebuffVisit(step.visit, false, false, 123001, 120);
	EXPECT_FALSE(step.visit.notifiedThisVisit);
	step = stepRebuffVisit(step.visit, true, true, 124000, 120);
	EXPECT_TRUE(step.notify);
}

TEST(SimBotGroupRallyTest, RallySlotsAreDeterministicAndSpread) {
	RallySlotPolicy policy;
	Vector3 point(100.f, 200.f, 5.f);
	Vector3 first = rallySlot(42, point, 0, policy);
	EXPECT_EQ(first.getX(), rallySlot(42, point, 0, policy).getX());
	EXPECT_NE(first.getX(), rallySlot(43, point, 0, policy).getX());
	EXPECT_GE(first.distanceTo2d(point), policy.rallyMinMeters);
	EXPECT_LE(first.distanceTo2d(point), policy.rallyMaxMeters);
}

} // namespace
