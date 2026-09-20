#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotAbilityTiers.h"

TEST(SimBotAbilityTiersTest, ParsesTrailingDigitsOnly) {
	SimBotAbilityTier melee = SimBotAbilityTiers::parseAbilityTier(
		"Melee1hHit2");
	EXPECT_EQ(melee.chainKey, "melee1hhit");
	EXPECT_EQ(melee.tier, 2);

	SimBotAbilityTier saber = SimBotAbilityTiers::parseAbilityTier(
		"saber2hfrenzy");
	EXPECT_EQ(saber.chainKey, "saber2hfrenzy");
	EXPECT_EQ(saber.tier, 0);

	SimBotAbilityTier intimidate = SimBotAbilityTiers::parseAbilityTier(
		"intimidate1");
	EXPECT_EQ(intimidate.chainKey, "intimidate");
	EXPECT_EQ(intimidate.tier, 1);

	SimBotAbilityTier digits = SimBotAbilityTiers::parseAbilityTier("123");
	EXPECT_TRUE(digits.chainKey.isEmpty());
	EXPECT_EQ(digits.tier, 123);
}

TEST(SimBotAbilityTiersTest, KeepsHighestTierInStableChainOrder) {
	Vector<String> input;
	input.add("intimidate1");
	input.add("unarmedHit1");
	input.add("intimidate2");
	input.add("INTIMIDATE2");
	input.add("unarmedHit1");

	Vector<String> kept;
	Vector<String> superseded;
	SimBotAbilityTiers::collapseAbilityTiers(input, kept, superseded);

	ASSERT_EQ(kept.size(), 2);
	EXPECT_EQ(kept.get(0), "intimidate2");
	EXPECT_EQ(kept.get(1), "unarmedHit1");
	ASSERT_EQ(superseded.size(), 1);
	EXPECT_EQ(superseded.get(0), "intimidate1");
}

TEST(SimBotAbilityTiersTest, DoesNotCollideAcrossFamilies) {
	Vector<String> input;
	input.add("melee1hhit2");
	input.add("melee2hhit2");
	input.add("saber2hfrenzy");

	Vector<String> kept;
	Vector<String> superseded;
	SimBotAbilityTiers::collapseAbilityTiers(input, kept, superseded);

	EXPECT_EQ(kept.size(), 3);
	EXPECT_TRUE(superseded.isEmpty());
}

TEST(SimBotAbilityTiersTest, KeepsDigitsOnlyNamesAsSeparateChains) {
	Vector<String> input;
	input.add("123");
	input.add("456");

	Vector<String> kept;
	Vector<String> superseded;
	SimBotAbilityTiers::collapseAbilityTiers(input, kept, superseded);

	ASSERT_EQ(kept.size(), 2);
	EXPECT_EQ(kept.get(0), "123");
	EXPECT_EQ(kept.get(1), "456");
	EXPECT_TRUE(superseded.isEmpty());
}
