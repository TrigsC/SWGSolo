#include "gtest/gtest.h"

#include "server/zone/managers/combat/CombatManager.h"

TEST(CombatSpecialCostTest, MatchesPlayerFormulaForFractionalCosts) {
	float health = 0.f;
	float action = 0.f;
	float mind = 0.f;

	CombatManager::specialAttackHamCostFromInputs(100.f, 50.f, 25.f,
		0.35f, 0.5f, 0.25f, 420.f, 270.f, 510.f, health, action, mind);

	EXPECT_NEAR(health, 31.5f, 0.0001f);
	EXPECT_NEAR(action, 25.625f, 0.0001f);
	EXPECT_NEAR(mind, 5.15625f, 0.0001f);
}

TEST(CombatSpecialCostTest, IntimidateOneZeroMultipliersAreFree) {
	float health = 0.f;
	float action = 0.f;
	float mind = 0.f;

	CombatManager::specialAttackHamCostFromInputs(150.f, 125.f, 100.f,
		0.f, 0.f, 0.f, 300.f, 300.f, 300.f, health, action, mind);

	EXPECT_FLOAT_EQ(health, 0.f);
	EXPECT_FLOAT_EQ(action, 0.f);
	EXPECT_FLOAT_EQ(mind, 0.f);
}

TEST(CombatSpecialCostTest, RefusesAtCostAndAcceptsAboveCost) {
	float health = 0.f;
	float action = 0.f;
	float mind = 0.f;

	CombatManager::specialAttackHamCostFromInputs(40.f, 0.f, 0.f,
		1.f, 0.f, 0.f, 300.f, 300.f, 300.f, health, action, mind);

	EXPECT_FLOAT_EQ(health, 40.f);
	EXPECT_FALSE(40.f > health);
	EXPECT_TRUE(41.f > health);
}
