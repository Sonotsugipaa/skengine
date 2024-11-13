#pragma once

#include <skengine_fwd.hpp>

#include <cassert>
#include <idgen.hpp>



namespace SKENGINE_NAME_NS {

	template <idgen::Id T>
	struct IdGeneratorWrapper {
		std::mutex            mutex;
		idgen::IdGenerator<T> gen;

		T    generate() noexcept { mutex.lock(); auto r = gen.generate(); mutex.unlock(); return r; }
		void recycle(T id)  noexcept { mutex.lock(); gen.recycle(id); mutex.unlock(); }
	};

	template <idgen::Id T>
	IdGeneratorWrapper<T> id_generator;

}
