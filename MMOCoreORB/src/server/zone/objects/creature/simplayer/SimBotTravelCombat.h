/*
 * SimBotTravelCombat.h
 *
 * Pure helpers for F_0.9.8c travel combat, kept free of agents, zones and locks
 * so they can be asserted directly in core3tests.
 *
 * Live evidence 2026-10-09: 8 of 9 deaths happened on the walk to a mission,
 * never at the lair. On a travel leg nothing evaluated HAM, so a bot fought to
 * the death, and when a fight ended it walked on at whatever HAM it had left.
 *
 * Owner rules: on foot a bot stops and fights (the base mover already holds a
 * bot in combat); one whose ranged weapon has any "_hit_while_moving" may fight
 * on the run; below the retreat threshold it retreats TOWARD the mission, not
 * back to town, so it does not rubberband through the same creatures; there is
 * no cap on travel retreats.
 */

#ifndef SIMBOTTRAVELCOMBAT_H_
#define SIMBOTTRAVELCOMBAT_H_

#include <cmath>

namespace SimBotTravelCombat {

enum Mode {
	MOVE = 0,
	FLEE = 1,
	REST = 2,
};

inline const char* modeName(int mode) {
	switch (mode) {
	case FLEE: return "FLEE";
	case REST: return "REST";
	default: return "MOVE";
	}
}

struct Inputs {
	// A live, eligible creature is attacking the bot.
	bool attacked = false;
	// Lowest HAM pool is below the retreat threshold.
	bool belowRetreat = false;
	// Lowest HAM pool is below the post-fight rest threshold.
	bool belowRest = false;
	// HAM is back to resumeHamPct.
	bool readyToResume = false;
	// FLEE only: the flee leg has ended (arrived, or its path failed).
	bool fleeArrived = false;
};

inline Mode nextMode(Mode current, const Inputs& in) {
	if (current == FLEE) {
		if (!in.fleeArrived)
			return FLEE;
		// Still chased at the flee point: flee again (the caller picks a new
		// point). Otherwise recover before walking on.
		return in.attacked ? FLEE : REST;
	}

	if (in.attacked && in.belowRetreat)
		return FLEE;

	if (current == REST) {
		// Attacked while resting but not yet in retreat range: fight it out
		// under the base in-combat hold; the rest resumes when it is over.
		if (in.attacked)
			return MOVE;
		return in.readyToResume ? MOVE : REST;
	}

	// MOVE. While attacked the base mover already holds the bot and it fights.
	// A fight that ended low sends the bot to rest instead of walking into the
	// next creature at that HAM.
	if (!in.attacked && in.belowRest)
		return REST;
	return MOVE;
}

// Fight on the run: a ranged weapon whose "<weapon>_hit_while_moving" mod is
// above zero (Core3 applies it to the moving-attack penalty,
// CombatManager.cpp:1956). Any value counts (owner). Melee always stops.
inline bool weaponFightsOnTheRun(bool isMeleeWeapon, int hitWhileMovingMod) {
	return !isMeleeWeapon && hitWhileMovingMod > 0;
}

// ---- lair retreat (F_0.9.8e, owner 2026-10-11) ------------------------------
// A bot retreating from a lair fight must not stand and wait to die. Seven lair
// deaths in one run came 21-57s into a retreat, five of them to wildlife that
// had wandered in (which the wave de-aggro, scoped to the lair's own creatures,
// never sheds): the retreat ran one 40m leg and then rested, neither moving
// again nor fighting back.

enum RetreatStep {
	RETREAT_REST = 0,
	RETREAT_REFLEE = 1,
	RETREAT_DEFEND = 2,
};

// The retreat leg just ended. Still chased: run again, up to maxReflees legs
// per retreat. Otherwise rest.
inline RetreatStep retreatLegEnded(bool chased, int reflees, int maxReflees) {
	return chased && reflees < maxReflees ? RETREAT_REFLEE : RETREAT_REST;
}

// While resting. Attacked with HAM to spare: fight back. Attacked below the
// retreat threshold: run again -- and once the legs are spent, fight anyway,
// because standing still is the one thing a player would never do.
inline RetreatStep whileResting(bool attacked, bool belowRetreat, int reflees,
		int maxReflees) {
	if (!attacked)
		return RETREAT_REST;
	if (belowRetreat && reflees < maxReflees)
		return RETREAT_REFLEE;
	return RETREAT_DEFEND;
}

// Mop-up hold (owner 2026-10-10): in MOVE a bot does not walk while a creature
// is aimed at it -- the lair's survivors after the lair dies, or wildlife on the
// road. A hold that has run maxHoldMs is EXHAUSTED and stays released until
// nothing is aimed at the bot any more, so a creature that targets the bot but
// can never reach it cannot pin it until the phase TTL.
inline bool mopUpExhausted(bool attacked, bool wasExhausted,
		unsigned long long heldMs, unsigned long long maxHoldMs) {
	if (!attacked)
		return false;
	return wasExhausted || heldMs >= maxHoldMs;
}

inline bool mopUpHold(bool attacked, bool fightOnTheRun, bool exhausted) {
	return attacked && !fightOnTheRun && !exhausted;
}

// The next flee point, on the ground plane (x east, y north).
//  - Forward: fleeDistance along the bearing to the destination.
//  - Attacker ahead (within 60 degrees of that bearing): veer 60 degrees to the
//    side away from it, still gaining ground.
//  - Destination closer than two flee distances: go to the side instead, so a
//    fleeing bot does not run into the lair's own camp.
inline void fleePoint(float posX, float posY, float destX, float destY,
		bool hasAttacker, float attackerX, float attackerY, float fleeDistance,
		float& outX, float& outY) {
	float dx = destX - posX;
	float dy = destY - posY;
	float destDistance = std::sqrt(dx * dx + dy * dy);
	float ax = attackerX - posX;
	float ay = attackerY - posY;
	float attackerDistance = std::sqrt(ax * ax + ay * ay);
	bool attackerKnown = hasAttacker && attackerDistance > 0.01f;

	float fx = 1.f;
	float fy = 0.f;
	if (destDistance > 0.01f) {
		fx = dx / destDistance;
		fy = dy / destDistance;
	} else if (attackerKnown) {
		fx = -ax / attackerDistance;
		fy = -ay / attackerDistance;
	}

	// The perpendicular on the side away from the attacker (left if none).
	float px = -fy;
	float py = fx;
	if (attackerKnown && px * ax + py * ay > 0.f) {
		px = -px;
		py = -py;
	}

	float dirX = fx;
	float dirY = fy;
	if (destDistance < 2.f * fleeDistance) {
		dirX = px;
		dirY = py;
	} else if (attackerKnown &&
			(fx * ax + fy * ay) / attackerDistance > 0.5f) {
		// cos 60 = 0.5, sin 60 = 0.866: 60 degrees off the bearing.
		dirX = fx * 0.5f + px * 0.8660254f;
		dirY = fy * 0.5f + py * 0.8660254f;
	}

	outX = posX + dirX * fleeDistance;
	outY = posY + dirY * fleeDistance;
}

}

#endif /* SIMBOTTRAVELCOMBAT_H_ */
