/*
 * SimBotMissionPlacement.h
 *
 * Pure helpers for stock-style mission lair placement (missionBoard.
 * stockPlacement), free of zones and locks so core3tests can assert them.
 *
 * Live evidence 2026-10-13: 80% of Mos Eisley offer searches found no point.
 * The bot board demanded a spawn-permitted point at the TERMINAL, which the
 * stock game never does (MissionManagerImplementation::
 * randomizeGenericDestroyMission checks bounds, water and city only), and threw
 * away two thirds of its tries on a random bearing outside the terminal's arc.
 * The stock game finds the exact point on ARRIVAL instead
 * (DestroyMissionObjectiveImplementation::findValidSpawnPosition): random
 * offsets around the waypoint, widening every 32 tries, up to 256 tries.
 */

#ifndef SIMBOTMISSIONPLACEMENT_H_
#define SIMBOTMISSIONPLACEMENT_H_

#include <cmath>

namespace SimBotMissionPlacement {

// Stock: 256 tries, starting 128m either side of the waypoint.
const int REVEAL_MAX_TRIES = 256;
const float REVEAL_BASE_HALF_WIDTH = 128.f;
// Stock doubles without bound (16km by the last block). A bot has already
// walked to the waypoint, so the search stays within a walkable distance.
const float REVEAL_MAX_HALF_WIDTH = 512.f;

// A bearing in [0, 360) drawn uniformly from terminalBearing +/- arcDegrees.
// unit is a uniform sample in [0, 1]. An arc of 180 or more is the full circle.
inline float bearingInArc(float terminalBearing, float arcDegrees, float unit) {
	if (unit < 0.f)
		unit = 0.f;
	if (unit > 1.f)
		unit = 1.f;
	if (arcDegrees < 0.f)
		arcDegrees = 0.f;
	if (arcDegrees > 180.f)
		arcDegrees = 180.f;
	float bearing = terminalBearing - arcDegrees + unit * 2.f * arcDegrees;
	bearing = std::fmod(bearing, 360.f);
	if (bearing < 0.f)
		bearing += 360.f;
	if (bearing >= 360.f)
		bearing = 0.f;
	return bearing;
}

// Half-width of the square searched around the waypoint on a given try
// (0-based): doubles every 32 tries, as stock does, up to the cap.
inline float revealSearchHalfWidth(int tryIndex) {
	if (tryIndex < 0)
		tryIndex = 0;
	float halfWidth = REVEAL_BASE_HALF_WIDTH;
	for (int block = tryIndex / 32; block > 0 &&
			halfWidth < REVEAL_MAX_HALF_WIDTH; --block)
		halfWidth *= 2.f;
	return halfWidth > REVEAL_MAX_HALF_WIDTH ? REVEAL_MAX_HALF_WIDTH : halfWidth;
}

// Offset along one axis for a uniform sample in [0, 1].
inline float revealOffset(float halfWidth, float unit) {
	if (unit < 0.f)
		unit = 0.f;
	if (unit > 1.f)
		unit = 1.f;
	return halfWidth - unit * 2.f * halfWidth;
}

}

#endif /* SIMBOTMISSIONPLACEMENT_H_ */
