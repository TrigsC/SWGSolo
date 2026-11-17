#include "gtest/gtest.h"

#include "server/zone/managers/creature/CreatureHarvestYield.h"
#include "server/zone/objects/creature/simplayer/SimBotHarvest.h"

namespace {

using namespace SimBotHarvest;

TEST(SimBotHarvestTest, ChoosesLargestPositiveNeedAndUsesFallbackOrderForTies) {
	const uint64_t available[3] = {100, 100, 100};
	const uint64_t need[3] = {10, 30, 20};
	EXPECT_EQ(chooseFamily(available, need), MEAT);

	const Family reverseOrder[3] = {BONE, MEAT, HIDE};
	const uint64_t tiedNeed[3] = {40, 40, 40};
	EXPECT_EQ(chooseFamily(available, tiedNeed, reverseOrder), BONE);
}

TEST(SimBotHarvestTest, MissingFamiliesAndNoFamiliesAreRejected) {
	const uint64_t onlyBone[3] = {0, 0, 12};
	const uint64_t need[3] = {100, 100, 100};
	EXPECT_EQ(chooseFamily(onlyBone, need), BONE);

	const uint64_t none[3] = {0, 0, 0};
	EXPECT_EQ(chooseFamily(none, need), NONE);
}

TEST(SimBotHarvestTest, ZeroNeedFallsBackToLargestCorpseFamily) {
	const uint64_t available[3] = {40, 90, 70};
	const uint64_t noNeed[3] = {0, 0, 0};
	EXPECT_EQ(chooseFamily(available, noNeed), MEAT);
}

TEST(SimBotHarvestTest, MapsResourceTypesAndOrganicContainerGots) {
	EXPECT_EQ(familyFromResourceType("hide_woolly"), HIDE);
	EXPECT_EQ(familyFromResourceType("meat_domesticated"), MEAT);
	EXPECT_EQ(familyFromResourceType("bone_woolly"), BONE);
	EXPECT_EQ(familyFromResourceType("metal"), NONE);
	EXPECT_EQ(familyFromGameObjectType(0x40000A), HIDE);
	EXPECT_EQ(familyFromGameObjectType(0x400009), MEAT);
	EXPECT_EQ(familyFromGameObjectType(0x40000B), BONE);
	EXPECT_EQ(familyFromGameObjectType(0), NONE);
}

TEST(SimBotHarvestTest, ClampsNeedAndQualityToSafeRanges) {
	EXPECT_EQ(needUnits(100, 70, 50), 0U);
	EXPECT_EQ(needUnits(100, 20, 30), 50U);
	const int attributes[4] = {1000, 500, -1, 1500};
	EXPECT_FLOAT_EQ(resourceQualityFactor(attributes, 4, 0.25f), 1.f);
	const int poorAttributes[2] = {0, 100};
	EXPECT_FLOAT_EQ(resourceQualityFactor(poorAttributes, 2, 0.25f), 0.25f);
}

TEST(SimBotHarvestTest, EqualNeedPrefersTheLargerCorpseYield) {
	const uint64_t available[3] = {20, 90, 50};
	const uint64_t equalNeed[3] = {500, 500, 500};
	EXPECT_EQ(chooseFamily(available, equalNeed), MEAT);
	const uint64_t hideAndBoneTied[3] = {500, 100, 500};
	EXPECT_EQ(chooseFamily(available, hideAndBoneTied), BONE);
}

TEST(SimBotHarvestTest, QualityIgnoresAttributesTheSpawnLacks) {
	// A hide spawn has only a few of the ten attributes; the rest read 0.
	const int attributes[10] = {800, 600, 0, 0, 0, 0, 0, 0, 0, 0};
	EXPECT_FLOAT_EQ(resourceQualityFactor(attributes, 10, 0.25f), 0.7f);
}

TEST(SimBotHarvestTest, PricesWithoutFloorRespectMinimumAndMaximum) {
	HumanFloor none;
	PriceDecision price = priceResourceListing(100, 3, 0.5f, none, 200, 1000);
	EXPECT_EQ(price.base, 150U);
	EXPECT_EQ(price.price, 200U);

	price = priceResourceListing(100, 10, 1.f, none, 1, 1000);
	EXPECT_EQ(price.price, 1000U);
	EXPECT_FALSE(price.skipFloorAboveMax);

	price = priceResourceListing(100, 20, 1.f, none, 1, 100);
	EXPECT_EQ(price.price, 0U);
	EXPECT_FALSE(price.skipFloorAboveMax);
}

TEST(SimBotHarvestTest, CeilHumanFloorNeverUndercutsAndSkipsAboveMaximum) {
	HumanFloor floor;
	floor.price = 10;
	floor.units = 3;
	PriceDecision price = priceResourceListing(4, 1, 0.25f, floor, 1, 100);
	EXPECT_EQ(price.floor, 14U);
	EXPECT_EQ(price.price, 14U);

	price = priceResourceListing(100, 1, 1.f, floor, 1, 100);
	EXPECT_EQ(price.price, 0U);
	EXPECT_TRUE(price.skipFloorAboveMax);
}

TEST(SimBotHarvestTest, SellThresholdAndDispatchCapHonorRemainingNeed) {
	EXPECT_FALSE(shouldSellStack(99, 100, 1));
	EXPECT_TRUE(shouldSellStack(100, 100, 1));
	EXPECT_FALSE(shouldSellStack(100, 100, 0));
	EXPECT_EQ(capDispatchSignal(500, 0, 0), 0U);
	EXPECT_EQ(capDispatchSignal(500, 1000, 250), 500U);
	EXPECT_EQ(capDispatchSignal(500, 1000, 1000), 0U);
}

TEST(SimBotHarvestTest, ScoutExperienceUsesStockBaseAndMultiplier) {
	EXPECT_EQ(CreatureHarvestYield::scoutExperience(4, 1.5f), 58);
	EXPECT_EQ(CreatureHarvestYield::scoutExperience(4, 1.f, 1.f, 1.f, 1.5f), 58);
}

TEST(SimBotHarvestTest, StockYieldPreservesBandsFloorGroupAndInCellRule) {
	using CreatureHarvestYield::calculate;
	EXPECT_EQ(calculate(1.f, 1, 0.10f, 1.f, false).quantity, 1);
	EXPECT_EQ(calculate(100.f, 1, 0.60f, 1.f, false).quantity, 3);
	EXPECT_EQ(calculate(100.f, 1, 0.10f, 1.f, false).quantity, 1);
	EXPECT_EQ(calculate(100.f, 10, 0.10f, 1.f, false).quantity, 5);
	EXPECT_EQ(calculate(100.f, 10, 0.30f, 1.f, false).quantity, 7);
	EXPECT_EQ(calculate(100.f, 10, 0.60f, 1.f, false).quantity, 10);
	EXPECT_EQ(calculate(100.f, 10, 0.80f, 1.f, false).quantity, 12);
	EXPECT_EQ(calculate(100.f, 10, 0.80f, 1.5f, false).quantity, 18);
	EXPECT_EQ(calculate(100.f, 10, 0.80f, 1.5f, true).quantity, 1);
}

} // namespace
