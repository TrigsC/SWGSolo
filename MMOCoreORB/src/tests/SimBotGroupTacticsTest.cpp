#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotGroupTactics.h"

namespace {

using namespace SimBotGroupTactics;

TEST(SimBotGroupTacticsTest, ParsesTacticsAndKeepsMissionPrecedence) {
	TacticVocabulary vocabulary;
	std::vector<SimBotGrouping::NamedBot> names{{17, "Paki"}};

	TacticRequest request = parseTacticCommand("Paki, who has spin?", vocabulary,
		names);
	EXPECT_EQ(request.command, TACTIC_SPIN_POLL);
	EXPECT_FALSE(request.all);
	ASSERT_EQ(request.targetIdentityIds.size(), 1U);
	EXPECT_EQ(request.targetIdentityIds[0], 17U);

	EXPECT_EQ(parseTacticCommand("spin", vocabulary, names).command,
		TACTIC_NONE);
	EXPECT_EQ(parseTacticCommand("do you have spin", vocabulary, names).command,
		TACTIC_SPIN_POLL);
	EXPECT_EQ(parseTacticCommand("pull", vocabulary, names).command,
		TACTIC_PULL);
	EXPECT_EQ(parseTacticCommand("spin the lair", vocabulary, names).command,
		TACTIC_SPIN_ON);
	EXPECT_EQ(parseTacticCommand("attack the lair", vocabulary, names).command,
		TACTIC_SPIN_GO);
	EXPECT_EQ(parseTacticCommand("ok hit the lair", vocabulary, names).command,
		TACTIC_SPIN_GO);
	EXPECT_EQ(parseTacticCommand("no spin", vocabulary, names).command,
		TACTIC_SPIN_OFF);
	EXPECT_EQ(parseTacticCommand("stop spinning", vocabulary, names).command,
		TACTIC_SPIN_OFF);
	EXPECT_EQ(parseTacticCommand("take foods", vocabulary, names).command,
		TACTIC_EAT);
	EXPECT_EQ(parseTacticCommand("Paki grab 2 kreetle missions", vocabulary,
		names).command, TACTIC_NONE);
	EXPECT_EQ(parseTacticCommand("Paki back off", vocabulary, names).command,
		TACTIC_BACK_OFF);
}

TEST(SimBotGroupTacticsTest, ChoosesNearestNonPackAndExplainsRejection) {
	std::vector<PullCandidate> candidates = {
		{10.f, false, true, true, false, false, 0},
		{2.f, false, false, true, false, false, 0},
		{1.f, true, false, true, false, false, 0}};
	PullTargetChoice choice = choosePullTarget(candidates);
	EXPECT_EQ(choice.index, 1);
	EXPECT_EQ(choice.reason, "selected");

	candidates[0].inCombat = true;
	candidates[1].inCombat = true;
	candidates[2].inCombat = true;
	choice = choosePullTarget(candidates);
	EXPECT_EQ(choice.index, -1);
	EXPECT_EQ(choice.reason, "all_busy");

	choice = choosePullTarget({{1.f, false, false, true, false, false, 1}});
	EXPECT_EQ(choice.index, -1);
	EXPECT_EQ(choice.reason, "faction");
}

TEST(SimBotGroupTacticsTest, PullTimesOutOnlyAfterTagOrReturnDeadline) {
	PullState state;
	PullInputs inputs;
	inputs.nowMs = 100;
	inputs.withinEngageRange = true;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, TAG);

	inputs.nowMs = 8100;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, ABORT);
	EXPECT_EQ(state.reason, "tag_unconfirmed");

	state = PullState();
	inputs = PullInputs();
	inputs.nowMs = 0;
	inputs.withinEngageRange = true;
	state = pullStep(state, inputs);
	inputs.nowMs = 1;
	inputs.targetAimedAtBot = true;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, RETURN);
	inputs.nowMs = 45001;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, ABORT);
	EXPECT_EQ(state.reason, "timeout");
}

TEST(SimBotGroupTacticsTest, PullPhasesHaveTheirOwnBudgets) {
	PullInputs inputs;
	inputs.nowMs = 1000;
	PullState state = pullStep(PullState(), inputs);
	EXPECT_EQ(state.stage, APPROACH);
	inputs.nowMs = 46000;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, ABORT);
	EXPECT_EQ(state.reason, "unreachable");

	// A 40 s approach still leaves the return its full budget from the tag.
	state = PullState();
	inputs = PullInputs();
	inputs.nowMs = 1000;
	state = pullStep(state, inputs);
	inputs.nowMs = 41000;
	inputs.withinEngageRange = true;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, TAG);
	inputs.nowMs = 42000;
	inputs.targetAimedAtBot = true;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, RETURN);
	inputs.nowMs = 80000;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, RETURN);
	inputs.nowMs = 87000;
	state = pullStep(state, inputs);
	EXPECT_EQ(state.stage, ABORT);
	EXPECT_EQ(state.reason, "timeout");
}

TEST(SimBotGroupTacticsTest, PollAndSpinEngagementUseFactsAndSuppression) {
	SpinPollAnswer answer = spinPollAnswer(true, false, 0.83f, 9);
	EXPECT_EQ(answer.lineKey, "spinYesNoBuffs");
	EXPECT_EQ(answer.hamPercent, 83);
	EXPECT_EQ(answer.wornArmorPieces, 9);
	// A quiet lair next to the leader is not a go signal.
	EXPECT_FALSE(spinShouldEngage(true, true, 0, 42, 5.f, false, false, 20.f));
	EXPECT_TRUE(spinShouldEngage(true, true, 0, 42, 20.f, true, false, 20.f));
	EXPECT_TRUE(spinShouldEngage(true, true, 0, 42, 5.f, false, true, 20.f));
	EXPECT_FALSE(spinShouldEngage(true, true, 0, 42, 20.5f, true, true, 20.f));
	EXPECT_FALSE(spinShouldEngage(true, true, 42, 42, 2.f, true, true, 20.f));
	EXPECT_FALSE(spinShouldEngage(true, false, 0, 42, 2.f, true, true, 20.f));
	EXPECT_FALSE(spinShouldEngage(false, true, 0, 42, 2.f, true, true, 20.f));
}

} // namespace
