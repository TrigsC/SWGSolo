/*
 * SimPveRelocationDeadline.h
 *
 * The effective deadline for a PvE market relocation, as a pure function of the
 * configured value.
 *
 * Why this is not just clampMinerInt(): that helper treats 0 as "keep the
 * current value" (SimPlayerManager.cpp:1298), so routing this setting through it
 * made the documented `relocationDeadlineSeconds = 0` silently mean "1800"
 * instead of "disabled" -- the one value an owner would reach for to restore the
 * pre-fix behaviour was the one value that could not work. Disable has to be
 * expressible, so the rule lives here where it can be asserted directly.
 */

#ifndef SIMPVERELOCATIONDEADLINE_H_
#define SIMPVERELOCATIONDEADLINE_H_

namespace SimPveRelocationDeadline {

// Below this a typo could start cancelling healthy multi-leg relocations
// mid-flight, so a small positive value is raised rather than honoured.
const int kMinSeconds = 60;
const int kMaxSeconds = 86400;

// configured: the value read from Lua (or `fallback` when the field is absent).
// fallback:   the compiled-in default, used only for a malformed negative.
// Returns 0 to disable the reaper, otherwise a value in [kMin, kMax].
inline int normalize(int configured, int fallback) {
	if (configured == 0)
		return 0;
	if (configured < 0)
		return fallback;
	if (configured < kMinSeconds)
		return kMinSeconds;
	if (configured > kMaxSeconds)
		return kMaxSeconds;
	return configured;
}

}

#endif /* SIMPVERELOCATIONDEADLINE_H_ */
