#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotLoadout.h"

namespace {
using namespace SimBotLoadout;

WeaponStats weapon(uint64 objectId, const char* xpType, float minDamage,
		float maxDamage, float speed) {
	WeaponStats result;
	result.objectId = objectId;
	result.xpType = xpType;
	result.minDamage = minDamage;
	result.maxDamage = maxDamage;
	result.attackSpeed = speed;
	return result;
}

TEST(SimBotLoadout, ScoresDamagePerSecondAndUsesDeterministicTies) {
	WeaponStats fast = weapon(10, "rifle", 10, 20, 1.f);
	WeaponStats slow = weapon(20, "rifle", 20, 40, 2.f);
	EXPECT_FLOAT_EQ(scoreWeapon(fast), 15.f);
	EXPECT_FLOAT_EQ(scoreWeapon(slow), 15.f);
	EXPECT_TRUE(preferred(fast, slow));

	slow.maxRange = 50;
	EXPECT_TRUE(preferred(slow, fast));
	slow.maxRange = fast.maxRange;
	slow.objectId = 5;
	EXPECT_TRUE(preferred(slow, fast));
}

TEST(SimBotLoadout, EmptyCertificationListAndSubsetAreCertified) {
	Vector<String> held;
	EXPECT_TRUE(isCertified(Vector<String>(), held));
	Vector<String> required;
	required.add("cert_rifle");
	EXPECT_FALSE(isCertified(required, held));
	held.add("cert_rifle");
	EXPECT_TRUE(isCertified(required, held));
}

TEST(SimBotLoadout, MapsWeaponTypesToTrainingClasses) {
	EXPECT_TRUE(weaponClassCandidates(0x20007).get(0) == String("combat_meleespecialize_onehand"));
	EXPECT_TRUE(weaponClassCandidates(0x20008).get(0) == String("combat_meleespecialize_twohand"));
	EXPECT_TRUE(weaponClassCandidates(0x20009).get(0) == String("combat_meleespecialize_polearm"));
	EXPECT_TRUE(weaponClassCandidates(0x2000A).get(0) == String("combat_rangedspecialize_pistol"));
	EXPECT_TRUE(weaponClassCandidates(0x2000B).get(0) == String("combat_rangedspecialize_carbine"));
	EXPECT_TRUE(weaponClassCandidates(0x2000C).get(0) == String("combat_rangedspecialize_rifle"));
	EXPECT_EQ(weaponClassCandidates(0x20003).size(), 0);
}

TEST(SimBotLoadout, ChoosesCertifiedVaultWeaponForNextClass) {
	WeaponStats starter = weapon(0, "rifle", 10, 10, 1.f);
	WeaponStats uncertified = weapon(11, "rifle", 100, 100, 1.f);
	uncertified.certificationsRequired.add("cert_rifle");
	WeaponStats certified = weapon(12, "rifle", 30, 30, 1.f);
	std::vector<WeaponStats> candidates{uncertified, certified};
	Vector<String> held;
	LoadoutChoice choice = chooseWeapon(candidates, "rifle", false, held,
		starter, 0, 10.f);
	EXPECT_EQ(choice.source, VAULT);
	EXPECT_EQ(choice.objectId, 12U);

	held.add("cert_rifle");
	choice = chooseWeapon(candidates, "rifle", false, held, starter, 0, 10.f);
	EXPECT_EQ(choice.objectId, 11U);
}

TEST(SimBotLoadout, CompletedTemplateCanUseAnotherClass) {
	WeaponStats starter = weapon(0, "rifle", 10, 10, 1.f);
	WeaponStats pistol = weapon(33, "pistol", 30, 30, 1.f);
	std::vector<WeaponStats> candidates{pistol};
	LoadoutChoice incomplete = chooseWeapon(candidates, "rifle", false,
		Vector<String>(), starter, 0, 10.f);
	EXPECT_EQ(incomplete.source, STARTER);
	LoadoutChoice complete = chooseWeapon(candidates, "rifle", true,
		Vector<String>(), starter, 0, 10.f);
	EXPECT_EQ(complete.source, VAULT);
	EXPECT_EQ(complete.objectId, 33U);
}

TEST(SimBotLoadout, HysteresisKeepsCurrentVaultWeapon) {
	WeaponStats starter = weapon(0, "rifle", 10, 10, 1.f);
	WeaponStats current = weapon(41, "rifle", 20, 20, 1.f);
	WeaponStats nearUpgrade = weapon(42, "rifle", 21, 21, 1.f);
	std::vector<WeaponStats> candidates{current, nearUpgrade};
	LoadoutChoice choice = chooseWeapon(candidates, "rifle", false,
		Vector<String>(), starter, 41, 10.f);
	EXPECT_EQ(choice.objectId, 41U);
}

TEST(SimBotLoadout, HysteresisAppliesAgainstStarter) {
	WeaponStats starter = weapon(0, "rifle", 20, 20, 1.f);
	WeaponStats marginal = weapon(43, "rifle", 21, 21, 1.f);
	WeaponStats clear = weapon(44, "rifle", 30, 30, 1.f);
	std::vector<WeaponStats> marginalOnly{marginal};
	LoadoutChoice choice = chooseWeapon(marginalOnly, "rifle", false,
		Vector<String>(), starter, 0, 10.f);
	EXPECT_EQ(choice.source, STARTER);
	std::vector<WeaponStats> both{marginal, clear};
	choice = chooseWeapon(both, "rifle", false, Vector<String>(), starter, 0,
		10.f);
	EXPECT_EQ(choice.source, VAULT);
	EXPECT_EQ(choice.objectId, 44U);
}

TEST(SimBotLoadout, ProtectedSetKeepsEquippedAndBestPlannedClasses) {
	WeaponStats onehand = weapon(51, "onehand", 20, 20, 1.f);
	WeaponStats betterOnehand = weapon(52, "onehand", 30, 30, 1.f);
	WeaponStats rifle = weapon(53, "rifle", 40, 40, 1.f);
	std::vector<WeaponStats> candidates{onehand, betterOnehand, rifle};
	Vector<String> plan;
	plan.add("onehand");
	plan.add("rifle");
	std::vector<uint64_t> protectedIds = protectedSet(candidates, 51, plan,
		Vector<String>(), Vector<String>());
	EXPECT_EQ(protectedIds.size(), 3U);
	EXPECT_NE(std::find(protectedIds.begin(), protectedIds.end(), 51),
		protectedIds.end());
	EXPECT_NE(std::find(protectedIds.begin(), protectedIds.end(), 52),
		protectedIds.end());
	EXPECT_NE(std::find(protectedIds.begin(), protectedIds.end(), 53),
		protectedIds.end());
}

TEST(SimBotLoadout, UpgradeDecisionChecksClassCertsQualityAndBudget) {
	WeaponStats listing = weapon(61, "rifle", 30, 30, 1.f);
	listing.certificationsRequired.add("cert_rifle");
	Vector<String> held;
	EXPECT_EQ(upgradeDecision(listing, 10, 10.f, 1000, 0, 100, .5f,
		20000, 10.f, held, "rifle", false), UNCERTIFIED);
	held.add("cert_rifle");
	EXPECT_EQ(upgradeDecision(listing, 10, 10.f, 1000, 0, 100, .5f,
		20000, 10.f, held, "pistol", false), WRONG_CLASS);
	EXPECT_EQ(upgradeDecision(listing, 10, 10.f, 1000, 0, 100, .5f,
		20000, 10.f, held, "rifle", false), BUY);
	EXPECT_EQ(upgradeDecision(listing, 500, 10.f, 1000, 0, 100, .5f,
		20000, 10.f, held, "rifle", false), OVER_BUDGET);
	EXPECT_EQ(upgradeDecision(listing, 10, 10.f, 50, 0, 100, .5f,
		20000, 10.f, held, "rifle", false), UNAFFORDABLE);
}

TEST(SimBotLoadout, UpgradeDecisionUsesReservedBalanceAndBothCeilings) {
	WeaponStats listing = weapon(62, "rifle", 30, 30, 1.f);
	EXPECT_EQ(upgradeDecision(listing, 450, 10.f, 1000, 0, 100, .5f,
		20000, 10.f, Vector<String>(), "rifle", false), BUY);
	EXPECT_EQ(upgradeDecision(listing, 451, 10.f, 1000, 0, 100, .5f,
		20000, 10.f, Vector<String>(), "rifle", false), OVER_BUDGET);
	EXPECT_EQ(upgradeDecision(listing, 201, 10.f, 1000, 500, 100, .5f,
		20000, 10.f, Vector<String>(), "rifle", false), OVER_BUDGET);
	EXPECT_EQ(upgradeDecision(listing, 500, 10.f, 1000, 0, 100, .5f,
		400, 10.f, Vector<String>(), "rifle", false), OVER_BUDGET);
}

} // namespace
