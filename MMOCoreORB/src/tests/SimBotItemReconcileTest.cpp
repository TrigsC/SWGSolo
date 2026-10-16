#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotItemReconcile.h"

namespace {
using namespace SimBotItemReconcile;

TEST(SimBotItemReconcile, HeldInOwningBagIsUnchanged) {
	Decision result = decide(ROW_HELD, 7, LOCATION_IDENTITY_BAG, 7, true);
	EXPECT_EQ(result.state, DECISION_UNCHANGED);
	EXPECT_EQ(result.identityId, 7U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, HeldMissingBecomesOrphanedAndDecrements) {
	Decision result = decide(ROW_HELD, 7, LOCATION_MISSING, 0, true);
	EXPECT_EQ(result.state, DECISION_ORPHANED);
	EXPECT_EQ(result.identityId, 0U);
	EXPECT_EQ(result.ledgerDelta, -1);
}

TEST(SimBotItemReconcile, HeldInAnotherBagBecomesOrphaned) {
	Decision result = decide(ROW_HELD, 7, LOCATION_OTHER_BAG, 8, true);
	EXPECT_EQ(result.state, DECISION_ORPHANED);
	EXPECT_EQ(result.identityId, 0U);
	EXPECT_EQ(result.ledgerDelta, -1);
}

TEST(SimBotItemReconcile, ListedInOwningAuctionRemainsListed) {
	Decision result = decide(ROW_LISTED, 7, LOCATION_LISTED, 7, true);
	EXPECT_EQ(result.state, DECISION_UNCHANGED);
	EXPECT_EQ(result.identityId, 7U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, HeldListedObjectBecomesHeld) {
	Decision result = decide(ROW_HELD, 7, LOCATION_LISTED, 7, true);
	EXPECT_EQ(result.state, DECISION_HELD);
	EXPECT_EQ(result.identityId, 7U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, ListedObjectBackInBagBecomesHeld) {
	Decision result = decide(ROW_LISTED, 7, LOCATION_IDENTITY_BAG, 7, true);
	EXPECT_EQ(result.state, DECISION_HELD);
	EXPECT_EQ(result.identityId, 7U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, ListedMissingObjectDecrementsHoldings) {
	Decision result = decide(ROW_LISTED, 7, LOCATION_MISSING, 0, true);
	EXPECT_EQ(result.state, DECISION_ORPHANED);
	EXPECT_EQ(result.identityId, 0U);
	EXPECT_EQ(result.ledgerDelta, -1);
}

TEST(SimBotItemReconcile, UntrackedListedObjectIsAdoptedAsListed) {
	Decision result = decideUntrackedObject(LOCATION_LISTED, 7, true);
	EXPECT_EQ(result.state, DECISION_LISTED);
	EXPECT_EQ(result.identityId, 7U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, ReservedRowInOwningBagBecomesOrphaned) {
	Decision pending = decide(ROW_PENDING, 7, LOCATION_IDENTITY_BAG, 7, true);
	Decision consumed = decide(ROW_CONSUMED, 7, LOCATION_IDENTITY_BAG, 7, true);
	EXPECT_EQ(pending.state, DECISION_ORPHANED);
	EXPECT_EQ(consumed.state, DECISION_ORPHANED);
	EXPECT_EQ(pending.ledgerDelta, 0);
	EXPECT_EQ(consumed.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, PendingMissingBecomesLost) {
	Decision result = decide(ROW_PENDING, 7, LOCATION_MISSING, 0, true);
	EXPECT_EQ(result.state, DECISION_LOST);
	EXPECT_EQ(result.identityId, 0U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, ConsumedInAnyBagBecomesOrphaned) {
	Decision result = decide(ROW_CONSUMED, 7, LOCATION_OTHER_BAG, 8, true);
	EXPECT_EQ(result.state, DECISION_ORPHANED);
	EXPECT_EQ(result.identityId, 0U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, ConsumedMissingIsUnchanged) {
	Decision result = decide(ROW_CONSUMED, 7, LOCATION_MISSING, 0, true);
	EXPECT_EQ(result.state, DECISION_UNCHANGED);
	EXPECT_EQ(result.identityId, 7U);
}

TEST(SimBotItemReconcile, LostAndOrphanedRowsAreStable) {
	Decision lost = decide(ROW_LOST, 0, LOCATION_DIRECT_VAULT, 0, false);
	Decision orphaned = decide(ROW_ORPHANED, 0, LOCATION_UNMAPPED_BAG, 0, false);
	EXPECT_EQ(lost.state, DECISION_UNCHANGED);
	EXPECT_EQ(orphaned.state, DECISION_UNCHANGED);
}

TEST(SimBotItemReconcile, UntrackedOwningBagRecoversHeldRow) {
	Decision result = decideUntrackedObject(LOCATION_IDENTITY_BAG, 7, true);
	EXPECT_EQ(result.state, DECISION_HELD);
	EXPECT_EQ(result.identityId, 7U);
	EXPECT_EQ(result.ledgerDelta, 1);
}

TEST(SimBotItemReconcile, UntrackedObjectWithoutRecordIsOrphaned) {
	Decision result = decideUntrackedObject(LOCATION_IDENTITY_BAG, 7, false);
	EXPECT_EQ(result.state, DECISION_ORPHANED);
	EXPECT_EQ(result.identityId, 0U);
	EXPECT_EQ(result.ledgerDelta, 0);
}

TEST(SimBotItemReconcile, UnmappedLocationsAreOrphaned) {
	Decision direct = decideUntrackedObject(LOCATION_DIRECT_VAULT, 0, false);
	Decision bag = decideUntrackedObject(LOCATION_UNMAPPED_BAG, 0, false);
	EXPECT_EQ(direct.state, DECISION_ORPHANED);
	EXPECT_EQ(bag.state, DECISION_ORPHANED);
}
} // namespace
