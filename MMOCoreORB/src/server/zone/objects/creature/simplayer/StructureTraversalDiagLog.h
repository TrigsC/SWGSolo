/*
 * StructureTraversalDiagLog.h
 * Gated diagnostics for the structure traversal foundation.
 */

#ifndef STRUCTURETRAVERSALDIAGLOG_H_
#define STRUCTURETRAVERSALDIAGLOG_H_

#include <fstream>

#include "system/lang/String.h"
#include "system/lang/StringBuffer.h"
#include "system/lang/System.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/scene/WorldCoordinates.h"

class StructureTraversalDiagLog {
public:
	static bool& loggingEnabledRef() {
		static bool enabled = false;
		return enabled;
	}

	static bool& zeroClipLoggingEnabledRef() {
		static bool enabled = false;
		return enabled;
	}

	static void setLoggingEnabled(bool value) {
		loggingEnabledRef() = value;
	}

	static bool isLoggingEnabled() {
		return loggingEnabledRef();
	}

	static void setZeroClipLoggingEnabled(bool value) {
		zeroClipLoggingEnabledRef() = value;
	}

	// D8's scan summary is mandatory evidence -- openings=0 is the result that
	// would refute the two-door premise, so it must not depend on the general
	// traversal logging flag being on as well.
	static bool& hollowScanLoggingEnabledRef() {
		static bool enabled = false;
		return enabled;
	}

	static void setHollowScanLoggingEnabled(bool value) {
		hollowScanLoggingEnabledRef() = value;
	}

	static void writeHollowScan(const String& line) {
		if (!hollowScanLoggingEnabledRef())
			return;

		std::ofstream logFile("log/structuretraversal.log", std::ios::app);

		if (!logFile.is_open())
			return;

		logFile << "t=" << System::getMiliTime() << "ms "
				<< line.toCharArray() << "\n";
		logFile.close();
	}

	static void writeZeroClip(const String& line) {
		if (!zeroClipLoggingEnabledRef())
			return;

		std::ofstream logFile("log/structuretraversal.log", std::ios::app);

		if (!logFile.is_open())
			return;

		logFile << "t=" << System::getMiliTime() << "ms "
				<< line.toCharArray() << "\n";
		logFile.close();
	}

	static void write(const String& line) {
		if (!loggingEnabledRef())
			return;

		std::ofstream logFile("log/structuretraversal.log", std::ios::app);

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
				<< "," << worldPos.getZ() << ") cell=" << cellOid
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

#endif /* STRUCTURETRAVERSALDIAGLOG_H_ */
