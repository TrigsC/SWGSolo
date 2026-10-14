#include "gtest/gtest.h"

#include <vector>

#include "server/zone/objects/creature/simplayer/SimBotLootStep.h"

using namespace SimBotLootStep;

TEST(SimBotLootStep, SelectsNearestUnexpiredClaimWithinDetour) {
	ClaimCandidate first;
	first.corpseOid = 101;
	first.x = 20.f;
	first.expiresAtMs = 5000;
	ClaimCandidate second;
	second.corpseOid = 102;
	second.x = 4.f;
	second.y = 3.f;
	second.expiresAtMs = 5000;
	ClaimCandidate third;
	third.corpseOid = 103;
	third.x = 1.f;
	third.y = 1.f;
	third.expiresAtMs = 5000;
	std::vector<ClaimCandidate> claims;
	claims.push_back(first);
	claims.push_back(second);
	claims.push_back(third);

	EXPECT_EQ(2, selectNearest(claims, 0.f, 0.f, 10.f, 1000));
}

TEST(SimBotLootStep, SkipsExpiredAndOverDetourClaims) {
	ClaimCandidate expired;
	expired.corpseOid = 101;
	expired.x = 1.f;
	expired.y = 1.f;
	expired.expiresAtMs = 1000;
	ClaimCandidate distant;
	distant.corpseOid = 102;
	distant.x = 100.f;
	distant.expiresAtMs = 5000;
	std::vector<ClaimCandidate> claims;
	claims.push_back(expired);
	claims.push_back(distant);

	EXPECT_EQ(-1, selectNearest(claims, 0.f, 0.f, 10.f, 1000));
}

TEST(SimBotLootStep, ExitReasonsHaveStablePriority) {
	EXPECT_EQ(EXIT_BOT_DEAD, decideExit(false, true, false, 100, 200, 300));
	EXPECT_EQ(EXIT_NEW_ATTACKER, decideExit(true, true, true, 100, 200, 300));
	EXPECT_EQ(EXIT_CORPSE_GONE, decideExit(true, false, false, 100, 200, 300));
	EXPECT_EQ(EXIT_CLAIM_EXPIRED, decideExit(true, false, true, 200, 200, 300));
	EXPECT_EQ(EXIT_TIMEOUT, decideExit(true, false, true, 300, 400, 300));
	EXPECT_EQ(EXIT_NONE, decideExit(true, false, true, 100, 200, 300));
}
