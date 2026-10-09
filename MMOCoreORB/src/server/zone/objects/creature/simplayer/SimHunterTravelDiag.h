/*
 * SimHunterTravelDiag.h
 *
 * F_0.9.8b diagnostic: what happens to a mission bot between accepting a
 * contract and reaching its lair. Live evidence 2026-10-09: 8 of 9 deaths came
 * 3-6 minutes after OFFERS_ACCEPT with no lair reveal and no kill by the bot --
 * it dies on the walk out, where retreat is never evaluated (only HUNTING
 * checks it) and onTick's interceptor self-defense fights back while the bot
 * keeps walking. The owner has also watched attackers stop and re-engage
 * repeatedly without ever fully disengaging; resetInterceptorCombat sheds every
 * defender bilaterally whenever no attacker is selectable, which would produce
 * exactly that.
 *
 * Per-body travel-combat tally, written by the controller and taken once by
 * SimPlayerManager::onPveHunterDied for the HUNTER_DEATH line. Header-only and
 * a LEAF: its mutex is never held while any other lock is taken. Active only
 * while missiondiag logging is on.
 */

#ifndef SIMHUNTERTRAVELDIAG_H_
#define SIMHUNTERTRAVELDIAG_H_

#include <atomic>

#include "system/io/ObjectInputStream.h"
#include "system/io/ObjectOutputStream.h"
#include "system/lang/String.h"
#include "system/lang/System.h"
#include "system/thread/Locker.h"
#include "system/thread/Mutex.h"
#include "system/util/VectorMap.h"

struct SimHunterTravelTally {
	uint64 updatedMs = 0;
	int engages = 0;
	int disengages = 0;
	uint64 lastEngageMs = 0;
	uint64 lastDisengageMs = 0;
	String lastAttacker;
	// The controller's phase when it saw the death, stored BEFORE mission
	// cleanup rewrites the order phase, so whichever finalizer wins reads it.
	String phaseAtDeath;
	int lastAttackerLevel = 0;
	float lastHamFraction = 1.f;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

class SimHunterTravelDiag {
private:
	// Bounded so a body that never dies (despawn, retirement) cannot grow the
	// map; the stalest entry is evicted first.
	enum { MAX_TRACKED = 64 };

	static std::atomic<bool>& enabledStorage() {
		static std::atomic<bool> enabled(false);
		return enabled;
	}

	static Mutex& mutexStorage() {
		static Mutex mutex;
		return mutex;
	}

	static VectorMap<uint64, SimHunterTravelTally>& talliesStorage() {
		static VectorMap<uint64, SimHunterTravelTally> tallies;
		return tallies;
	}

	static SimHunterTravelTally& slotLocked(uint64 bodyOid) {
		VectorMap<uint64, SimHunterTravelTally>& tallies = talliesStorage();
		if (!tallies.contains(bodyOid)) {
			while (tallies.size() >= MAX_TRACKED) {
				int oldest = 0;
				for (int i = 1; i < tallies.size(); ++i) {
					if (tallies.elementAt(i).getValue().updatedMs <
							tallies.elementAt(oldest).getValue().updatedMs)
						oldest = i;
				}
				tallies.remove(oldest);
			}
			tallies.put(bodyOid, SimHunterTravelTally());
		}
		return tallies.get(bodyOid);
	}

public:
	static bool isEnabled() {
		return enabledStorage().load(std::memory_order_acquire);
	}

	static void setEnabled(bool value) {
		enabledStorage().store(value, std::memory_order_release);
		if (!value) {
			Locker lock(&mutexStorage());
			talliesStorage().removeAll();
		}
	}

	static void noteEngage(uint64 bodyOid, const String& attacker,
			int attackerLevel, float hamFraction) {
		if (bodyOid == 0 || !isEnabled())
			return;
		Locker lock(&mutexStorage());
		SimHunterTravelTally& tally = slotLocked(bodyOid);
		tally.updatedMs = System::getMiliTime();
		tally.engages++;
		tally.lastEngageMs = tally.updatedMs;
		tally.lastAttacker = attacker;
		tally.lastAttackerLevel = attackerLevel;
		tally.lastHamFraction = hamFraction;
	}

	static void noteDisengage(uint64 bodyOid, float hamFraction) {
		if (bodyOid == 0 || !isEnabled())
			return;
		Locker lock(&mutexStorage());
		SimHunterTravelTally& tally = slotLocked(bodyOid);
		tally.updatedMs = System::getMiliTime();
		tally.disengages++;
		tally.lastDisengageMs = tally.updatedMs;
		tally.lastHamFraction = hamFraction;
	}

	static void notePhaseAtDeath(uint64 bodyOid, const String& phase) {
		if (bodyOid == 0 || !isEnabled())
			return;
		Locker lock(&mutexStorage());
		SimHunterTravelTally& tally = slotLocked(bodyOid);
		tally.updatedMs = System::getMiliTime();
		tally.phaseAtDeath = phase;
	}

	// A new contract starts a fresh tally, so the counts describe one trip.
	static void reset(uint64 bodyOid) {
		Locker lock(&mutexStorage());
		talliesStorage().drop(bodyOid);
	}

	static bool take(uint64 bodyOid, SimHunterTravelTally& out) {
		Locker lock(&mutexStorage());
		VectorMap<uint64, SimHunterTravelTally>& tallies = talliesStorage();
		if (!tallies.contains(bodyOid))
			return false;
		out = tallies.get(bodyOid);
		tallies.drop(bodyOid);
		return true;
	}
};

#endif /* SIMHUNTERTRAVELDIAG_H_ */
