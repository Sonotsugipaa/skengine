#pragma once

#include <glm/vec3.hpp>

#include "draw-geometry/core.hpp"

#include <ui/ui.hpp>

#include <vk-util/memory.hpp>

#include <memory>



namespace SKENGINE_NAME_NS {
inline namespace gui {

	class UpdateableElement : virtual ui::Element {
	public:
		bool ui_elem_hasBeenModified() const noexcept override { return upd_elem_stateCtr == upd_elem_lastDrawStateCtr; }

	protected:
		void upd_elem_fwdState() { ++ upd_elem_stateCtr; }
		void upd_elem_update()   { upd_elem_lastDrawStateCtr = upd_elem_stateCtr; }

	private:
		uint_fast16_t  upd_elem_lastDrawStateCtr = 0;
		uint_fast16_t  upd_elem_stateCtr         = 1;
	};

}}



namespace SKENGINE_NAME_NS::placeholder {

	class Polys {
	public:
		VkDeviceSize vertexCount;
		VkDeviceSize instanceCount;
		std::unique_ptr<std::byte[]> vertexInput;
	};


	struct RectTemplate {
		static constexpr PolyVertex vertices[] = {
			{ .position = { -1.0f, -1.0f,  1.0f } },
			{ .position = { +1.0f, -1.0f,  1.0f } },
			{ .position = { +1.0f, +1.0f,  1.0f } },
			{ .position = { -1.0f, +1.0f,  1.0f } } };
		static constexpr PolyInstance instances[] = {
			{ .color = { 1.0f, 0.0f, 0.23529411764705882f, 0.2f }, .transform = glm::mat4(1.0f) } };

		static Polys instantiate(glm::vec2 p0, glm::vec2 p1);
	};

}
