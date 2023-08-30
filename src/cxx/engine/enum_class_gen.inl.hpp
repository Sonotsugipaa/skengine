#pragma once

#include <concepts>
#include <atomic>



namespace SKENGINE_NAME_NS {

	template <typename T>
	concept ScopedEnum = requires(T t) {
		requires std::integral<std::underlying_type_t<T>>;
	};


	template <ScopedEnum T>
	T generate_id() {
		using int_t = std::underlying_type_t<T>;
		static std::atomic<int_t> last = 0;
		int_t r = last.fetch_add(1, std::memory_order_relaxed);
		return T(r);
	}

}
