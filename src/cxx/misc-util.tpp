#pragma once

#include <utility>
#include <type_traits>
#include <cstring>
#include <ranges>
#include <memory>
#include <cassert>



namespace util {

	/// \brief Wrapper for primitive values that automatically resets moved-from variables.
	/// \tparam T         The underlying type of the Moveable object.
	/// \tparam zero_init The value to assign to the moved-from variable.
	///
	/// `Moveable` member variables can be used for pointers or descriptors of
	/// owned resources that are not destroyed by the default destructor,
	/// without having to define (non-default) move constructor and assignment
	/// operators - as shown in the following example snippet.
	///
	///     struct Foo {
	///        Moveable<file_descriptor_t, 0> fdesc;
	///        size_t size;
	///
	///        Foo() = default;  // Moveable<...>() default-initializes its underlying value
	///
	///        template <typename Object>
	///        Foo(const char* filename):
	///           fdesc(filelib_open(filename)),
	///           size(filelib_getSize(fdesc))
	///        { }
	///
	///        ~Foo() {
	///           if(fdesc.value != 0) {
	///              filelib_close(fdesc);
	///              fdesc = 0;
	///           }
	///        }
	///
	///        Foo(Foo&& mv) = default;             // Moveable<...> sets mv's value to `0` here
	///        Foo& operator=(Foo&& mv) = default;  // ^ Ditto
	///
	///     }
	///
	template <typename T, T default_value_ = T { }>
	struct Moveable {
		using value_type = T;
		static constexpr T default_value = default_value_;

		T value;

		Moveable(): value(default_value) { }
		template <typename U> Moveable(const U& v): value(v) { }
		template <typename U> Moveable(U&& v): value(std::move(v)) { }
		~Moveable() { value = default_value; }

		template <typename U, U uz> Moveable(Moveable<U, uz>&& mv): value(std::move(mv.value)) {
			mv.value = uz;
		}

		template <typename U, U uz> Moveable& operator=(Moveable<U, uz>&& mv) {
			value = std::move(mv.value);
			mv.value = uz;
			return *this;
		}

		operator bool() const noexcept { return value != default_value; }
	};


	template <typename T> concept TypeUnsafeVectorEntry = std::is_trivial_v<T>;

	class TypeUnsafeVector {
	public:
		template <TypeUnsafeVectorEntry T>
		T* data(this auto& self) noexcept {
			using R = T*;
			bool bool_tuv_data = bool(self.tuv_data);
			auto offset        = 2 * sizeof(size_t);
			if(bool_tuv_data) return reinterpret_cast<R>(self.tuv_data.get() + offset);
			else              return R(nullptr);
		}

		template <TypeUnsafeVectorEntry T>
		auto* back(this auto& self) noexcept {
			assert(self.tuv_data);
			assert(self.size() >= 1);
			return self.template data<T>() + self.size() - 1;
		}

		template <TypeUnsafeVectorEntry T>
		void reserve(size_t newCap) {
			size_t newCapBytesPlusOverhead = (newCap * sizeof(T)) + (2 * sizeof(size_t));
			if(tuv_data) {
				auto oldCap = tuv_capacity();
				if(oldCap >= newCap) return;
				auto oldSizeBytes = tuv_size() * sizeof(T);
				auto replace = std::make_unique_for_overwrite<std::byte[]>(newCapBytesPlusOverhead);
				memcpy(replace.get(), tuv_data.get(), oldSizeBytes + (2 * sizeof(size_t)));
				tuv_data = std::move(replace);
			} else {
				tuv_data = std::make_unique_for_overwrite<std::byte[]>(newCapBytesPlusOverhead);
				tuv_size() = 0;
				tuv_capacity() = newCap;
			}
		}

		template <TypeUnsafeVectorEntry T>
		void resize(size_t newSize) {
			if(newSize < 1) [[unlikely]] { if(tuv_data) tuv_size() = newSize; return; }
			size_t newCapBytes = std::bit_ceil(newSize) * sizeof(T);
			size_t newCapBytesPlusOverhead = newCapBytes + (2 * sizeof(size_t));
			auto newSizeBytes = newSize * sizeof(T);
			if(tuv_data) {
				auto oldCapBytes = capacity() * sizeof(T);
				tuv_size() = newSize;
				if(newSizeBytes > oldCapBytes) {
					auto oldSizeBytes = tuv_size() * sizeof(T);
					auto replace = std::make_unique_for_overwrite<std::byte[]>(newCapBytesPlusOverhead);
					memcpy(replace.get(), tuv_data.get(), oldSizeBytes + (2 * sizeof(size_t)));
					tuv_data = std::move(replace);
				}
			} else {
				tuv_data = std::make_unique_for_overwrite<std::byte[]>(newCapBytesPlusOverhead);
				tuv_size() = newSize;
				tuv_capacity() = newCapBytes;
			}
		}

		template <TypeUnsafeVectorEntry T>
		T* emplaceBack() {
			if(tuv_data) resize<T>(tuv_size() + 1);
			else         resize<T>(             1);
			return back<T>();
		}

		auto size()     const noexcept {
			auto r = const_cast<TypeUnsafeVector*>(this)->tuv_size();
			return r; }
		auto capacity() const noexcept {return const_cast<TypeUnsafeVector*>(this)->tuv_capacity(); }

	private:
		std::unique_ptr<std::byte[]> tuv_data;
		size_t& tuv_size() noexcept { assert(tuv_data); return *reinterpret_cast<size_t*>(tuv_data.get()); }
		size_t& tuv_capacity() noexcept { assert(tuv_data); return *reinterpret_cast<size_t*>(tuv_data.get() + sizeof(size_t)); }
	};

}
