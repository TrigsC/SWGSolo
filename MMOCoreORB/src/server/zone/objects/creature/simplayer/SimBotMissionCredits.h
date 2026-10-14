/* Pure mission-credit arithmetic shared by the manager and unit tests. */

#ifndef SIMBOTMISSIONCREDITS_H_
#define SIMBOTMISSIONCREDITS_H_

namespace SimBotMissionCredits {

inline int dividedReward(int reward, int divisor) {
	return reward / (divisor < 1 ? 1 : divisor);
}

inline bool withinBounds(int amount, int minimum, int maximum, int divisor) {
	return amount >= dividedReward(minimum, divisor) &&
		amount <= dividedReward(maximum, divisor);
}

}

#endif /* SIMBOTMISSIONCREDITS_H_ */
