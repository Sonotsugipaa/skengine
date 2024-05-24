#pragma once

#include "types.hpp"
#include "object_storage.hpp"
#include "renderer.hpp"

#include <vk-util/memory.hpp>

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <thread>
#include <memory>
#include <optional>
#include <condition_variable>
#include <unordered_map>

#include <spdlog/logger.h>



namespace SKENGINE_NAME_NS {

	/// \brief A Renderer that manages light sources, their device storage and
	///        the view/camera logistics.
	///
	class WorldRenderer : public Renderer {
	public:
		struct ProjectionInfo {
			float verticalFov = ((90.0 /* degrees */) * (std::numbers::pi_v<double> / 180.0));
			float zNear       = 0.1f;
			float zFar        = 10.0f;
		};

		struct LightStorage {
			vkutil::ManagedBuffer buffer;
			dev::Light*           mappedPtr;
			uint32_t bufferCapacity;
			uint32_t rayCount;
			uint32_t pointCount;
		};

		struct NewRayLight {
			glm::vec3 direction;
			glm::vec3 color;
			float     intensity;
			float     aoaThreshold;
		};

		struct NewPointLight {
			glm::vec3 position;
			glm::vec3 color;
			float     intensity;
			float     falloffExponent;
		};

		struct GframeData {
			vkutil::ManagedBuffer lightStorage;
			vkutil::BufferDuplex frameUbo;
			VkDescriptorSet frameDset;
			uint32_t lightStorageCapacity;
			VkExtent2D lastRenderExtent;
		};

		static constexpr uint32_t FRAME_UBO_BINDING     = 0;
		static constexpr uint32_t LIGHT_STORAGE_BINDING = 1;

		template <typename K, typename V> using Umap = std::unordered_map<K, V>;
		using RayLights   = Umap<ObjectId, RayLight>;
		using PointLights = Umap<ObjectId, PointLight>;

		WorldRenderer();
		WorldRenderer(WorldRenderer&&);
		~WorldRenderer();

		static WorldRenderer create(
			std::shared_ptr<spdlog::logger>,
			std::shared_ptr<ObjectStorage>,
			const ProjectionInfo& );

		static void destroy(WorldRenderer&);

		std::string_view name() const noexcept override { return "world-surface"; }
		void afterSwapchainCreation(ConcurrentAccess&, unsigned) override;
		void duringPrepareStage(ConcurrentAccess&, unsigned, VkCommandBuffer) override;
		void duringDrawStage(ConcurrentAccess&, unsigned, VkCommandBuffer) override;

		const glm::mat4& getViewTransf() noexcept;

		const glm::vec3& getViewPosition () const noexcept { return mState.viewPosXyz; }
		const glm::vec3& getViewRotation () const noexcept { return mState.viewDirYpr; }
		const glm::vec3& getAmbientLight () const noexcept { return mState.ambientLight; }

		/// \brief Sets the 3D projection parameters.
		///
		void setProjection(ProjectionInfo pi) noexcept { mState.projInfo = std::move(pi); mState.projTransfOod = true; }

		/// \brief Sets the view position of the camera.
		/// \param lazy Whether the view is to be considered out of date afterwards.
		///
		void setViewPosition(const glm::vec3& xyz, bool lazy = false) noexcept;

		/// \brief Sets the yaw, pitch and roll of the camera.
		/// \param lazy Whether the view is to be considered out of date afterwards.
		///
		void setViewRotation(const glm::vec3& ypr, bool lazy = false) noexcept;

		/// \brief Sets the ambient lighting color, which acts as a light source that
		///        acts on every surface from every direction.
		/// \param lazy Whether the view is to be considered out of date afterwards.
		///
		void setAmbientLight(const glm::vec3& rgb, bool lazy = false) noexcept;

		/// \brief Rotates the view so that `xyz - pos` in world space equals (0, 0, -1) in view space.
		/// \param lazy Whether the view is to be considered out of date afterwards.
		///
		void setViewDirection (const glm::vec3& xyz, bool lazy = false) noexcept;
		void rotate           (const glm::vec3& ypr, bool lazy = false) noexcept; ///< Similar to `WorldRenderer::setViewRotation`, but it's relative to the current position and rotation.
		void rotateTowards    (const glm::vec3& xyz, bool lazy = false) noexcept; ///< Similar to `WorldRenderer::setViewDirection`, but it's relative to the current position.

		[[nodiscard]] ObjectId createRayLight   (const NewRayLight&);
		[[nodiscard]] ObjectId createPointLight (const NewPointLight&);
		void                   removeLight      (ObjectId);
		const RayLight&   getRayLight      (ObjectId) const;
		const PointLight& getPointLight    (ObjectId) const;
		RayLight&         modifyRayLight   (ObjectId);
		PointLight&       modifyPointLight (ObjectId);

		VmaAllocator vma() const noexcept { return mState.objectStorage->vma(); }
		VkDevice vkDevice() const noexcept { VmaAllocatorInfo ai; vmaGetAllocatorInfo(vma(), &ai); return ai.device; }

		const auto& lightStorage() const noexcept { return mState.lightStorage; }

	private:
		struct {
			std::shared_ptr<spdlog::logger> logger;
			std::shared_ptr<ObjectStorage> objectStorage;
			std::vector<GframeData> gframes;
			RayLights    rayLights;
			PointLights  pointLights;
			LightStorage lightStorage;
			ProjectionInfo projInfo;
			glm::mat4 projTransfCache;
			glm::mat4 viewTransfCache;
			glm::vec3 viewPosXyz;
			glm::vec3 viewDirYpr;
			glm::vec3 ambientLight;
			VkDescriptorPool gframeDpool;
			bool projTransfOod       : 1;
			bool viewTransfCacheOod  : 1;
			bool lightStorageOod     : 1;
			bool lightStorageDsetOod : 1;
			bool initialized         : 1;
		} mState;
	};

}
