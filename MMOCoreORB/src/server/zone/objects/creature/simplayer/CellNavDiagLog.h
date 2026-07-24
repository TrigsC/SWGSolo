/*
 * CellNavDiagLog.h
 * Pass-1 cell-entry navigation diagnostics.
 */

#ifndef CELLNAVDIAGLOG_H_
#define CELLNAVDIAGLOG_H_

#include <fstream>

#include "system/lang/String.h"
#include "system/lang/StringBuffer.h"
#include "system/lang/System.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/scene/WorldCoordinates.h"

class CellNavDiagLog {
public:
	// Master gate for ALL cell-nav diagnostic output (ENGINE_*, STARPORT_WP_*,
	// TICKET_COLLECTOR_RESOLVED, PVP_COLLECTOR_APPROACH, ENGINE_DIRECTION, ...).
	// Defaults OFF so production carries the full instrumentation dormant, at zero
	// cost; flip cellNavDiag.logging = true in sim_player_manager.lua to re-enable.
	// Function-local static keeps a single shared flag across all translation units.
	static bool& loggingEnabledRef() {
		static bool enabled = false;
		return enabled;
	}
	static void setLoggingEnabled(bool value) { loggingEnabledRef() = value; }
	static bool isLoggingEnabled() { return loggingEnabledRef(); }

	static void write(const String& line) {
		if (!loggingEnabledRef())
			return;

		std::ofstream logFile("log/cellnav.log", std::ios::app);

		if (!logFile.is_open())
			return;

		logFile << "t=" << System::getMiliTime() << "ms "
				<< line.toCharArray() << "\n";
		logFile.close();
	}

	static String fmtPos(const Vector3& worldPos, const Vector3& localPos,
			CellObject* cell) {
		StringBuffer buf;
		uint64 cellOid = cell == nullptr ? 0 : cell->getObjectID();
		buf << "world=(" << worldPos.getX() << "," << worldPos.getY()
				<< "," << worldPos.getZ() << ") cell="
				<< cellOid
				<< " local=(" << localPos.getX() << "," << localPos.getY()
				<< "," << localPos.getZ() << ")";
		return buf.toString();
	}

	static String fmtPos(const WorldCoordinates& coordinates) {
		return fmtPos(coordinates.getWorldPosition(), coordinates.getPoint(),
				coordinates.getCell());
	}

	static String fmtPos(AiAgent* agent) {
		if (agent == nullptr)
			return "world=(null) cell=0 local=(null)";

		return fmtPos(WorldCoordinates(agent));
	}
};

#endif /* CELLNAVDIAGLOG_H_ */
