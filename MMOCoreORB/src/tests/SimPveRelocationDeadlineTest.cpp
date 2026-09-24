#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimPveRelocationDeadline.h"

// These exist because routing this setting through clampMinerInt() was a real
// shipped defect: that helper maps 0 to "keep the current value", so the
// documented `relocationDeadlineSeconds = 0` silently meant 1800 and the reaper
// could not be turned off at all. The zero case below is the regression test.

namespace {
const int kDefault = 1800;
}

TEST(SimPveRelocationDeadlineTest, ZeroDisablesTheReaper) {
	// The whole point: 0 must survive as 0, not fall back to the default.
	EXPECT_EQ(SimPveRelocationDeadline::normalize(0, kDefault), 0);
}

TEST(SimPveRelocationDeadlineTest, AbsentFieldKeepsTheDefault) {
	// getIntField returns the supplied default when the key is missing, so the
	// configured value and the fallback are the same number in that case.
	EXPECT_EQ(SimPveRelocationDeadline::normalize(kDefault, kDefault), kDefault);
}

TEST(SimPveRelocationDeadlineTest, SmallPositiveIsRaisedToTheFloor) {
	// A typo must not start cancelling healthy multi-leg relocations mid-flight.
	EXPECT_EQ(SimPveRelocationDeadline::normalize(1, kDefault),
		SimPveRelocationDeadline::kMinSeconds);
	EXPECT_EQ(SimPveRelocationDeadline::normalize(59, kDefault),
		SimPveRelocationDeadline::kMinSeconds);
}

TEST(SimPveRelocationDeadlineTest, FloorItselfIsHonouredExactly) {
	EXPECT_EQ(SimPveRelocationDeadline::normalize(60, kDefault), 60);
}

TEST(SimPveRelocationDeadlineTest, OrdinaryValuePassesThrough) {
	EXPECT_EQ(SimPveRelocationDeadline::normalize(900, kDefault), 900);
	EXPECT_EQ(SimPveRelocationDeadline::normalize(1800, kDefault), 1800);
}

TEST(SimPveRelocationDeadlineTest, CeilingIsClamped) {
	EXPECT_EQ(SimPveRelocationDeadline::normalize(
		SimPveRelocationDeadline::kMaxSeconds, kDefault),
		SimPveRelocationDeadline::kMaxSeconds);
	EXPECT_EQ(SimPveRelocationDeadline::normalize(
		SimPveRelocationDeadline::kMaxSeconds + 1, kDefault),
		SimPveRelocationDeadline::kMaxSeconds);
	EXPECT_EQ(SimPveRelocationDeadline::normalize(999999999, kDefault),
		SimPveRelocationDeadline::kMaxSeconds);
}

TEST(SimPveRelocationDeadlineTest, NegativeIsMalformedAndKeepsTheDefault) {
	// Distinct from 0: a negative is not a deliberate disable, so it must not
	// silently turn the safety net off.
	EXPECT_EQ(SimPveRelocationDeadline::normalize(-1, kDefault), kDefault);
	EXPECT_EQ(SimPveRelocationDeadline::normalize(-86400, kDefault), kDefault);
}

TEST(SimPveRelocationDeadlineTest, DisableIsReachableFromAnyDefault) {
	// Guards against a future refactor reintroducing "0 means keep current" by
	// threading the fallback back into the zero branch.
	EXPECT_EQ(SimPveRelocationDeadline::normalize(0, 60), 0);
	EXPECT_EQ(SimPveRelocationDeadline::normalize(0, 86400), 0);
	EXPECT_EQ(SimPveRelocationDeadline::normalize(0, 0), 0);
}
