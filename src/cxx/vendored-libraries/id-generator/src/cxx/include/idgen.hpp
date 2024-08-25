#pragma once

#include <type_traits>
#include <concepts>
#include <limits>
#include <cstdint>
#include <deque>



#ifdef assert
	#define try_assert_(W_) assert(W_)
#else
	#define try_assert_(W_) ((void) (W_))
#endif



namespace idgen {

	template <typename T>
	concept SignedInt = std::signed_integral<T>;

	template <typename T>
	concept UnsignedInt = std::unsigned_integral<T>;

	template <typename T>
	concept GenericInt = SignedInt<T> || UnsignedInt<T>;

	template <typename T>
	concept ScopedEnum = std::is_scoped_enum_v<T> && GenericInt<std::underlying_type_t<T>>;

	template <typename T>
	concept Id = GenericInt<T> || ScopedEnum<T>;


	// invalidId < minId  ||  maxId < invalidId
	// baseid <= generate() <= maxId

	template <SignedInt   T> constexpr T baseId()    noexcept { return 0; }
	template <UnsignedInt T> constexpr T baseId()    noexcept { return 1; }
	template <SignedInt   T> constexpr T invalidId() noexcept { return std::numeric_limits<T>::min(); }
	template <UnsignedInt T> constexpr T invalidId() noexcept { return 0; }
	template <SignedInt   T> constexpr T minId()     noexcept { return invalidId<T>() + 1; }
	template <UnsignedInt T> constexpr T minId()     noexcept { return baseId<T>(); }
	template <GenericInt  T> constexpr T maxId()     noexcept { return std::numeric_limits<T>::max(); }

	template <ScopedEnum T> constexpr T baseId()    noexcept { return T(baseId<std::underlying_type_t<T>>()); }
	template <ScopedEnum T> constexpr T invalidId() noexcept { return T(invalidId<std::underlying_type_t<T>>()); }
	template <ScopedEnum T> constexpr T minId()     noexcept { return T(minId<std::underlying_type_t<T>>()); }
	template <ScopedEnum T> constexpr T maxId()     noexcept { return T(maxId<std::underlying_type_t<T>>()); }

	static_assert(invalidId<int8_t>() == -128);
	static_assert(minId<int8_t>()     == -127);
	static_assert(maxId<int8_t>()     == 127);
	static_assert(baseId<int8_t>()    == 0);

	static_assert(invalidId<uint8_t>() == 0);
	static_assert(minId<uint8_t>()     == 1);
	static_assert(maxId<uint8_t>()     == 255);
	static_assert(baseId<uint8_t>()    == 1);



	template <ScopedEnum T>
	class IdGenerator {
	public:
		T generate() noexcept {
			using Ut = std::underlying_type_t<T>;

			Ut r;

			if(! gen_recycledSegments.empty()) [[likely]] {
				auto& segm = gen_recycledSegments.front();
				r = segm.begin;
				++ segm.begin;
				if(segm.begin == segm.end) {
					gen_recycledSegments.erase(gen_recycledSegments.begin());
				}
				return T(r);
			}

			r = Ut(++ gen_value);
			try_assert_(r >= minId<Ut>());
			try_assert_(r <= maxId<Ut>());
			return T(r);
		}

		void recycle(T id) {
			using Ut = std::underlying_type_t<T>;

			Ut idv = Ut(id);

			try_assert_(idv >= minId<Ut>());
			try_assert_(idv <= maxId<Ut>());
			if(idv < minId<Ut>()) [[unlikely]] return;
			if(idv > maxId<Ut>()) [[unlikely]] return;

			if(gen_recycledSegments.empty()) {
				gen_recycledSegments.push_back({ idv, Ut(idv + Ut(1)) });
				return;
			}

			// The back of the queue is a likely element to be removed
			if(idv == gen_value) {
				-- gen_value;
				if(! gen_recycledSegments.empty()) {
					auto& back = gen_recycledSegments.back();
					try_assert_(back.begin < back.end);
					-- back.end;
					if(back.begin == back.end) gen_recycledSegments.erase(gen_recycledSegments.end() - 1);
				}
				return;
			}
			try_assert_(idv <= gen_value);
			if(idv > gen_value) [[unlikely]] return;

			auto insertIter0 = gen_recycledSegments.begin();
			auto end         = gen_recycledSegments.end();
			auto insertIter1 = insertIter0 + 1;
			while(insertIter1 != end) {
				bool idAlreadyRecycled = (idv >= insertIter0->begin) && (idv < insertIter0->end);
				try_assert_(! idAlreadyRecycled);
				if(idAlreadyRecycled) [[unlikely]] return;
				if(idv < insertIter1->begin) break;
				insertIter0 = insertIter1;
				++ insertIter1;
			}

			bool gt0 = idv >= insertIter0->end;
			bool lt0 = idv <  insertIter0->begin;
			try_assert_(gt0 || lt0); (void) lt0;
			if(gt0) {
				bool merged = false;
				if(idv == insertIter0->end) {
					++ insertIter0->end;
					merged = true;
				}
				if(insertIter1 != end) {
					if(insertIter1->begin == Ut(idv + Ut(1))) {
						merged = true;
						if(insertIter0->end == insertIter1->begin) {
							insertIter0->end = insertIter1->end;
							gen_recycledSegments.erase(insertIter1);
						} else {
							-- insertIter1->begin;
						}
					}
				}
				if(! merged) {
					gen_recycledSegments.insert(insertIter1, { idv, Ut(idv + Ut(1)) });
				}
			} else {
				try_assert_(lt0);
				bool merged = false;
				if(idv == insertIter0->begin - 1) {
					insertIter0->begin = idv;
					merged = true;
				}
				if(! merged) {
					gen_recycledSegments.insert(insertIter0, { idv, Ut(idv + Ut(1)) });
				}
			}
		}

	private:
		struct Segment {
			std::underlying_type_t<T> begin = baseId<std::underlying_type_t<T>>();
			std::underlying_type_t<T> end   = baseId<std::underlying_type_t<T>>();
		};

		std::underlying_type_t<T> gen_value = baseId<std::underlying_type_t<T>>() - 1;
		std::deque<Segment>       gen_recycledSegments;
	};

}



#undef try_assert_
