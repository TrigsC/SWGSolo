/*
 * Pure decisions used by the PlayerBot item-vault boot reconciliation pass.
 * Keep this header free of engine/container types so it can be exercised by
 * the small gtest without bringing up a zone server.
 */
#ifndef SIMBOTITEMRECONCILE_H_
#define SIMBOTITEMRECONCILE_H_

#include <stdint.h>

namespace SimBotItemReconcile {

enum RowState {
	ROW_HELD,
	ROW_PENDING,
	ROW_CONSUMED,
	ROW_LOST,
	ROW_ORPHANED
};

enum ObjectLocation {
	LOCATION_MISSING,
	LOCATION_IDENTITY_BAG,
	LOCATION_OTHER_BAG,
	LOCATION_DIRECT_VAULT,
	LOCATION_UNMAPPED_BAG
};

enum DecisionState {
	DECISION_UNCHANGED,
	DECISION_HELD,
	DECISION_LOST,
	DECISION_ORPHANED
};

struct Decision {
	DecisionState state = DECISION_UNCHANGED;
	uint64_t identityId = 0;
	int ledgerDelta = 0;
};

inline bool isCountedHeld(RowState state) {
	return state == ROW_HELD;
}

inline Decision decide(RowState rowState, uint64_t rowIdentityId,
		ObjectLocation location, uint64_t locationIdentityId,
		bool hasIdentityRecord) {
	(void)hasIdentityRecord;
	Decision decision;
	decision.identityId = rowIdentityId;

	if (rowState == ROW_LOST || rowState == ROW_ORPHANED)
		return decision;

	if (rowState == ROW_HELD && location == LOCATION_IDENTITY_BAG &&
			rowIdentityId != 0 && rowIdentityId == locationIdentityId)
		return decision;

	if ((rowState == ROW_PENDING || rowState == ROW_CONSUMED) &&
			location == LOCATION_IDENTITY_BAG && rowIdentityId != 0 &&
			rowIdentityId == locationIdentityId) {
		decision.state = DECISION_ORPHANED;
		decision.identityId = 0;
		decision.ledgerDelta = isCountedHeld(rowState) ? -1 : 0;
		return decision;
	}

	if (rowState == ROW_PENDING && location == LOCATION_MISSING) {
		decision.state = DECISION_LOST;
		decision.identityId = 0;
		return decision;
	}

	if (rowState == ROW_HELD) {
		decision.state = DECISION_ORPHANED;
		decision.identityId = 0;
		decision.ledgerDelta = -1;
		return decision;
	}

	if (rowState == ROW_CONSUMED && location != LOCATION_MISSING) {
		decision.state = DECISION_ORPHANED;
		decision.identityId = 0;
		return decision;
	}

	return decision;
}

inline Decision decideUntrackedObject(ObjectLocation location,
		uint64_t locationIdentityId, bool hasIdentityRecord) {
	Decision decision;
	if (location == LOCATION_IDENTITY_BAG && locationIdentityId != 0 &&
			hasIdentityRecord) {
		decision.state = DECISION_HELD;
		decision.identityId = locationIdentityId;
		decision.ledgerDelta = 1;
		return decision;
	}

	decision.state = DECISION_ORPHANED;
	return decision;
}

} // namespace SimBotItemReconcile

#endif // SIMBOTITEMRECONCILE_H_
