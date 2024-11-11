#pragma once


// This is a very single-header library, it uses an integer number instead of
// semantic versioning, and I care not what happens to it as long as I can
// easily copy it around and use it.
// I don't even want to bother with setting up multiple files for it,
// so I'm just embedding the Unlicense below just in case it ends up being
// necessary or even useful.


// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non-commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org>


// Version 2



#include <chrono>
#include <concepts>



namespace util {

	template <typename T> concept TimeRepType = std::is_arithmetic_v<T>;

	template <typename T> concept TimePeriodType = requires {
		{ T::num } -> std::convertible_to<intmax_t>;
		{ T::den } -> std::convertible_to<intmax_t>; };

	template <typename T> concept TimeDurationType = requires {
		requires TimeRepType<typename T::rep>;
		requires TimePeriodType<typename T::period>; };


	template <typename clock_tp = std::chrono::steady_clock, TimeDurationType duration_tp = clock_tp::duration>
	requires std::chrono::is_clock_v<clock_tp>
	class Timer {
	public:
		using Clock = clock_tp;
		using Dur   = duration_tp;

		Timer(): t_begin(Clock::now()) { }

		template <TimeDurationType Dur = Timer::Dur>
		auto count() const noexcept { return std::chrono::duration_cast<Dur>(Clock::now() - t_begin).count(); }

		template <TimeRepType Rep, TimePeriodType Period = Dur::period>
		auto count() const noexcept { return count<std::chrono::duration<Rep, Period>>(); }

		template <TimePeriodType Period, TimeRepType Rep = Dur::rep>
		auto count() const noexcept { return count<std::chrono::duration<Rep, Period>>(); }

	private:
		Clock::time_point t_begin;
	};


	template <typename duration_tp = std::chrono::steady_clock::duration>
	requires requires(duration_tp t) { { std::chrono::duration_cast<std::chrono::seconds>(t) } -> std::same_as<std::chrono::seconds>; }
	using SteadyTimer = Timer<std::chrono::steady_clock, duration_tp>;


	template <typename T> concept TimerType = requires (T t, T* tp) {
		typename T::Clock;
		typename T::Dur;
		T();
		{ t.template count<int, std::milli>() } -> std::integral;
	};

}
