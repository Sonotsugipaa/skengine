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
#include <unordered_set>
#include <unordered_map>



namespace SKENGINE_NAME_NS {

	class Engine;
	class WorldRenderer;
	class UiRenderer;


	struct DevMesh {
		vkutil::BufferDuplex indices;
		vkutil::BufferDuplex vertices;
	};


	struct UnboundDrawBatch {
		std::unordered_set<RenderObjectId> object_ids;
		MeshId     mesh_id;
		MaterialId material_id;
		uint32_t   vertex_offset;
		uint32_t   index_count;
		uint32_t   first_index;
	};


	struct Material {
		VkDescriptorSet texture_dset;
		vkutil::ManagedImage texture_diffuse;
		vkutil::ManagedImage texture_normal;
		vkutil::ManagedImage texture_specular;
		vkutil::ManagedImage texture_emissive;
		float emissive_mul;
		float diffuse_mul;
		float specular_exp;
	};


	class MeshSupplierInterface {
	public:
		virtual DevMesh msi_requestMesh(std::string_view locator) = 0;
		virtual void    msi_releaseMesh(std::string_view locator) noexcept = 0;
		virtual void msi_releaseAllMeshes() noexcept = 0;
	};


	class MaterialSupplierInterface {
	public:
		virtual Material msi_requestMaterial(std::string_view locator) = 0;
		virtual void     msi_releaseMaterial(std::string_view locator) noexcept = 0;
		virtual void msi_releaseAllMaterials() noexcept = 0;
	};


	/// \brief A collection of objects to be drawn, which may or
	///        may not be frequently modified.
	///
	/// The Renderer abstracts the process of sorting objects by
	/// meshes and materials, and creating (indirect) draw commands.
	///
	/// It does own buffers for draw commands and object-specific data;
	/// it does NOT own mesh-specific or material-specific data,
	/// like vertices or textures.
	///
	class Renderer {
	public:
		friend WorldRenderer;
		friend UiRenderer;

		struct MeshData {
			DevMesh     mesh;
			std::string locator;
		};

		using MeshLookup      = std::unordered_map<std::string_view, MeshId>;
		using MeshMap         = std::unordered_map<MeshId, MeshData>;
		using Objects         = std::unordered_map<RenderObjectId, RenderObject>;
		using UnboundBatchMap = std::unordered_map<MeshId, std::unordered_map<MaterialId, UnboundDrawBatch>>;
		using BatchList       = std::vector<DrawBatch>;

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

		VkBuffer getInstanceBuffer () const noexcept { return const_cast<VkBuffer>(mObjectBuffer.value); }
		void     commitObjects     (VkCommandBuffer);

		void reserve(size_t capacity);
		void shrinkToFit();

	private:
		VkDevice     mDevice = nullptr;
		VmaAllocator mVma;
		MeshSupplierInterface* mMsi;

		MeshLookup      mMeshLocators;
		MeshMap         mMeshes;
		Objects         mObjects;
		UnboundBatchMap mUnboundDrawBatches;
		BatchList       mDrawBatchList;
		vkutil::BufferDuplex mObjectBuffer;
		vkutil::BufferDuplex mBatchBuffer;

		bool mObjectsOod;
	};


	/// \brief A specialisation of Renderer, for drawing objects
	///        in a generic 3D space.
	///
	class WorldRenderer : public Renderer {
	public:
		WorldRenderer() = default;

		static WorldRenderer create  (VmaAllocator, MeshSupplierInterface&);
		static void          destroy (WorldRenderer&);

		const glm::mat4& getViewTransf() noexcept;

		const glm::vec3& getViewPosition () const noexcept { return mViewPosXyz; }
		const glm::vec3& getViewRotation () const noexcept { return mViewDirYpr; }
		void setViewPosition  (const glm::vec3& xyz) noexcept;
		void setViewRotation  (const glm::vec3& ypr) noexcept; ///< Sets the yaw, pitch and roll of the world-view transformation.
		void setViewDirection (const glm::vec3& xyz) noexcept; ///< Rotates the view so that `xyz - pos` in world space equals (0, 0, -1) in view space.
		void rotate           (const glm::vec3& ypr) noexcept; ///< Similar to `WorldRenderer::setViewRotation`, but it's relative to the current position and rotation.
		void rotateTowards    (const glm::vec3& xyz) noexcept; ///< Similar to `WorldRenderer::setViewDirection`, but it's relative to the current position.

	private:
		glm::mat4 mViewTransfCache;
		glm::vec3 mViewPosXyz;
		glm::vec3 mViewDirYpr;
		bool      mViewTransfCacheOod;

		WorldRenderer(Renderer&&);
	};


	/// \brief A specialisation of Renderer, for drawing objects
	///        user interface elements.
	///
	class UiRenderer : private Renderer {
	public:
		UiRenderer();

		using Renderer::getInstanceBuffer;
	};

}
