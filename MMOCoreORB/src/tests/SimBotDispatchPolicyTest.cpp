#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotDispatchPolicy.h"

using SimBotDispatchPolicy::ProgressionCandidate;

namespace {

ProgressionCandidate lair(const char* name, int minDifficulty,
		float demandScore = 0.f, bool samePlanet = false,
		const char* planet = "naboo") {
	ProgressionCandidate candidate;
	candidate.lairTemplate = name;
	candidate.planet = planet;
	candidate.minDifficulty = minDifficulty;
	candidate.demandScore = demandScore;
	candidate.samePlanet = samePlanet;
	return candidate;
}

}

// ---- isLairFightable --------------------------------------------------------

TEST(SimBotDispatchPolicyTest, PackLairIsOffLimitsToAPackAvoidingBot) {
	// The observed waste: a non-elite solo bot sent to the dalyrake lair.
	EXPECT_FALSE(SimBotDispatchPolicy::isLairFightable(true, true));
}

TEST(SimBotDispatchPolicyTest, PackLairIsOpenToAnEliteOrGroupedBot) {
	EXPECT_TRUE(SimBotDispatchPolicy::isLairFightable(true, false));
}

TEST(SimBotDispatchPolicyTest, NonPackLairIsAlwaysFightable) {
	EXPECT_TRUE(SimBotDispatchPolicy::isLairFightable(false, true));
	EXPECT_TRUE(SimBotDispatchPolicy::isLairFightable(false, false));
}

// ---- progressionCandidateBefore ---------------------------------------------

TEST(SimBotDispatchPolicyTest, HardestLairAtOrBelowTierWins) {
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("ikopi", 5), lair("gnort", 2), 6));
	EXPECT_FALSE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("gnort", 2), lair("ikopi", 5), 6));
}

TEST(SimBotDispatchPolicyTest, LairAtTierCountsAsAtOrBelow) {
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("ikopi", 6), lair("gnort", 2), 6));
}

TEST(SimBotDispatchPolicyTest, AnyLairAtOrBelowBeatsAnyLairAbove) {
	// Master TK starting a rifle: rifle tier 1. The easy lair wins even though
	// the harder one is still inside the +5 window the caller allows.
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("kreetle", 1), lair("gorg", 4), 1));
	EXPECT_FALSE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("gorg", 4), lair("kreetle", 1), 1));
}

TEST(SimBotDispatchPolicyTest, AmongLairsAboveTierTheEasiestWins) {
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("chuba", 3), lair("ikopi", 5), 1));
}

TEST(SimBotDispatchPolicyTest, XpFitOutranksDemand) {
	// Owner rule: progression is above resource extraction.
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("ikopi", 5, 0.f), lair("gnort", 2, 9999.f), 6));
}

TEST(SimBotDispatchPolicyTest, DemandBreaksAnXpTie) {
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("nuna", 4, 300.f), lair("chuba", 4, 100.f), 6));
	EXPECT_FALSE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("chuba", 4, 100.f), lair("nuna", 4, 300.f), 6));
}

TEST(SimBotDispatchPolicyTest, SamePlanetBreaksADemandTie) {
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(
		lair("paralope", 4, 0.f, true, "corellia"),
		lair("gorg", 4, 0.f, false, "tatooine"), 6));
}

TEST(SimBotDispatchPolicyTest, FullTieIsDeterministic) {
	ProgressionCandidate a = lair("ikopi", 5, 0.f, false, "naboo");
	ProgressionCandidate b = lair("ikopi", 5, 0.f, false, "rori");
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(a, b, 6));
	EXPECT_FALSE(SimBotDispatchPolicy::progressionCandidateBefore(b, a, 6));
	EXPECT_FALSE(SimBotDispatchPolicy::progressionCandidateBefore(a, a, 6));
}

TEST(SimBotDispatchPolicyTest, NonPackLairBeatsPackLairForAnEliteBot) {
	// Nordihi, tier 10: the level-10 HERD eopie lair (8-12) must win over the
	// level-12 PACK wrix lair (10-14) even though wrix is the better XP fit.
	ProgressionCandidate wrix = lair("talus_wrix_lair", 10, 500.f, true);
	wrix.isPack = true;
	ProgressionCandidate eopie = lair("tatooine_jundland_eopie_lair", 8);
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(eopie, wrix, 10));
	EXPECT_FALSE(SimBotDispatchPolicy::progressionCandidateBefore(wrix, eopie, 10));
}

TEST(SimBotDispatchPolicyTest, PackLairsStillRankAmongThemselvesByXpFit) {
	// With only pack lairs in the window, an elite bot still gets the best fit.
	ProgressionCandidate hard = lair("hard_pack", 10);
	ProgressionCandidate easy = lair("easy_pack", 6);
	hard.isPack = true;
	easy.isPack = true;
	EXPECT_TRUE(SimBotDispatchPolicy::progressionCandidateBefore(hard, easy, 10));
}

TEST(SimBotDispatchPolicyTest, PackLairIsNeverAFallbackWhileANonPackLairExists) {
	// Codex regression: two remote non-pack lairs whose relocations are refused
	// plus a same-planet pack lair. Packs are excluded before the retries, so
	// the refusals end in no_reachable_lair (grind), never in the pack lair.
	EXPECT_FALSE(SimBotDispatchPolicy::packLairsEligible({false, false, true}));
	EXPECT_FALSE(SimBotDispatchPolicy::packLairsEligible({true, false}));
}

TEST(SimBotDispatchPolicyTest, PackLairsAreEligibleWhenNothingElseFits) {
	EXPECT_TRUE(SimBotDispatchPolicy::packLairsEligible({true, true}));
	EXPECT_TRUE(SimBotDispatchPolicy::packLairsEligible({}));
}

// ---- normalizeGrindFallbackMinutes ------------------------------------------

TEST(SimBotDispatchPolicyTest, GrindFallbackWindowIsClamped) {
	EXPECT_EQ(SimBotDispatchPolicy::normalizeGrindFallbackMinutes(0), 5);
	EXPECT_EQ(SimBotDispatchPolicy::normalizeGrindFallbackMinutes(-3), 5);
	EXPECT_EQ(SimBotDispatchPolicy::normalizeGrindFallbackMinutes(30), 30);
	EXPECT_EQ(SimBotDispatchPolicy::normalizeGrindFallbackMinutes(9999), 240);
}

// ---- isTemplateComplete -----------------------------------------------------

namespace {

Vector<String> strings(std::initializer_list<const char*> items) {
	Vector<String> out;
	for (const char* item : items)
		out.add(item);
	return out;
}

}

TEST(SimBotDispatchPolicyTest, TemplateWithNothingLeftIsComplete) {
	EXPECT_TRUE(SimBotDispatchPolicy::isTemplateComplete(strings({}),
		strings({"medical"})));
}

TEST(SimBotDispatchPolicyTest, OnlyDeferredBoxesLeftIsComplete) {
	// Master TK + Master Rifleman + Medic 444x with every combat box trained:
	// what remains is paid in medical and crafting XP, which kills cannot earn.
	EXPECT_TRUE(SimBotDispatchPolicy::isTemplateComplete(
		strings({"medical", "medical", "crafting_medicine_general"}),
		strings({"medical", "crafting_medicine_general", "crafting_general"})));
}

TEST(SimBotDispatchPolicyTest, MasterOfOneProfessionIsNotTheTemplate) {
	// Master TK done, the rifle half of the template still to go.
	EXPECT_FALSE(SimBotDispatchPolicy::isTemplateComplete(
		strings({"combat_rifle", "combat_rifle", "medical"}),
		strings({"medical", "crafting_medicine_general"})));
}

TEST(SimBotDispatchPolicyTest, UnresolvedBoxBlocksCompletion) {
	EXPECT_FALSE(SimBotDispatchPolicy::isTemplateComplete(strings({""}),
		strings({"medical"})));
}

// ---- chooseGrindXpType ------------------------------------------------------

TEST(SimBotDispatchPolicyTest, GrindTierFollowsTheWeaponInHand) {
	// Master TK now grinding rifle: the rifle is equipped, so its stream sets
	// the tier even though the next box might be paid in combat_general.
	EXPECT_EQ(SimBotDispatchPolicy::chooseGrindXpType("combat_rifle",
		"combat_general", true), String("combat_rifle"));
}

TEST(SimBotDispatchPolicyTest, GrindTierFallsBackToTheNextBoxStream) {
	EXPECT_EQ(SimBotDispatchPolicy::chooseGrindXpType("",
		"combat_meleespecialize_unarmed", true),
		String("combat_meleespecialize_unarmed"));
	EXPECT_EQ(SimBotDispatchPolicy::chooseGrindXpType("",
		"combat_meleespecialize_unarmed", false), String());
}
