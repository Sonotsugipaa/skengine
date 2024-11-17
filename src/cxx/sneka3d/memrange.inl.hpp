#pragma once

#include <cstdint>
#include <concepts>
#include <bit>

#include <posixfio_tl.hpp>



namespace sneka {

	template <std::integral T>
	constexpr T serialize(T value) {
		using enum std::endian;
		static_assert((native == little) || (native == big));
		if constexpr (native == little) return value;
		return std::byteswap(value);
	}

	template <std::integral T>
	constexpr T deserialize(T value) { return serialize(value); }


	template <std::integral T>
	T hash(std::byte* data, size_t bytes) {
		T r = 0;
		T acc;
		while(bytes >= sizeof(T)) {
			memcpy(acc, data, sizeof(T));
			r = std::rotl(r, 1) ^ acc;
		}
		if(bytes > 0) {
			acc = 0; memcpy(acc, data, bytes);
			r = std::rotl(r, 1) ^ acc;
		}
		return r;
	}


	class MemoryRange {
	public:
		struct Raw {
			std::byte* data;
			size_t size;
		};

		enum class Owner { eRaw = 1, ePosixfioMmap = 2 };


		MemoryRange(nullptr_t = nullptr): mr_raw { }, mr_owner(Owner(0)) { }

		MemoryRange(posixfio::MemMapping mmap): mr_pfioMmap(std::move(mmap)), mr_owner(Owner::ePosixfioMmap) { }

		~MemoryRange() {
			auto owner = mr_owner;
			mr_owner = Owner(0);
			switch(owner) {
				case Owner::eRaw: delete[] mr_raw.data; return;
				case Owner::ePosixfioMmap: mr_pfioMmap = { }; return;
				default: return;
			}
		}

		auto& operator=(nullptr_t) noexcept { this->~MemoryRange(); return * new (this) MemoryRange(); }

		MemoryRange(MemoryRange&& mv):
			mr_owner(mv.mr_owner)
		{
			mv.mr_owner = Owner(0);
			switch(mr_owner) {
				case Owner::eRaw: mr_raw = mv.mr_raw; return;
				case Owner::ePosixfioMmap: mr_pfioMmap = std::move(mv.mr_pfioMmap); return;
			}
			mv = nullptr;
		}
		auto& operator=(MemoryRange&& mv) { this->~MemoryRange(); return * new (this) MemoryRange(std::move(mv)); }

		static MemoryRange allocate(size_t bytes) {
			MemoryRange r;
			r.mr_raw.data = reinterpret_cast<std::byte*>(::operator new[](bytes));
			r.mr_raw.size = bytes;
			r.mr_owner = Owner::eRaw;
			return r;
		}


		std::byte* data(this auto& self) noexcept {
			switch(self.mr_owner) {
				case Owner::eRaw: return self.mr_raw.data;
				case Owner::ePosixfioMmap: return self.mr_pfioMmap.template get<std::byte>();
				default: return nullptr;
			}
		}

		size_t size() const noexcept {
			switch(mr_owner) {
				case Owner::eRaw: return mr_raw.size;
				case Owner::ePosixfioMmap: return mr_pfioMmap.size();
				default: return 0;
			}
		}

	private:
		union {
			posixfio::MemMapping mr_pfioMmap;
			Raw mr_raw;
		};
		Owner mr_owner;
	};

}
