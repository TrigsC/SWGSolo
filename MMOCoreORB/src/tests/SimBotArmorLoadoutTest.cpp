#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotArmorLoadout.h"
#include "server/zone/objects/creature/simplayer/SimBotAppearance.h"

namespace {
using namespace SimBotArmorLoadout;

ArmorStats armor(uint64 objectId, const char* slot, int rating,
		int encumbrance = 0) {
	ArmorStats result;
	result.objectId = objectId;
	result.slotKey = slot;
	result.slotNames.add(slot);
	result.hitLocationMask = 1;
	result.armorRating = rating;
	result.healthEncumbrance = encumbrance;
	result.allowedRaces.add(42);
	return result;
}

TEST(SimBotArmorLoadout, ScoresArmorPiercingBothDirections) {
	ArmorScoringConfig config;
	ArmorStats stronger = armor(1, "chest", 2);
	EXPECT_FLOAT_EQ(scoreArmor(stronger, config), .75f);

	ArmorStats pierced = armor(2, "chest", 0);
	config.assumedArmorPiercing = 1;
	EXPECT_FLOAT_EQ(scoreArmor(pierced, config), 0.f);
}

TEST(SimBotArmorLoadout, ScoresVulnerabilityWeightsAndCoverage) {
	ArmorStats stats = armor(3, "chest", 4);
	ArmorScoringConfig config;
	config.damageTypeWeights[KINETIC] = 1.f;
	for (int i = ENERGY; i < LIGHTSABER; ++i)
		config.damageTypeWeights[i] = 0.f;
	EXPECT_FLOAT_EQ(scoreArmor(stats, config), .9375f);

	stats.vulnerabilityMask = damageTypeMask(KINETIC);
	EXPECT_FLOAT_EQ(scoreArmor(stats, config), 0.f);
	stats.vulnerabilityMask = 0;
	stats.hitLocationMask = 0;
	EXPECT_FLOAT_EQ(scoreArmor(stats, config), 0.f);

	stats.hitLocationMask = 1;
	stats.resists[KINETIC] = 50.f;
	EXPECT_FLOAT_EQ(scoreArmor(stats, config), .96875f);
}

TEST(SimBotArmorLoadout, ReportsUsabilityReasons) {
	ArmorStats stats = armor(4, "chest", 1);
	Vector<String> skills;
	UsableReason reason = OK;

	EXPECT_FALSE(isUsable(stats, 7, Factions::FACTIONNEUTRAL, skills, &reason));
	EXPECT_EQ(reason, RACE);
	stats.faction = Factions::FACTIONIMPERIAL;
	EXPECT_FALSE(isUsable(stats, 42, Factions::FACTIONNEUTRAL, skills, &reason));
	EXPECT_EQ(reason, FACTION);
	stats.faction = Factions::FACTIONNEUTRAL;
	stats.certificationsRequired.add("skill_box");
	EXPECT_FALSE(isUsable(stats, 42, Factions::FACTIONNEUTRAL, skills,
		&reason));
	EXPECT_EQ(reason, CERT);
	skills.add("skill_box");
	EXPECT_TRUE(isUsable(stats, 42, Factions::FACTIONNEUTRAL, skills, &reason));
	EXPECT_EQ(reason, OK);
	stats.hitLocationMask = 0;
	EXPECT_FALSE(isUsable(stats, 42, Factions::FACTIONNEUTRAL, skills, &reason));
	EXPECT_EQ(reason, NO_COVERAGE);
}

TEST(SimBotArmorLoadout, MirrorsEncumbranceBoundaryAndNegativeValues) {
	ArmorStats stats = armor(5, "chest", 1, 10);
	int secondaries[6] = {10, 11, 20, 20, 20, 20};
	EXPECT_FALSE(passesEncumbrance(stats, secondaries));
	secondaries[0] = 11;
	EXPECT_TRUE(passesEncumbrance(stats, secondaries));

	stats.healthEncumbrance = -10;
	secondaries[0] = 0;
	secondaries[1] = 0;
	EXPECT_TRUE(passesEncumbrance(stats, secondaries));
}

TEST(SimBotArmorLoadout, ComputesRequiredBuffPools) {
	ArmorStats stats = armor(6, "chest", 1);
	stats.healthEncumbrance = 1;
	stats.actionEncumbrance = 2;
	stats.mindEncumbrance = 3;
	EXPECT_EQ(requiredBuffPools(stats), static_cast<unsigned int>(
		STRENGTH | CONSTITUTION | QUICKNESS | STAMINA | FOCUS | WILLPOWER));

	stats.healthEncumbrance = -1;
	stats.actionEncumbrance = 0;
	stats.mindEncumbrance = 0;
	EXPECT_EQ(requiredBuffPools(stats), 0U);
}

TEST(SimBotArmorLoadout, ChoosesBestNonOverlappingPiecesWithTies) {
	ArmorStats low = armor(10, "chest", 2, 10);
	ArmorStats high = armor(11, "chest", 4, 20);
	ArmorStats tie = armor(12, "head", 2, 1);
	ArmorStats tieOid = armor(13, "head", 2, 1);
	tieOid.objectId = 13;
	low.slotNames.add("head");
	low.slotKey = "chest,head";
	std::vector<ArmorStats> candidates{low, high, tie, tieOid};
	Vector<String> skills;
	ArmorChoice choice = chooseArmor(candidates, std::vector<uint64_t>(), 42,
		Factions::FACTIONNEUTRAL, skills, ArmorScoringConfig(), 10.f);
	ASSERT_EQ(choice.pieces.size(), 2U);
	EXPECT_EQ(choice.pieces[0].stats.objectId, 11U);
	EXPECT_EQ(choice.pieces[1].stats.objectId, 12U);
}

TEST(SimBotArmorLoadout, AppliesHysteresisAndAcceptsPositiveEmptyKey) {
	ArmorStats current = armor(20, "chest", 4);
	ArmorStats marginal = armor(21, "chest", 5);
	std::vector<ArmorStats> candidates{current, marginal};
	std::vector<uint64_t> worn{20};
	ArmorChoice choice = chooseArmor(candidates, worn, 42,
		Factions::FACTIONNEUTRAL, Vector<String>(), ArmorScoringConfig(), 10.f);
	ASSERT_EQ(choice.pieces.size(), 1U);
	EXPECT_EQ(choice.pieces[0].stats.objectId, 20U);
	EXPECT_EQ(choice.pieces[0].status, HELD_BY_HYSTERESIS);

	ArmorStats empty = armor(22, "", 2);
	empty.slotKey = "";
	empty.slotNames.removeAll();
	choice = chooseArmor(std::vector<ArmorStats>{empty},
		std::vector<uint64_t>(), 42, Factions::FACTIONNEUTRAL,
		Vector<String>(), ArmorScoringConfig(), 10.f);
	ASSERT_EQ(choice.pieces.size(), 1U);
	EXPECT_EQ(choice.pieces[0].stats.objectId, 22U);
}

TEST(SimBotArmorLoadout, NeverHoldsAnUnusableWornPieceByHysteresis) {
	// The worn chest no longer fits the body race; a marginally better usable
	// chest must replace it rather than lose to the hysteresis margin.
	ArmorStats wornUnusable = armor(25, "chest", 5);
	wornUnusable.allowedRaces.removeAll();
	wornUnusable.allowedRaces.add(7);
	ArmorStats usable = armor(26, "chest", 4);
	std::vector<ArmorStats> candidates{wornUnusable, usable};
	std::vector<uint64_t> worn{25};
	ArmorChoice choice = chooseArmor(candidates, worn, 42,
		Factions::FACTIONNEUTRAL, Vector<String>(), ArmorScoringConfig(), 10.f);
	ASSERT_EQ(choice.pieces.size(), 1U);
	EXPECT_EQ(choice.pieces[0].stats.objectId, 26U);
	EXPECT_EQ(choice.pieces[0].status, NEW);
}

TEST(SimBotArmorLoadout, ProtectedSetKeepsWornAndBestSparePerSlot) {
	ArmorStats worn = armor(30, "chest", 2);
	ArmorStats spare = armor(31, "chest", 4);
	ArmorStats inferior = armor(32, "chest", 1);
	std::vector<ArmorStats> candidates{worn, spare, inferior};
	std::vector<uint64_t> protectedIds = protectedSet(candidates,
		std::vector<uint64_t>{30}, 42, Factions::FACTIONNEUTRAL,
		Vector<String>(), ArmorScoringConfig());
	EXPECT_EQ(protectedIds.size(), 2U);
	EXPECT_NE(std::find(protectedIds.begin(), protectedIds.end(), 30U),
		protectedIds.end());
	EXPECT_NE(std::find(protectedIds.begin(), protectedIds.end(), 31U),
		protectedIds.end());
	EXPECT_EQ(std::find(protectedIds.begin(), protectedIds.end(), 32U),
		protectedIds.end());
}

TEST(SimBotArmorLoadout, UpgradeDecisionReportsEveryReason) {
	ArmorStats listing = armor(40, "chest", 4);
	const int64_t bank = 1000;
	EXPECT_EQ(upgradeDecision(listing, 100, 0.f, bank, 0, 100, .5f,
		20000, 10.f, OK, 100), BUY);
	EXPECT_EQ(upgradeDecision(listing, 100, 1.f, bank, 0, 100, .5f,
		20000, 10.f, OK, 100), NOT_BETTER);
	EXPECT_EQ(upgradeDecision(listing, 100, 0.f, bank, 0, 100, .5f,
		20000, 10.f, RACE, 100), UNUSABLE);

	listing.healthEncumbrance = 100;
	EXPECT_EQ(upgradeDecision(listing, 100, 0.f, bank, 0, 100, .5f,
		20000, 10.f, OK, 100), TOO_ENCUMBERING);
	listing.healthEncumbrance = 0;
	EXPECT_EQ(upgradeDecision(listing, 451, 0.f, bank, 0, 100, .5f,
		20000, 10.f, OK, 100), OVER_BUDGET);
	EXPECT_EQ(upgradeDecision(listing, 10, 0.f, 50, 0, 100, .5f,
		20000, 10.f, OK, 100), UNAFFORDABLE);
}

TEST(SimBotAppearance, RendezvousIsDeterministicAndSpreadsIdentities) {
	std::vector<String> bodies{"human_male", "bothan_female", "wookiee_male"};
	EXPECT_EQ(SimBotAppearance::chooseBody(17, bodies),
		SimBotAppearance::chooseBody(17, bodies));
	std::vector<int> winners;
	for (uint64 identity = 1; identity <= 100; ++identity)
		winners.push_back(SimBotAppearance::chooseBody(identity, bodies));
	bool spread = false;
	for (size_t i = 1; i < winners.size(); ++i)
		spread = spread || winners[i] != winners[0];
	EXPECT_TRUE(spread);
}

TEST(SimBotAppearance, AppendingVariantOnlyMovesItsWinners) {
	std::vector<String> original{"a", "b", "c"};
	std::vector<String> appended = original;
	appended.push_back("d");
	int changed = 0;
	for (uint64 identity = 1; identity <= 1000; ++identity) {
		if (SimBotAppearance::chooseOutfitVariant(identity, original) !=
				SimBotAppearance::chooseOutfitVariant(identity, appended))
			++changed;
	}
	EXPECT_GT(changed, 0);
	EXPECT_LT(changed, 500);
}

TEST(SimBotAppearance, MorphSideAndFeatureValuesAreStable) {
	EXPECT_EQ(SimBotAppearance::chooseMorphSide(42, 7),
		SimBotAppearance::chooseMorphSide(42, 7));
	int first = SimBotAppearance::chooseFeatureValue(42, 5, 11);
	EXPECT_EQ(first, SimBotAppearance::chooseFeatureValue(42, 5, 11));
	EXPECT_GE(first, 0);
	EXPECT_LT(first, 5);
	bool firstSide = SimBotAppearance::chooseMorphSide(42, 7);
	int firstValue = firstSide ? 7 : 0;
	int secondValue = firstSide ? 0 : 7;
	EXPECT_EQ((firstValue != 0 ? 1 : 0) +
		(secondValue != 0 ? 1 : 0), 1);
}

} // namespace
