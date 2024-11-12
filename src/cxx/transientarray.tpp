#pragma once

#include <utility>
#include <type_traits>
#include <cstring>
#include <ranges>



namespace util {

	/// \brief A range of trivially-copyable contiguous objects,
	///        that may or may not own them.
	///
	/// \tparam T The underlying type of the range.
	///
	/// A `TransientArray<T>` holds a pointer to the beginning of a contiguous
	/// range of T, such as a `T` array or a `std::vector<T>`,
	/// its length, and whether it owns the underlying data.
	/// If and only if the `TransientArray` does own the underlying data,
	/// then it destroys it when the destructor is called.
	///
	/// The user must be careful when managing a long-living `TransientArray`,
	/// because it cannot account for non-owned underlying data being deleted;
	/// however, if the user knows the `TransientArray` is non-owning,
	/// it is safe to delete the underlying data *before* the former, as long as
	/// the range isn't accessed after either deletion/destruction.
	///
	/// Example:
	///
	///     { // This is valid
	///       auto array = new int[] { 1, 2, 3 };
	///       auto range = TransientArray::referenceTo(array, 3);
	///       std::cout << range[1] << std::endl;
	///       assert(! range.ownsMemory());
	///       delete[] array;
	///       // implicit ~TransientArray()
	///     }
	///     { // This reads a deleted object, and causes undefined behavior
	///       auto array = new int[] { 1, 2, 3 };
	///       auto range = TransientArray::referenceTo(array, 3);
	///       delete[] array;
	///       std::cout << range[1] << std::endl; // UB here
	///       // implicit ~TransientArray(),
	///     }
	///
	/// \note As suggested by the constrained type of the template parameter T,
	///       creating a byte-by-byte copy of an object of type T must be
	///       defined behavior.
	///
	template <typename T>
	requires std::is_trivially_copyable_v<T>
	struct TransientArray {
	public:
		template <typename Range> requires
			std::ranges::sized_range<Range> &&
			std::ranges::input_range<Range> &&
			(! std::ranges::contiguous_range<Range>)
		static constexpr TransientArray copyOf(Range range) {
			TransientArray r;
			size_t length = std::ranges::size(range);
			if(length > 0) [[likely]] {
				size_t bytes = length * sizeof(T);
				r.tr_begin = reinterpret_cast<T*>(operator new[](bytes));
				for(size_t offset = 0; auto& cp : range) {
					memcpy(const_cast<std::remove_const_t<T>*>(r.tr_begin) + offset, &cp, sizeof(T));
					++ offset;
				}
				r.tr_length = size_t(1) | (length << size_t(1));
			} else {
				r.tr_begin = nullptr;
				r.tr_length = 0;
			}
			return r;
		}

		template <typename Range> requires
			std::ranges::sized_range<Range> &&
			std::ranges::input_range<Range> &&
			std::ranges::contiguous_range<Range>
		static constexpr TransientArray copyOf(const Range& range) {
			TransientArray r;
			auto begin  = std::to_address(std::ranges::begin(range));
			auto length = std::ranges::size(range);
			if(length > 0) [[likely]] {
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

		template <typename Range> requires
			std::ranges::sized_range<Range> &&
			std::ranges::input_range<Range> &&
			std::ranges::contiguous_range<Range>
		static constexpr TransientArray referenceTo(Range& range) noexcept {
			TransientArray r;
			auto begin = std::to_address(std::ranges::begin(range));
			r.tr_begin = begin,
			r.tr_length = size_t(std::ranges::size(range)) << size_t(1);
			return r;
		}

		static constexpr TransientArray copyOf(const T* begin, const T* end) { return copyOf(std::ranges::subrange<const T*>(begin, end)); }
		static constexpr TransientArray referenceTo(T* begin, T* end) noexcept { auto r = std::ranges::subrange<T*>(begin, end); return referenceTo(r); }

		template <size_t length_tp> static constexpr TransientArray      copyOf(T (&a)[length_tp])          { return      copyOf(a, a + length_tp); }
		template <size_t length_tp> static constexpr TransientArray referenceTo(T (&a)[length_tp]) noexcept { return referenceTo(a, a + length_tp); }

		constexpr TransientArray(): tr_begin(nullptr), tr_length(0) { }

		template <typename U> requires std::same_as<T, std::remove_cvref_t<U>>
		constexpr TransientArray(std::initializer_list<U> initList):
			TransientArray(copyOf(std::ranges::subrange(initList.begin(), initList.end())))
		{ }

		explicit
		constexpr TransientArray(const TransientArray& cp):
			tr_length(0)
		{
			if(cp.ownsMemory()) [[unlikely]] {
				*this = cp.copy();
			} else {
				tr_begin  = cp.tr_begin;
				tr_length = cp.tr_length;
			}
		}

		constexpr TransientArray(TransientArray&& mv):
			tr_begin(mv.tr_begin),
			tr_length(mv.tr_length)
		{
			mv.tr_length = 0;
		}

		auto& operator=(const TransientArray& cp) { this->~TransientArray(); return * new (this) TransientArray(cp); }
		auto& operator=(TransientArray&& mv)      { this->~TransientArray(); return * new (this) TransientArray(std::move(mv)); }

		template <typename U> requires std::same_as<T, std::remove_cvref_t<U>>
		auto& operator=(std::initializer_list<U> initList) { this->~TransientArray(); return * new (this) TransientArray(std::move(initList)); }

		constexpr ~TransientArray() {
			if(tr_length & size_t(1)) [[unlikely]] operator delete[](const_cast<std::remove_const_t<T>*>(tr_begin));
		}

		auto copy() const& noexcept { return copyOf(tr_begin, tr_begin + size()); }

		constexpr bool ownsMemory() const noexcept { return (tr_length & size_t(1)) == 1; }
		constexpr size_t size() const noexcept { return tr_length >> size_t(1); }

		constexpr T*       begin()       noexcept { return tr_begin; }
		constexpr const T* begin() const noexcept { return tr_begin; }
		constexpr T*         end()       noexcept { return tr_begin + size(); }
		constexpr const T*   end() const noexcept { return tr_begin + size(); }
		constexpr T&       operator[](size_t i)       noexcept { return tr_begin[i]; }
		constexpr const T& operator[](size_t i) const noexcept { return tr_begin[i]; }

	private:
		T*     tr_begin;
		size_t tr_length;
	};

}
