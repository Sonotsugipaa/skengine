#pragma once

#include <skengine_fwd.hpp>

#define VK_NO_PROTOTYPES // Don't need those in the header
#include <vulkan/vulkan.h>

#include <vk-util/memory.hpp>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <span>
#include <memory>



namespace SKENGINE_NAME_NS {
inline namespace geom {

	struct PolyVertex {
		alignas(glm::vec4) glm::vec3 pos;
	};

	struct PolyInstance {
		alignas(glm::vec4) glm::vec4 col;
		alignas(glm::vec4) glm::mat4 transform;
	};


	struct PipelineSetCreateInfo {
		VkRenderPass    renderPass;
		uint32_t        subpass;
		VkPipelineCache pipelineCache;
	};


	struct PipelineSet {
		VkPipelineLayout layout;
		VkPipeline polyLine;
		VkPipeline polyFill;
		//VkPipeline text;

		static PipelineSet create  (VkDevice, std::span<VkDescriptorSetLayout>, const PipelineSetCreateInfo&);
		static void        destroy (VkDevice, PipelineSet&) noexcept;
	};


	struct RectangleBounds {
		float offset[2];
		float size[2];
	};


	class Shape : private std::vector<PolyVertex> {
	public:
		using Sptr = std::shared_ptr<const Shape>;

		Shape() = default;
		Shape(std::vector<PolyVertex>, const RectangleBounds& innerBounds);

		const std::vector<PolyVertex>& vertices    () const noexcept { return *this; }
		const RectangleBounds&         innerBounds () const noexcept { return shape_innerBounds; }

	private:
		RectangleBounds shape_innerBounds;
	};


	class ShapeInstance {
	public:
		ShapeInstance() = default;
		ShapeInstance(Shape::Sptr, PolyInstance);

		const Shape& shape() const { return *shape_i_shape.get(); }
		void setShape(Shape::Sptr newShape) { shape_i_shape = std::move(newShape); }

		/* */ PolyInstance& instance()       { return shape_i_instance; }
		const PolyInstance& instance() const { return shape_i_instance; }

	private:
		Shape::Sptr  shape_i_shape;
		PolyInstance shape_i_instance;
	};


	class ShapeSet {
	public:
		ShapeSet(): shape_set_state(0b000) { }

		#ifndef NDEBUG
			~ShapeSet(); // Only needed for (c)asserting that the buffer has been destroyed
		#endif

		static ShapeSet create(VmaAllocator, std::vector<ShapeInstance>);
		static void     destroy(VmaAllocator, ShapeSet&) noexcept;

		void forceNextCommit() noexcept;
		void commitVkBuffer(VmaAllocator vma) { if(0 == (shape_set_state & 0b001)) [[unlikely]] shape_set_commit(vma); }

		ShapeInstance& modifyShapeInstance(unsigned index) noexcept;

		vkutil::Buffer& vertexBuffer       () noexcept { return shape_set_vtxBuffer; }
		vkutil::Buffer& drawIndirectBuffer () noexcept { return shape_set_drawBuffer; }
		unsigned instanceCount () const noexcept { return shape_set_instanceCount; }
		unsigned vertexCount   () const noexcept { return shape_set_vertexCount; }
		unsigned drawCmdCount  () const noexcept { return shape_set_drawCount; }

	private:
		enum class State : unsigned {
			// 100 & shape set is initialized
			// 010 & do destroy buffers
			// 001 & do not flush buffers
			eUnitialized = 0b000,
			eEmpty       = 0b101,
			eOutOfDate   = 0b110,
			eUpToDate    = 0b111
		};

		ShapeSet(State state): shape_set_state(unsigned(state)) { }

		void shape_set_commit(VmaAllocator);

		std::vector<ShapeInstance> shape_set_shapes;
		vkutil::Buffer shape_set_vtxBuffer;  // [  instances  ][  vertices          ]
		vkutil::Buffer shape_set_drawBuffer; // [  draw_cmds         ]
		unsigned shape_set_instanceCount;
		unsigned shape_set_vertexCount;
		unsigned shape_set_drawCount;
		unsigned shape_set_state;
	};

}}
