#include "static_allocator.hpp"

#include <limits>
#include <cassert>
#include <algorithm>



namespace rll_alloc::_util_impl {

	constexpr auto bitmap_value_occupied = _type_unaware_impl::bitmap_word_t(PageStatus::eOccupied);
	constexpr auto bitmap_value_last     = _type_unaware_impl::bitmap_word_t(PageStatus::eAllocationEnd);

	constexpr _type_unaware_impl::bitmap_word_t bitmap_word_bits =
		std::numeric_limits<_type_unaware_impl::bitmap_word_t>::digits;

	constexpr _type_unaware_impl::bitmap_word_t pages_per_word =
		bitmap_word_bits >> 1;


	void bitmap_set(
			_type_unaware_impl::bitmap_word_t* bitmap,
			size_t page,
			_type_unaware_impl::bitmap_word_t value
	) {
		using word_t = _type_unaware_impl::bitmap_word_t;
		assert(value <= bitmap_value_occupied);
		value = value & bitmap_value_occupied;
		bitmap += page / pages_per_word;
		page = page % pages_per_word;
		word_t mask_lsh = page << 1;
		word_t mask = word_t(bitmap_value_occupied << mask_lsh);
		word_t new_value =
			word_t(*bitmap & word_t(~ mask)) |
			word_t(value << mask_lsh);
		*bitmap = new_value;
	}

	_type_unaware_impl::bitmap_word_t bitmap_get(
			const _type_unaware_impl::bitmap_word_t* bitmap,
			size_t page
	) {
		bitmap += page / pages_per_word;
		page = page % pages_per_word;
		auto page_rsh = page << 1;
		return (*bitmap >> page_rsh) & bitmap_value_occupied;
	}


	size_t find_empty_page_seq(
			const _type_unaware_impl::bitmap_word_t* bitmap,
			size_t page_count,
			size_t stride,
			size_t offset_max,
			size_t* offset
	) {
		using _util_impl::bitmap_get;
		assert(stride > 0);

		size_t offset_cp;

		for(offset_cp = *offset; offset_cp <= offset_max; offset_cp += stride) {
			// Seek an empty page
			if(0b00 == bitmap_get(bitmap, offset_cp)) {
				size_t end = offset_cp + page_count;
				for(size_t j = offset_cp + 1; j < end; ++j) {
					// Check whether the required pages fit into this hole
					if(0b00 != bitmap_get(bitmap, j)) {
						offset_cp = j; // No need to repeat this for every uneligible page
						end = 0;
					}
				}

				// At this point:
				// end == 0   =>   bitmap[j] != 00   =>   no fit
				// end != 0   =>   bitmap[j] == 00   =>   found fit
				if(end != 0) {
					*offset += stride;
					return offset_cp;
				}
			}
		}

		{
			size_t offset_aligned = stride - (offset_cp % stride);
			if(offset_aligned == stride) offset_aligned = 0;
			offset_aligned = offset_cp + offset_aligned;
			*offset = offset_aligned;
		}
		return SIZE_MAX;
	}


	void fill_page_seq(
			_type_unaware_impl::bitmap_word_t* bitmap,
			size_t offset,
			size_t page_count
	) {
		using _util_impl::bitmap_set;
		assert(page_count > 0);
		size_t end = offset + page_count - 1;

		for(size_t j = offset; j < end; ++j) {
			bitmap_set(bitmap, j, bitmap_value_occupied);
		}
		bitmap_set(bitmap, end, bitmap_value_last);
	}


	void erase_page_seq(
			_type_unaware_impl::bitmap_word_t* bitmap,
			size_t offset
	) {
		using _util_impl::bitmap_get;
		using _util_impl::bitmap_set;

		_type_unaware_impl::bitmap_word_t bitmap_got;
		do {
			bitmap_got = bitmap_get(bitmap, offset);
			assert(bitmap_got == bitmap_value_occupied || bitmap_got == bitmap_value_last);
			bitmap_set(bitmap, offset, 0b00);
			++ offset;
		} while(bitmap_got == bitmap_value_occupied);
	}

}



namespace rll_alloc::_type_unaware_impl {

	void new_state(StaticAllocatorState& state, size_t page_count) {
		assert(page_count > 0);
		if(page_count == 0) {
			state.allocBitmap = nullptr;
			return;
		}
		auto word_count =
			(page_count / _util_impl::pages_per_word) +
			(page_count % _util_impl::pages_per_word != 0);
		state = StaticAllocatorState {
			.allocBitmap = new bitmap_word_t[word_count](0),
			.pageCount = page_count,
			.curStride = page_count / 2,
			.curCursor = 0,
			.lastFailStride = SIZE_MAX };
	}


	void delete_state(StaticAllocatorState& state) {
		delete[] state.allocBitmap;
		state.allocBitmap = nullptr;
	}


	std::size_t init_stride(std::size_t pages) noexcept {
		assert(pages > 1);
		return std::max<std::size_t>(1, pages / 2);
	}


	size_t occupy_empty_page_seq(
			StaticAllocatorState& state,
			size_t page_count,
			size_t min_alignment = 0
	) noexcept {
		using namespace _util_impl;

		if(page_count < 1 || state.lastFailStride < state.curStride) return SIZE_MAX;

		// The stride must be >= the minimum alignment, or the required page count if the latter == 0
		size_t stop_below_stride;
		if(min_alignment > 0) {
			stop_below_stride = min_alignment;
		} else {
			stop_below_stride = page_count;
		}
		assert(stop_below_stride > 0);

		size_t stride = state.curStride;
		size_t cursor = state.curCursor;
		size_t max_cursor = state.pageCount - page_count;
		size_t last_fail_stride = state.lastFailStride;

		size_t r;

		do {
			// Find a big enough empty aligned sequence
			// NOTE: `find_empty_page_seq` also moves the offset to the correct stride
			r = find_empty_page_seq(state.allocBitmap, page_count, stride, max_cursor, &cursor);
			if(r != SIZE_MAX) {
				fill_page_seq(state.allocBitmap, r, page_count);
				stop_below_stride = SIZE_MAX;
				last_fail_stride = SIZE_MAX;
				// End of cycle
			} else {
				// Move forward: decrease the stride as the cursor reached the end
				cursor = 0;
				stride = stride >> 1;
				last_fail_stride = stride;
			}
		} while(stride >= stop_below_stride);

		state.curStride = std::max<size_t>(1, stride);
		state.curCursor = cursor;
		state.lastFailStride = last_fail_stride;

		return r;
	}


	void free_page_seq(
			StaticAllocatorState& state,
			size_t free_at_cursor
	) noexcept {
		using namespace _util_impl;

		erase_page_seq(state.allocBitmap, free_at_cursor);

		// Find the maximum alignment
		size_t alignment_max = init_stride(state.pageCount);
		while(free_at_cursor % alignment_max != 0) {
			alignment_max = alignment_max >> 1;
			assert(alignment_max > 0);
		}

		// Is the maximum alignment higher than the current stride?
		if(alignment_max > state.curStride) {
			state.curStride = alignment_max;
			state.curCursor = 0;
		}
		else if(alignment_max == state.curStride && state.curCursor < free_at_cursor) {
			state.curCursor = free_at_cursor;
		}

		state.lastFailStride = SIZE_MAX;
	}


	bool try_resize_page_seq(
			StaticAllocatorState& state,
			size_t resize_offset,
			size_t new_size
	) noexcept {
		using _util_impl::bitmap_set;
		using _type_unaware_impl::bitmap_word_t;
		size_t seq_length = 0;
		size_t cursor = resize_offset;
		auto* bitmap = state.allocBitmap;

		bitmap_word_t page_status;
		while((page_status = _util_impl::bitmap_get(bitmap, cursor++)) == _util_impl::bitmap_value_occupied) {
			assert(cursor <= state.pageCount);
			++ seq_length;
		}

		assert(page_status == _util_impl::bitmap_value_last);
		if(resize_offset + new_size > state.pageCount) return false;

		if(seq_length < new_size) {
			// Grow the sequence
			size_t i;
			size_t beg = resize_offset + seq_length;
			size_t end = beg + new_size - 1;
			for(i = beg; i < end; ++i) {
				bitmap_set(bitmap, i, _util_impl::bitmap_value_occupied);
				assert(i+1 <= state.pageCount);
			}
			bitmap_set(bitmap, i, _util_impl::bitmap_value_last);
		} else {
			// Shrink the sequence
			size_t beg = resize_offset + new_size;
			size_t end = beg + seq_length;
			for(size_t i = beg; i < end; ++i) {
				assert(i <= state.pageCount);
				bitmap_set(bitmap, i, 0b00);
			}
		}

		return true;
	}


	PageStatus get_page_status(const StaticAllocatorState& state, size_t offset) noexcept {
		return PageStatus(_util_impl::bitmap_get(state.allocBitmap, offset));
	}

}



namespace rll_alloc {

	OutOfPagesException::OutOfPagesException():
			std::runtime_error("allocator out of pages")
	{ }

}
