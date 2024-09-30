#include "render_process.hpp"



namespace SKENGINE_NAME_NS {

	namespace {

		template <idgen::ScopedEnum T>
		constexpr auto idFromIndex(T idx) noexcept {
			return T(std::underlying_type_t<T>(idx) + idgen::baseId<std::underlying_type_t<T>>());
		}

		template <idgen::ScopedEnum T>
		constexpr auto idFromIndex(std::underlying_type_t<T> idx) noexcept {
			return T(idx + idgen::baseId<std::underlying_type_t<T>>());
		}

		template <idgen::ScopedEnum T>
		constexpr auto idToIndex(T id) noexcept {
			return std::underlying_type_t<T>(id) - idgen::baseId<std::underlying_type_t<T>>();
		}


		constexpr auto rtargetNegIdFromIndex(render_target_id_e idx) noexcept {
			using Id = RenderTargetId;
			using id_e = render_target_id_e;
			return Id(- (idx + id_e(1)));
		}

		constexpr auto rtargetNegIdFromIndex(RenderTargetId idx) noexcept { return rtargetNegIdFromIndex(render_target_id_e(idx)); }

		constexpr auto rtargetNegIdToIndex(RenderTargetId id) noexcept {
			using Id = RenderTargetId;
			using id_e = render_target_id_e;
			return Id(- (id_e(id) - id_e(1)));
		}

	}

}
