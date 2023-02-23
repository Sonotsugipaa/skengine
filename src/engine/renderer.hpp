#pragma once

#include "types.hpp"

#include "vkutil/buffer_alloc.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <unordered_map>



namespace SKENGINE_NAME_NS {

	/// \brief A collection of objects to be drawn, which may or
	///        may not be frequently modified.
	///
	/// The Renderer abstracts the process of sorting objects by
	/// meshes and materials, and creating (indirect) draw commands.
	///
	class Renderer {
	public:
		Renderer();
		~Renderer();

		ObjectId createObject ();
		void     removeObject (ObjectId) noexcept;
		void     clearObjects () noexcept;
		std::optional<const RenderObject*> getObject    (ObjectId) const noexcept;
		std::optional<RenderObject*>       modifyObject (ObjectId) noexcept;

		VkBuffer getObjectStorageBuffer () const noexcept { return const_cast<VkBuffer>(mDevObjectBuffer.value); }
		VkBuffer getDrawCommandBuffer   () const noexcept { return const_cast<VkBuffer>(mDrawCmdBuffer.value); }
		void     updateBuffers          () noexcept;

	private:
		std::unordered_map<ObjectId, RenderObject> mObjects;
		std::unordered_map<MeshId, std::unordered_map<MaterialId, DrawBatch>> mDrawBatches;
		std::vector<bool>      mDevObjectDirtyBitset;
		vkutil::VmaBuffer      mDevObjectBuffer;
		vkutil::VmaBuffer      mDrawCmdBuffer;
		dev::RenderObject*     mDevObjectPtr;
		VkDrawIndirectCommand* mDrawCmdPtr;
		bool                   mDevObjectBufferDirty;
		bool                   mDrawCmdBufferDirty;
	};


	/// \brief A specialisation of Renderer, for drawing objects
	///        in a generic 3D space.
	///
	class WorldRenderer : public Renderer {
	public:
		WorldRenderer();

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

		using Renderer::getObjectStorageBuffer;
		using Renderer::getDrawCommandBuffer;
	};

}
