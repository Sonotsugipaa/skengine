#pragma once

#include <concepts>
#include <unordered_set>
#include <unordered_map>



namespace sneka {

	template <typename T>
	concept BasicHashable = requires(const T& ctr) {
		{ auto(ctr.hash()) } -> std::same_as<std::size_t>;
	};

	template <typename T>
	concept BasicEqualComparable = requires(const T& ctr0, const T& ctr1) {
		{ auto(ctr0 == ctr1) } -> std::convertible_to<bool>;
	};

	template <typename T>
	concept BasicUnorderedKeyType =
		BasicHashable<T> &&
		BasicEqualComparable<T> &&
		(! requires { typename std::hash<T>; }) &&
		(! requires { typename std::equal_to<T>; });


	template <typename T>
	struct BasicHash {
		auto operator()(const T& t) const noexcept requires ( BasicHashable<T>) { return t.hash(); }
		auto operator()(const T& t) const noexcept requires (!BasicHashable<T>) { static const std::hash<T> h; return h(t); }
	};

	template <BasicEqualComparable T>
	struct BasicEqualTo {
		auto operator()(const T& t0, const T& t1) const noexcept requires ( BasicEqualComparable<T>) { return 0 == memcmp(&t0, &t1, sizeof(T)); }
		auto operator()(const T& t0, const T& t1) const noexcept requires (!BasicEqualComparable<T>) { static const std::equal_to<T> eq; return eq(t0, t1); }
	};


	template <typename T>             using BasicUset = std::unordered_set<T,    BasicHash<T>, BasicEqualTo<T>>;
	template <typename T, typename V> using BasicUmap = std::unordered_map<T, V, BasicHash<T>, BasicEqualTo<T>>;

}
