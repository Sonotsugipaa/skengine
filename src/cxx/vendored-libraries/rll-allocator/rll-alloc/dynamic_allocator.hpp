#pragma once

#include "static_allocator.hpp"

#include <concepts>
#include <vector>
#include <cassert>



namespace rll_alloc {

	template <typename T>
	concept Preallocator = requires(
		T t,
		T&& t_rvalue,
		size_t page_count,
		const StaticAllocator<typename T::addr_t, T::pageSize>& bucket_ref
	) {
		typename T::addr_t;
		{ T::pageSize } -> std::convertible_to<size_t>;
		{ t.acquire_space(page_count) } -> std::same_as<typename T::addr_t>;
		{ t.release_space(bucket_ref) };
		{ T() };
		{ T(t_rvalue) };
	};



	namespace _template_impl {

		template <Preallocator iface_t, typename BucketVector>
		size_t occupy_empty_page_seq_dyn(
				iface_t& iface,
				BucketVector& buckets,
				size_t& cursor,
				size_t bucket_pages,
				size_t required_pages
		) {
			#define RLL_ALLOC_Q_RETN_(CUR_, RVAL_) { cursor = CUR_; return RVAL_; }
			size_t cursor_next = cursor;
			size_t buckets_count = buckets.size();

			{ // Try to occupy an existing bucket
				assert(buckets_count == 0 || buckets_count > cursor);
				if(buckets_count > 0) do {
					auto& bucket = buckets[cursor_next];
					Allocation<typename iface_t::addr_t> occupied_addr = bucket.try_alloc(required_pages);
					if(occupied_addr.pageCount > 0) RLL_ALLOC_Q_RETN_(cursor_next, occupied_addr.base);

					++ cursor_next;
					if(cursor_next >= buckets_count) cursor_next = 0;
				} while(cursor_next != cursor);
			}

			{ // Create a new bucket and occupy it
				buckets.push_back(
					typename BucketVector::value_type(iface.acquire_space(bucket_pages), bucket_pages) );
				Allocation<typename iface_t::addr_t> occupied_addr = buckets.back().try_alloc(required_pages);
				if(occupied_addr.pageCount > 0) {
					RLL_ALLOC_Q_RETN_(buckets_count, occupied_addr.base);
				} else {
					return SIZE_MAX;
				}
			}

			#undef RLL_ALLOC_Q_RETN_
		}


		template <Preallocator preallocator_t, typename BucketVector>
		void free_page_seq_dyn(
				preallocator_t& preallocator,
				BucketVector& buckets,
				size_t& cursor,
				typename preallocator_t::addr_t free_address
		) {
			size_t cursor_next = cursor;
			size_t buckets_count = buckets.size();

			(void) preallocator;

			assert(buckets_count > cursor);
			if(buckets_count > 0) do {
				auto& bucket = buckets[cursor_next];
				auto bucket_begin = bucket.base();
				auto bucket_end = bucket_begin + (bucket.pageCount() * BucketVector::value_type::pageSize);
				if(free_address >= bucket_begin && free_address < bucket_end) {
					cursor = cursor_next;
					bucket.dealloc(free_address);
					return;
				}

				++ cursor_next;
				if(cursor_next >= buckets_count) cursor_next = 0;
			} while(cursor_next != cursor);
		}

	}



	template <Preallocator preallocator_t>
	class DynAllocator {
	public:
		using addr_t = preallocator_t::addr_t;
		using alloc_t = Allocation<addr_t>;
		using static_allocator_t = StaticAllocator<addr_t, preallocator_t::pageSize>;
		static constexpr size_t pageSize = preallocator_t::pageSize;

		DynAllocator() = default;

		DynAllocator(preallocator_t preallocator, size_t pages_per_bucket):
				ownPreallocator(std::move(preallocator)),
				bucketCursor(0),
				pagesPerBucket(pages_per_bucket)
		{ }

		~DynAllocator() {
			for(auto& bucket : buckets) {
				ownPreallocator.release_space(bucket);
			}
		}

		alloc_t try_alloc(size_t required_page_count) noexcept {
			if(required_page_count < 1) return { 0, 0 };
			size_t page_offset = _template_impl::occupy_empty_page_seq_dyn<decltype(ownPreallocator), decltype(buckets)>(
				ownPreallocator, buckets,
				bucketCursor,
				pagesPerBucket,
				required_page_count );
			if(page_offset == SIZE_MAX) return { 0, 0 };
			return {
				page_offset,
				required_page_count };
		}

		addr_t alloc(size_t required_page_count) {
			auto allocd = try_alloc(required_page_count);
			if(allocd.pageCount < 1) throw OutOfPagesException();
			return allocd.base;
		}

		void dealloc(addr_t allocation_base) noexcept {
			_template_impl::free_page_seq_dyn<decltype(ownPreallocator), decltype(buckets)>(
				ownPreallocator, buckets,
				bucketCursor,
				allocation_base );
		}

		preallocator_t&       preallocator()       { return ownPreallocator; }
		const preallocator_t& preallocator() const { return ownPreallocator; }

	private:
		[[no_unique_address]] preallocator_t ownPreallocator;
		std::vector<static_allocator_t> buckets;
		size_t bucketCursor;
		size_t pagesPerBucket;
	};

}
