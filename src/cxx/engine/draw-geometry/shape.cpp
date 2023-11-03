#include <vulkan/vulkan.h> // This *needs* to be included before "core.hpp" because of `VK_NO_PROTOTYPES`
#include <vk-util/memory.hpp> // Same as above
#include "core.hpp"

#include <vk-util/error.hpp>

#include <cassert>
#include <unordered_set>
#include <utility>



namespace SKENGINE_NAME_NS {

namespace shape_impl {
namespace {

	constexpr PolyInstance* bufferInstancesPtr(void* base) {
		return reinterpret_cast<PolyInstance*>(base);
	}

	constexpr PolyVertex* bufferVerticesPtr(void* base, size_t instanceCount) {
		return reinterpret_cast<PolyVertex*>((reinterpret_cast<PolyInstance*>(base) + instanceCount));
	}


	struct InputData {
		using DrawCmds  = std::vector<VkDrawIndirectCommand>;
		using Instances = std::vector<PolyInstance>;
		using Vertices  = std::vector<PolyVertex>;
		DrawCmds  drawCmds;
		Instances instances;
		Vertices  vertices;
	};


	InputData sortInputData(std::span<const ShapeInstance> shapes) {
		using ShapeInstMap   = std::unordered_map<const Shape*, std::vector<const PolyInstance*>>;
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
		VkDeviceSize vertexBytes = inputData.vertices.size() * sizeof(PolyVertex);
		VkDeviceSize instanceBytes   = inputData.instances.size() * sizeof(PolyInstance);
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
			std::span<const ShapeInstance> shapes
	) {
		assert(! shapes.empty());

		auto inputData = sortInputData(shapes);
		*dstInstanceCount = inputData.instances.size();
		*dstVertexCount   = inputData.vertices.size();
		*dstDrawCmdCount  = inputData.drawCmds.size();

		{ // Create and write the buffers
			VkDeviceSize instanceBytes = (*dstInstanceCount) * sizeof(PolyInstance);
			VkDeviceSize vertexBytes   = (*dstVertexCount) * sizeof(PolyVertex);
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

	Shape::Shape(std::vector<PolyVertex> vtxs):
			std::vector<PolyVertex>(std::move(vtxs))
	{ }


	ShapeSet ShapeSet::create(VmaAllocator vma, std::vector<ShapeInstance> shapes) {
		if(shapes.empty()) {
			return ShapeSet(State::eEmpty);
		}

		ShapeSet r = State::eOutOfDate;
		r.shape_set_shapes = std::move(shapes);
		shape_impl::createBuffers(
			vma,
			&r.shape_set_vtxBuffer, &r.shape_set_drawBuffer,
			&r.shape_set_vtxPtr,
			&r.shape_set_instanceCount,
			&r.shape_set_vertexCount,
			&r.shape_set_drawCount,
			std::span<ShapeInstance>(r.shape_set_shapes.begin(), r.shape_set_shapes.end()) );
		return r;
	}


	void ShapeSet::destroy(VmaAllocator vma, ShapeSet& shapes) noexcept {
		if(shapes.shape_set_state & 0b010 /* 0b010 = needs destruction */) {
			vmaUnmapMemory(vma, shapes.shape_set_vtxBuffer);
			vkutil::Buffer::destroy(vma, shapes.shape_set_vtxBuffer);
			vkutil::Buffer::destroy(vma, shapes.shape_set_drawBuffer);
		}
		shapes.shape_set_shapes.clear();
		shapes.shape_set_state = unsigned(State::eUnitialized);
	}


	void ShapeSet::forceNextCommit() noexcept {
		switch(State(shape_set_state)) {
			#ifndef NDEBUG
				default:
			#endif
			case State::eUnitialized: std::unreachable(); break;
			case State::eOutOfDate:   [[fallthrough]];
			case State::eEmpty:       /* NOP */ break;
			case State::eUpToDate:    shape_set_state = unsigned(State::eOutOfDate);
		}
	}


	ModifiableShapeInstance ShapeSet::modifyShapeInstance(unsigned i) noexcept {
		assert(shape_set_state & 0b100 /* 0b100 = is initialized */);
		ShapeSet::forceNextCommit();
		auto ptr = shape_impl::bufferInstancesPtr(shape_set_vtxPtr) + i;
		return { ptr->color, ptr->transform };
	}


	void ShapeSet::shape_set_commitBuffers(VmaAllocator vma) {
		VkDeviceSize instanceBytes = shape_set_instanceCount * sizeof(PolyInstance);
		VkDeviceSize vertexBytes   = shape_set_vertexCount * sizeof(PolyVertex);
		VkDeviceSize drawCmdBytes  = shape_set_drawCount * sizeof(VkDrawIndirectCommand);
		VK_CHECK(vmaFlushAllocation, vma, shape_set_vtxBuffer, 0, instanceBytes + vertexBytes);
		VK_CHECK(vmaFlushAllocation, vma, shape_set_drawBuffer, 0, drawCmdBytes);
		switch(State(shape_set_state)) {
			#ifndef NDEBUG
				default:
			#endif
			case State::eUnitialized: std::unreachable(); break;
			case State::eOutOfDate:   shape_set_state = unsigned(State::eUpToDate); break;
			case State::eEmpty:       [[fallthrough]];
			case State::eUpToDate:    /* NOP */ break;
		}
	}
}

}
