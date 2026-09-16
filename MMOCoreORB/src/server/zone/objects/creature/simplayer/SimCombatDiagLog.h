/*
 * SimCombatDiagLog.h
 * F_0.9.6-D1 simulation-combat observability.
 *
 * The logger is header-only so the engine trace points do not add another
 * compilation unit or manager dependency.  It is deliberately runtime gated:
 * the default-off path performs only the atomic gate load at each caller.
 */

#ifndef SIMCOMBATDIAGLOG_H_
#define SIMCOMBATDIAGLOG_H_

#include <atomic>
#include <fstream>

#include "system/lang/String.h"
#include "system/lang/StringBuffer.h"
#include "system/lang/System.h"
#include "system/thread/Locker.h"
#include "system/thread/Mutex.h"
#include "system/util/Vector.h"
#include "system/util/VectorMap.h"

class SimCombatDiagLog {
private:
	struct TraceState {
		uint64 lines = 0;
		bool budgetReported = false;
		VectorMap<String, uint64> lastOutcomeAtMs;
	};

	static std::atomic<bool>& loggingEnabledStorage() {
		static std::atomic<bool> enabled(false);
		return enabled;
	}

	static std::atomic<int>& maxLinesStorage() {
		static std::atomic<int> maxLines(2000);
		return maxLines;
	}

	static std::atomic<uint64>& writeErrorsStorage() {
		static std::atomic<uint64> errors(0);
		return errors;
	}

	static Mutex& allowlistMutexStorage() {
		static Mutex mutex;
		return mutex;
	}

	static Mutex& writeMutexStorage() {
		static Mutex mutex;
		return mutex;
	}

	static VectorMap<uint64, TraceState>& traceStateStorage() {
		static VectorMap<uint64, TraceState> states;
		return states;
	}

	static std::atomic<uint64>& runIdStorage() {
		// Seeded from wall-clock on each enable, NOT a process-local counter.
		// The log is append-only across restarts, so a counter starting at 0
		// writes run=1 for every boot and makes old and new evidence
		// indistinguishable - the exact confusion this field exists to prevent.
		static std::atomic<uint64> runId(0);
		return runId;
	}

	static String& lastWriteErrorStorage() {
		static String error;
		return error;
	}

	static void writeLine(const String& line) {
		Locker writeLock(&writeMutexStorage());
		std::ofstream logFile("log/simcombat.log", std::ios::app);

		if (!logFile.is_open()) {
			writeErrorsStorage().fetch_add(1, std::memory_order_relaxed);
			lastWriteErrorStorage() = "open_failed";
			return;
		}

		logFile << line.toCharArray() << "\n";
		logFile.flush();
		if (!logFile) {
			writeErrorsStorage().fetch_add(1, std::memory_order_relaxed);
			lastWriteErrorStorage() = "write_or_flush_failed";
			return;
		}

		logFile.close();
		if (!logFile) {
			writeErrorsStorage().fetch_add(1, std::memory_order_relaxed);
			lastWriteErrorStorage() = "close_failed";
		}
	}

	static bool reserveLine(uint64 oid, const String& outcome,
		String& lineOut) {
		Locker allowlistLock(&allowlistMutexStorage());
		VectorMap<uint64, TraceState>& states = traceStateStorage();
		if (!states.contains(oid))
			return false;

		TraceState& state = states.get(oid);
		uint64 nowMs = System::getMiliTime();
		int maxLines = maxLinesStorage().load(std::memory_order_relaxed);
		if (maxLines < 100)
			maxLines = 100;

		if (state.lines >= static_cast<uint64>(maxLines)) {
			if (state.budgetReported)
				return false;

			state.budgetReported = true;
			state.lines++;
			StringBuffer budgetLine;
			budgetLine << "t=" << nowMs << "ms run=" <<
				runIdStorage().load(std::memory_order_acquire)
				<< " ev=BUDGET_EXHAUSTED oid=" << oid
				<< " maxLinesPerAgent=" << maxLines;
			lineOut = budgetLine.toString();
			return true;
		}

		String outcomeKey = outcome.isEmpty() ? String("event") : outcome;
		if (state.lastOutcomeAtMs.contains(outcomeKey) &&
				nowMs - state.lastOutcomeAtMs.get(outcomeKey) < 5000)
			return false;

		state.lastOutcomeAtMs.put(outcomeKey, nowMs);
		state.lines++;
		return true;
	}

public:
	// The atomic load is intentionally lock-free and is the first operation at
	// every engine trace point.  Do not replace this with the older diagnostic
	// classes' plain function-local bool.
	static bool isLoggingEnabled() {
		return loggingEnabledStorage().load(std::memory_order_acquire);
	}

	static void setLoggingEnabled(bool value) {
		if (value) {
			loggingEnabledStorage().store(false, std::memory_order_release);
			{
				Locker allowlistLock(&allowlistMutexStorage());
				for (int i = 0; i < traceStateStorage().size(); ++i) {
					TraceState& state = traceStateStorage().elementAt(i).getValue();
					state.lines = 0;
					state.budgetReported = false;
					state.lastOutcomeAtMs.removeAll();
				}
			}
			runIdStorage().store(System::getMiliTime(),
				std::memory_order_release);
			writeErrorsStorage().store(0, std::memory_order_relaxed);
			{
				Locker writeLock(&writeMutexStorage());
				lastWriteErrorStorage() = "";
			}
			loggingEnabledStorage().store(true, std::memory_order_release);
			return;
		}

		loggingEnabledStorage().store(false, std::memory_order_release);
	}

	static void setMaxLinesPerAgent(int value) {
		if (value < 100)
			value = 100;
		else if (value > 100000)
			value = 100000;
		maxLinesStorage().store(value, std::memory_order_release);
	}

	static void addTraced(uint64 oid) {
		if (oid == 0)
			return;

		Locker allowlistLock(&allowlistMutexStorage());
		if (!traceStateStorage().contains(oid))
			traceStateStorage().put(oid, TraceState());
	}

	static void removeTraced(uint64 oid) {
		if (oid == 0)
			return;

		Locker allowlistLock(&allowlistMutexStorage());
		traceStateStorage().drop(oid);
	}

	static void clearTraced() {
		Locker allowlistLock(&allowlistMutexStorage());
		traceStateStorage().removeAll();
	}

	// Call only after isLoggingEnabled() is true.  This keeps the default-off
	// behaviour task free of allowlist mutex traffic.
	static bool isTraced(uint64 oid) {
		Locker allowlistLock(&allowlistMutexStorage());
		return traceStateStorage().contains(oid);
	}

	static Vector<uint64> snapshotTraced() {
		Vector<uint64> result;
		Locker allowlistLock(&allowlistMutexStorage());
		for (int i = 0; i < traceStateStorage().size(); ++i)
			result.add(traceStateStorage().elementAt(i).getKey());
		return result;
	}

	static int tracedCount() {
		Locker allowlistLock(&allowlistMutexStorage());
		return traceStateStorage().size();
	}

	// ev=<event> oid=<oid> <extra>.  outcome is the stable transition key; it
	// is separate from extra so changing queue depths does not defeat the
	// repeated-outcome rate limit.
	static void event(const String& ev, uint64 oid, const String& extra,
		const String& outcome = String()) {
		if (!isLoggingEnabled())
			return;

		String line;
		if (!reserveLine(oid, outcome.isEmpty() ? ev : outcome, line))
			return;

		if (line.isEmpty()) {
			StringBuffer buf;
			buf << "t=" << System::getMiliTime() << "ms run=" <<
				runIdStorage().load(std::memory_order_acquire)
				<< " ev=" << ev << " oid=" << oid;
			if (!extra.isEmpty())
				buf << " " << extra;
			line = buf.toString();
		}

		writeLine(line);
	}

	static uint64 writeErrors() {
		return writeErrorsStorage().load(std::memory_order_relaxed);
	}

	static String lastWriteError() {
		Locker writeLock(&writeMutexStorage());
		return lastWriteErrorStorage();
	}

	static Mutex& tracedMutex() {
		return allowlistMutexStorage();
	}
};

#endif /* SIMCOMBATDIAGLOG_H_ */
