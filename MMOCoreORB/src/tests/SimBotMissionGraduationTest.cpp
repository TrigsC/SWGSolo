#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotMissionGraduation.h"

// The F_0.9.8 rule: a planned bot takes missions once it has box 4 in ONE line
// of brawler or marksman (owner decision 2026-09-22). Most of these cases are
// the ones that must NOT qualify -- the owner's stated concern was an untrained
// or thinly trained novice going out on missions.

namespace {

Vector<String> prefixes() {
	Vector<String> list;
	list.add("combat_brawler");
	list.add("combat_marksman");
	return list;
}

Vector<String> skills(std::initializer_list<const char*> names) {
	Vector<String> list;
	for (const char* name : names)
		list.add(name);
	return list;
}

}

TEST(SimBotMissionGraduationTest, UntrainedNoviceHasNoDepth) {
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({}), prefixes()), 0);
	// The novice box is a profession entry, not a line box, so it never counts.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(
		skills({"combat_brawler_novice"}), prefixes()), 0);
}

TEST(SimBotMissionGraduationTest, FullLineReachesDepthFour) {
	// Exactly what live identities 84 and 274 carry: a 4xxx brawler.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_novice", "combat_brawler_unarmed_01",
		"combat_brawler_unarmed_02", "combat_brawler_unarmed_03",
		"combat_brawler_unarmed_04"}), prefixes()), 4);
}

TEST(SimBotMissionGraduationTest, ThreeDeepIsBelowAThresholdOfFour) {
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_novice", "combat_brawler_unarmed_01",
		"combat_brawler_unarmed_02", "combat_brawler_unarmed_03"}),
		prefixes()), 3);
}

TEST(SimBotMissionGraduationTest, SpreadThinAcrossLinesDoesNotAddUp) {
	// Four boxes, but only one deep in each line. The owner chose depth in one
	// line over a box count precisely so this bot does NOT qualify.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_novice", "combat_brawler_unarmed_01",
		"combat_brawler_1handmelee_01", "combat_brawler_2handmelee_01",
		"combat_brawler_polearm_01"}), prefixes()), 1);
}

TEST(SimBotMissionGraduationTest, LineNameContainingADigitStillParses) {
	// "1handmelee" puts a digit inside the line name; depth must come from the
	// trailing _0N only.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_1handmelee_04"}), prefixes()), 4);
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_2handmelee_02"}), prefixes()), 2);
}

TEST(SimBotMissionGraduationTest, MarksmanLinesCount) {
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_marksman_novice", "combat_marksman_rifle_04"}),
		prefixes()), 4);
}

TEST(SimBotMissionGraduationTest, MasterBoxCarriesNoDepth) {
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_master"}), prefixes()), 0);
}

TEST(SimBotMissionGraduationTest, UnlistedProfessionIsIgnored) {
	// Teras Kasi is an elite profession, not brawler, so its boxes do not
	// satisfy a brawler/marksman rule; neither does a medic box.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_unarmed_novice", "combat_unarmed_speed_04",
		"science_medic_injury_04"}), prefixes()), 0);
}

TEST(SimBotMissionGraduationTest, PrefixMatchesOnlyAtAnUnderscoreBoundary) {
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawlerx_unarmed_04"}), prefixes()), 0);
}

TEST(SimBotMissionGraduationTest, MalformedSuffixesAreRejected) {
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_unarmed_4", "combat_brawler_unarmed_004",
		"combat_brawler_unarmed_0x", "combat_brawler_unarmed_00"}),
		prefixes()), 0);
}

TEST(SimBotMissionGraduationTest, NoListedProfessionsMeansNoDepth) {
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_unarmed_04"}), Vector<String>()), 0);
}

// The same helper decides when pack-aggro avoidance LIFTS: once the bot has a
// line box in an elite profession ("xx1x Teras Kasi"), it is expected to hold a
// whole lair's aggro rather than dodge pack creatures (owner rule 2026-09-22).

namespace {

Vector<String> elitePrefixes() {
	Vector<String> list;
	list.add("combat_unarmed");
	list.add("combat_rifleman");
	return list;
}

}

TEST(SimBotMissionGraduationTest, EliteNoviceBoxAloneIsNotCapable) {
	// A 4xxx brawler that has only just opened Teras Kasi is still a novice
	// elite: the novice box carries no line depth, so avoidance stays on.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_unarmed_04", "combat_unarmed_novice"}),
		elitePrefixes()), 0);
}

TEST(SimBotMissionGraduationTest, OneEliteLineBoxMakesTheBotCapable) {
	// "xx1x Teras Kasi" -- one box in an elite line clears the threshold of 1.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_unarmed_novice", "combat_unarmed_speed_01"}),
		elitePrefixes()), 1);
}

TEST(SimBotMissionGraduationTest, BasicProfessionBoxesAreNotEliteCapable) {
	// A full 4xxx brawler with no elite training must NOT count as capable.
	EXPECT_EQ(SimBotMissionGraduation::maxLineDepth(skills({
		"combat_brawler_novice", "combat_brawler_unarmed_01",
		"combat_brawler_unarmed_02", "combat_brawler_unarmed_03",
		"combat_brawler_unarmed_04"}), elitePrefixes()), 0);
}
