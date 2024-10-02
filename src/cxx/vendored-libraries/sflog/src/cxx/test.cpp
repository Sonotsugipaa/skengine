#include <sflog.hpp>

#include <unistd.h>
#include <posixfio_tl.hpp>

#include <iostream>
#include <memory>



using posixfio::FileView;
using posixfio::ArrayOutputBuffer;



int main() {
	using namespace std::string_view_literals;
	auto stdoutbb = ArrayOutputBuffer<>(STDOUT_FILENO);
	auto stdoutb = &stdoutbb;
	sflog::formatTo(*stdoutb,         "Hello, {}!\n", "posixfio buffer"); stdoutb->flush();
	sflog::formatTo(stdoutb->file(),  "Hello, {}!\n", "posixfio fileview");
	sflog::formatTo(&std::cout,       "Hello, {}!\n", "std::cout*");
	sflog::formatTo(std::cout,        "Hello, {}!\n", "std::cout"); std::cout.flush();

	auto logger = sflog::Logger<decltype(stdoutb)>(
		stdoutb,
		sflog::Level::eAll,
		sflog::AnsiSgr::eYes,
		"["sv, "Skengine "sv, ""sv, "]: "sv );
	int        i  = 2;
	const int& ir = i;
	logger.trace   ("    Trace log" "     {} {}={} {} {}.", 1, i, ir, i+1, ir+2);
	logger.debug   ("    Debug log" "     {} {}={} {} {}.", 1, i, ir, i+1, ir+2);
	logger.info    ("     Info log""      {} {}={} {} {}.", 1, i, ir, i+1, ir+2);
	logger.warn    ("     Warn log""      {} {}={} {} {}.", 1, i, ir, i+1, ir+2);
	logger.error   ("    Error log" "     {} {}={} {} {}.", 1, i, ir, i+1, ir+2);
	logger.critical(" Critical log"    "  {} {}={} {} {}.", 1, i, ir, i+1, ir+2);

	return EXIT_SUCCESS;
}
