/*
 * MissionDiagLog.h
 * P.8.7 destroy-mission BOARD diagnostics (offer generation + spawn placement).
 *
 * Why this exists: generatePveBotMissionOffers() and its two helpers reject with
 * bare `continue` / `return false` and record nothing, so a board that produces
 * zero offers is indistinguishable from one that was never asked. The abandon
 * reason is then flattened a second time -- every failure surfaces on the
 * dashboard as the generic "abandoned" that completeOrder() writes from the
 * TRAVEL_HOME epilogue. This file names the exact gate that rejected.
 *
 * Written to its own file (log/missiondiag.log) rather than core3.log so an
 * offer-generation failure can be parsed without wading through zone spam.
 * Mirrors TravelDiagLog/CellNavDiagLog gating: a single function-local static
 * shared across all translation units, default OFF, flipped from
 * sim_player_manager.lua (missionDiag.logging).
 *
 * Every line is key=value so it greps and splits cleanly:
 *   t=<ms> ev=<EVENT> id=<identityId> ...
 */

#ifndef MISSIONDIAGLOG_H_
#define MISSIONDIAGLOG_H_

#include <fstream>

#include "system/lang/String.h"
#include "system/lang/StringBuffer.h"
#include "system/lang/System.h"
#include "engine/util/u3d/Vector3.h"

class MissionDiagLog {
public:
	static bool& loggingEnabledRef() {
		static bool enabled = false;
		return enabled;
	}
	static void setLoggingEnabled(bool value) { loggingEnabledRef() = value; }
	static bool isLoggingEnabled() { return loggingEnabledRef(); }

	static void write(const String& line) {
		if (!loggingEnabledRef())
			return;

		std::ofstream logFile("log/missiondiag.log", std::ios::app);

		if (!logFile.is_open())
			return;

		logFile << "t=" << System::getMiliTime() << "ms "
				<< line.toCharArray() << "\n";
		logFile.close();
	}

	// ev=<event> id=<identityId> <extra>
	static void event(const String& ev, uint64 identityId, const String& extra) {
		if (!loggingEnabledRef())
			return;

		StringBuffer buf;
		buf << "ev=" << ev << " id=" << identityId;

		if (!extra.isEmpty())
			buf << " " << extra;

		write(buf.toString());
	}

	static String fmtVec(const String& key, const Vector3& v) {
		StringBuffer buf;
		buf << key << "=(" << v.getX() << "," << v.getY() << "," << v.getZ()
			<< ")";
		return buf.toString();
	}
};

#endif /* MISSIONDIAGLOG_H_ */
