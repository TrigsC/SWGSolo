#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotTrainingOrder.h"

// Owner rule 2026-09-30. Teras Kasi, mapped from what each box actually grants
// rather than from its skill id, because the ids mislead:
//   xx4x Speed               combat_unarmed_ability    unarmed_speed
//   x1xx Balance Conditioning combat_unarmed_speed      combat_equillibrium
//   1xxx Meditation           combat_unarmed_accuracy   meditate
//   xxx1 combat-XP tree       combat_unarmed_support    combat_general XP

namespace {

Vector<String> tkPriority() {
	Vector<String> list;
	list.add("combat_unarmed_ability");
	list.add("combat_unarmed_speed");
	list.add("combat_unarmed_accuracy");
	list.add("combat_unarmed_support");
	return list;
}

}

TEST(SimBotTrainingOrderTest, SpeedLineRanksFirst) {
	// The line named "ability" is the Speed line -- it grants unarmed_speed.
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_ability_01",
		tkPriority()), 0);
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_ability_04",
		tkPriority()), 0);
}

TEST(SimBotTrainingOrderTest, DeclaredOrderIsHonoured) {
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_speed_02",
		tkPriority()), 1);
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_accuracy_03",
		tkPriority()), 2);
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_support_01",
		tkPriority()), 3);
}

TEST(SimBotTrainingOrderTest, UnlistedLineRanksAfterEveryListedLine) {
	// Novice and master belong to no line, so they rank after all of them. Kahn
	// still places novice first and master last because of prerequisites; rank
	// only breaks ties among boxes that are already ready.
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_novice",
		tkPriority()), 4);
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_brawler_unarmed_02",
		tkPriority()), 4);
}

TEST(SimBotTrainingOrderTest, PrefixMatchesOnlyAtAnUnderscoreBoundary) {
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_speedy_01",
		tkPriority()), 4);
}

TEST(SimBotTrainingOrderTest, EmptyPriorityRanksEverythingEqually) {
	// A plan that declares no priority must keep its cost-then-name order
	// exactly, so every skill ranks the same and the tie-break decides.
	Vector<String> none;
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_support_01", none), 0);
	EXPECT_EQ(SimBotTrainingOrder::lineRank("combat_unarmed_ability_01", none), 0);
}

// ---- pickParallelStep -------------------------------------------------------

namespace {

const char* kUnarmed = "combat_meleespecialize_unarmed";
const char* kGeneral = "combat_general";

SimBotTrainingOrder::TrainingCandidate box(const char* xpType, bool frontier,
		bool affordable) {
	SimBotTrainingOrder::TrainingCandidate candidate;
	candidate.xpType = xpType;
	candidate.frontier = frontier;
	candidate.affordable = affordable;
	return candidate;
}

}

TEST(SimBotTrainingOrderTest, ScarceStreamDoesNotBlockAnAbundantOne) {
	// The observed stall: support_01 (combat_general, 36k of 60k) sat ahead of
	// the unarmed lines while the bot held 370k unarmed. Different streams, so
	// the affordable unarmed head proceeds.
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kGeneral, true, false),   // support_01
		box(kUnarmed, true, true),    // ability_01 -- Speed 1
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), 1);
}

TEST(SimBotTrainingOrderTest, SameStreamCannotJumpItsOwnHead) {
	// Codex regression, round 1. After Speed 1, Speed 2 is not yet affordable
	// but Balance 1 is, and both spend UNARMED XP. The earlier revision picked
	// Balance 1 and broke the owner's "Speed to 4 first". Balance 1 is not the
	// head of the unarmed stream, so it must wait; with nothing else
	// affordable the bot earns toward Speed 2.
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kUnarmed, true, false),   // ability_02 -- Speed 2, the stream head
		box(kUnarmed, true, true),    // speed_01   -- Balance 1, same stream
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), 0);
}

TEST(SimBotTrainingOrderTest, OtherStreamStillProceedsWhileOneWaits) {
	// Same as above plus an affordable combat_general head: that one is taken,
	// because it spends a different pool -- the "trained separately" rule.
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kUnarmed, true, false),   // ability_02 -- Speed 2 (head, waiting)
		box(kUnarmed, true, true),    // speed_01   -- Balance 1 (not a head)
		box(kGeneral, true, true),    // support_01 -- combat-XP tree
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), 2);
}

TEST(SimBotTrainingOrderTest, AffordableHeadsAreTakenInPlanOrder) {
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kUnarmed, true, true),
		box(kGeneral, true, true),
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), 0);
}

TEST(SimBotTrainingOrderTest, NothingAffordableFallsBackToFirstHead) {
	// With nothing buyable the bot earns toward the first head by priority,
	// which is also what the weapon loadout is chosen to fund.
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kUnarmed, false, false),
		box(kUnarmed, true, false),
		box(kGeneral, true, false),
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), 1);
}

TEST(SimBotTrainingOrderTest, NonFrontierBoxIsNeverPickedEvenIfAffordable) {
	// A box whose prerequisites are not trained must not be bought just because
	// its XP is available -- that would buy Speed 3 before Speed 2.
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kUnarmed, false, true),
		box(kGeneral, true, false),
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), 1);
}

TEST(SimBotTrainingOrderTest, NonFrontierBoxDoesNotClaimItsStream) {
	// A blocked box is not the head, so a later frontier box on the same XP type
	// can still lead that stream.
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kUnarmed, false, false),
		box(kUnarmed, true, true),
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), 1);
}

TEST(SimBotTrainingOrderTest, NoFrontierMeansNothingToDo) {
	std::vector<SimBotTrainingOrder::TrainingCandidate> c = {
		box(kUnarmed, false, true),
		box(kGeneral, false, true),
	};
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(c), -1);
	std::vector<SimBotTrainingOrder::TrainingCandidate> none;
	EXPECT_EQ(SimBotTrainingOrder::pickParallelStep(none), -1);
}
