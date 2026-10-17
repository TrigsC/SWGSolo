/*
 * Refusal codes shared by the native sim-bot bazaar buyer and its caller.
 */
#ifndef AUCTIONMANAGERBAZAAR_H_
#define AUCTIONMANAGERBAZAAR_H_

namespace AuctionManagerBazaar {

enum BuyResult {
	SUCCESS = 0,
	GONE,
	PRICE_CHANGED,
	OWN_LISTING,
	SELLER_UNRESOLVED,
	TRANSFER_FAILED,
	INVALID
};

}

#endif // AUCTIONMANAGERBAZAAR_H_
