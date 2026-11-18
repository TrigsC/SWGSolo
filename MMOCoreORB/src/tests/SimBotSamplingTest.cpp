#include "gtest/gtest.h"

#include "server/zone/managers/resource/resourcespawner/ResourceSampleYield.h"
#include "server/zone/objects/creature/simplayer/SimBotHarvest.h"
#include "server/zone/objects/creature/simplayer/SimBotSampling.h"

#include <map>

namespace {

TEST(SimBotSampling, StockDensityBandsAndRateFailure) {
	ResourceSampleYield::Input input;
	input.density = .09f;
	input.surveySkill = 100;
	input.roll100 = 99;
	input.roll3 = 2;
	EXPECT_EQ(ResourceSampleYield::calculate(input).outcome,
		ResourceSampleYield::DENSITY_TOO_LOW);

	// 40% clears the skill-0 density floor (32), so the rate decides.
	input.density = .40f;
	input.surveySkill = 0;
	input.roll100 = 0;
	EXPECT_EQ(ResourceSampleYield::calculate(input).outcome,
		ResourceSampleYield::RATE_FAILED);

	input.roll100 = 40;
	EXPECT_EQ(ResourceSampleYield::calculate(input).outcome,
		ResourceSampleYield::TOO_FEW_UNITS);
}

TEST(SimBotSampling, StockUnitsAndXpPreserveGambleAndRichBands) {
	ResourceSampleYield::Input input;
	input.density = .80f;
	input.surveySkill = 100;
	input.roll100 = 0;
	input.roll3 = 0;
	ResourceSampleYield::Result normal = ResourceSampleYield::calculate(input);
	EXPECT_EQ(normal.maxUnits, 20);
	EXPECT_EQ(normal.units, 20);
	EXPECT_EQ(normal.xpcap, 40);
	EXPECT_EQ(normal.xp, 40);

	input.gamble = true;
	input.gambleRoll = 1;
	ResourceSampleYield::Result gamble = ResourceSampleYield::calculate(input);
	EXPECT_EQ(gamble.units, 100);
	EXPECT_EQ(gamble.xpcap, 50);
	EXPECT_EQ(gamble.xp, 250);

	input.gamble = false;
	input.gambleRoll = 0;
	input.richLocationPresent = true;
	input.richInRange = false;
	ResourceSampleYield::Result richNearby = ResourceSampleYield::calculate(input);
	EXPECT_EQ(richNearby.units, 20);
	EXPECT_EQ(richNearby.xpcap, 50);
	EXPECT_EQ(richNearby.xp, 50);

	input.richInRange = true;
	ResourceSampleYield::Result rich = ResourceSampleYield::calculate(input);
	EXPECT_EQ(rich.units, 100);
	EXPECT_EQ(rich.xpcap, 50);
	EXPECT_EQ(rich.xp, 250);
}

TEST(SimBotSampling, ToolClassesMapToNamesAndTemplates) {
	EXPECT_EQ(SimBotSampling::toolClassFromName("MINERAL"),
		SimBotSampling::MINERAL);
	EXPECT_STREQ(SimBotSampling::toolClassName(SimBotSampling::INORGANIC),
		"inorganic");
	EXPECT_STREQ(SimBotSampling::toolTemplate(SimBotSampling::MINERAL),
		"object/tangible/survey_tool/survey_tool_mineral.iff");
	EXPECT_STREQ(SimBotSampling::toolTemplate(SimBotSampling::CHEMICAL),
		"object/tangible/survey_tool/survey_tool_liquid.iff");
	EXPECT_STREQ(SimBotSampling::toolTemplate(SimBotSampling::WATER),
		"object/tangible/survey_tool/survey_tool_moisture.iff");
	EXPECT_STREQ(SimBotSampling::toolTemplate(SimBotSampling::FLORA),
		"object/tangible/survey_tool/survey_tool_lumber.iff");
	EXPECT_STREQ(SimBotSampling::toolTemplate(SimBotSampling::GEOTHERMAL), "");
	EXPECT_EQ(SimBotSampling::toolClassFromName("unknown"), 0);
}

TEST(SimBotSampling, EligibilityRequiresMatchingToolClass) {
	EXPECT_TRUE(SimBotSampling::isSampleable(SimBotSampling::MINERAL, true,
		SimBotSampling::MINERAL));
	EXPECT_FALSE(SimBotSampling::isSampleable(SimBotSampling::MINERAL, true,
		SimBotSampling::GAS));
	// Stock: an inorganic tool also surveys every inorganic spawn.
	EXPECT_TRUE(SimBotSampling::isSampleable(SimBotSampling::MINERAL, true,
		SimBotSampling::INORGANIC));
	EXPECT_FALSE(SimBotSampling::isSampleable(SimBotSampling::FLORA, false,
		SimBotSampling::INORGANIC));
	// No stock tool exists for geothermal, so nothing can hold one.
	EXPECT_FALSE(SimBotSampling::isSampleable(SimBotSampling::GEOTHERMAL, true,
		SimBotSampling::GEOTHERMAL));
}

TEST(SimBotSampling, ClassPriceUsesConfiguredValueAndDefault) {
	std::map<std::string, uint64_t> prices;
	prices["mineral"] = 7;
	EXPECT_EQ(SimBotSampling::pricePerUnit(prices, "MINERAL", 3), 7U);
	EXPECT_EQ(SimBotSampling::pricePerUnit(prices, "gas", 3), 3U);
}

TEST(SimBotSampling, StaleStackPolicyAndOrderingAreDeterministic) {
	EXPECT_TRUE(SimBotSampling::isStaleStack(99, 100, false, .50f, .75f));
	EXPECT_FALSE(SimBotSampling::isStaleStack(100, 100, false, .50f, .75f));
	EXPECT_FALSE(SimBotSampling::isStaleStack(99, 100, true, .50f, .75f));
	EXPECT_FALSE(SimBotSampling::isStaleStack(99, 100, false, .75f, .75f));

	SimBotSampling::StaleStack lowest;
	lowest.objectId = 20;
	lowest.units = 10;
	lowest.qualityFactor = .25f;
	SimBotSampling::StaleStack smaller;
	smaller.objectId = 10;
	smaller.units = 5;
	smaller.qualityFactor = .25f;
	EXPECT_TRUE(SimBotSampling::staleStackComesBefore(smaller, lowest));
}

TEST(SimBotSampling, QualityPolicyReusesHarvestQualityFactor) {
	const int attributes[2] = {800, 600};
	EXPECT_FLOAT_EQ(SimBotHarvest::resourceQualityFactor(attributes, 2, .25f),
		.7f);
}

} // namespace
