/*
 * SimEarlyGameDeathLog.h
 * F_0.9.6 P.10g early-game combat telemetry: the killing blow of a sim bot.
 *
 * Header-only and runtime gated like SimCombatDiagLog. The capture point is
 * AiAgentImplementation::notifyObjectDestructionObservers, the last place that
 * still knows the attacker before CreatureManager::notifyDestruction clears the
 * victim's threat map and defenders. The victim is locked there, so this store
 * is a LEAF: its mutex is never held while any other lock is taken.
 */

#ifndef SIMEARLYGAMEDEATHLOG_H_
#define SIMEARLYGAMEDEATHLOG_H_

#include <atomic>

#include "system/io/ObjectInputStream.h"
#include "system/io/ObjectOutputStream.h"
#include "system/lang/String.h"
#include "system/lang/System.h"
#include "system/thread/Locker.h"
#include "system/thread/Mutex.h"
#include "system/util/VectorMap.h"

struct SimEarlyGameDeathCause {
	uint64 capturedMs = 0;
	uint64 attackerOid = 0;
	String attackerTemplate;
	int attackerLevel = 0;
	// The engine passes the victim itself when the damage has no attacker.
	bool attackerIsSelf = false;
	bool attackerIsCreature = false;
	bool attackerIsPlayer = false;
	bool attackerIsSimBot = false;
	bool attackerInCell = false;
	float attackerDistance = 0.f;
	bool combatAction = false;
	int threatEntries = 0;
	uint32 totalDamage = 0;
	uint64 topDamagerOid = 0;
	String topDamagerTemplate;
	uint32 topDamage = 0;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

class SimEarlyGameDeathLog {
private:
	// Captures nobody collects (a sim bot outside the early-game loop) must not
	// accumulate while the gate is on.
	enum { MAX_PENDING = 64 };

	static std::atomic<bool>& enabledStorage() {
		static std::atomic<bool> enabled(false);
		return enabled;
	}

	static Mutex& mutexStorage() {
		static Mutex mutex;
		return mutex;
	}

	static VectorMap<uint64, SimEarlyGameDeathCause>& causesStorage() {
		static VectorMap<uint64, SimEarlyGameDeathCause> causes;
		return causes;
	}

public:
	// Lock-free: the engine capture point reads only this while the gate is off.
	// F_0.9.8b: missiondiag also wants the killing blow of MISSION bots, but
	// must not switch on the early-game combat telemetry that isEnabled()
	// gates. Capture runs when either is on; the store clears only when both
	// are off.
	static std::atomic<bool>& missionCaptureStorage() {
		static std::atomic<bool> enabled(false);
		return enabled;
	}

	static bool isEnabled() {
		return enabledStorage().load(std::memory_order_acquire);
	}

	static bool isCaptureEnabled() {
		return isEnabled() ||
			missionCaptureStorage().load(std::memory_order_acquire);
	}

	static void setEnabled(bool value) {
		enabledStorage().store(value, std::memory_order_release);
		if (!isCaptureEnabled()) {
			Locker lock(&mutexStorage());
			causesStorage().removeAll();
		}
	}

	static void setMissionCaptureEnabled(bool value) {
		missionCaptureStorage().store(value, std::memory_order_release);
		if (!isCaptureEnabled()) {
			Locker lock(&mutexStorage());
			causesStorage().removeAll();
		}
	}

	// Keyed by the dying body's OID; the controller takes it on its dead tick.
	static void record(uint64 bodyOid, const SimEarlyGameDeathCause& cause) {
		if (bodyOid == 0)
			return;

		Locker lock(&mutexStorage());
		VectorMap<uint64, SimEarlyGameDeathCause>& causes = causesStorage();
		if (causes.contains(bodyOid)) {
			causes.get(bodyOid) = cause;
			return;
		}
		while (causes.size() >= MAX_PENDING) {
			int oldest = 0;
			for (int i = 1; i < causes.size(); ++i) {
				if (causes.elementAt(i).getValue().capturedMs <
						causes.elementAt(oldest).getValue().capturedMs)
					oldest = i;
			}
			causes.remove(oldest);
		}
		causes.put(bodyOid, cause);
	}

	static bool take(uint64 bodyOid, SimEarlyGameDeathCause& out) {
		Locker lock(&mutexStorage());
		VectorMap<uint64, SimEarlyGameDeathCause>& causes = causesStorage();
		if (!causes.contains(bodyOid))
			return false;
		out = causes.get(bodyOid);
		causes.drop(bodyOid);
		return true;
	}
};

#endif /* SIMEARLYGAMEDEATHLOG_H_ */
