#pragma once

#include <concepts>
#include <atomic>
#include <cassert>



namespace SKENGINE_NAME_NS {

	template <typename T>
	concept ScopedEnum = requires(T t) {
		t = T(std::underlying_type_t<T>(1));
	};


	template <ScopedEnum T>
	T generate_id() {
		using int_t = std::underlying_type_t<T>;
		static std::atomic<int_t> last = 1;
		int_t r = last.fetch_add(1, std::memory_order_relaxed);
		assert(r != 0);
		return T(r);
	}

}
