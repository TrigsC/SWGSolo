#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotBazaar.h"

namespace {
using namespace SimBotBazaar;

TEST(SimBotBazaar, PriceUsesMarkupAndMinimum) {
	PriceDecision result = priceListing(100, 3.f, 50, 20000, 0);
	EXPECT_EQ(result.reason, SKIP_NONE);
	EXPECT_EQ(result.base, 300);
	EXPECT_EQ(result.price, 300);
	EXPECT_EQ(priceListing(1, 1.f, 50, 20000, 0).price, 50);
	EXPECT_EQ(priceListing(10000, 3.f, 50, 20000, 0).price, 20000);
	EXPECT_EQ(priceListing(100, 0.01f, 50, 20000, 0).base, 50);
	EXPECT_EQ(priceListing(1000, 200.f, 50, 20000, 0).base, 20000);
}

TEST(SimBotBazaar, HumanFloorNeverUndercut) {
	EXPECT_EQ(priceListing(100, 3.f, 50, 20000, 200).price, 300);
	EXPECT_EQ(priceListing(100, 3.f, 50, 20000, 400).price, 400);
}

TEST(SimBotBazaar, PersistedAskIsTheRelistAnchor) {
	EXPECT_EQ(priceListing(100, 10.f, 50, 20000, 0, 300).price, 300);
	EXPECT_EQ(priceListing(100, 1.f, 50, 20000, 400, 300).price, 400);
}

TEST(SimBotBazaar, HumanAboveCapSkips) {
	PriceDecision result = priceListing(100, 3.f, 50, 20000, 20001);
	EXPECT_EQ(result.reason, SKIP_HUMAN_ABOVE_CAP);
	EXPECT_EQ(priceListing(100, 3.f, 50, 20000, 20000).price, 20000);
}

TEST(SimBotBazaar, ZeroValueSkips) {
	EXPECT_EQ(priceListing(0, 3.f, 50, 20000, 0).reason, SKIP_NO_VALUE);
}

TEST(SimBotBazaar, GenericJunkIsNotSellable) {
	EXPECT_FALSE(isSellable(100, JUNKGENERIC, false, true));
	EXPECT_FALSE(isSellable(100, JUNKGENERIC | 4, false, true));
	EXPECT_TRUE(isSellable(100, 0, false, true));
	EXPECT_FALSE(isSellable(100, 0, true, true));
	EXPECT_FALSE(isSellable(100, 0, false, false));
}

TEST(SimBotBazaar, MerchantFeeReductionMatchesStock) {
	EXPECT_EQ(listingFee(20, false, false), 20);
	EXPECT_EQ(listingFee(20, true, false), 16);
	EXPECT_EQ(listingFee(20, false, true), 12);
}

TEST(SimBotBazaar, PublishAndSaleSettlementWinsInEitherOrder) {
	EXPECT_EQ(applyListingEvent(PENDING, PUBLISHED), LISTED);
	EXPECT_EQ(applyListingEvent(LISTED, SOLD_EVENT), SOLD);
	EXPECT_EQ(applyListingEvent(PENDING, SOLD_EVENT), SOLD);
	EXPECT_EQ(applyListingEvent(SOLD, PUBLISHED), SOLD);
	EXPECT_EQ(applyListingEvent(PENDING, PUBLISH_FAILED), RETRIEVED);
	EXPECT_EQ(applyListingEvent(SOLD, RETRIEVED_EVENT), SOLD);
}

TEST(SimBotBazaar, ReconcileListedRows) {
	EXPECT_EQ(decideListingReconcile(ROW_LISTED, AUCTION_FORSALE, true,
		0, 0, LOCATION_LISTED, true), RECONCILE_UNCHANGED);
	EXPECT_EQ(decideListingReconcile(ROW_NONE, AUCTION_FORSALE, true,
		0, 0, LOCATION_LISTED, true), RECONCILE_ADOPT_LISTED);
	EXPECT_EQ(decideListingReconcile(ROW_NONE, AUCTION_FORSALE, false,
		0, 0, LOCATION_OTHER_BAG, false), RECONCILE_ORPHANED);
	EXPECT_EQ(decideListingReconcile(ROW_LISTED, AUCTION_MISSING, false,
		0, 0, LOCATION_IDENTITY_BAG, true), RECONCILE_RETRIEVED);
	EXPECT_EQ(decideListingReconcile(ROW_LISTED, AUCTION_MISSING, false,
		0, 0, LOCATION_MISSING, true), RECONCILE_ORPHANED);
	EXPECT_EQ(decideListingReconcile(ROW_LISTED, AUCTION_FORSALE, true,
		5, 0, LOCATION_LISTED, true), RECONCILE_REVERSE);
}

TEST(SimBotBazaar, ReconcileSaleReceipts) {
	EXPECT_EQ(decideListingReconcile(ROW_LISTED, AUCTION_SOLD, true,
		0, 0, LOCATION_LISTED, true), RECONCILE_SETTLE);
	EXPECT_EQ(decideListingReconcile(ROW_NONE, AUCTION_SOLD, true,
		0, 0, LOCATION_LISTED, true), RECONCILE_ADOPT_SOLD);
	EXPECT_EQ(decideListingReconcile(ROW_SOLD, AUCTION_SOLD, false,
		25, -1, LOCATION_BUYER, true), RECONCILE_RELEASE);
	EXPECT_EQ(decideListingReconcile(ROW_LISTED, AUCTION_FORSALE, true,
		100, -1, LOCATION_LISTED, true), RECONCILE_REVERSE);
	EXPECT_EQ(decideListingReconcile(ROW_SOLD, AUCTION_MISSING, false,
		100, -1, LOCATION_MISSING, true), RECONCILE_RELEASE);
	EXPECT_EQ(decideListingReconcile(ROW_SOLD, AUCTION_RETRIEVED, false,
		100, -1, LOCATION_MISSING, true), RECONCILE_RELEASE);
}

TEST(SimBotBazaar, AppliedMarksAndReceiptSignsAreExplicit) {
	EXPECT_TRUE(isAppliedLedgerRow(10, 10, false));
	EXPECT_FALSE(isAppliedLedgerRow(11, 10, false));
	EXPECT_FALSE(isAppliedLedgerRow(10, 10, true));
	EXPECT_FALSE(isAppliedLedgerRow(11, 10, true));
	EXPECT_TRUE(hasCreditReceipt(1));
	EXPECT_FALSE(hasCreditReceipt(0));
	EXPECT_TRUE(hasItemReceipt(-1));
	EXPECT_FALSE(hasItemReceipt(1));
}

TEST(SimBotBazaar, PartialReceiptWritesAreIndependent) {
	ReceiptDecision none = decideReceiptWrites(0, 0);
	EXPECT_TRUE(none.writeCredit);
	EXPECT_TRUE(none.writeItem);
	ReceiptDecision creditOnly = decideReceiptWrites(25, 0);
	EXPECT_FALSE(creditOnly.writeCredit);
	EXPECT_TRUE(creditOnly.writeItem);
	ReceiptDecision itemOnly = decideReceiptWrites(0, -1);
	EXPECT_TRUE(itemOnly.writeCredit);
	EXPECT_FALSE(itemOnly.writeItem);
	ReceiptDecision both = decideReceiptWrites(25, -1);
	EXPECT_FALSE(both.writeCredit);
	EXPECT_FALSE(both.writeItem);
}

TEST(SimBotBazaar, VoidRowsAboveTheAppliedMarkNeverCount) {
	EXPECT_FALSE(isAppliedLedgerRow(42, 41, false));
	int64_t creditSum = isAppliedLedgerRow(42, 41, false) ? 42 : 0;
	EXPECT_EQ(creditSum, 0);
	EXPECT_TRUE(decideReceiptWrites(creditSum, 0).writeCredit);
	EXPECT_TRUE(decideReceiptWrites(creditSum, 0).writeItem);
}

TEST(SimBotBazaar, HighWaterNeverMovesBackAfterFailure) {
	EXPECT_EQ(retainInsertedHighWater(10, 7U), 10U);
	EXPECT_EQ(retainInsertedHighWater(10, 12U), 12U);
	EXPECT_EQ(retainInsertedHighWater(retainInsertedHighWater(10, 12U), 11U),
		12U);
}

TEST(SimBotBazaar, SettlementModeExcludesNewMutations) {
	EXPECT_TRUE(settlementModeAllows(SETTLEMENT_SALE));
	EXPECT_TRUE(settlementModeAllows(SETTLEMENT_RECONCILE));
	EXPECT_TRUE(settlementModeAllows(SETTLEMENT_DELETION_RETRIEVAL));
	EXPECT_FALSE(settlementModeAllows(SETTLEMENT_NEW_LISTING));
	EXPECT_FALSE(settlementModeAllows(SETTLEMENT_AWARD));
}

TEST(SimBotBazaar, HeldSaleReleaseRequiresBothReceiptsUnlessUnresolvable) {
	EXPECT_FALSE(mayReleaseHeldSale(true, false, true, true));
	EXPECT_TRUE(mayReleaseHeldSale(true, true, true, true));
	EXPECT_TRUE(mayReleaseHeldSale(false, false, false, true));
	// A gone buyer never releases before the seller's receipts are applied.
	EXPECT_FALSE(mayReleaseHeldSale(false, false, true, false));
	EXPECT_TRUE(mayReleaseHeldSale(true, true, true, false));
	// A void sale row above the applied mark is not a paid receipt.
	EXPECT_FALSE(mayReleaseHeldSale(false, true, true, true));
}

TEST(SimBotBazaar, SweepHandlesExpiryAndDrain) {
	EXPECT_TRUE(shouldSweep(AUCTION_FORSALE, 10, 10, true, false));
	EXPECT_TRUE(shouldSweep(AUCTION_FORSALE, 100, 10, false, true));
	EXPECT_TRUE(shouldSweep(AUCTION_EXPIRED, 0, 0, true, false));
	EXPECT_FALSE(shouldSweep(AUCTION_SOLD, 10, 10, false, true));
}

} // namespace
