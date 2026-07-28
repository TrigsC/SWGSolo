/*
 * TravelDiagLog.h
 * P.8.7 interplanetary / ticket-collector travel diagnostics.
 *
 * Written to its own file (log/traveldiag.log) rather than core3.log so a stuck
 * departure leg can be parsed without wading through zone spam. Mirrors
 * CellNavDiagLog's gating: a single function-local static shared across all
 * translation units, default OFF, flipped from sim_player_manager.lua
 * (travelDiag.logging).
 *
 * Every line is key=value so it greps and splits cleanly:
 *   t=<ms> ev=<EVENT> oid=<bodyOid> ...
 */

#ifndef TRAVELDIAGLOG_H_
#define TRAVELDIAGLOG_H_

#include <fstream>

#include "system/lang/String.h"
#include "system/lang/StringBuffer.h"
#include "system/lang/System.h"
#include "engine/util/u3d/Vector3.h"

class TravelDiagLog {
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

		std::ofstream logFile("log/traveldiag.log", std::ios::app);

		if (!logFile.is_open())
			return;

		logFile << "t=" << System::getMiliTime() << "ms "
				<< line.toCharArray() << "\n";
		logFile.close();
	}

	// ev=<event> oid=<bodyOid> <extra>
	static void event(const String& ev, uint64 bodyOid, const String& extra) {
		if (!loggingEnabledRef())
			return;

		StringBuffer buf;
		buf << "ev=" << ev << " oid=" << bodyOid;

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

	static String fmtDist(const String& key, float distance) {
		StringBuffer buf;
		buf << key << "=" << distance;
		return buf.toString();
	}
};

#endif /* TRAVELDIAGLOG_H_ */
