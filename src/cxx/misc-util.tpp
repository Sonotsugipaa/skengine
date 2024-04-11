#pragma once

#include <utility>
#include <type_traits>
#include <cstring>



namespace util {

	template <typename T>
	concept TriviallyCopyable = std::is_trivially_copyable_v<T>;


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
	};


	/// \brief A range of trivially-copyable contiguous objects,
	///        that may or may not own them.
	///
	/// \tparam T The underlying type of the range.
	///
	/// A `TransientPtrRange<T>` holds a pointer to the beginning of a contiguous
	/// range of T, such as a `T` array or a `std::vector<T>`,
	/// its length, and whether it owns the underlying data.
	/// If and only if the `TransientPtrRange` does own the underlying data,
	/// then it destroys it when the destructor is called.
	///
	/// The user must be careful when managing a long-living `TransientPtrRange`,
	/// because it cannot account for non-owned underlying data being deleted;
	/// however, if the user knows the `TransientPtrRange` is non-owing,
	/// it is safe to delete the underlying data *before* the former, as long as
	/// the range isn't accessed after either deletion/destruction.
	///
	/// Example:
	///
	///     { // This is valid
	///       auto array = new int[] { 1, 2, 3 };
	///       auto range = TransientPtrRange::referenceTo(array, 3);
	///       std::cout << range[1] << std::endl;
	///       assert(! range.ownsMemory());
	///       delete[] array;
	///       // implicit ~TransientPtrRange()
	///     }
	///     { // This reads a deleted object, and causes undefined behavior
	///       auto array = new int[] { 1, 2, 3 };
	///       auto range = TransientPtrRange::referenceTo(array, 3);
	///       delete[] array;
	///       std::cout << range[1] << std::endl; // UB here
	///       // implicit ~TransientPtrRange(),
	///     }
	///
	/// \note As suggested by the constrained type of the template parameter T,
	///       creating a byte-by-byte copy of an object of type T must be
	///       defined behavior.
	///
	template <TriviallyCopyable T>
	struct TransientPtrRange {
	public:
		static TransientPtrRange copyOf(const T* begin, const T* end) {
			TransientPtrRange r;
			size_t length = end - begin;
			if(end > begin) [[likely]] {
				size_t bytes = length * sizeof(T);
				r.tr_begin = reinterpret_cast<T*>(operator new[](bytes));
				memcpy(const_cast<std::remove_const_t<T>*>(r.tr_begin), begin, bytes);
				r.tr_length = size_t(1) | (length << size_t(1));
			} else {
				r.tr_begin = nullptr;
				r.tr_length = 0;
			}
			return r;
		}

		static constexpr TransientPtrRange referenceTo(T* begin, T* end) noexcept {
			TransientPtrRange r;
			r.tr_begin = begin,
			r.tr_length = ((end - begin) << size_t(1));
			return r;
		}

		template <size_t length_tp> static constexpr TransientPtrRange      copyOf(T (&a)[length_tp])          { return      copyOf(a, a + length_tp); }
		template <size_t length_tp> static constexpr TransientPtrRange referenceTo(T (&a)[length_tp]) noexcept { return referenceTo(a, a + length_tp); }

		constexpr TransientPtrRange() = default;

		explicit
		constexpr TransientPtrRange(const TransientPtrRange& cp):
			tr_length(0)
		{
			if(cp.ownsMemory()) [[unlikely]] {
				*this = cp.copy();
			} else {
				tr_begin  = cp.tr_begin;
				tr_length = cp.tr_length;
			}
		}

		constexpr TransientPtrRange(TransientPtrRange&& mv):
			tr_begin(mv.tr_begin),
			tr_length(mv.tr_length)
		{
			mv.tr_length = 0;
		}

		auto& operator=(const TransientPtrRange& cp) { this->~TransientPtrRange(); return * new (this) TransientPtrRange(cp); }
		auto& operator=(TransientPtrRange&& mv)      { this->~TransientPtrRange(); return * new (this) TransientPtrRange(std::move(mv)); }

		constexpr ~TransientPtrRange() {
			#define LIKELINESS_ unlikely // "You pay marginally less for what you don't use" - the some-overhead principle
			if(tr_length & size_t(1)) [[LIKELINESS_]] operator delete[](const_cast<std::remove_const_t<T>*>(tr_begin));
			#undef LIKELINESS_
		}

		auto copy() const& noexcept { return copyOf(tr_begin, tr_begin + size()); }

		constexpr bool ownsMemory() const noexcept { return (tr_length & size_t(1)) == 1; }
		constexpr size_t size() const noexcept { return tr_length >> size_t(1); }

		constexpr T*       begin()       noexcept { return tr_begin; }
		constexpr const T* begin() const noexcept { return tr_begin; }
		constexpr T*         end()       noexcept { return tr_begin + size(); }
		constexpr const T*   end() const noexcept { return tr_begin + size(); }
		constexpr T*       operator[](size_t i)       noexcept { return tr_begin + i; }
		constexpr const T* operator[](size_t i) const noexcept { return tr_begin + i; }

	private:
		T*     tr_begin;
		size_t tr_length;
	};

}
