#include <fmamdl/layout.hpp>

#include <spdlog/spdlog.h>

#include <cstring>



int main(int argc, char** argv) {
	using namespace fmamdl;

	if(argc < 2) return 1;

	if((argc == 3) && (0 == strcmp("fail", argv[2]))) {
		try {
			Layout::fromCstring(argv[1]);
			spdlog::error("Task did not fail.");
			return 1;
		} catch(LayoutStringError& err) {
			spdlog::info("Task failed successfully.");
			return 0;
		}
	} else {
		auto l    = Layout::fromCstring(argv[1]);
		auto cur  = argv+2;
		bool fail = false;

		spdlog::info("Layout has {} data", l.length());
		for(int i = 0; i < argc-2; ++i) {
			if(cur[i][0] == '\0' || cur[i][1] == '\0') return 2;
			bool real;
			bool signd;
			uint8_t width;
			if(cur[i][0] == 'f')      { real = true;  signd = true; }
			else if(cur[i][0] == 's') { real = false; signd = true; }
			else                      { real = false; signd = false; }
			width = cur[i][1] - '0';
			auto tiCmp = l[i];
			spdlog::info(
				">  Datum {} = {}{}{} ({}{}{})",
				i,
				tiCmp.isSigned()? "s":"u", tiCmp.isReal()? "f":"i", uint8_t(tiCmp.width()),
				signd?            "s":"u", real?           "f":"i", uint8_t(width) );
			if(tiCmp != DatumType(real, signd, std::log2(width))) fail = true;
		}

		return fail? 4 : 0;
	}

	std::unreachable();
}
