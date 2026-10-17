/*
 * Pure bazaar decisions used by the PlayerBot sell surface.
 *
 * Keep this header independent from Core3/engine types.  The live bazaar and
 * progression-store code have deliberately complicated lifetimes; these
 * small state and pricing functions make the policy reviewable and testable
 * without a zone, object store, or database.
 */
#ifndef SIMBOTBAZAAR_H_
#define SIMBOTBAZAAR_H_

#include <algorithm>
#include <cmath>
#include <stdint.h>

namespace SimBotBazaar {

static const int JUNKGENERIC = 1;

enum SkipReason {
	SKIP_NONE,
	SKIP_JUNK,
	SKIP_NO_VALUE,
	SKIP_NO_TRADE,
	SKIP_NOT_HELD,
	SKIP_HUMAN_ABOVE_CAP,
	SKIP_INVALID_PRICE
};

struct PriceDecision {
	int price = 0;
	SkipReason reason = SKIP_NONE;
	int base = 0;
	int floor = 0;
};

inline bool isSellable(int junkValue, int junkDealerMask, bool noTrade,
		bool rowHeld) {
	return rowHeld && junkValue > 0 &&
		(junkDealerMask & JUNKGENERIC) == 0 && !noTrade;
}

inline int clampPrice(int price, int minPrice, int maxPrice) {
	if (maxPrice < 1)
		return 0;
	minPrice = std::max(1, minPrice);
	if (minPrice > maxPrice)
		minPrice = maxPrice;
	return std::max(minPrice, std::min(maxPrice, price));
}

inline PriceDecision priceListing(int junkValue, float markup, int minPrice,
		int maxPrice, int humanFloor, int persistedAsk = 0) {
	PriceDecision decision;
	decision.floor = humanFloor;
	if (junkValue <= 0) {
		decision.reason = SKIP_NO_VALUE;
		return decision;
	}
	if (humanFloor > maxPrice) {
		decision.reason = SKIP_HUMAN_ABOVE_CAP;
		return decision;
	}

	markup = std::max(0.1f, std::min(100.f, markup));
	int base = static_cast<int>(std::lround(static_cast<double>(junkValue) * markup));
	decision.base = clampPrice(base, minPrice, maxPrice);
	int anchor = persistedAsk > 0 ? clampPrice(persistedAsk, minPrice, maxPrice) :
		decision.base;
	decision.price = std::max(anchor,
			clampPrice(humanFloor, minPrice, maxPrice));
	if (decision.price < 1) {
		decision.reason = SKIP_INVALID_PRICE;
		return decision;
	}
	return decision;
}

enum PurchaseReason {
	BUY,
	OVER_CEILING,
	NO_CEILING,
	UNAFFORDABLE,
	OWN_LISTING
};

inline int buyCeiling(int junkValue, float markup, int minPrice,
		int maxPrice, int entryMaxPrice) {
	if (maxPrice < 1)
		return 0;

	int ceiling = 0;
	if (junkValue > 0) {
		markup = std::max(0.1f, std::min(100.f, markup));
		ceiling = static_cast<int>(std::lround(
			static_cast<double>(junkValue) * markup));
	} else if (entryMaxPrice > 0) {
		ceiling = entryMaxPrice;
	} else {
		return 0;
	}

	return clampPrice(ceiling, minPrice, maxPrice);
}

inline PurchaseReason decidePurchase(int price, int ceiling, int64_t bank,
		int64_t reserved, int64_t reserveCredits, bool ownListing) {
	if (ownListing)
		return OWN_LISTING;
	if (ceiling <= 0)
		return NO_CEILING;
	if (price > ceiling)
		return OVER_CEILING;
	if (price > bank - reserved - reserveCredits)
		return UNAFFORDABLE;
	return BUY;
}

inline int unmetNeed(int quantity, int ownedCount, int pendingCount) {
	return std::max(0, quantity - ownedCount - pendingCount);
}

inline bool reservedForNeed(int rank, int quantity) {
	return rank >= 0 && rank < quantity;
}

inline bool settlementDropsItemRow(uint64_t rowIdentity,
		uint64_t sellerIdentity) {
	return rowIdentity == sellerIdentity;
}

inline bool isAppliedLedgerRow(uint64_t rowId, uint64_t appliedMark,
		bool voided) {
	return rowId != 0 && !voided && rowId <= appliedMark;
}

inline bool hasCreditReceipt(int64_t receiptSum) {
	return receiptSum > 0;
}

inline bool hasItemReceipt(int64_t receiptSum) {
	return receiptSum < 0;
}

struct ReceiptDecision {
	bool writeCredit = false;
	bool writeItem = false;
};

inline ReceiptDecision decideReceiptWrites(int64_t creditReceiptSum,
		int64_t itemReceiptSum) {
	ReceiptDecision decision;
	decision.writeCredit = !hasCreditReceipt(creditReceiptSum);
	decision.writeItem = !hasItemReceipt(itemReceiptSum);
	return decision;
}

inline uint64_t retainInsertedHighWater(uint64_t retained,
		uint64_t inserted) {
	return std::max(retained, inserted);
}

inline int listingFee(int baseFee, bool hasMerchant1, bool hasMerchant3) {
	float reduction = hasMerchant3 ? 0.60f : (hasMerchant1 ? 0.80f : 1.0f);
	return static_cast<int>(baseFee * reduction);
}

enum ListingState {
	PENDING,
	LISTED,
	SOLD,
	RETRIEVED,
	ORPHANED
};

enum ListingEvent {
	PUBLISHED,
	PUBLISH_FAILED,
	SOLD_EVENT,
	RETRIEVED_EVENT,
	RECONCILED_LISTED,
	RECONCILED_SOLD,
	RECONCILED_RETRIEVED,
	RECONCILED_ORPHANED
};

inline ListingState applyListingEvent(ListingState state, ListingEvent event) {
	// A sale is durable evidence from the object store.  Once observed it wins
	// over a late publish callback or a stale retrieval callback.
	if (state == SOLD)
		return SOLD;
	if (event == SOLD_EVENT || event == RECONCILED_SOLD)
		return SOLD;

	switch (event) {
	case PUBLISHED:
		return state == SOLD ? SOLD : LISTED;
	case PUBLISH_FAILED:
		return RETRIEVED;
	case RETRIEVED_EVENT:
	case RECONCILED_RETRIEVED:
		return RETRIEVED;
	case RECONCILED_ORPHANED:
		return ORPHANED;
	case RECONCILED_LISTED:
		return LISTED;
	case SOLD_EVENT:
	case RECONCILED_SOLD:
		return SOLD;
	}
	return state;
}

enum RowState {
	ROW_NONE,
	ROW_LISTED,
	ROW_SOLD,
	ROW_RETRIEVED,
	ROW_ORPHANED
};

enum AuctionStatus {
	AUCTION_MISSING,
	AUCTION_FORSALE,
	AUCTION_SOLD,
	AUCTION_EXPIRED,
	AUCTION_RETRIEVED
};

enum ObjectLocation {
	LOCATION_MISSING,
	LOCATION_IDENTITY_BAG,
	LOCATION_OTHER_BAG,
	LOCATION_LISTED,
	LOCATION_PARENTLESS,
	LOCATION_BUYER
};

enum ReconcileDecision {
	RECONCILE_UNCHANGED,
	RECONCILE_SETTLE,
	RECONCILE_SETTLE_FROM_BUYER,
	RECONCILE_RELEASE,
	RECONCILE_RETRIEVED,
	RECONCILE_ORPHANED,
	RECONCILE_REVERSE,
	RECONCILE_ADOPT_LISTED,
	RECONCILE_ADOPT_SOLD
};

inline ReconcileDecision decideListingReconcile(RowState rowState,
		AuctionStatus auctionStatus, bool ownerIsThisBag,
		int64_t creditReceiptSum, int64_t itemReceiptSum,
		ObjectLocation objectLocation, bool bagMatchesIdentity,
		bool buyerEvidence = false) {
	const bool creditReceipt = creditReceiptSum > 0;
	const bool itemReceipt = itemReceiptSum < 0;
	const bool bothReceipts = creditReceipt && itemReceipt;
	const bool liveBotListing = auctionStatus == AUCTION_FORSALE &&
		ownerIsThisBag;
	const bool heldBotSale = auctionStatus == AUCTION_SOLD && ownerIsThisBag;

	if (liveBotListing && (creditReceipt || itemReceipt))
		return RECONCILE_REVERSE;
	// A row-less held sale is adopted first (acquisition repair), then settled.
	if (rowState == ROW_NONE && heldBotSale)
		return RECONCILE_ADOPT_SOLD;
	if (heldBotSale)
		return RECONCILE_SETTLE;
	if (rowState == ROW_SOLD && auctionStatus == AUCTION_SOLD &&
			!ownerIsThisBag)
		return RECONCILE_RELEASE;
	if (rowState == ROW_SOLD && (auctionStatus == AUCTION_MISSING ||
			auctionStatus == AUCTION_RETRIEVED))
		return RECONCILE_RELEASE;

	if (rowState == ROW_LISTED && auctionStatus == AUCTION_FORSALE &&
			ownerIsThisBag)
		return RECONCILE_UNCHANGED;
	if (rowState == ROW_LISTED && auctionStatus == AUCTION_MISSING &&
			objectLocation == LOCATION_IDENTITY_BAG && bagMatchesIdentity)
		return RECONCILE_RETRIEVED;
	if (rowState == ROW_LISTED && auctionStatus == AUCTION_MISSING &&
			objectLocation == LOCATION_OTHER_BAG && buyerEvidence)
		return RECONCILE_SETTLE_FROM_BUYER;
	if (rowState == ROW_LISTED && auctionStatus == AUCTION_MISSING)
		return RECONCILE_ORPHANED;

	// A row-less object is adopted from the object store.  A held sale is
	// settled immediately after adoption; a live listing only needs its row.
	if (rowState == ROW_NONE && auctionStatus == AUCTION_FORSALE &&
			!ownerIsThisBag && !bagMatchesIdentity)
		return RECONCILE_ORPHANED;
	if (rowState == ROW_NONE && liveBotListing)
		return RECONCILE_ADOPT_LISTED;
	if (rowState == ROW_SOLD && auctionStatus == AUCTION_SOLD && bothReceipts)
		return RECONCILE_RELEASE;

	return RECONCILE_UNCHANGED;
}

enum SettlementOperation {
	SETTLEMENT_FLUSH,
	SETTLEMENT_SALE,
	SETTLEMENT_SWEEP,
	SETTLEMENT_RECONCILE,
	SETTLEMENT_DELETION_RETRIEVAL,
	SETTLEMENT_PURCHASE_RESOLVE,
	SETTLEMENT_NEW_PURCHASE,
	SETTLEMENT_NEW_LISTING,
	SETTLEMENT_AWARD,
	SETTLEMENT_TRAINING
};

inline bool settlementModeAllows(SettlementOperation operation) {
	// A gate-off settlement load may only make already-promised value durable:
	// flush pending rows, settle/reconcile sales, sweep/retrieve listings, and
	// perform deletion-path retrieval. It never awards, trains, or creates a
	// new listing.
	return operation == SETTLEMENT_FLUSH || operation == SETTLEMENT_SALE ||
		operation == SETTLEMENT_SWEEP || operation == SETTLEMENT_RECONCILE ||
		operation == SETTLEMENT_DELETION_RETRIEVAL ||
		operation == SETTLEMENT_PURCHASE_RESOLVE;
}

enum PurchaseReconcileDecision {
	COMMIT,
	ALREADY_COMMITTED,
	REFUND,
	DONE
};

// Boot resolution of an applied, unrefunded purchase debit. An auction OID is
// never reused, so a listing that still carries it proves the object store
// reverted the buy (or never ran it): refund, whatever MySQL recorded. The
// item receipt proves a commit that later moves of the item cannot undo;
// without one, delivery is proved only by the object sitting in the bag.
inline PurchaseReconcileDecision decidePurchaseReconcile(int64_t netDebit,
		bool auctionStillListed, bool objectInIdentityBag,
		int64_t itemReceiptSum) {
	if (netDebit >= 0)
		return DONE;
	if (auctionStillListed)
		return REFUND;
	if (itemReceiptSum > 0)
		return ALREADY_COMMITTED;
	return objectInIdentityBag ? COMMIT : REFUND;
}

inline bool mayReleaseHeldSale(bool creditReceiptApplied,
		bool itemReceiptApplied, bool sellerResolvable, bool buyerPresent) {
	// A held sale is the only durable evidence of the bot's sale, so it is
	// released only once both receipts are applied, or when the seller can
	// never be paid. A missing buyer does not hasten release: the buyer-gone
	// path inside releaseSimBotSale runs after the receipts are durable.
	(void)buyerPresent;
	return !sellerResolvable || (creditReceiptApplied && itemReceiptApplied);
}

inline bool shouldSweep(AuctionStatus status, uint64_t expireAt,
		uint64_t now, bool gateOn, bool drainOnDisable) {
	if (status == AUCTION_EXPIRED)
		return true;
	if (status != AUCTION_FORSALE)
		return false;
	if (!gateOn && drainOnDisable)
		return true;
	return expireAt != 0 && expireAt <= now;
}

} // namespace SimBotBazaar

#endif // SIMBOTBAZAAR_H_
