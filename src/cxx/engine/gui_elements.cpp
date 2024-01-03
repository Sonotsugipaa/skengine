#include <vulkan/vulkan.h> // VK_NO_PROTOTYPES, defined in other headers, makes it necessary to include this one first

#include "gui.hpp"

#include "engine.hpp"

#include <glm/gtc/matrix_transform.hpp>



namespace SKENGINE_NAME_NS::gui {

	namespace {

		const auto rectShape = std::make_shared<geom::Shape>(
			std::vector<PolyVertex> {
				{{ -1.0f, -1.0f,  0.0f }},
				{{ -1.0f, +1.0f,  0.0f }},
				{{ +1.0f, +1.0f,  0.0f }},
				{{ +1.0f, -1.0f,  0.0f }} });


		ShapeSet makeCrossShapeSet(float strokeWidth, const glm::vec4& color) {
			auto strokeHeight = strokeWidth;
			auto vbar = ShapeReference(rectShape, color, glm::scale(glm::mat4(1.0f), { 1.0f, strokeHeight, 1.0f }));
			auto hbar = ShapeReference(rectShape, color, glm::scale(glm::mat4(1.0f), { strokeWidth, 1.0f, 1.0f }));
			return ShapeSet({ vbar, hbar });
		}



		ShapeSet makeFrameShapeSet(float strokeWidth, const glm::vec4& color) {
			constexpr glm::mat4 mat1 = glm::mat4(1.0f);
			auto strokeHeight = strokeWidth;
			#define TRANSF_(LEFT_, TOP_, W_, H_) glm::scale(glm::translate(mat1, { float(LEFT_), float(TOP_), 0.0f }), { float(W_), float(H_), 1.0f })
			auto hbar0 = ShapeReference(rectShape, color, TRANSF_(0.0f, -1.0f, 1.0f, strokeHeight));
			auto hbar1 = ShapeReference(rectShape, color, TRANSF_(0.0f, +1.0f, 1.0f, strokeHeight));
			auto vbar0 = ShapeReference(rectShape, color, TRANSF_(-1.0f, 0.0f, strokeWidth, 1.0f));
			auto vbar1 = ShapeReference(rectShape, color, TRANSF_(+1.0f, 0.0f, strokeWidth, 1.0f));
			return ShapeSet({ vbar0, vbar1, hbar0, hbar1 });
		}

	}


	Cross::Cross(VmaAllocator vma, float strokeLength, float strokeWidth, const glm::vec4& color):
			BasicPolygon(vma, makeCrossShapeSet(strokeWidth, color), true),
			cross_strokeLength(strokeLength),
			cross_strokeWidth(strokeWidth)
	{ }


	Frame::Frame(VmaAllocator vma, float strokeWidth, const glm::vec4& color):
			BasicPolygon(vma, makeFrameShapeSet(strokeWidth, color), true),
			frame_strokeWidth(strokeWidth)
	{ }

}
