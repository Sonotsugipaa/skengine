#include "tick-regulator.hpp"

#include <utility>
#include <thread>
#include <cassert>



namespace tickreg {

	namespace impl {

		delta_t reaverage(delta_t cur_value, delta_t new_value, delta_t cur_avg, delta_t lookback_size) {
			// delta_t value_weight = cur_value    / lookback_size;
			// delta_t other_weight = cur_avg      - value_weight;
			// delta_t new_weight   = new_value    / lookback_size;
			// return                 other_weight + new_weight;
			return (cur_avg - (cur_value / lookback_size)) + (new_value / lookback_size);
		}


		void wstratCantKeepUp(TimePoint, delta_t, delta_t) {
			/* NOP */
		}

		void wstratYield(TimePoint prev_begin, delta_t burst, delta_t delta) {
			using SecondsFlt = std::chrono::duration<delta_t, std::chrono::seconds::period>;
			(void) burst;

			auto wait_until = std::chrono::time_point_cast<SecondsFlt>(prev_begin) + SecondsFlt(delta);
			if(Clock::now() < wait_until) std::this_thread::yield();
		}

		void wstratSleepUntil(TimePoint prev_begin, delta_t burst, delta_t delta) {
			using SecondsFlt = std::chrono::duration<delta_t, std::chrono::seconds::period>;
			using std::chrono::time_point_cast;
			(void) burst;

			TimePoint wait_until = time_point_cast<Duration>(prev_begin + duration_cast<Duration>(SecondsFlt(delta)));
			std::this_thread::sleep_until(wait_until);
		}


		constexpr WaitStrategyState strat_table[] = {
			WaitStrategyState::eCantKeepUp,
			WaitStrategyState::eYield,
			WaitStrategyState::eSleepUntil,
			WaitStrategyState::eAlwaysYield,
			WaitStrategyState::eAlwaysSleepUntil };

		constexpr strategy_fn_t fn_table[] = {
			wstratCantKeepUp,
			wstratYield,
			wstratSleepUntil,
			wstratYield,
			wstratSleepUntil };


		enum class StrategyChangeReason : uint_least32_t {
			eLowDelta  =  8,
			eHighDelta = 16,
			eHighBurst = 24
		};


		template <StrategyChangeReason reason>
		void changeStrategy(WaitStrategyState* dst_strat, strategy_fn_t* dst_fn, strategy_flag_t mask) {
			using int_t = std::underlying_type_t<WaitStrategyState>;

			assert(
				int_t(reason) ==  8 ||
				int_t(reason) == 16 ||
				int_t(reason) == 24 );

			auto i = (int_t(*dst_strat) >> int_t(reason)) & int_t(0xFF);
			assert((int_t(strat_table[i]) & 0xFF) == i);
			if((1 << i) & mask) {
				*dst_strat = strat_table[i];
				*dst_fn    = fn_table[i];
			}
		}

	}


	Timer::Timer(size_t lookback_size, delta_t default_delta):
			m_lookbackSize(lookback_size),
			m_avgMetrics({ 0.0, default_delta }),
			m_previousBegin(Clock::now()),
			m_lastBegin(m_previousBegin),
			m_currentLine(0),
			m_lastCycleEnded(false)
	{
		assert(m_lookbackSize > 1);
		m_lookback = std::make_unique<LookbackLine[]>(m_lookbackSize);
		for(size_t i = 0; i < m_lookbackSize; ++i) {
			m_lookback[i] = {
				.burst = 0.0,
				.delta = default_delta };
		}
	}


	delta_t Timer::estBurst() const noexcept {
		return m_avgMetrics.burst;
	}

	delta_t Timer::estDelta() const noexcept {
		return m_avgMetrics.delta;
	}

	TimePoint Timer::lastBeginCycle() const noexcept {
		return m_lastBegin;
	}

	delta_t Timer::lastBurst() const noexcept {
		return m_lookback[(m_currentLine + m_lookbackSize - 1) % m_lookbackSize].burst;
	}

	delta_t Timer::lastDelta() const noexcept {
		return m_lookback[m_currentLine % m_lookbackSize].delta;
	}


	void Timer::setLookbackSize(size_t new_size, delta_t default_delta) {
		auto new_ptr = std::make_unique<LookbackLine[]>(new_size);
		auto cp      = std::min<size_t>(new_size, m_lookbackSize);
		for(size_t i = 0; i < cp; ++i) {
			new_ptr[i] = m_lookback[i];
		}
		for(size_t i = cp; i < new_size; ++i) {
			new_ptr[i] = {
				.burst = 0.0,
				.delta = default_delta };
		}
		m_lookback = std::move(new_ptr);
	}


	void Timer::resetEstimates(delta_t default_delta) noexcept {
		m_avgMetrics.burst = 0.0;
		m_avgMetrics.delta = default_delta;
		for(size_t i = 0; i < m_lookbackSize; ++i) {
			auto& ln = m_lookback[i];
			ln.burst = 0.0;
			ln.delta = default_delta;
		}
	}


	void Timer::beginCycle() {
		using Seconds = std::chrono::duration<delta_t, std::chrono::seconds::period>;
		auto& ln      = m_lookback[m_currentLine % m_lookbackSize];

		m_lastBegin        = Clock::now();
		auto new_delta     = std::chrono::duration_cast<Seconds>(m_lastBegin - m_previousBegin).count();
		m_avgMetrics.delta = impl::reaverage(ln.delta, new_delta, m_avgMetrics.delta, m_lookbackSize);
		m_lastCycleEnded   = false;
		ln.delta           = new_delta;
	}


	void Timer::endCycle() {
		if(m_lastCycleEnded) [[unlikely]] return;

		{
			using Seconds = std::chrono::duration<delta_t, std::chrono::seconds::period>;
			auto& ln = m_lookback[m_currentLine % m_lookbackSize];
			auto now = Clock::now();
			auto new_burst = std::chrono::duration_cast<Seconds>(now - m_lastBegin).count();
			m_avgMetrics.burst = impl::reaverage(ln.burst, new_burst, m_avgMetrics.burst, m_lookbackSize);
			ln.burst = new_burst;
		}

		m_lastCycleEnded = true;
		++ m_currentLine;
		m_previousBegin = m_lastBegin;
	}


	Regulator::Regulator(
			unsigned               lookback_size,
			delta_t                desired_delta,
			WaitStrategyState      initial_strat,
			const RegulatorParams& params
	):
			Tracker(lookback_size, desired_delta),
			m_params(params),
			m_desiredDelta(desired_delta)
	{
		assert(m_desiredDelta >= 0.0);
		assert(m_params.deltaTolerance >= 0.0);
		setStrategy(initial_strat);
	}


	bool Regulator::estCanKeepUp() const noexcept {
		return m_currentStrategy == WaitStrategyState::eCantKeepUp;
	}


	void Regulator::setStrategy(WaitStrategyState wss) noexcept {
		#ifndef NDEBUG
		switch(wss) {
			default: abort();
			case WaitStrategyState::eCantKeepUp:  [[fallthrough]];
			case WaitStrategyState::eYield:       [[fallthrough]];
			case WaitStrategyState::eSleepUntil:  [[fallthrough]];
			case WaitStrategyState::eAlwaysYield: [[fallthrough]];
			case WaitStrategyState::eAlwaysSleepUntil: break;
		}
		#endif

		using int_t = std::underlying_type_t<WaitStrategyState>;

		m_currentStrategy = wss;
		auto i = int_t(m_currentStrategy) & int_t(0xFF);
		m_currentStrategyFn = impl::fn_table[i];
	}


	void Regulator::setDesiredDelta(delta_t desired_delta) {
		m_desiredDelta = desired_delta;
	}


	void Regulator::awaitNextTick() {
		/* |------------------------------<----|-------->
		* desired     = 2
		* avg         = 2.1
		* (des*2)-avg = 1.9 */
		delta_t comp = m_params.compensationFactor;
		delta_t comp1 = comp + delta_t(1.0);
		m_currentStrategyFn(
			m_lastBegin,
			m_avgMetrics.burst,
			(m_desiredDelta * comp1) - (m_avgMetrics.delta * comp) );
	}


	void Regulator::endCycle() {
		Timer::endCycle();

		{
			using R = impl::StrategyChangeReason;
			delta_t delta_factor = delta_t(1.0) + m_params.deltaTolerance;
			delta_t delta_ratio  = m_avgMetrics.delta / m_desiredDelta;
			auto&      burst_tol = m_params.burstTolerance;
			auto       old_strat = m_currentStrategy;
			#define STRAT_CHANGE_(COND_, REASON_) (COND_) [[unlikely]] { impl::changeStrategy<R::REASON_>(&m_currentStrategy, &m_currentStrategyFn, m_params.strategyMask); }
			if      STRAT_CHANGE_(m_avgMetrics.burst >  burst_tol + m_desiredDelta, eHighBurst)
			else if STRAT_CHANGE_(delta_ratio        >                delta_factor, eHighDelta)
			else if STRAT_CHANGE_(delta_ratio        < delta_t(1.0) / delta_factor, eLowDelta)
			if(old_strat != m_currentStrategy) resetEstimates(m_desiredDelta);
			#undef STRAT_CHANGE
		}
	}

}
