#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotMissionPlacement.h"

using namespace SimBotMissionPlacement;

namespace {

float arcDelta(float bearing, float terminalBearing) {
	float delta = std::fabs(bearing - terminalBearing);
	while (delta > 180.f)
		delta = std::fabs(delta - 360.f);
	return delta;
}

}

TEST(SimBotMissionPlacementTest, EveryBearingFallsInsideTheArc) {
	// The live Mos Eisley terminals: atan2 bearings, negative included.
	const float terminals[] = {26.6325f, -40.8002f, -92.3673f, -175.938f, 179.f};
	for (float terminal : terminals) {
		for (int i = 0; i <= 100; ++i) {
			const float bearing = bearingInArc(terminal, 60.f, i / 100.f);
			EXPECT_GE(bearing, 0.f);
			EXPECT_LT(bearing, 360.f);
			EXPECT_LE(arcDelta(bearing, terminal), 60.01f);
		}
	}
}

TEST(SimBotMissionPlacementTest, ArcEndsAndCentreMapToTheSample) {
	EXPECT_NEAR(bearingInArc(90.f, 60.f, 0.f), 30.f, 0.01f);
	EXPECT_NEAR(bearingInArc(90.f, 60.f, 0.5f), 90.f, 0.01f);
	EXPECT_NEAR(bearingInArc(90.f, 60.f, 1.f), 150.f, 0.01f);
	// Wraps below zero.
	EXPECT_NEAR(bearingInArc(-40.f, 60.f, 0.f), 260.f, 0.01f);
}

TEST(SimBotMissionPlacementTest, WideArcIsTheFullCircle) {
	EXPECT_NEAR(bearingInArc(0.f, 500.f, 0.f), 180.f, 0.01f);
	EXPECT_NEAR(bearingInArc(0.f, 500.f, 0.75f), 90.f, 0.01f);
}

TEST(SimBotMissionPlacementTest, SearchWidensEvery32TriesUpToTheCap) {
	EXPECT_FLOAT_EQ(revealSearchHalfWidth(0), 128.f);
	EXPECT_FLOAT_EQ(revealSearchHalfWidth(31), 128.f);
	EXPECT_FLOAT_EQ(revealSearchHalfWidth(32), 256.f);
	EXPECT_FLOAT_EQ(revealSearchHalfWidth(64), 512.f);
	EXPECT_FLOAT_EQ(revealSearchHalfWidth(REVEAL_MAX_TRIES - 1), 512.f);
	EXPECT_FLOAT_EQ(revealSearchHalfWidth(-5), 128.f);
}

TEST(SimBotMissionPlacementTest, OffsetSpansBothSidesOfTheWaypoint) {
	EXPECT_FLOAT_EQ(revealOffset(128.f, 0.f), 128.f);
	EXPECT_FLOAT_EQ(revealOffset(128.f, 0.5f), 0.f);
	EXPECT_FLOAT_EQ(revealOffset(128.f, 1.f), -128.f);
}
