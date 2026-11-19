#ifndef EMULATOR_SRC_COMMON_LITEAB_H_
#define EMULATOR_SRC_COMMON_LITEAB_H_

#include <chrono>
#include <cstdlib>

namespace Common {

// Lite A/B measurement: KYTY_LITE_AB_PERIOD=<seconds> turns experimental code paths on and off
// every period, so both run in the same scene of one session.
inline int LiteAbPeriod() {
	static const int period = [] {
		const char* value = std::getenv("KYTY_LITE_AB_PERIOD");
		return value != nullptr ? std::atoi(value) : 0;
	}();
	return period;
}

// True outside A/B mode, and in the "on" half of each A/B period.
inline bool LiteAbPhaseOn() {
	const int period = LiteAbPeriod();
	if (period <= 0) {
		return true;
	}
	const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
	                         std::chrono::steady_clock::now().time_since_epoch())
	                         .count();
	return (seconds / period) % 2 == 0;
}

// Lite core reserve (KYTY_LITE_CORE_RESERVE=1): the GPU thread gets the last physical core to itself,
// both SMT siblings, and guest threads run on the other cores, so the busiest emulator thread never
// shares a core with a game thread. Call from the thread itself; `gpu` selects which side it is.
// Defined in kernel/pthread.cpp.
void LiteApplyCoreReserve(bool gpu);

} // namespace Common

#endif // EMULATOR_SRC_COMMON_LITEAB_H_
