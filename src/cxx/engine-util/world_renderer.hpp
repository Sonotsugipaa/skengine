#pragma once

#include <engine/types.hpp>
#include <engine/shader_cache.hpp>
#include <engine/renderer.hpp>

#include "object_storage.hpp"

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



namespace SKENGINE_NAME_NS {

	namespace dev {

		struct CullPassUbo {
			ALIGNF32(4) glm::mat4      view_transf;
			ALIGNF32(4) std::float32_t frustum_lrtb[4];
			ALIGNF32(2) std::float32_t z_range[2];
			ALIGNI32(2) uint32_t       padding0[2];
			ALIGNI32(1) bool           frustum_culling_enabled;
			ALIGNI32(1) uint32_t       padding1[3];
		};
		static_assert(sizeof(CullPassUbo) == 64+16+8+8+16);

	}



	/// \brief Data that is shared between all WorldRenderers, and allows
	///        them to share ObjectStorage instances and viceversa.
	///
	struct WorldRendererSharedState {
		VkDescriptorSetLayout objDsetLayout;
		VkDescriptorSetLayout materialDsetLayout;
		VkDescriptorSetLayout gframeUboDsetLayout;
		VkPipelineLayout cullPassPipelineLayout;
		VkPipelineLayout rdrPipelineLayout;
	};


	/// \brief A Renderer that manages light sources, their device storage and
	///        the view/camera logistics.
	///
	class WorldRenderer : public Renderer {
	public:
		struct RdrParams {
			static const RdrParams defaultParams;
			std::float32_t fovY;
			std::float32_t zNear;
			std::float32_t zFar;
			uint32_t       shadeStepCount;
			std::float32_t pointLightDistanceThreshold;
			std::float32_t shadeStepSmoothness;
			std::float32_t shadeStepExponent;
			std::float32_t ditheringSteps;
			bool cullingEnabled;
		};

		struct ProjectionInfo {
			float verticalFov = ((90.0 /* degrees */) * (std::numbers::pi_v<double> / 180.0));
			float zNear       = 0.1f;
			float zFar        = 10.0f;
		};

		struct PipelineParameters {
			bool                primitiveRestartEnable;
			VkPrimitiveTopology topology;
			VkCullModeFlagBits  cullMode;
			VkFrontFace         frontFace;
			VkPolygonMode       polygonMode;
			float               lineWidth;
			bool                rasterizerDiscardEnable;
			bool                depthTestEnable;
			bool                depthWriteEnable;
			VkCompareOp         depthCompareOp;
			bool                blendEnable;
			VkBlendFactor       srcColorBlendFactor;
			VkBlendFactor       dstColorBlendFactor;
			VkBlendOp           colorBlendOp;
			VkBlendFactor       srcAlphaBlendFactor;
			VkBlendFactor       dstAlphaBlendFactor;
			VkBlendOp           alphaBlendOp;
			ShaderRequirement   shaderRequirement;
		};
		static const PipelineParameters defaultPipelineParams;

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
			struct OsData {
				std::pair<vkutil::Buffer, size_t> objBfCopy;
				std::pair<vkutil::Buffer, size_t> objIdBfCopy;
				std::pair<vkutil::Buffer, size_t> drawCmdBfCopy;
				vkutil::BufferDuplex cullPassUbo;
				VkDescriptorSet objDset;
			};
			std::vector<OsData> osData;
			vkutil::ManagedBuffer lightStorage;
			vkutil::BufferDuplex frameUbo;
			VkDescriptorSet frameDset;
			VkExtent2D lastRenderExtent;
			uint32_t lightStorageCapacity;
			bool frameDsetOod;
		};

		// 3d pipeline dset location/binding constants
		static constexpr uint32_t RDR_GFRAME_DSET_LOC       = 0;
		static constexpr uint32_t RDR_MATERIAL_DSET_LOC     = 1;
		static constexpr uint32_t RDR_OBJ_DSET_LOC          = 2;
		static constexpr uint32_t RDR_OBJ_STG_BINDING       = 0;
		static constexpr uint32_t RDR_OBJ_ID_STG_BINDING    = 1;
		static constexpr uint32_t RDR_FRAME_UBO_BINDING     = 0;
		static constexpr uint32_t RDR_LIGHT_STORAGE_BINDING = 1;
		static constexpr uint32_t RDR_DIFFUSE_TEX_BINDING   = 0;
		static constexpr uint32_t RDR_NORMAL_TEX_BINDING    = 1;
		static constexpr uint32_t RDR_SPECULAR_TEX_BINDING  = 2;
		static constexpr uint32_t RDR_EMISSIVE_TEX_BINDING  = 3;
		static constexpr uint32_t RDR_MATERIAL_UBO_BINDING  = 4;

		// cull pass dset location/binding constants
		static constexpr uint32_t CULL_OBJ_DSET_LOC = 0;
		static constexpr uint32_t CULL_CMD_DSET_LOC = 1;
		static constexpr uint32_t CULL_OBJ_STG_BINDING = 0;
		static constexpr uint32_t CULL_OBJ_ID_STG_BINDING = 1;
		static constexpr uint32_t CULL_CMD_BINDING = 2;
		static constexpr uint32_t CULL_UBO_BINDING = 3;

		template <typename K, typename V> using Umap = std::unordered_map<K, V>;
		using RayLights   = Umap<ObjectId, RayLight>;
		using PointLights = Umap<ObjectId, PointLight>;

		WorldRenderer();
		WorldRenderer(WorldRenderer&&);
		~WorldRenderer();

		static WorldRenderer create(
			Logger,
			VmaAllocator,
			RdrParams,
			std::shared_ptr<WorldRendererSharedState>,
			std::shared_ptr<std::vector<ObjectStorage>>,
			const ProjectionInfo&,
			util::TransientArray<PipelineParameters> = { defaultPipelineParams } );

		static void destroy(WorldRenderer&);

		static void initSharedState(VkDevice, WorldRendererSharedState&);
		static void destroySharedState(VkDevice, WorldRendererSharedState&);

		std::string_view name() const noexcept override { return "world-surface"; }
		void prepareSubpasses(const SubpassSetupInfo&, VkPipelineCache, ShaderCacheInterface*) override;
		void forgetSubpasses(const SubpassSetupInfo&) override;
		void afterSwapchainCreation(ConcurrentAccess&, unsigned) override;
		void duringPrepareStage(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) override;
		void duringDrawStage(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) override;
		void afterRenderPass(ConcurrentAccess&, const DrawInfo&, VkCommandBuffer) override;

		const glm::mat4& getViewTransf() noexcept;

		const glm::vec3& getViewPosition () const noexcept { return mState.viewPosXyz; }
		const glm::vec3& getViewRotation () const noexcept { return mState.viewDirYpr; }
		const glm::vec3& getAmbientLight () const noexcept { return mState.ambientLight; }

		/// \brief This function serves a temporary yet important role, that must be restructured-out as soon as possible.
		///
		void setRtargetId_TMP_UGLY_NAME(RenderTargetId id) { mState.rtargetId = id; }

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

		void setFrustumCulling(bool enabled) noexcept { mState.params.cullingEnabled = enabled; }
		bool isFrustumCullingEnabled() noexcept { return mState.params.cullingEnabled; }

		VmaAllocator vma() const noexcept { return mState.vma; }

		const auto& lightStorage() const noexcept { return mState.lightStorage; }

	private:
		struct {
			Logger logger;
			VmaAllocator vma;
			RdrParams params;
			std::shared_ptr<std::vector<ObjectStorage>> objectStorages;
			std::shared_ptr<WorldRendererSharedState> sharedState;
			std::shared_ptr<ShaderCacheInterface> shaderCache;
			util::TransientArray<PipelineParameters> pipelineParams;
			std::vector<GframeData> gframes;
			std::vector<VkPipeline> rdrPipelines;
			VkPipeline cullPassPipeline;
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
			RenderTargetId rtargetId;
			bool projTransfOod        : 1;
			bool viewTransfCacheOod   : 1;
			bool lightStorageOod      : 1;
			bool lightStorageDsetsOod : 1;
			bool initialized          : 1;
		} mState;
	};


	constexpr WorldRenderer::PipelineParameters WorldRenderer::defaultPipelineParams = {
		.primitiveRestartEnable  = true,
		.topology                = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
		.cullMode                = VK_CULL_MODE_BACK_BIT,
		.frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.polygonMode             = VK_POLYGON_MODE_FILL,
		.lineWidth               = 1.0f,
		.rasterizerDiscardEnable = false,
		.depthTestEnable         = true,
		.depthWriteEnable        = true,
		.depthCompareOp          = VK_COMPARE_OP_LESS_OR_EQUAL,
		.blendEnable             = false,
		.srcColorBlendFactor     = { },
		.dstColorBlendFactor     = { },
		.colorBlendOp            = { },
		.srcAlphaBlendFactor     = { },
		.dstAlphaBlendFactor     = { },
		.alphaBlendOp            = { },
		.shaderRequirement       = ShaderRequirement { "default", PipelineLayoutId::e3d }
	};

}
