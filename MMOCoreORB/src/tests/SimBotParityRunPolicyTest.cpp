#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotParityRunPolicy.h"

namespace {

using namespace SimBotParityRunPolicy;

TEST(SimBotParityRunPolicyTest, EmptyAllowlistIncludesEveryScenario) {
	EXPECT_TRUE(inScope("group_rally", {}));
	EXPECT_TRUE(inScope("group_mission", {"group_mission", "group_rally"}));
	EXPECT_FALSE(inScope("group_buff", {"group_mission", "group_rally"}));
}

TEST(SimBotParityRunPolicyTest, ContinueOnFailureControlsCursorAdvance) {
	EXPECT_EQ(nextIndexAfterResult(2, "FAIL", false), 2);
	EXPECT_EQ(nextIndexAfterResult(2, "FAIL", true), 3);
	EXPECT_EQ(nextIndexAfterResult(2, "PASS", false), 3);
}

TEST(SimBotParityRunPolicyTest, CapStartsAtExactBoundary) {
	EXPECT_FALSE(runCapReached(0, 60000, 1));
	EXPECT_FALSE(runCapReached(1000, 60999, 1));
	EXPECT_TRUE(runCapReached(1000, 61000, 1));
	EXPECT_FALSE(runCapReached(1000, 61000, 0));
}

TEST(SimBotParityRunPolicyTest, FailureClassSeparatesWorldFromScope) {
	FailureInputs inputs;
	inputs.noEligibleBorrowedBot = true;
	EXPECT_EQ(classifyParityFailure(inputs), "environment");

	inputs = FailureInputs();
	inputs.borrowedBotDied = true;
	inputs.killerCaptured = true;
	inputs.killerIsScenarioTarget = true;
	EXPECT_EQ(classifyParityFailure(inputs), "scope");

	inputs.killerIsScenarioTarget = false;
	EXPECT_EQ(classifyParityFailure(inputs), "environment");
}

} // namespace
