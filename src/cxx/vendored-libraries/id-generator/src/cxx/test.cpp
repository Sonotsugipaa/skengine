#include <cassert> // Must be included before `idgen.hpp`
#include "include/idgen.hpp"

#include <fmt/core.h>

#include <string>
#include <cstdlib>
#include <cstdint>
#include <set>
#include <unordered_set>

#include <unistd.h>
#include <posixfio_tl.hpp>



auto stdoutBuf = posixfio::OutputBuffer(posixfio::FileView(STDOUT_FILENO), 4096);

using id8s_e = int8_t;  enum class Id8s : id8s_e { };
using id8u_e = uint8_t; enum class Id8u : id8u_e { };



template <typename Id, bool tp_verbose>
bool testSequentialGenerations(size_t genCount) {
	using id_e = std::underlying_type_t<Id>;

	idgen::IdGenerator<Id> generator;
	std::unordered_set<Id> generated;

	std::string fmtBuffer;
	fmtBuffer.reserve(128);

	Id   expectedGen;
	Id   gen;
	bool fail = false;

	std::set<Id> eraseQueue;
	size_t eraseCount = 0;

	auto rmId = [&](id_e id) {
		assert(size_t(id) < genCount);
		generator.recycle(Id(id));
		generated.erase(Id(id));
		eraseQueue.insert(Id(id));
		++ eraseCount;
	};

	for(size_t i = 0; i < genCount + eraseCount; ++i) {
		if(i == genCount / 2) [[unlikely]] {
			rmId(45);
			rmId(48);
			rmId(46);
			rmId(47);
		}

		expectedGen = Id(size_t(idgen::baseId<Id>()) + i - eraseCount);
		if(! eraseQueue.empty()) {
			expectedGen = * eraseQueue.begin();
			eraseQueue.erase(eraseQueue.begin());
		}

		fmtBuffer.clear();
		gen = generator.generate();
		if(gen != expectedGen) {
			bool ins = generated.insert(gen).second;
			if(ins) {
				fmt::format_to(std::back_inserter(fmtBuffer), "Generated  {} instead of {}\n", id_e(gen), id_e(expectedGen)); }
			else {
				fmt::format_to(std::back_inserter(fmtBuffer), "Duplicated {} instead of {}\n", id_e(gen), id_e(expectedGen)); }
			fail = true;
		} else {
			if constexpr(tp_verbose) {
				fmt::format_to(std::back_inserter(fmtBuffer), "Generated  {}\n", id_e(gen));
			}
			if(! generated.insert(gen).second) {
				fmt::format_to(std::back_inserter(fmtBuffer), "Duplicated {}\n", id_e(gen));
				fail = true;
			}
		}
		if(! fmtBuffer.empty()) stdoutBuf.writeAll(fmtBuffer.data(), fmtBuffer.size());
		expectedGen = Id(id_e(expectedGen) + 1);
	}

	return ! fail;
}



int main(int argn, char** args) {
	bool fail = false;

	try {
		fail = testSequentialGenerations<Id8s, true>(128)? fail : true;
		fail = testSequentialGenerations<Id8u, true>(255)? fail : true;
	} catch(...) {
		return EXIT_FAILURE;
	}

	return fail? EXIT_FAILURE : EXIT_SUCCESS;
}
