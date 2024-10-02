#include "core.hpp"

#include <vk-util/memory.hpp>
#include <vk-util/error.hpp>

#include <cassert>
#include <unordered_set>
#include <utility>



namespace SKENGINE_NAME_NS {

namespace shape_impl {
namespace {

	constexpr geom::Instance* bufferInstancesPtr(void* base) {
		return reinterpret_cast<geom::Instance*>(base);
	}

	constexpr geom::Vertex* bufferVerticesPtr(void* base, size_t instanceCount) {
		return reinterpret_cast<geom::Vertex*>((reinterpret_cast<geom::Instance*>(base) + instanceCount));
	}


	struct InputData {
		using DrawCmds  = std::vector<VkDrawIndirectCommand>;
		using Instances = std::vector<geom::Instance>;
		using Vertices  = std::vector<geom::Vertex>;
		DrawCmds  drawCmds;
		Instances instances;
		Vertices  vertices;
	};


	InputData sortInputData(std::span<const DrawableShapeInstance> shapes) {
		using ShapeInstMap   = std::unordered_map<const Shape*, std::vector<const geom::Instance*>>;
		using ShapeOffsetMap = std::unordered_map<const Shape*, unsigned>;

		InputData r;
		auto uniqueShapes = ShapeInstMap(shapes.size());

		for(auto& shapeInst : shapes) {
			uniqueShapes[&shapeInst.shape()].push_back(&shapeInst.instance());
		}

		auto shapeIndices = ShapeOffsetMap(uniqueShapes.size());
		auto insertShapeGetOffset = [&](const Shape* shape) {
			auto ins = shapeIndices.insert(ShapeOffsetMap::value_type(shape, r.vertices.size()));
			if(ins.second) {
				// The shape's vertices need to be inserted
				r.vertices.insert(r.vertices.end(), shape->vertices().begin(), shape->vertices().end());
			} else {
				// The vertices already exist; NOP
			}
			return ins.first->second;
		};

		for(auto& shape : uniqueShapes) {
			unsigned vtxOffset  = insertShapeGetOffset(shape.first);
			unsigned instOffset = r.instances.size();
			for(auto& ins : shape.second) r.instances.push_back(*ins);
			VkDrawIndirectCommand cmd = { };
			cmd.firstInstance = instOffset;
			cmd.instanceCount = shape.second.size();
			cmd.firstVertex   = vtxOffset;
			cmd.vertexCount   = shape.first->vertices().size();
			r.drawCmds.push_back(cmd);
		}

		return r;
	}


	void updateBuffers(
			VmaAllocator vma,
			vkutil::Buffer   drawCmdBuffer,
			void*            vtxPtr,
			const InputData& inputData
	) {
		VkDeviceSize vertexBytes = inputData.vertices.size() * sizeof(geom::Vertex);
		VkDeviceSize instanceBytes   = inputData.instances.size() * sizeof(geom::Instance);
		VkDeviceSize drawCmdBytes  = inputData.drawCmds.size() * sizeof(VkDrawIndirectCommand);
		memcpy(bufferInstancesPtr(vtxPtr), inputData.instances.data(), instanceBytes);
		memcpy(bufferVerticesPtr(vtxPtr, inputData.instances.size()), inputData.vertices.data(), vertexBytes);
		void* drawPtr;
		VK_CHECK(vmaMapMemory, vma, drawCmdBuffer, &drawPtr);
		memcpy(drawPtr, inputData.drawCmds.data(), drawCmdBytes);
		vmaUnmapMemory(vma, drawCmdBuffer);
	}


	void createBuffers(
			VmaAllocator vma,
			vkutil::Buffer* dstVtxBuffer,
			vkutil::Buffer* dstDrawCmdBuffer,
			void**    dstVtxPtr,
			unsigned* dstInstanceCount,
			unsigned* dstVertexCount,
			unsigned* dstDrawCmdCount,
			std::span<const DrawableShapeInstance> shapes
	) {
		assert(! shapes.empty());

		auto inputData = sortInputData(shapes);
		*dstInstanceCount = inputData.instances.size();
		*dstVertexCount   = inputData.vertices.size();
		*dstDrawCmdCount  = inputData.drawCmds.size();

		{ // Create and write the buffers
			VkDeviceSize instanceBytes = (*dstInstanceCount) * sizeof(geom::Instance);
			VkDeviceSize vertexBytes   = (*dstVertexCount) * sizeof(geom::Vertex);
			VkDeviceSize drawCmdBytes  = (*dstDrawCmdCount) * sizeof(VkDrawIndirectCommand);
			VkDeviceSize vtxBufferSize  = instanceBytes + vertexBytes;

			vkutil::BufferCreateInfo bcInfo = { };
			bcInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			bcInfo.size  = vtxBufferSize;
			vkutil::AllocationCreateInfo acInfo = { };
			acInfo.requiredMemFlags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			acInfo.preferredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			acInfo.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			acInfo.vmaUsage = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
			*dstVtxBuffer = vkutil::ManagedBuffer::create(vma, bcInfo, acInfo);

			bcInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			bcInfo.size = drawCmdBytes;
			acInfo.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			*dstDrawCmdBuffer = vkutil::ManagedBuffer::create(vma, bcInfo, acInfo);

			// Copy data
			VK_CHECK(vmaMapMemory, vma, *dstVtxBuffer, dstVtxPtr);
			updateBuffers(vma, *dstDrawCmdBuffer, *dstVtxPtr, inputData);
		}
	}

}}



inline namespace geom {

	Shape::Shape(const std::vector<PolyVertex>& vtxs):
			std::vector<Vertex>(),
			shape_type(PipelineType::ePoly)
	{ reserve(vtxs.size()); for(auto& v : vtxs) push_back(Vertex { .poly = v }); }

	Shape::Shape(const std::vector<TextVertex>& vtxs):
			std::vector<Vertex>(),
			shape_type(PipelineType::eText)
	{ reserve(vtxs.size()); for(auto& v : vtxs) push_back(Vertex { .text = v }); }

	Shape::Shape(std::vector<Vertex> vtxs, PipelineType type):
			std::vector<Vertex>(std::move(vtxs)),
			shape_type(type)
	{ }


	DrawableShapeSet DrawableShapeSet::create(VmaAllocator vma, std::vector<DrawableShapeInstance> shapes) {
		if(shapes.empty()) {
			return DrawableShapeSet(State::eEmpty);
		}

		DrawableShapeSet r = State::eOutOfDate;
		r.dr_shape_set_shapes = std::move(shapes);
		shape_impl::createBuffers(
			vma,
			&r.dr_shape_set_vtxBuffer, &r.dr_shape_set_drawBuffer,
			&r.dr_shape_set_vtxPtr,
			&r.dr_shape_set_instanceCount,
			&r.dr_shape_set_vertexCount,
			&r.dr_shape_set_drawCount,
			std::span<DrawableShapeInstance>(r.dr_shape_set_shapes.begin(), r.dr_shape_set_shapes.end()) );
		return r;
	}


	DrawableShapeSet DrawableShapeSet::create(VmaAllocator vma, ShapeSet shapes) {
		auto shapeInstances = std::vector<DrawableShapeInstance>();
		shapeInstances.reserve(shapes.size());
		for(auto& shape : shapes) {
			shapeInstances.push_back(DrawableShapeInstance(
				std::move(shape.shape),
				geom::Instance {
					.color     = shape.color,
					.transform = shape.transform } ));
		}
		return create(vma, std::move(shapeInstances));
	}


	void DrawableShapeSet::destroy(VmaAllocator vma, DrawableShapeSet& shapes) noexcept {
		if(shapes.dr_shape_set_state & 0b010 /* 0b010 = needs destruction */) {
			vmaUnmapMemory(vma, shapes.dr_shape_set_vtxBuffer);
			vkutil::Buffer::destroy(vma, shapes.dr_shape_set_vtxBuffer);
			vkutil::Buffer::destroy(vma, shapes.dr_shape_set_drawBuffer);
		}
		shapes.dr_shape_set_shapes.clear();
		shapes.dr_shape_set_state = unsigned(State::eUnitialized);
	}


	void DrawableShapeSet::forceNextCommit() noexcept {
		switch(State(dr_shape_set_state)) {
			#ifndef NDEBUG
				default:
			#endif
			case State::eUnitialized: std::unreachable(); break;
			case State::eOutOfDate:   [[fallthrough]];
			case State::eEmpty:       /* NOP */ break;
			case State::eUpToDate:    dr_shape_set_state = unsigned(State::eOutOfDate);
		}
	}


	ModifiableShapeInstance DrawableShapeSet::modifyShapeInstance(unsigned i) noexcept {
		assert(dr_shape_set_state & 0b100 /* 0b100 = is initialized */);
		DrawableShapeSet::forceNextCommit();
		auto ptr = shape_impl::bufferInstancesPtr(dr_shape_set_vtxPtr) + i;
		return { ptr->color, ptr->transform };
	}


	void DrawableShapeSet::dr_shape_set_commitBuffers(VmaAllocator vma) {
		VkDeviceSize instanceBytes = dr_shape_set_instanceCount * sizeof(geom::Instance);
		VkDeviceSize vertexBytes   = dr_shape_set_vertexCount * sizeof(PolyVertex);
		VkDeviceSize drawCmdBytes  = dr_shape_set_drawCount * sizeof(VkDrawIndirectCommand);
		VK_CHECK(vmaFlushAllocation, vma, dr_shape_set_vtxBuffer, 0, instanceBytes + vertexBytes);
		VK_CHECK(vmaFlushAllocation, vma, dr_shape_set_drawBuffer, 0, drawCmdBytes);
		switch(State(dr_shape_set_state)) {
			#ifndef NDEBUG
				default:
			#endif
			case State::eUnitialized: std::unreachable(); break;
			case State::eOutOfDate:   dr_shape_set_state = unsigned(State::eUpToDate); break;
			case State::eEmpty:       [[fallthrough]];
			case State::eUpToDate:    /* NOP */ break;
		}
	}
}

}
