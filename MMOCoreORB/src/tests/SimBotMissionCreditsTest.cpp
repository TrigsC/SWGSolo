#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotMissionCredits.h"

using namespace SimBotMissionCredits;

TEST(SimBotMissionCreditsTest, DivisorIsClampedToOne) {
	EXPECT_EQ(dividedReward(100, 0), 100);
	EXPECT_EQ(dividedReward(100, -2), 100);
	EXPECT_EQ(dividedReward(100, 1), 100);
}

TEST(SimBotMissionCreditsTest, DividesRewardForGroups) {
	EXPECT_EQ(dividedReward(100, 3), 33);
}

TEST(SimBotMissionCreditsTest, BoundsIncludeBothEdges) {
	EXPECT_TRUE(withinBounds(10, 10, 20, 1));
	EXPECT_TRUE(withinBounds(20, 10, 20, 1));
	EXPECT_FALSE(withinBounds(9, 10, 20, 1));
	EXPECT_FALSE(withinBounds(21, 10, 20, 1));
}

TEST(SimBotMissionCreditsTest, BoundsUseTheSameDivisorRule) {
	EXPECT_TRUE(withinBounds(3, 10, 11, 3));
	EXPECT_TRUE(withinBounds(10, 10, 11, 0));
	EXPECT_FALSE(withinBounds(3, 10, 11, 0));
	EXPECT_FALSE(withinBounds(2, 10, 11, 3));
}
