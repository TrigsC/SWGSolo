#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotAreaAttackGuard.h"

namespace {

// isArea, isCone, areaRange, coneRange, commandRange, hasWeapon, weaponMax,
// splash, thrownOrHeavy
float reach(bool isArea, bool isCone, int areaRange, int coneRange,
		int commandRange, bool hasWeapon = true, int weaponMax = 5,
		bool splash = false, bool thrownOrHeavy = false) {
	return SimBotAreaAttackGuard::reachMeters(isArea, isCone, areaRange,
		coneRange, commandRange, hasWeapon, weaponMax, splash, thrownOrHeavy);
}

}

TEST(SimBotAreaAttackGuardTest, SingleTargetCommandNeverEndangersTheLair) {
	float r = reach(false, false, -1, -1, -1);
	EXPECT_LT(r, 0.f);
	EXPECT_FALSE(SimBotAreaAttackGuard::endangers(r, 0.f));
}

TEST(SimBotAreaAttackGuardTest, SpinAttackNextToTheLairIsRefused) {
	// unarmedSpinAttack2: areaAction, areaRange 16, range -1. Nordihi fighting a
	// nudfuh 10m from its lair.
	float r = reach(true, false, 16, -1, -1);
	EXPECT_FLOAT_EQ(r, 16.f);
	EXPECT_TRUE(SimBotAreaAttackGuard::endangers(r, 10.f));
}

TEST(SimBotAreaAttackGuardTest, MarginCoversTheLairFootprint) {
	float r = reach(true, false, 16, -1, -1);
	EXPECT_TRUE(SimBotAreaAttackGuard::endangers(r, 24.f));
	EXPECT_FALSE(SimBotAreaAttackGuard::endangers(r, 24.5f));
}

TEST(SimBotAreaAttackGuardTest, SpinAttackOnTheRoadIsAllowed) {
	float r = reach(true, false, 16, -1, -1);
	EXPECT_FALSE(SimBotAreaAttackGuard::endangers(r, 60.f));
}

TEST(SimBotAreaAttackGuardTest, AreaInheritingRangeFallsBackToTheWeapon) {
	// Engine rule: a negative areaRange becomes the weapon's max range.
	EXPECT_FLOAT_EQ(reach(true, false, -1, -1, -1, true, 64), 64.f);
}

TEST(SimBotAreaAttackGuardTest, ConeUsesConeRangeFirst) {
	// A 24m Jedi cone: coneRange wins over the command range.
	EXPECT_FLOAT_EQ(reach(false, true, -1, 24, 5), 24.f);
}

TEST(SimBotAreaAttackGuardTest, ConeWithoutConeRangeUsesTheCommandRange) {
	EXPECT_FLOAT_EQ(reach(false, true, -1, -1, 32), 32.f);
}

TEST(SimBotAreaAttackGuardTest, ConeWithNoRangesUsesTheWeapon) {
	EXPECT_FLOAT_EQ(reach(false, true, -1, -1, -1, true, 64), 64.f);
}

TEST(SimBotAreaAttackGuardTest, SplashDamageAddsTheCommandRange) {
	EXPECT_FLOAT_EQ(reach(true, false, 10, -1, 6, true, 64, true), 16.f);
}

TEST(SimBotAreaAttackGuardTest, ThrownOrHeavyWeaponReachesMaxRangePlusArea) {
	// A grenade or minefield: weapon max range + areaRange.
	EXPECT_FLOAT_EQ(reach(true, false, 10, -1, -1, true, 32, false, true), 42.f);
}

TEST(SimBotAreaAttackGuardTest, NoWeaponAndNoRangeHitsNothing) {
	float r = reach(true, false, -1, -1, -1, false, 0);
	EXPECT_LT(r, 0.f);
	EXPECT_FALSE(SimBotAreaAttackGuard::endangers(r, 0.f));
}
