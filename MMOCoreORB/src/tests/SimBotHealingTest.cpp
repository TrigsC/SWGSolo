#include "gtest/gtest.h"

#include "server/zone/objects/creature/simplayer/SimBotHealing.h"

namespace {

using namespace SimBotHealing;

TEST(SimBotHealingTest, ParsesTargetsAndRejectsOtherChatTiers) {
	HealVocabulary vocabulary;
	std::vector<SimBotGrouping::NamedBot> names{{17, "Paki"}, {23, "Towe"}};

	HealRequest request = parseHealCommand("heal me", vocabulary, names, true);
	EXPECT_EQ(request.command, HEAL_TARGET);
	EXPECT_EQ(request.patientKind, SPEAKER);
	EXPECT_TRUE(request.healerIdentityIds.empty());

	request = parseHealCommand("Paki, heal Towe", vocabulary, names, true);
	EXPECT_EQ(request.command, HEAL_TARGET);
	ASSERT_EQ(request.healerIdentityIds.size(), 1U);
	EXPECT_EQ(request.healerIdentityIds[0], 17U);
	EXPECT_EQ(request.patientKind, NAMED_BOT);
	EXPECT_EQ(request.patientName, "Towe");

	request = parseHealCommand("heal everyone", vocabulary, names, true);
	EXPECT_EQ(request.patientKind, EVERYONE);
	EXPECT_EQ(parseHealCommand("heal everyone", vocabulary, names, false).command,
		HEAL_NONE);
	EXPECT_EQ(parseHealCommand("Paki grab 2 kreetle missions", vocabulary, names,
		true).command, HEAL_NONE);
	EXPECT_EQ(parseHealCommand("rally at theed", vocabulary, names, true).command,
		HEAL_NONE);
}

TEST(SimBotHealingTest, AppliesThresholdsOnlyWhenThereIsDamage) {
	EXPECT_FALSE(needsHeal(0.20f, 0.20f, false, 0.70f));
	EXPECT_TRUE(needsHeal(0.70f, 1.0f, true, 0.70f));
	EXPECT_FALSE(needsHeal(0.71f, 0.71f, true, 0.70f));

	PlayerBotHealingPolicy policy;
	std::vector<PatientCandidate> candidates{
		{1, 101, false, 0.80f, 0.80f, true, false, 5.f, true, true, false, false},
		{2, 102, false, 0.40f, 0.90f, true, false, 5.f, true, true, true, false},
		{3, 103, true, 0.35f, 0.90f, true, false, 2.f, true, true, false, false}};

	PatientChoice choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, 2);
	EXPECT_EQ(choice.reason, "selected");

	candidates[2].healthFraction = 0.50f;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, 1);
}

TEST(SimBotHealingTest, OrdersExplicitPatientsAndExplainsRefusals) {
	PlayerBotHealingPolicy policy;
	std::vector<PatientCandidate> candidates{
		{1, 101, false, 0.80f, 0.80f, true, false, 5.f, true, true, false, true},
		{2, 102, false, 0.80f, 0.80f, true, false, 5.f, true, true, false, false}};
	PatientChoice choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, 0);

	candidates[0].healable = false;
	candidates[1].healable = false;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, -1);
	EXPECT_EQ(choice.reason, "faction");

	candidates[0].healable = true;
	candidates[1].healable = true;
	candidates[0].distanceMeters = 40.f;
	candidates[1].distanceMeters = 41.f;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, -1);
	EXPECT_EQ(choice.reason, "out_of_range");

	candidates[0].hasHealthDamage = false;
	candidates[1].hasHealthDamage = false;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.reason, "no_damage");
}

TEST(SimBotHealingTest, ExplainsMissesOnlyForPatientsWhoNeedAHeal) {
	PlayerBotHealingPolicy policy;
	// A scratched puller out of sight or past maxApproach is not a patient.
	std::vector<PatientCandidate> candidates{
		{1, 101, true, 1.0f, 1.0f, false, false, 0.f, true, true, false, false},
		{2, 102, false, 0.95f, 1.0f, true, false, 10.f, true, false, false, false},
		{3, 103, false, 0.90f, 1.0f, true, false, 45.f, true, true, false, false}};
	PatientChoice choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, -1);
	EXPECT_EQ(choice.reason, "nothing");
	EXPECT_EQ(choice.reasonIndex, -1);

	candidates[1].healthFraction = 0.50f;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.reason, "unreachable");
	EXPECT_EQ(choice.reasonIndex, 1);

	candidates[2].healthFraction = 0.40f;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.reason, "out_of_range");
	EXPECT_EQ(choice.reasonIndex, 2);

	candidates[1].losClear = true;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, 1);
	EXPECT_EQ(choice.reason, "selected");
}

TEST(SimBotHealingTest, NamedOrdersNeverHealSomeoneElse) {
	PlayerBotHealingPolicy policy;
	// "Paki, heal Towe": Towe (ordered) is unhurt; a badly hurt member beside
	// him is not a substitute.
	std::vector<PatientCandidate> candidates{
		{1, 101, false, 1.0f, 1.0f, false, false, 4.f, true, true, false, true},
		{2, 102, false, 0.20f, 0.90f, true, false, 4.f, true, true, true, false}};
	PatientChoice choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, -1);
	EXPECT_EQ(choice.reason, "no_damage");

	candidates[0].hasHealthDamage = true;
	candidates[0].healthFraction = 0.95f;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.index, 0);

	candidates[0].distanceMeters = 50.f;
	choice = chooseHealPatient(candidates, policy);
	EXPECT_EQ(choice.reason, "out_of_range");
	EXPECT_EQ(choice.reasonIndex, 0);
}

TEST(SimBotHealingTest, ChoosesBestUsableStimAndUsesPartialStacksFirst) {
	std::vector<StimStack> stacks{
		{101, 1, 10, 20, 300, false, false, false},
		{102, 2, 2, 20, 300, false, false, false},
		{103, 3, 4, 40, 900, false, false, false},
		{104, 4, 1, 10, 1200, true, false, false}};
	EXPECT_EQ(chooseStim(stacks, 20), 1);
	EXPECT_EQ(chooseStim(stacks, 10), -1);
	EXPECT_EQ(chooseStim(stacks, 40), 2);
}

TEST(SimBotHealingTest, ChoosesNearestReadyMedic) {
	std::vector<HealerCandidate> candidates{
		{11, true, true, true, 20.f},
		{12, true, false, true, 2.f},
		{13, true, true, false, 1.f},
		{14, true, true, true, 4.f}};
	EXPECT_EQ(chooseHealer(candidates), 3);
}

TEST(SimBotHealingTest, HealStepHasSeparateApproachAndApplyBudgets) {
	HealState state;
	HealInputs inputs;
	inputs.nowMs = 1000;
	state = healStep(state, inputs);
	EXPECT_EQ(state.stage, APPROACH);

	inputs.nowMs = 21001;
	state = healStep(state, inputs);
	EXPECT_EQ(state.stage, ABORT);
	EXPECT_EQ(state.reason, "unreachable");

	state = HealState();
	inputs = HealInputs();
	inputs.nowMs = 1000;
	inputs.withinApplyRange = true;
	state = healStep(state, inputs);
	EXPECT_EQ(state.stage, APPLY);

	inputs.nowMs = 9000;
	state = healStep(state, inputs);
	EXPECT_EQ(state.stage, ABORT);
	EXPECT_EQ(state.reason, "timeout");

	state = HealState();
	inputs = HealInputs();
	inputs.nowMs = 1000;
	inputs.patientDead = true;
	state = healStep(state, inputs);
	EXPECT_EQ(state.stage, ABORT);
	EXPECT_EQ(state.reason, "patient_dead");
}

TEST(SimBotHealingTest, PoolsUsableStimsForRestock) {
	Vector<PlayerBotHealingPolicy::StimEntry> entries;
	PlayerBotHealingPolicy::StimEntry low;
	low.templateCrc = 1;
	low.medicineUse = 5;
	PlayerBotHealingPolicy::StimEntry high;
	high.templateCrc = 2;
	high.medicineUse = 40;
	entries.add(low);
	entries.add(high);
	std::vector<uint32> pending;

	// Not a medic: nothing to buy.
	EXPECT_TRUE(stimRestockNeeds(entries, {}, 0, pending, 20).empty());

	// A low-skill medic shops only the tier it can use.
	std::vector<int> needs = stimRestockNeeds(entries, {}, 10, pending, 20);
	ASSERT_EQ(needs.size(), 1u);
	EXPECT_EQ(needs[0], 0);

	// Usable doses are pooled; unusable doses do not count.
	StimStack usable;
	usable.objectId = 9;
	usable.doses = 12;
	usable.medicineUseRequired = 5;
	StimStack unusable = usable;
	unusable.objectId = 10;
	unusable.medicineUseRequired = 40;
	EXPECT_EQ(stimRestockNeeds(entries, {usable, unusable}, 10, pending,
		20).size(), 1u);
	usable.doses = 20;
	EXPECT_TRUE(stimRestockNeeds(entries, {usable}, 10, pending, 20).empty());

	// A pending usable purchase blocks the next one.
	pending.push_back(1);
	EXPECT_TRUE(stimRestockNeeds(entries, {}, 10, pending, 20).empty());
}

TEST(SimBotHealingTest, PinsStockFactionTable) {
	EXPECT_FALSE(stockHealFactionVerdict(true, 1, FACTION_OVERT, 0,
		FACTION_ONLEAVE, true, false));
	EXPECT_TRUE(stockHealFactionVerdict(true, 1, FACTION_COVERT, 0,
		FACTION_ONLEAVE, true, false));
	EXPECT_FALSE(stockHealFactionVerdict(true, 1, FACTION_COVERT, 0,
		FACTION_ONLEAVE, true, true));
	EXPECT_FALSE(stockHealFactionVerdict(false, 1, FACTION_COVERT, 0,
		FACTION_ONLEAVE, true, false));
	EXPECT_TRUE(stockHealFactionVerdict(false, 1, FACTION_ONLEAVE, 0,
		FACTION_ONLEAVE, true, false));
}

} // namespace
