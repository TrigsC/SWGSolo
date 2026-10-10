#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotTravelCombat.h"

using namespace SimBotTravelCombat;

namespace {

Inputs inputs(bool attacked, bool belowRetreat, bool belowRest,
		bool readyToResume, bool fleeArrived = false) {
	Inputs in;
	in.attacked = attacked;
	in.belowRetreat = belowRetreat;
	in.belowRest = belowRest;
	in.readyToResume = readyToResume;
	in.fleeArrived = fleeArrived;
	return in;
}

}

// ---- nextMode -----------------------------------------------------------

TEST(SimBotTravelCombatTest, AttackedWithHamStaysMovingAndFightsUnderTheHold) {
	EXPECT_EQ(nextMode(MOVE, inputs(true, false, true, false)), MOVE);
}

TEST(SimBotTravelCombatTest, AttackedBelowRetreatFlees) {
	EXPECT_EQ(nextMode(MOVE, inputs(true, true, true, false)), FLEE);
	EXPECT_EQ(nextMode(REST, inputs(true, true, true, false)), FLEE);
}

TEST(SimBotTravelCombatTest, FleeingContinuesUntilTheLegEnds) {
	EXPECT_EQ(nextMode(FLEE, inputs(true, true, true, false, false)), FLEE);
	EXPECT_EQ(nextMode(FLEE, inputs(false, true, true, false, false)), FLEE);
}

TEST(SimBotTravelCombatTest, StillChasedAtTheFleePointFleesAgain) {
	// No cap on travel retreats (owner).
	EXPECT_EQ(nextMode(FLEE, inputs(true, true, true, false, true)), FLEE);
}

TEST(SimBotTravelCombatTest, ClearAtTheFleePointRests) {
	EXPECT_EQ(nextMode(FLEE, inputs(false, true, true, false, true)), REST);
}

TEST(SimBotTravelCombatTest, FightEndingLowRestsInsteadOfWalkingOn) {
	// The "powering through": a fight won at low HAM, then the next creature.
	EXPECT_EQ(nextMode(MOVE, inputs(false, false, true, false)), REST);
}

TEST(SimBotTravelCombatTest, FightEndingHealthyKeepsWalking) {
	EXPECT_EQ(nextMode(MOVE, inputs(false, false, false, false)), MOVE);
}

TEST(SimBotTravelCombatTest, RestEndsWhenRecovered) {
	EXPECT_EQ(nextMode(REST, inputs(false, false, false, false)), REST);
	EXPECT_EQ(nextMode(REST, inputs(false, false, false, true)), MOVE);
}

TEST(SimBotTravelCombatTest, AttackedWhileRestingWithHamFights) {
	EXPECT_EQ(nextMode(REST, inputs(true, false, true, false)), MOVE);
}

// ---- weaponFightsOnTheRun -------------------------------------------------

TEST(SimBotTravelCombatTest, MeleeNeverFightsOnTheRun) {
	EXPECT_FALSE(weaponFightsOnTheRun(true, 50));
}

TEST(SimBotTravelCombatTest, RangedFightsOnTheRunWithAnyHitWhileMoving) {
	EXPECT_TRUE(weaponFightsOnTheRun(false, 1));
	EXPECT_FALSE(weaponFightsOnTheRun(false, 0));
}

// ---- fleePoint ------------------------------------------------------------

TEST(SimBotTravelCombatTest, FleesForwardTowardTheMission) {
	// Destination 1000m north, attacker behind (south).
	float x = 0.f, y = 0.f;
	fleePoint(0.f, 0.f, 0.f, 1000.f, true, 0.f, -10.f, 40.f, x, y);
	EXPECT_NEAR(x, 0.f, 0.01f);
	EXPECT_NEAR(y, 40.f, 0.01f);
}

TEST(SimBotTravelCombatTest, VeersAwayWhenTheAttackerIsAhead) {
	// Attacker ahead and slightly east: veer 60 degrees WEST, still north.
	float x = 0.f, y = 0.f;
	fleePoint(0.f, 0.f, 0.f, 1000.f, true, 3.f, 10.f, 40.f, x, y);
	EXPECT_LT(x, -30.f);
	EXPECT_GT(y, 0.f);
	EXPECT_NEAR(std::sqrt(x * x + y * y), 40.f, 0.05f);
}

TEST(SimBotTravelCombatTest, GoesSidewaysNearTheDestination) {
	// 60m from the lair with a 40m flee: sideways, away from an attacker east.
	float x = 0.f, y = 0.f;
	fleePoint(0.f, 0.f, 0.f, 60.f, true, 10.f, 0.f, 40.f, x, y);
	EXPECT_NEAR(x, -40.f, 0.01f);
	EXPECT_NEAR(y, 0.f, 0.01f);
}

TEST(SimBotTravelCombatTest, AtTheDestinationFleesAwayFromTheAttacker) {
	float x = 0.f, y = 0.f;
	fleePoint(0.f, 0.f, 0.f, 0.f, true, 0.f, 10.f, 40.f, x, y);
	EXPECT_NEAR(std::sqrt(x * x + y * y), 40.f, 0.05f);
	EXPECT_LE(y, 0.01f);
}

// ---- mop-up hold ------------------------------------------------------------

TEST(SimBotTravelCombatTest, BotDoesNotWalkAwayWhileSomethingIsAimedAtIt) {
	// The lair just died and its survivors are still on the bot.
	EXPECT_TRUE(mopUpHold(true, false, false));
	EXPECT_FALSE(mopUpHold(false, false, false));
}

TEST(SimBotTravelCombatTest, FightOnTheRunWeaponKeepsWalking) {
	EXPECT_FALSE(mopUpHold(true, true, false));
}

TEST(SimBotTravelCombatTest, HoldGivesUpAfterItsLimit) {
	EXPECT_FALSE(mopUpExhausted(true, false, 179000, 180000));
	EXPECT_TRUE(mopUpExhausted(true, false, 180000, 180000));
	EXPECT_FALSE(mopUpHold(true, false, true));
}

TEST(SimBotTravelCombatTest, ExhaustedHoldStaysReleasedUntilClear) {
	// Still aimed at: stays released even though the timer restarted.
	EXPECT_TRUE(mopUpExhausted(true, true, 0, 180000));
	// Clear: the next attack may hold again.
	EXPECT_FALSE(mopUpExhausted(false, true, 0, 180000));
}
