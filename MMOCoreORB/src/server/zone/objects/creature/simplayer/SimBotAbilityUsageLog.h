/*
 * SimBotAbilityUsageLog.h
 * F_0.9.7 bounded skill-derived PlayerBot combat telemetry.
 *
 * This is a leaf store. It may be written while an AiAgent is locked and it
 * never takes an agent or manager lock itself.
 */

#ifndef SIMBOTABILITYUSAGELOG_H_
#define SIMBOTABILITYUSAGELOG_H_

#include <atomic>

#include "system/lang/String.h"
#include "system/io/ObjectInputStream.h"
#include "system/io/ObjectOutputStream.h"
#include "system/thread/Locker.h"
#include "system/thread/Mutex.h"
#include "system/util/Vector.h"
#include "system/util/VectorMap.h"

struct SimBotAbilityUseEntry {
	uint64 bodyOid = 0;
	String command;
	uint64 targetOid = 0;
	uint64 targetStatesBefore = 0;
	uint64 atMs = 0;
	String targetTemplate;
	bool special = false;

	bool toBinaryStream(ObjectOutputStream* stream) const { return true; }
	bool parseFromBinaryStream(ObjectInputStream* stream) { return true; }
};

class SimBotAbilityUsageLog {
private:
	struct BodyHistory {
		Vector<SimBotAbilityUseEntry> entries;
		VectorMap<String, uint64> commandCounts;
		// OR of every recorded targetStatesBefore; survives ring eviction so an
		// early Intimidate in a long fight is still provable.
		uint64 targetStatesSeen = 0;
		uint64 lastAtMs = 0;
	};

	static std::atomic<bool>& enabledStorage() {
		static std::atomic<bool> enabled(false);
		return enabled;
	}

	static Mutex& mutexStorage() {
		static Mutex mutex;
		return mutex;
	}

	static VectorMap<uint64, BodyHistory>& historiesStorage() {
		static VectorMap<uint64, BodyHistory> histories;
		return histories;
	}

	static VectorMap<String, uint64>& commandCountsStorage() {
		static VectorMap<String, uint64> counts;
		return counts;
	}

	static uint64& specialsStorage() {
		static uint64 count = 0;
		return count;
	}

	static uint64& defaultsStorage() {
		static uint64 count = 0;
		return count;
	}

	static uint64& freeStateForcedStorage() {
		static uint64 count = 0;
		return count;
	}

	static uint64& reserveSkipsStorage() {
		static uint64 count = 0;
		return count;
	}

	static int& maxBodiesStorage() {
		static int maxBodies = 0;
		return maxBodies;
	}

	static int& historySizeStorage() {
		static int historySize = 12;
		return historySize;
	}

	static void clearLocked() {
		historiesStorage().removeAll();
		commandCountsStorage().removeAll();
		specialsStorage() = 0;
		defaultsStorage() = 0;
		freeStateForcedStorage() = 0;
		reserveSkipsStorage() = 0;
	}

public:
	static bool isEnabled() {
		return enabledStorage().load(std::memory_order_acquire);
	}

	static void setEnabled(bool value) {
		enabledStorage().store(value, std::memory_order_release);
		if (!value) {
			Locker lock(&mutexStorage());
			clearLocked();
		}
	}

	// The manager updates this from its live roster count. The log itself only
	// stores at most that many body histories, so a retired body cannot consume
	// an unbounded slot while the manager is between maintenance passes.
	static void setMaxBodies(int maxBodies) {
		if (maxBodies < 0)
			maxBodies = 0;
		Locker lock(&mutexStorage());
		maxBodiesStorage() = maxBodies;
		while (historiesStorage().size() > maxBodiesStorage()) {
			int oldest = 0;
			for (int i = 1; i < historiesStorage().size(); ++i) {
				if (historiesStorage().elementAt(i).getValue().lastAtMs <
						historiesStorage().elementAt(oldest).getValue().lastAtMs)
					oldest = i;
			}
			historiesStorage().remove(oldest);
		}
	}

	static void setHistorySize(int historySize) {
		if (historySize < 0)
			historySize = 0;
		Locker lock(&mutexStorage());
		historySizeStorage() = historySize;
		for (int i = historiesStorage().size() - 1; i >= 0; --i) {
			Vector<SimBotAbilityUseEntry>& entries =
				historiesStorage().elementAt(i).getValue().entries;
			while (entries.size() > historySize)
				entries.remove(0);
		}
	}

	static void remove(uint64 bodyOid) {
		if (bodyOid == 0)
			return;
		Locker lock(&mutexStorage());
		historiesStorage().drop(bodyOid);
	}

	static void record(const SimBotAbilityUseEntry& entry, int historySize = -1) {
		if (!isEnabled() || entry.bodyOid == 0 || entry.command.isEmpty())
			return;

		Locker lock(&mutexStorage());
		if (maxBodiesStorage() == 0)
			return;
		if (historySize < 0)
			historySize = historySizeStorage();
		VectorMap<uint64, BodyHistory>& histories = historiesStorage();
		if (!histories.contains(entry.bodyOid)) {
			while (histories.size() >= maxBodiesStorage()) {
				int oldest = 0;
				for (int i = 1; i < histories.size(); ++i) {
					if (histories.elementAt(i).getValue().lastAtMs <
						histories.elementAt(oldest).getValue().lastAtMs)
						oldest = i;
				}
				histories.remove(oldest);
			}
			histories.put(entry.bodyOid, BodyHistory());
		}

		BodyHistory& history = histories.get(entry.bodyOid);
		history.lastAtMs = entry.atMs;
		history.targetStatesSeen |= entry.targetStatesBefore;
		if (historySize > 0) {
			while (history.entries.size() >= historySize)
				history.entries.remove(0);
			history.entries.add(entry);
		}
		String command = entry.command.toLowerCase();
		commandCountsStorage().put(command,
			(commandCountsStorage().contains(command) ?
				commandCountsStorage().get(command) : 0) + 1);
		history.commandCounts.put(command,
			(history.commandCounts.contains(command) ?
				history.commandCounts.get(command) : 0) + 1);
		if (entry.special)
			++specialsStorage();
		else
			++defaultsStorage();
	}

	static void recordFreeStateForced() {
		if (!isEnabled())
			return;
		Locker lock(&mutexStorage());
		++freeStateForcedStorage();
	}

	static void recordReserveSkip() {
		if (!isEnabled())
			return;
		Locker lock(&mutexStorage());
		++reserveSkipsStorage();
	}

	static void snapshot(uint64 bodyOid, Vector<SimBotAbilityUseEntry>& entries) {
		entries.removeAll();
		Locker lock(&mutexStorage());
		if (historiesStorage().contains(bodyOid))
			entries = historiesStorage().get(bodyOid).entries;
	}

	static void snapshotCommandCounts(VectorMap<String, uint64>& counts) {
		Locker lock(&mutexStorage());
		counts = commandCountsStorage();
	}

	static void snapshotCommandCounts(uint64 bodyOid,
		VectorMap<String, uint64>& counts) {
		counts.removeAll();
		Locker lock(&mutexStorage());
		if (historiesStorage().contains(bodyOid))
			counts = historiesStorage().get(bodyOid).commandCounts;
	}

	static uint64 targetStatesSeen(uint64 bodyOid) {
		Locker lock(&mutexStorage());
		return historiesStorage().contains(bodyOid) ?
			historiesStorage().get(bodyOid).targetStatesSeen : 0;
	}

	static int bodiesWithHistory() {
		Locker lock(&mutexStorage());
		return historiesStorage().size();
	}

	static uint64 specialsEnqueued() {
		Locker lock(&mutexStorage());
		return specialsStorage();
	}

	static uint64 defaultsEnqueued() {
		Locker lock(&mutexStorage());
		return defaultsStorage();
	}

	static uint64 freeStateForced() {
		Locker lock(&mutexStorage());
		return freeStateForcedStorage();
	}

	static uint64 reserveSkips() {
		Locker lock(&mutexStorage());
		return reserveSkipsStorage();
	}
};

#endif /* SIMBOTABILITYUSAGELOG_H_ */
