#include "sys-resources.hpp"

#include <thread>
#include <stdexcept>

#if defined OS_LINUX
	extern "C" {
		#include <fcntl.h>
		#include <unistd.h>
		#include <sys/sysinfo.h>
	}
	#include <cassert>
	#define SYSFILE_SMT "/sys/devices/system/cpu/smt/active"
#elif defined OS_WINDOWS

#else
	#error "No OS macro defined"
#endif



namespace {

	sysres::Topology queryTopology() {
		sysres::Topology r;
		#if defined OS_LINUX
			int nproc = get_nprocs();
			assert(nproc > 0);
			r.logicalThreads = r.physicalThreads = nproc;
			int fd = open(SYSFILE_SMT, O_RDONLY);
			if(fd == -1) throw std::runtime_error("Failed to open " SYSFILE_SMT);
			char c;
			int rd = read(fd, &c, 1);
			close(fd);
			if(rd != 1) throw std::runtime_error("Failed to read " SYSFILE_SMT);
			switch(c) {
				case '0': break;
				case '1': assert((r.physicalThreads % 2) == 0); r.physicalThreads = std::max<unsigned>(1, r.physicalThreads / 2); break;
				default: throw std::runtime_error(SYSFILE_SMT " is neither 0 nor 1");
				// The reason for the hard failure is that I could not find any documentation on the file,
				// and I would account for systems with >2 threads per core; I suspect said file may report
				// the number of logical threads minus one, which I have absolutely no way to verify.
			}
		#elif defined OS_WINDOWS
			r.physicalThreads =
			r.logicalThreads = std::thread::hardware_concurrency();
		#else
			r.physicalThreads =
			r.logicalThreads = std::thread::hardware_concurrency();
		#endif
		return r;
	}

}



namespace sysres {

	Topology topologyGlvalue = queryTopology();
	const Topology& topology = topologyGlvalue;


	void updateTopologyInfo() {
		topologyGlvalue = queryTopology();
	}

}
