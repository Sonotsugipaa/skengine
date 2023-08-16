#pragma once
/// \file
/// This entire thing is wrong.
///
/// The understanding-oriented documentation in /doc treats objects as
/// sets of meshes (as it should), but this header treats them as instances.
///
/// Here's what *should* happen:\n
/// the Renderer should hold a buffer of indirect draw commands, and one
/// of instances; it should also break given objects (from models) into
/// instances, *then* turn them into draw commands.
///
/// It does not have to worry about vertex buffers, because those belong
/// to the model (you know, memory-mapped model files and such) and the engine
/// does (will do) all the vertex buffer binding itself.\n
/// The entire point of the Renderer is for the engine to tell it
/// "I have these objects (with these locations) that have these models
/// (I already know where the vertices are), please turn them into instances
/// and draw commands."
///
/// I will worry about bones in the future, I can't see how it could
/// possibly go wrong.
///

#include "types.hpp"

#include "vk-util/memory.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <unordered_map>



namespace SKENGINE_NAME_NS {

	class Engine;


	struct DevMesh {
		vkutil::BufferDuplex indices;
		vkutil::BufferDuplex vertices;
	};


	struct UnboundDrawBatch {
		RenderObjectId object_id;
		MeshId         mesh_id;
		MaterialId     material_id;
		uint32_t       instance_count;
	};


	class MeshSupplierInterface {
	public:
		virtual DevMesh msi_requestMesh(std::string_view locator) = 0;
		virtual void    msi_releaseMesh(std::string_view locator) noexcept = 0;
		virtual void msi_releaseAllMeshes() noexcept = 0;
	};


	/// \brief A collection of objects to be drawn, which may or
	///        may not be frequently modified.
	///
	/// The Renderer abstracts the process of sorting objects by
	/// meshes and materials, and creating (indirect) draw commands.
	///
	/// It does own buffers for draw commands and object-specific data;
	/// it does NOT own mesh-specific or material-specific data.
	///
	class Renderer {
	public:
		struct MeshData {
			DevMesh     mesh;
			std::string locator;
		};

		using MeshLookup = std::unordered_map<std::string_view, MeshId>;
		using MeshMap    = std::unordered_map<MeshId, MeshData>;
		using Objects    = std::unordered_map<RenderObjectId, RenderObject>;
		using BatchMap   = std::unordered_map<MeshId, std::unordered_map<MaterialId, UnboundDrawBatch>>;

		Renderer() = default;

		static Renderer create  (VmaAllocator, MeshSupplierInterface&);
		static void     destroy (Renderer&);

		RenderObjectId createObject (RenderObject);
		void           removeObject (RenderObjectId) noexcept;
		void           clearObjects () noexcept;
		std::optional<const RenderObject*> getObject    (RenderObjectId) const noexcept;
		std::optional<RenderObject*>       modifyObject (RenderObjectId) noexcept;

		MeshId         fetchMesh (std::string_view locator);
		MeshId         setMesh   (std::string_view locator, DevMesh);
		const DevMesh* getMesh   (MeshId) const noexcept;
		void           eraseMesh (MeshId) noexcept;

		VkBuffer getDrawCommandBuffer () const noexcept { return const_cast<VkBuffer>(mDrawCmdBuffer.value); }
		void     commitBuffers        (VkCommandBuffer, VkFence signalFence);

		void reserve(size_t capacity);
		void shrinkToFit();

	private:
		VkDevice     mDevice = nullptr;
		VmaAllocator mVma;
		MeshSupplierInterface* mMsi;

		MeshLookup mMeshLocators;
		MeshMap    mMeshes;
		Objects    mObjects;
		BatchMap   mDrawBatches;
		vkutil::BufferDuplex mDrawCmdBuffer;

		bool mObjectsOod;
	};


	/// \brief A specialisation of Renderer, for drawing objects
	///        in a generic 3D space.
	///
	class WorldRenderer : public Renderer {
	public:
		using Renderer::create;
		using Renderer::destroy;

		const glm::mat4& getViewTransf() noexcept;

		const glm::vec3& getViewPos() const noexcept { return mViewPosXyz; }
		const glm::vec3& getViewDir() const noexcept { return mViewDirYpr; }
		void setViewPos (const glm::vec3& pos) noexcept { mViewPosXyz = pos; mViewTransfCacheOod = true; }
		void setViewDir (const glm::vec3& dir) noexcept { mViewDirYpr = dir; mViewTransfCacheOod = true; }

	private:
		glm::mat4 mViewTransfCache;
		glm::vec3 mViewPosXyz;
		glm::vec3 mViewDirYpr;
		bool      mViewTransfCacheOod;
	};


	/// \brief A specialisation of Renderer, for drawing objects
	///        user interface elements.
	///
	class UiRenderer : private Renderer {
	public:
		UiRenderer();

		using Renderer::getDrawCommandBuffer;
	};

}
