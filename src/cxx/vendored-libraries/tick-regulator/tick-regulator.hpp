#pragma once

#include <chrono>
#include <memory>



namespace tickreg {

	/*

	|   burst   |     wait time = delta - burst      |
	| _________ |  _   _   _   _   _   _   _   _   _ |
	|           |                                    |
	|           |                                    \ beginCycle()
	|           |
	|           \------------- endCycle()
	|
	\ beginCycle()

	`burst` and `delta` are measured in seconds

	*/


	using Clock     = std::chrono::steady_clock;
	using Duration  = Clock::duration;
	using TimePoint = Clock::time_point;

	using delta_t         = double;
	using strategy_fn_t   = void (*)(TimePoint last_begin, delta_t burst, delta_t delta);
	using strategy_flag_t = uint_least32_t;


	struct LookbackLine {
		delta_t burst;
		delta_t delta;
	};


	enum class WaitStrategyFlags : strategy_flag_t {
		eCantKeepUp = 1 << 0,
		eYield      = 1 << 1,
		eSleepUntil = 1 << 2,
		eAll        = ~ strategy_flag_t(0)
	};


	struct RegulatorParams {
		/** A number within the interval (0, +inf);
		* it determines how easily the regulator changes strategy based
		* on the average delta compared to the desired value.
		*
		* For example, with a tolerance of 0.5, the average delta would
		* have to be within the interval (0.5, 1.5), and one of 0.1
		* within (0.9, 1.1). */
		delta_t deltaTolerance;

		/** A real number;
		* it offsets the comparison between the average burst and the
		* average delta when deciding whether to change strategy.
		*
		* Pseudo-algorithm:
		* if (avg_burst > des_delta + tolerance) {
		* ... change_strategy();
		* } */
		delta_t burstTolerance;

		/** A number within the interval [0, +inf);
		* used to determine how to compensate for inconsistent/biased
		* deltas when waiting for the next tick;
		* higher values result in higher deltas.
		*
		* If the compensation factor is 0, the average delta
		* is almost guaranteed to be higher than the desired
		* value because the time spent between a
		* `cycleEnd() => cycleStart()` transition is never 0.
		*
		* If the compensation factor is too high, the average
		* delta is more precise but the delta variance becomes
		* excessive.
		*
		* The recommended value depends on the desired framerate;
		* during a test a value equal to `lookbackSize`
		* seemed to be appropriate for running a loop 72 times
		* per second, but a compensation factor of 1 is a safe
		* default for most situations. */
		delta_t compensationFactor;


		/** A combination of `WaitStrategyState` bit values;
		* strategy states that are not included here are excluded as
		* candidates when changing strategy. */
		strategy_flag_t strategyMask;
	};



	enum class WaitStrategyState : uint_least32_t {
		// 0x (next state if burst is higher than delta)
		//    (next state if delta is too high)
		//    (next state if delta is too low)
		//    (unique number)
		eCantKeepUp       = 0x00000100,
		eYield            = 0x01000201,
		eSleepUntil       = 0x00010202,
		eAlwaysYield      = 0x03030303,
		eAlwaysSleepUntil = 0x04040404
	};


	class Timer {
	public:
		Timer() = default;
		Timer(size_t lookback_size, delta_t default_delta);
		Timer(Timer&&) = default;

		Timer& operator=(Timer&&) = default;

		delta_t   estBurst()       const noexcept;
		delta_t   estDelta()       const noexcept;
		delta_t   lastBurst()      const noexcept;
		delta_t   lastDelta()      const noexcept;
		TimePoint lastBeginCycle() const noexcept;

		void setLookbackSize(size_t new_size, delta_t default_delta);
		void resetEstimates(delta_t default_delta) noexcept;

		void beginCycle();
		void endCycle();

	protected:
		std::unique_ptr<LookbackLine[]> m_lookback;
		size_t                          m_lookbackSize;
		LookbackLine                    m_avgMetrics;
		TimePoint                       m_previousBegin;
		TimePoint                       m_lastBegin;
		uint_fast32_t                   m_currentLine;
		bool                            m_lastCycleEnded;
	};

	using Tracker [[deprecated("\"Tracker\" renamed to \"Timer\"")]] = Timer;


	class Regulator : public Timer {
	public:
		Regulator() = default;
		Regulator(unsigned lookback_size, delta_t desired_delta, WaitStrategyState initial_strat, const RegulatorParams& params);
		Regulator(Regulator&&) = default;

		Regulator(unsigned lookback_size, delta_t desired_delta):
				Regulator(
					lookback_size,
					desired_delta,
					WaitStrategyState::eSleepUntil,
					{ 0.25, 0.01, 0.0, strategy_flag_t(WaitStrategyFlags::eSleepUntil) } )
		{ }

		Regulator& operator=(Regulator&&) = default;

		bool estCanKeepUp()   const noexcept;

		void        setStrategy(WaitStrategyState) noexcept;
		auto        currentStrategy() const noexcept { return m_currentStrategy; }
		const auto& params()          const noexcept { return m_params; }

		delta_t getDesiredDelta() const noexcept { return m_desiredDelta; }
		void    setDesiredDelta(delta_t);

		void awaitNextTick();

		void endCycle();

	private:
		RegulatorParams                 m_params;
		WaitStrategyState               m_currentStrategy;
		strategy_fn_t                   m_currentStrategyFn;
		delta_t                         m_desiredDelta;
	};

}
