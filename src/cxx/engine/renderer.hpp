#pragma once

#include "types.hpp"

#include "vk-util/memory.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <optional>
#include <unordered_map>



namespace SKENGINE_NAME_NS {

	#warning "Trim the Engine-Renderer codependency"
	class Engine;


	/// \brief A collection of objects to be drawn, which may or
	///        may not be frequently modified.
	///
	/// The Renderer abstracts the process of sorting objects by
	/// meshes and materials, and creating (indirect) draw commands.
	///
	class Renderer {
	public:
		Renderer() = default;

		static Renderer create(Engine&);
		static void destroy(Renderer&);

		RenderObjectId createObject (RenderObject);
		void           removeObject (RenderObjectId) noexcept;
		void           clearObjects () noexcept;
		std::optional<const RenderObject*> getObject    (RenderObjectId) const noexcept;
		std::optional<RenderObject*>       modifyObject (RenderObjectId) noexcept;

		VkBuffer getObjectStorageBuffer () const noexcept { return const_cast<VkBuffer>(mDevObjectBuffer.value); }
		VkBuffer getDrawCommandBuffer   () const noexcept { return const_cast<VkBuffer>(mDrawCmdBuffer.value); }
		void     commitBuffers          (VkCommandBuffer, VkFence signalFence) noexcept;

		void reserve(size_t capacity);
		void shrinkToFit();

	private:
		enum class State {
			eClean,
			eObjectBufferDirty,
			eDrawCmdBufferDirty,
			eReconstructionNeeded
		};

		Engine* mEngine;
		std::unordered_map<RenderObjectId, RenderObject> mObjects;
		std::unordered_map<MeshId, std::unordered_map<MaterialId, DrawBatch>> mDrawBatches;
		std::vector<bool>    mDevObjectDirtyBitset;
		vkutil::BufferDuplex mDevObjectBuffer;
		vkutil::BufferDuplex mDrawCmdBuffer;
		State mState;

		#ifndef NDEBUG
			bool mIsInitialized = false;
		#endif
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
