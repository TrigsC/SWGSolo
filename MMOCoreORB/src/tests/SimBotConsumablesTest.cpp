#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotConsumables.h"

namespace {

using namespace SimBotConsumables;

ConsumableEntry entry(uint32 templateCrc, uint32 buffCrc, bool drink,
		float maxPrice = 1000.f) {
	ConsumableEntry result;
	result.templateCrc = templateCrc;
	result.buffCrc = buffCrc;
	result.isDrink = drink;
	result.maxPricePerDose = maxPrice;
	return result;
}

TEST(SimBotConsumables, DigestsWholeIntervalsAndPreservesPartialInterval) {
	DigestResult result = digest(5, 1000, 1000 + 2 * kDigestIntervalMs + 9000);
	EXPECT_EQ(result.fill, 3);
	EXPECT_EQ(result.updatedAtMs, 1000 + 2 * kDigestIntervalMs);

	result = digest(1, 1000, 1000 + kDigestIntervalMs);
	EXPECT_EQ(result.fill, 0);
	EXPECT_EQ(result.updatedAtMs, 1000 + kDigestIntervalMs);
}

TEST(SimBotConsumables, ReportsRoomTimingAfterMaterializingDigestion) {
	EXPECT_EQ(msUntilRoom(99, 1000, 1000, 2), kDigestIntervalMs);
	EXPECT_EQ(msUntilRoom(100, 1000, 1000, 2), 2 * kDigestIntervalMs);
	EXPECT_EQ(msUntilRoom(95, 1000, 1000 + kDigestIntervalMs, 5), 0U);
}

TEST(SimBotConsumables, ChoosesEntriesInOrderAndClearsSmallestStackFirst) {
	std::vector<ConsumableEntry> entries{
		entry(10, 101, false), entry(20, 202, true), entry(30, 303, false)};
	std::vector<HeldStack> stacks;
	stacks.push_back(HeldStack());
	stacks.back().objectId = 1001;
	stacks.back().templateCrc = 10;
	stacks.back().doses = 8;
	stacks.back().filling = 5;
	stacks.push_back(HeldStack());
	stacks.back().objectId = 1002;
	stacks.back().templateCrc = 10;
	stacks.back().doses = 2;
	stacks.back().filling = 5;
	stacks.push_back(HeldStack());
	stacks.back().objectId = 1003;
	stacks.back().templateCrc = 20;
	stacks.back().doses = 4;
	stacks.back().filling = 10;
	std::vector<bool> active{false, false, false};
	std::vector<uint64_t> remaining{0, 0, 0};
	Stomach stomach;

	Decision decision = chooseConsumptions(entries, stacks, active, remaining,
			stomach, 1000, Policy());
	ASSERT_EQ(decision.choices.size(), 2U);
	EXPECT_EQ(decision.choices[0].entryIndex, 0);
	EXPECT_EQ(decision.choices[0].stack.objectId, 1002U);
	EXPECT_EQ(decision.choices[1].entryIndex, 1);
	EXPECT_EQ(decision.stomach.food.fill, 5);
	EXPECT_EQ(decision.stomach.drink.fill, 10);
	ASSERT_EQ(decision.blocked.size(), 1U);
	EXPECT_EQ(decision.blocked[0].reason, "no_doses");
}

TEST(SimBotConsumables, ReportsActiveAndFullBlockers) {
	std::vector<ConsumableEntry> entries{
		entry(10, 101, false), entry(20, 202, true)};
	std::vector<HeldStack> stacks;
	stacks.push_back(HeldStack());
	stacks.back().objectId = 1001;
	stacks.back().templateCrc = 10;
	stacks.back().doses = 2;
	stacks.back().filling = 5;
	stacks.push_back(HeldStack());
	stacks.back().objectId = 1002;
	stacks.back().templateCrc = 20;
	stacks.back().doses = 2;
	stacks.back().filling = 5;
	std::vector<bool> active{true, false};
	std::vector<uint64_t> remaining{12000, 0};
	Stomach stomach;
	stomach.drink.fill = 98;
	stomach.drink.updatedAtMs = 1000;

	Decision decision = chooseConsumptions(entries, stacks, active, remaining,
			stomach, 1000, Policy());
	EXPECT_TRUE(decision.choices.empty());
	ASSERT_EQ(decision.blocked.size(), 2U);
	EXPECT_EQ(decision.blocked[0].reason, "buff_active");
	EXPECT_EQ(decision.blocked[0].buffRemainingMs, 12000U);
	EXPECT_EQ(decision.blocked[1].reason, "stomach_full");
}

TEST(SimBotConsumables, SchedulesSoonestCauseWithRecheckFloor) {
	std::vector<Blocked> blocked;
	Blocked buff;
	buff.reason = "buff_active";
	buff.buffRemainingMs = 90000;
	blocked.push_back(buff);
	Blocked stomach;
	stomach.reason = "stomach_full";
	stomach.filling = 2;
	stomach.isDrink = false;
	blocked.push_back(stomach);
	Blocked noDoses;
	noDoses.reason = "no_doses";
	blocked.push_back(noDoses);

	Stomach after;
	after.food.fill = 99;
	after.food.updatedAtMs = 1000;
	Policy policy;
	policy.minRecheckSeconds = 30;
	policy.noDoseRetrySeconds = 300;
	EXPECT_EQ(nextCheckAtMs(blocked, std::vector<uint64_t>{0}, after, 0, 1000,
			policy), 31000U);

	EXPECT_EQ(nextCheckAtMs(std::vector<Blocked>(), std::vector<uint64_t>{5000},
			after, 1, 1000, policy), 31000U);
}

TEST(SimBotConsumables, RestockUsesDosesAndRejectsPendingOrBadPrices) {
	EXPECT_TRUE(restockNeed(4, 10, false));
	EXPECT_FALSE(restockNeed(10, 10, false));
	EXPECT_FALSE(restockNeed(4, 10, true));
	EXPECT_FLOAT_EQ(pricePerDose(600, 6), 100.f);
	EXPECT_TRUE(acceptsListing(600, 6, 100.f, 1000));
	EXPECT_FALSE(acceptsListing(601, 6, 100.f, 1000));
	EXPECT_FALSE(acceptsListing(600, 0, 1000.f, 1000));
}

TEST(SimBotConsumables, AccuracySeamKeepsUnbuffedResultAndAddsFood) {
	// Unbuffed: the stock rule (total already holds the template fallback).
	EXPECT_EQ(foodAdjustedAiAccuracy(0, 0, 40, .75f, 6), 75);
	EXPECT_EQ(foodAdjustedAiAccuracy(5, 0, 40, .75f, 6), 11);
	// Buffed, food is the only skill mod: the fallback value comes back.
	EXPECT_EQ(foodAdjustedAiAccuracy(10, 10, 40, .75f, 6), 56);
	EXPECT_EQ(foodAdjustedAiAccuracy(10, 10, 0, .75f, 6), 85);
	// Buffed on top of other mods: the template stays hidden, as in stock.
	EXPECT_EQ(foodAdjustedAiAccuracy(15, 10, 40, .75f, 6), 21);
}

} // namespace
