#include <vulkan/vulkan.h> // This *needs* to be included before "core.hpp" because of `VK_NO_PROTOTYPES`
#include <vk-util/memory.hpp> // Same as above
#include "core.hpp"

#include <vk-util/error.hpp>

#include <cassert>
#include <unordered_set>



namespace SKENGINE_NAME_NS {

namespace shape_impl {
namespace {

	constexpr PolyInstance* bufferInstancesPtr(void* base) {
		return reinterpret_cast<PolyInstance*>(base);
	}

	constexpr PolyVertex* bufferVerticesPtr(void* base, size_t instanceCount) {
		return reinterpret_cast<PolyVertex*>((reinterpret_cast<PolyInstance*>(base) + instanceCount));
	}


	void createVertexBuffer(
			VmaAllocator vma,
			vkutil::Buffer* dstVtxBuffer,
			vkutil::Buffer* dstDrawCmdBuffer,
			unsigned* dstInstanceCount,
			unsigned* dstVertexCount,
			unsigned* dstDrawCmdCount,
			std::span<const ShapeInstance> shapes
	) {
		using ShapeInstMap   = std::unordered_map<const Shape*, std::vector<const PolyInstance*>>;
		using ShapeOffsetMap = std::unordered_map<const Shape*, unsigned>;
		using DrawCmds  = std::vector<VkDrawIndirectCommand>;
		using Instances = std::vector<PolyInstance>;
		using Vertices  = std::vector<PolyVertex>;
		DrawCmds  drawCmds;
		Instances instances;
		Vertices  vertices;

		assert(! shapes.empty());

		{ // Sort the input data
			auto uniqueShapes = ShapeInstMap(shapes.size());

			for(auto& shapeInst : shapes) {
				uniqueShapes[&shapeInst.shape()].push_back(&shapeInst.instance());
			}

			auto shapeIndices = ShapeOffsetMap(uniqueShapes.size());
			auto insertShapeGetOffset = [&](const Shape* shape) {
				auto ins = shapeIndices.insert(ShapeOffsetMap::value_type(shape, vertices.size()));
				if(ins.second) {
					// The shape's vertices need to be inserted
					vertices.insert(vertices.end(), shape->vertices().begin(), shape->vertices().end());
				} else {
					// The vertices already exist; NOP
				}
				return ins.first->second;
			};

			for(auto& shape : uniqueShapes) {
				unsigned vtxOffset  = insertShapeGetOffset(shape.first);
				unsigned instOffset = instances.size();
				instances.insert(instances.end(), shape.second.begin(), shape.second.end());
				VkDrawIndirectCommand cmd = { };
				cmd.firstInstance = instOffset;
				cmd.instanceCount = shape.second.size();
				cmd.firstVertex   = vtxOffset;
				cmd.vertexCount   = shape.first->vertices().size();
				drawCmds.push_back(cmd);
			}

			*dstInstanceCount = instances.size();
			*dstVertexCount   = vertices.size();
			*dstDrawCmdCount  = drawCmds.size();
		}

		{ // Create and write the buffers
			VkDeviceSize instanceBytes = instances.size() * sizeof(PolyInstance);
			VkDeviceSize vertexBytes   = vertices.size() * sizeof(PolyVertex);
			VkDeviceSize drawCmdBytes  = drawCmds.size() * sizeof(VkDrawIndirectCommand);
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
			void* ptr;
			VK_CHECK(vmaMapMemory, vma, *dstVtxBuffer, &ptr);
			memcpy(bufferInstancesPtr(ptr), instances.data(), instanceBytes);
			memcpy(bufferVerticesPtr(ptr, instances.size()), vertices.data(), vertexBytes);
			vmaUnmapMemory(vma, *dstVtxBuffer);
			VK_CHECK(vmaMapMemory, vma, *dstDrawCmdBuffer, &ptr);
			memcpy(ptr, drawCmds.data(),  drawCmdBytes);
			vmaUnmapMemory(vma, *dstDrawCmdBuffer);
		}
	}

}}



inline namespace geom {

	Shape::Shape(std::vector<PolyVertex> vtxs, const RectangleBounds& ib):
			std::vector<PolyVertex>(std::move(vtxs)),
			shape_innerBounds(ib)
	{ }


	ShapeSet ShapeSet::create(VmaAllocator vma, std::vector<ShapeInstance> shapes) {
		if(shapes.empty()) {
			return ShapeSet(State::eEmpty);
		}

		ShapeSet r = State::eOutOfDate;
		r.shape_set_shapes = std::move(shapes);
		shape_impl::createVertexBuffer(
			vma,
			&r.shape_set_vtxBuffer, &r.shape_set_drawBuffer,
			&r.shape_set_instanceCount,
			&r.shape_set_vertexCount,
			&r.shape_set_drawCount,
			std::span<ShapeInstance>(r.shape_set_shapes.begin(), r.shape_set_shapes.end()) );
		return r;
	}


	void ShapeSet::destroy(VmaAllocator vma, ShapeSet& shapes) {
		if(shapes.shape_set_state & 0b010 /* 0b010 = needs destruction */) {
			vkutil::Buffer::destroy(vma, shapes.shape_set_vtxBuffer);
			vkutil::Buffer::destroy(vma, shapes.shape_set_drawBuffer);
		}
		shapes.shape_set_shapes.clear();
		shapes.shape_set_state = unsigned(State::eUnitialized);
	}


	#ifndef NDEBUG
		ShapeSet::~ShapeSet() {
			assert(shape_set_state == unsigned(State::eUnitialized));
		}
	#endif


	void ShapeSet::commitVkBuffer(VmaAllocator vma) {
		VkDeviceSize instanceBytes = shape_set_instanceCount * sizeof(PolyInstance);
		VkDeviceSize vertexBytes   = shape_set_vertexCount * sizeof(PolyVertex);
		VkDeviceSize drawCmdBytes  = shape_set_drawCount * sizeof(VkDrawIndirectCommand);
		VK_CHECK(vmaFlushAllocation, vma, shape_set_vtxBuffer, 0, instanceBytes + vertexBytes);
		VK_CHECK(vmaFlushAllocation, vma, shape_set_drawBuffer, 0, drawCmdBytes);
	}
}

}
