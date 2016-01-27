#pragma once
#include "delta_accumulator.h"
#include <algorithm>
#include <cassert>
#undef min
/* credits goes to http://www.unagames.com/blog/daniele/2010/06/fixed-time-step-implementation-box2d */

namespace augs {
	delta_accumulator::delta_accumulator(double steps_per_second, unsigned max_steps)
		: steps_per_second(steps_per_second), max_steps(max_steps), accumulator(0.0), fixed_dt_milliseconds((1.0 / steps_per_second) * 1000.0), ratio(0.0), time_multiplier(1.0) {
	}

	unsigned delta_accumulator::count_logic_steps_to_perform() {
		accumulator += ticks.extract<std::chrono::milliseconds>() * time_multiplier;

		const unsigned steps = static_cast<unsigned>(std::floor(accumulator / fixed_dt_milliseconds));

		if (steps > 0) 
			accumulator -= steps * fixed_dt_milliseconds;

		assert(
			"Accumulator must have a value lesser than the fixed time step" &&
			accumulator < fixed_dt_milliseconds + FLT_EPSILON
			);

		return std::min(steps, max_steps);
	}

	double delta_accumulator::fraction_of_time_until_the_next_logic_step() const {
		return accumulator / fixed_dt_milliseconds;
	}

	void delta_accumulator::reset() {
		accumulator = ratio = 0.0;
		ticks.reset();
	}

	double delta_accumulator::delta_seconds() const {
		return fixed_dt_milliseconds / 1000.0;
	}

	double delta_accumulator::get_steps_per_second() const {
		return steps_per_second;
	}

	double delta_accumulator::delta_milliseconds() const {
		return fixed_dt_milliseconds;
	}

	void delta_accumulator::set_time_multiplier(double tm) {
		time_multiplier = tm;
	}
}
