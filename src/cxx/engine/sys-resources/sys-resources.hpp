#pragma once



namespace sysres {

	struct Topology {
		unsigned physicalThreads;
		unsigned logicalThreads;
		constexpr bool smt() const noexcept { return physicalThreads < logicalThreads; }
	};

	extern const Topology& topology;


	/// \return
	/// The optimal number of threads to use for a set of
	/// expensive tasks to execute synchronously, where their output
	/// is needed immediately after their submission.
	///
	inline unsigned optimalParallelCount() noexcept { return topology.physicalThreads; }


	/// \return
	/// The optimal number of threads to use for a set of
	/// expensive tasks to execute asynchronously, where their output
	/// isn't needed immediately after submission.
	///
	/// \note
	/// A worker pool does not benefit from having more workers
	/// than physical threads, but the overhead of running unrelated
	/// time-consuming tasks can be mitigated if the CPU features SMT.
	/// If SMT is unavailable, leave one "thread" for the rest of the system.
	///
	/// Obviously, this only applies if the entire program respects this
	/// function's return value:
	inline unsigned optimalWorkerCount() noexcept {
		// Rationale: a worker pool does not benefit from having more workers
		// than physical threads, but the overhead of running unrelated
		// time-consuming tasks can be mitigated if the CPU features SMT.
		// If SMT is unavailable, leave one "thread" for the rest of the system.
		//
		// Obviously, this only applies if the entire program respects this
		// function's return value:
		//
		auto& pt = topology.physicalThreads;
		if(topology.smt()) {
			return pt;
		} else {
			if(pt > 1) return pt - 1;
			else return pt;
		}
	}


	void updateTopologyInfo();

}
