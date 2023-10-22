#include "gui.hpp"



namespace SKENGINE_NAME_NS::placeholder {

	Polys RectTemplate::instantiate(glm::vec2 b0, glm::vec2 b1) {
		VkDeviceSize vertexSegmSize   = std::size(RectTemplate::vertices)  * sizeof(PolyVertex);
		VkDeviceSize instanceSegmSize = std::size(RectTemplate::instances) * sizeof(PolyInstance);

		Polys r = {
			.vertexCount   = std::size(RectTemplate::vertices),
			.instanceCount = std::size(RectTemplate::instances),
			.vertexInput = std::make_unique_for_overwrite<std::byte[]>(vertexSegmSize + instanceSegmSize) };

		if(b0.x > b1.x) std::swap(b0.x, b1.x);
		if(b0.y > b1.y) std::swap(b0.y, b1.y);

		auto * const vertices  = reinterpret_cast<PolyVertex*>(r.vertexInput.get());
		auto * const instances = reinterpret_cast<PolyInstance*>(r.vertexInput.get() + vertexSegmSize);

		#define SET_(O_, IX_, IY_) vertices[O_].pos.x = b ## IX_.x; vertices[O_].pos.y = b ## IY_.y;
		SET_(0,  0, 0)
		SET_(1,  1, 0)
		SET_(2,  1, 1)
		SET_(3,  0, 1)
		SET_(4,  0, 0)
		#undef SET_
		*instances = *RectTemplate::instances;

		return r;
	}

}
