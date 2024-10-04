#include <sflog.hpp>

#include <unistd.h>
#include <posixfio_tl.hpp>

#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <array>



using posixfio::FileView;
using posixfio::OutputBuffer;



using LoggerSink = OutputBuffer;
using Logger = sflog::Logger<LoggerSink*>;
using sflog::Level;
using sflog::OptionBit;

Logger logger;

struct CopyableInt {
	int i;
	CopyableInt(int i): i(i) { }
	CopyableInt(const CopyableInt& cp): i(cp.i) { logger.info("Copied int {}", i); }
	CopyableInt(CopyableInt&&) = default;
};

int format_as(const CopyableInt& ci) { return ci.i; }



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



template <Level... level_tp>
void benchmark(size_t count) {
	using Ftime = long double;
	auto restoreLevel = logger.getLevel();
	std::array<std::pair<Level, Ftime>, sizeof...(level_tp)> results;
	size_t runIndex = 0;
	constexpr auto sleep = [](auto ms) { logger.info("Sleeping for {}s...", float(ms)/1000.0f); std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
	auto run = [&](Level level) {
		logger.setLevel(level);
		sleep(1500);
		SteadyTimer<std::chrono::nanoseconds> timer;
		for(size_t i = 0; i < count; ++i) switch(level) {
			#define LOG_(L_, FN_) case L_: logger.FN_("Performing {} benchmark: iteration {}", sflog::levelStrOf(L_), i); break;
			LOG_(Level::eTrace   , trace   );
			LOG_(Level::eDebug   , debug   );
			LOG_(Level::eInfo    , info    );
			LOG_(Level::eWarn    , warn    );
			LOG_(Level::eError   , error   );
			LOG_(Level::eCritical, critical);
			default: std::unreachable(); abort();
			#undef LOG_
		}
		logger.flush();
		auto time = timer.count<Ftime>() / Ftime(1'000'000.0);
		results[runIndex ++] = { level, time };
	};
	for(auto level : { level_tp... }) run(level);
	logger.setLevel(Level::eInfo);
	for(auto result : results)
	logger.info("Benchmark[ {:8} ] finished with {} lines in {:.6g}ms; average line time is {:.6g}ms",
		sflog::levelStrOf(result.first),
		count,
		result.second,
		result.second / Ftime(count) );
	logger.setLevel(restoreLevel);
};



int main() {
	using namespace std::string_view_literals;
	LoggerSink stdoutbb = LoggerSink(STDOUT_FILENO, 1<<14);
	auto       stdoutb  = &stdoutbb;
	sflog::formatTo(*stdoutb,         "Hello, {}!\n", "posixfio buffer"); stdoutb->flush();
	sflog::formatTo(stdoutb->file(),  "Hello, {}!\n", "posixfio fileview");
	sflog::formatTo(&std::cout,       "Hello, {}!\n", "std::cout*");
	sflog::formatTo(std::cout,        "Hello, {}!\n", "std::cout"); std::cout.flush();

	logger = Logger(
		stdoutb,
		Level::eAll,
		OptionBit::eUseAnsiSgr | OptionBit::eAutoFlush,
		"["sv, "Skengine "sv, ""sv, "]: "sv );

	benchmark<
		Level::eDebug,
		Level::eWarn
	>(1000);

	int        i  = 2;
	const int& ir = i;
	std::string str = "1234";
	logger.trace   ("    Trace log" "     {} {}={} {} {} {}.", 1, i, ir, i+1, ir+2, str);
	logger.debug   ("    Debug log" "     {} {}={} {} {} {}.", 1, i, ir, i+1, ir+2, str);
	logger.info    ("     Info log""      {} {}={} {} {} {}.", 1, i, ir, i+1, ir+2, str);
	logger.warn    ("     Warn log""      {} {}={} {} {} {}.", 1, i, ir, i+1, ir+2, str);
	logger.error   ("    Error log" "     {} {}={} {} {} {}.", 1, i, ir, i+1, ir+2, str);
	logger.critical(" Critical log"    "  {} {}={} {} {} {}.", 1, i, ir, i+1, ir+2, str);
	CopyableInt ci = 255;
	logger.info("Copyable int {}", ci);

	return EXIT_SUCCESS;
}
