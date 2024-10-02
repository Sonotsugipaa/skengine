#pragma once

#include <chrono>
#include <concepts>



namespace util {

	template <typename clock_tp = std::chrono::steady_clock, typename duration_tp = clock_tp::duration>
	requires std::chrono::is_clock_v<clock_tp>
	class Timer {
	public:
		using Clock = clock_tp;
		using Dur   = duration_tp;

		Timer(): t_begin(Clock::now()) { }

		template <typename Rep = Dur::rep>
		Rep count() const noexcept { return Rep(std::chrono::duration_cast<Dur>(Clock::now() - t_begin).count()); }

	private:
		Clock::time_point t_begin;
	};


	template <typename duration_tp = std::chrono::steady_clock::duration>
	requires requires(duration_tp t) { { std::chrono::duration_cast<std::chrono::seconds>(t) } -> std::same_as<std::chrono::seconds>; }
	using SteadyTimer = Timer<std::chrono::steady_clock, duration_tp>;

}
