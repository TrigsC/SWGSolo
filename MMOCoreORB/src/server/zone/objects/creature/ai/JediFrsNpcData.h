/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions.*/

#ifndef JEDIFRSNPCDATA_H_
#define JEDIFRSNPCDATA_H_

// Authoritative FRS skill-mod ladders used by ranked NPC Jedi. These are the
// same values granted by the player FRS skill boxes and reproduce the archived
// per-rank tables in docs/frs-rank-values-{light,dark}.txt.
namespace JediFrsNpcData {

static constexpr int RANK_COUNT = 12;

// Ranked NPC Jedi use generated template sabers rather than player-crafted
// weapons. The raw gen-4 templates carry 40-48 Force, which makes every
// master saber special cost 60-144 Force. Normalize the generated NPC weapon
// to a realistic crafted-saber baseline; command multipliers still apply.
inline constexpr float getNpcSaberForceCost() {
	return 10.f;
}

// Anti-Force defenses are reactive. A real incoming Force attack refreshes
// this memory, while the individual cooldowns prevent 60-second Feedback and
// Absorb buffs from being maintained continuously.
enum : int {
	FORCE_THREAT_MEMORY_MS = 20000,
	FORCE_FEEDBACK_RECAST_MS = 120000,
	FORCE_ABSORB_RECAST_MS = 180000
};

static constexpr int LIGHT_CONTROL[RANK_COUNT] =
	{ 5, 10, 15, 20, 25, 35, 45, 55, 70, 85, 100, 120 };
static constexpr int DARK_CONTROL[RANK_COUNT] =
	{ 4, 6, 8, 10, 12, 15, 20, 25, 35, 45, 60, 75 };
static constexpr int LIGHT_POWER[RANK_COUNT] =
	{ 4, 6, 8, 10, 12, 15, 20, 25, 35, 45, 60, 75 };
static constexpr int DARK_POWER[RANK_COUNT] =
	{ 5, 10, 15, 20, 25, 35, 45, 55, 70, 85, 100, 120 };
static constexpr int MANIPULATION[RANK_COUNT] =
	{ 5, 8, 12, 16, 20, 25, 30, 35, 45, 55, 65, 80 };
static constexpr int REGEN_BONUS[RANK_COUNT] =
	{ 1, 2, 3, 4, 5, 6, 8, 9, 12, 14, 17, 20 };

inline int clampRank(int rank) {
	if (rank < 0)
		return 0;

	if (rank >= RANK_COUNT)
		return RANK_COUNT - 1;

	return rank;
}

inline int getControl(bool lightCouncil, int rank) {
	rank = clampRank(rank);

	return lightCouncil ? LIGHT_CONTROL[rank] : DARK_CONTROL[rank];
}

inline int getPower(bool lightCouncil, int rank) {
	rank = clampRank(rank);

	return lightCouncil ? LIGHT_POWER[rank] : DARK_POWER[rank];
}

inline int getManipulation(int rank) {
	return MANIPULATION[clampRank(rank)];
}

inline int getRegenBonus(int rank) {
	return REGEN_BONUS[clampRank(rank)];
}

inline int getMaxForceBonus(bool lightCouncil, int rank) {
	return (getControl(lightCouncil, rank) + getPower(lightCouncil, rank)) * 10;
}

// Force commands may spend exactly to the calculated cost. Saber specials
// mirror CombatManager's player rule and require current Force to be strictly
// greater than the calculated floating-point weapon cost.
inline bool canAffordAttackCost(int currentForce, float predictedCost,
		bool requiresForceAboveCost) {
	return predictedCost <= 0 ||
		(currentForce >= predictedCost &&
		(!requiresForceAboveCost || currentForce > predictedCost));
}

// Mirrors JediQueueCommand's amount + round(control * modifier) calculation
// without introducing floating-point drift into permanent NPC resist mods.
inline int getRoundedControlBonus(int control, int modifierHundredths) {
	return ((control * modifierHundredths) + 50) / 100;
}

} // namespace JediFrsNpcData

#endif // JEDIFRSNPCDATA_H_
