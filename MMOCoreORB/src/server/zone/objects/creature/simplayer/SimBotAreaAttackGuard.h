/*
 * SimBotAreaAttackGuard.h
 *
 * F_0.9.8d: a bot must not clip its own mission lair with an area or cone
 * special while it is fighting the lair's creatures.
 *
 * Owner observation 2026-10-11, confirmed in the log: Nordihi, the only bot
 * using unarmedSpinAttack2 (areaRange 16), made all 25 of its lair hits while
 * TARGETING A CREATURE and none on purpose; every other bot's 225 lair hits were
 * deliberate. A lair spawns its second wave on the first point of damage and its
 * third past half condition, so a stray area hit stacks every wave on a bot that
 * is still clearing the first one -- which is what the wave-by-wave tactics
 * exist to prevent. It went 0 for 6 on those lairs.
 *
 * The hunter controller publishes the lair's position while the lair is revealed
 * and the bot is NOT deliberately attacking it; AiAgent's special-attack
 * selector skips any area/cone command whose reach would include that point.
 * Header-only and a LEAF: its mutex is never held while any other lock is taken.
 */

#ifndef SIMBOTAREAATTACKGUARD_H_
#define SIMBOTAREAATTACKGUARD_H_

#include <atomic>

#include "system/io/ObjectInputStream.h"
#include "system/io/ObjectOutputStream.h"
#include "system/lang/System.h"
#include "system/thread/Locker.h"
#include "system/thread/Mutex.h"
#include "system/thread/atomic/AtomicLong.h"
#include "system/util/VectorMap.h"

struct SimBotAreaAttackProtected {
	float x = 0.f;
	float y = 0.f;
	uint64 updatedMs = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

class SimBotAreaAttackGuard {
private:
	// Bounded so a body that never clears its entry cannot grow the map.
	enum { MAX_TRACKED = 64 };

	static std::atomic<bool>& enabledStorage() {
		static std::atomic<bool> enabled(false);
		return enabled;
	}

	static Mutex& mutexStorage() {
		static Mutex mutex;
		return mutex;
	}

	static VectorMap<uint64, SimBotAreaAttackProtected>& pointsStorage() {
		static VectorMap<uint64, SimBotAreaAttackProtected> points;
		return points;
	}

	static AtomicLong& skipsStorage() {
		static AtomicLong skips;
		return skips;
	}

public:
	// Lair footprint plus the offset of an area centred on a melee target
	// rather than on the bot.
	static constexpr float MARGIN_METERS = 8.f;

	// ---- pure rule (unit-tested) -------------------------------------------

	// How far the command can land, or -1 for a single-target command. This
	// MIRRORS the range CombatManager::getAreaTargets computes (CombatManager.cpp
	// ~977) step for step, so the guard and the engine cannot disagree:
	//   range = areaRange; a cone uses coneRange, or the command range when
	//   coneRange is unset (-1); with a weapon, a still-negative range becomes the
	//   weapon's max range, splash damage adds the command range, and a thrown
	//   or heavy weapon reaches weapon max range + areaRange.
	static float reachMeters(bool isAreaAction, bool isConeAction, int areaRange,
			int coneRange, int commandRange, bool hasWeapon, int weaponMaxRange,
			bool splashDamage, bool thrownOrHeavyWeapon) {
		if (!isAreaAction && !isConeAction)
			return -1.f;
		int range = areaRange;
		if (isConeAction)
			range = coneRange > -1 ? coneRange : commandRange;
		if (hasWeapon) {
			if (range < 0)
				range = weaponMaxRange;
			if (splashDamage)
				range += commandRange;
			if (thrownOrHeavyWeapon)
				range = weaponMaxRange + areaRange;
		}
		// The engine hits nothing with a negative range.
		return range < 0 ? -1.f : static_cast<float>(range);
	}

	static bool endangers(float reach, float distanceToProtected) {
		return reach >= 0.f && distanceToProtected <= reach + MARGIN_METERS;
	}

	// ---- store ---------------------------------------------------------------

	static bool isEnabled() {
		return enabledStorage().load(std::memory_order_acquire);
	}

	static void setEnabled(bool value) {
		enabledStorage().store(value, std::memory_order_release);
		if (!value) {
			Locker lock(&mutexStorage());
			pointsStorage().removeAll();
		}
	}

	static void protect(uint64 bodyOid, float x, float y) {
		if (bodyOid == 0 || !isEnabled())
			return;
		Locker lock(&mutexStorage());
		VectorMap<uint64, SimBotAreaAttackProtected>& points = pointsStorage();
		if (!points.contains(bodyOid)) {
			while (points.size() >= MAX_TRACKED) {
				int oldest = 0;
				for (int i = 1; i < points.size(); ++i) {
					if (points.elementAt(i).getValue().updatedMs <
							points.elementAt(oldest).getValue().updatedMs)
						oldest = i;
				}
				points.remove(oldest);
			}
			points.put(bodyOid, SimBotAreaAttackProtected());
		}
		SimBotAreaAttackProtected& point = points.get(bodyOid);
		point.x = x;
		point.y = y;
		point.updatedMs = System::getMiliTime();
	}

	static void release(uint64 bodyOid) {
		Locker lock(&mutexStorage());
		pointsStorage().drop(bodyOid);
	}

	static bool lookup(uint64 bodyOid, float& x, float& y) {
		if (!isEnabled())
			return false;
		Locker lock(&mutexStorage());
		VectorMap<uint64, SimBotAreaAttackProtected>& points = pointsStorage();
		if (!points.contains(bodyOid))
			return false;
		const SimBotAreaAttackProtected& point = points.get(bodyOid);
		x = point.x;
		y = point.y;
		return true;
	}

	static void recordSkip() { skipsStorage().increment(); }
	static uint64 skips() { return skipsStorage().get(); }
};

#endif /* SIMBOTAREAATTACKGUARD_H_ */
