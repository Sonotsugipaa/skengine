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
		alignas(4 * sizeof(float)) glm::vec3 position;
	};

	struct PolyInstance {
		alignas(4 * sizeof(float)) glm::vec4 color;
		alignas(4 * sizeof(float)) glm::mat4 transform;
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
		Shape(std::vector<PolyVertex>);

		const std::vector<PolyVertex>& vertices() const noexcept { return *this; }
	};


	struct ShapeReference {
		Shape::Sptr shape;
		glm::vec4   color;
		glm::mat4   transform;

		ShapeReference() = default;

		ShapeReference(Shape::Sptr shape, glm::vec4 color, glm::mat4 transform):
			shape(std::move(shape)),
			color(color),
			transform(transform)
		{ }
	};


	class ShapeSet : public std::vector<ShapeReference> {
	public:
		using vector::vector;
	};


	class DrawableShapeInstance {
	public:
		DrawableShapeInstance() = default;
		DrawableShapeInstance(Shape::Sptr s, PolyInstance i): dr_shape_i_shape(std::move(s)), dr_shape_i_instance(std::move(i)) { }

		const Shape& shape() const { return *dr_shape_i_shape.get(); }
		void setShape(Shape::Sptr newShape) { dr_shape_i_shape = std::move(newShape); }

		/* */ PolyInstance& instance()       { return dr_shape_i_instance; }
		const PolyInstance& instance() const { return dr_shape_i_instance; }

	private:
		Shape::Sptr  dr_shape_i_shape;
		PolyInstance dr_shape_i_instance;
	};


	struct ModifiableShapeInstance {
		glm::vec4& color;
		glm::mat4& transform;
	};


	class DrawableShapeSet {
	public:
		DrawableShapeSet(): dr_shape_set_state(0b000) { }

		static DrawableShapeSet create(VmaAllocator, std::vector<DrawableShapeInstance>);
		static DrawableShapeSet create(VmaAllocator, ShapeSet);
		static void     destroy(VmaAllocator, DrawableShapeSet&) noexcept;

		void forceNextCommit() noexcept;
		void commitVkBuffers(VmaAllocator vma) { if(0 == (dr_shape_set_state & 0b001)) [[unlikely]] dr_shape_set_commitBuffers(vma); }

		ModifiableShapeInstance modifyShapeInstance(unsigned index) noexcept;

		operator bool()  { return State(dr_shape_set_state) != State::eUnitialized; }
		bool operator!() { return State(dr_shape_set_state) == State::eUnitialized; }

		vkutil::Buffer& vertexBuffer       () noexcept { return dr_shape_set_vtxBuffer; }
		vkutil::Buffer& drawIndirectBuffer () noexcept { return dr_shape_set_drawBuffer; }
		unsigned instanceCount () const noexcept { return dr_shape_set_instanceCount; }
		unsigned vertexCount   () const noexcept { return dr_shape_set_vertexCount; }
		unsigned drawCmdCount  () const noexcept { return dr_shape_set_drawCount; }

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

		DrawableShapeSet(State state): dr_shape_set_state(unsigned(state)) { }

		void dr_shape_set_commitBuffers(VmaAllocator);

		std::vector<DrawableShapeInstance> dr_shape_set_shapes;
		vkutil::Buffer dr_shape_set_vtxBuffer;  // [  instances  ][  vertices          ]
		vkutil::Buffer dr_shape_set_drawBuffer; // [  draw_cmds         ]
		void*    dr_shape_set_vtxPtr;
		unsigned dr_shape_set_instanceCount;
		unsigned dr_shape_set_vertexCount;
		unsigned dr_shape_set_drawCount;
		unsigned dr_shape_set_state;
	};

}}
