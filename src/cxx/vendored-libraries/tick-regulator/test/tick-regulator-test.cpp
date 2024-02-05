#include <tick-regulator.hpp>

#include <iostream>
#include <string>
#include <string_view>

#include <spdlog/spdlog.h>



int main(int argn, char** argv) {
	if(argn != 3) {
		std::cerr << "Usage: tick-regulator-test <number> <'s' | 'ms' | 'us' | 'ns'>\n";
		return 1;
	}

	char* arg1_end;
	double arg1 = std::strtod(argv[1], &arg1_end);
	if(! (*argv[1] != '\0' && *arg1_end == '\0')) return 2;

	std::string_view arg2_str = argv[2];
	if     (arg2_str ==  "s") (void) 0;
	else if(arg2_str == "ms") arg1 /= 1'000.0;
	else if(arg2_str == "us") arg1 /= 1'000'000.0;
	else if(arg2_str == "ns") arg1 /= 1'000'000'000.0;
	else return 3;

	spdlog::set_pattern("[%^%l%$] %v");

	tickreg::RegulatorParams params = {
		.deltaTolerance = 0.3,
		.burstTolerance = 0.0,
		.compensationFactor = 8.0,
		.strategyMask   = tickreg::strategy_flag_t(tickreg::WaitStrategyFlags::eAll) };
	tickreg::Regulator reg = tickreg::Regulator(8, arg1, tickreg::WaitStrategyState::eYield, params);
	tickreg::TimePoint run_until = tickreg::Clock::now() + std::chrono::milliseconds(800*16);
	auto               beg = tickreg::Clock::now();
	uint_fast32_t      counter = 0;

	do {
		reg.beginCycle();
		spdlog::info(
			"Burst [{:7.3f} / {:7.3f}] Delta [{:7.3f} / {:7.3f}] Strategy {:08x}",
			reg.lastBurst() * 1000.0, reg.estBurst() * 1000.0,
			reg.lastDelta() * 1000.0, reg.estDelta() * 1000.0,
			unsigned(reg.currentStrategy()) );
		std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(32.0f * std::abs(std::sin(float(counter) / 60.0f))));
		reg.endCycle();
		reg.awaitNextTick();
		++ counter;
	} while(tickreg::Clock::now() < run_until);

	auto end = tickreg::Clock::now();
	spdlog::info("Iterations {} Total time {:.3f}ms", counter, std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(end - beg).count());

	return EXIT_SUCCESS;
}
