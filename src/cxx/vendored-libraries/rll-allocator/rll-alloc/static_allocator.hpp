#pragma once

#include <cstdint>
#include <stdexcept>



namespace rll_alloc {

	namespace _type_unaware_impl {

		using bitmap_word_t = uint_fast32_t;

	}



	enum class PageStatus : _type_unaware_impl::bitmap_word_t {
		eFree          = 0b00,
		eAllocationEnd = 0b10,
		eOccupied      = 0b11
	};



	namespace _type_unaware_impl {

		using bitmap_word_t = uint_fast32_t;

		struct StaticAllocatorState {
			_type_unaware_impl::bitmap_word_t* allocBitmap;
			size_t pageCount;
			size_t curStride;
			size_t curCursor;
			size_t lastFailStride;
		};

		void new_state(StaticAllocatorState& state, size_t page_count);
		void delete_state(StaticAllocatorState&);

		size_t init_stride(size_t pages) noexcept;

		size_t occupy_empty_page_seq(StaticAllocatorState& state, size_t page_count, size_t min_alignment) noexcept;
		void free_page_seq(StaticAllocatorState& state, size_t free_at_offset) noexcept;
		bool try_resize_page_seq(StaticAllocatorState& state, size_t resize_offset, size_t new_size) noexcept;

		PageStatus get_page_status(const StaticAllocatorState& state, size_t offset) noexcept;

	}



	class OutOfPagesException : public virtual std::runtime_error {
	public:
		OutOfPagesException();
	};


	template <typename BaseAddrType>
	concept AddressType = requires(BaseAddrType t, size_t u) {
		size_t(t - t);
		t + size_t(u);
	};


	template <AddressType addr_type>
	struct Allocation {
		using addr_t = addr_type;
		addr_t base;
		size_t pageCount;
	};


	template <AddressType addr_type>
	class DumpInterface {
	public:
		virtual void dump_page(addr_type page_addr, PageStatus status) = 0;
	};


	template <AddressType addr_type, size_t page_size = 1>
	class StaticAllocator {
	public:
		static_assert(page_size > 0);

		using addr_t = addr_type;
		using alloc_t = Allocation<addr_t>;
		static constexpr size_t pageSize = page_size;

		StaticAllocator() { state.allocBitmap = nullptr; }

		StaticAllocator(const StaticAllocator&) = delete;

		StaticAllocator(StaticAllocator&& mv):
			addrBase(mv.addrBase),
			state(std::move(mv.state))
		{
			mv.state.allocBitmap = nullptr;
		}

		StaticAllocator(addr_t base, size_t page_count):
			addrBase(base),
			state { }
		{
			_type_unaware_impl::new_state(state, page_count);
		}

		StaticAllocator& operator=(StaticAllocator&& mv) {
			this->~StaticAllocator();
			return * new (this) StaticAllocator(std::move(mv));
		}

		~StaticAllocator() {
			if(state.allocBitmap) _type_unaware_impl::delete_state(state);
		}

		alloc_t try_alloc(size_t required_page_count, size_t min_alignment_pages = 0) noexcept {
			if(required_page_count < 1) return { 0, 0 };
			size_t page_offset = _type_unaware_impl::occupy_empty_page_seq(
				state,
				required_page_count,
				min_alignment_pages );
			if(page_offset == SIZE_MAX) return { 0, 0 };
			return {
				addrBase + (page_offset * pageSize),
				required_page_count };
		}

		addr_t alloc(size_t required_page_count, size_t min_alignment_pages = 0) {
			auto allocd = try_alloc(required_page_count, min_alignment_pages);
			if(allocd.pageCount < 1) throw OutOfPagesException();
			return allocd.base;
		}

		void dealloc(addr_t allocation_base) noexcept {
			_type_unaware_impl::free_page_seq(
				state,
				(allocation_base - addrBase) / pageSize );
		}

		bool try_resize(addr_t allocation_base, size_t new_size) {
			return _type_unaware_impl::try_resize_page_seq(
				state,
				(allocation_base - addrBase) / pageSize,
				new_size );
		}

		void dump_pages(DumpInterface<addr_t>& dump_iface) const {
			addr_t end = addrBase + (pageSize * state.pageCount);
			for(addr_t i = addrBase; i < end; i += pageSize) {
				dump_iface.dump_page(
					i,
					_type_unaware_impl::get_page_status(
						state,
						(i - addrBase) / pageSize ) );
			}
		}

		size_t pageCount() const noexcept { return state.pageCount; }
		addr_t base()      const noexcept { return addrBase; }

	private:
		addr_t addrBase;
		_type_unaware_impl::StaticAllocatorState state;
	};

};
