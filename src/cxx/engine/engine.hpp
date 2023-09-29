#pragma once

#include "input.hpp"
#include "renderer.hpp"
#include "shader_cache.hpp"

#include <vk-util/init.hpp>
#include <vk-util/memory.hpp>

#include <tick-regulator.hpp>

#include <stdexcept>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <string>
#include <unordered_set>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

#include "mutex_ptr.inl.hpp"

#include <spdlog/logger.h>

#include <SDL2/SDL_vulkan.h>



/// \file
///
/// # Ambiguous or elusive terminology and shorthands
///
///
/// ## lframe | logic frame
/// An lframe is one iteration of an engine's main thread, which
/// calls the members functions of `LoopInterface` that are not
/// prefixed with "async".
///
///
/// ## gframe | graphics frame
/// A gframe is a sequence of draw calls, during which data is prepared
/// and submitted to the engine's underlying low-level API.
/// Every gframe is started by an lframe, but the two are
/// not entirely synchronized; a gframe calls
/// `LoopInterface::loop_preRender` and `LoopInterface::loop_postRender`.
///
/// In Vulkan terms, a gframe prepares, starts, ends and submits a render pass.
///
///
/// ## Simple shorthands
/// These shorthands affect how camelCase and PascalCase names
/// are chosen: for instance, "get queue family indices" would become
/// "getQfamIndices" rather than "getQFamIndices".
///
/// - qfam  | queue family
/// - dset  | descriptor set
/// - dpool | descriptor pool
/// - cpool | command pool
/// - rpass | render pass
///



namespace SKENGINE_NAME_NS {

	class Engine;


	class EngineRuntimeError : public std::runtime_error {
	public:
		template <typename... Args>
		EngineRuntimeError(Args... args): std::runtime_error::runtime_error(args...) { }
	};

	class ShaderModuleReadError : public EngineRuntimeError {
	public:
		template <typename... Args>
		ShaderModuleReadError(Args... args): EngineRuntimeError::EngineRuntimeError(args...) { }
	};


	#warning "TODO: work on the preferences struct"
	struct EnginePreferences {
		static const EnginePreferences default_prefs;
		std::string phys_device_uuid;
		VkExtent2D  init_present_extent;
		VkExtent2D  max_render_extent;
		std::string_view asset_filename_prefix;
		std::shared_ptr<spdlog::logger> logger;
		spdlog::level::level_enum       log_level;
		VkPresentModeKHR   present_mode;
		VkSampleCountFlags sample_count;
		uint32_t       max_concurrent_frames;
		std::float32_t fov_y;
		std::float32_t z_near;
		std::float32_t z_far;
		uint32_t       shade_step_count;
		std::float32_t shade_step_smoothness;
		std::float32_t shade_step_exponent;
		std::float32_t upscale_factor;
		std::float32_t target_framerate;
		std::float32_t target_tickrate;
		bool           fullscreen;
	};


	struct ViewportPosition {
		float x;
		float y;
	};


	struct RpassConfig {
		static const RpassConfig default_cfg;

		std::string world_vertex_shader_file;
		std::string world_fragment_shader_file;
		std::string ui_vertex_shader_file;
		std::string ui_fragment_shader_file;
	};

	inline const RpassConfig RpassConfig::default_cfg = RpassConfig {
		.world_vertex_shader_file   = "world-vtx.spv",
		.world_fragment_shader_file = "world-frg.spv",
		.ui_vertex_shader_file      = "ui-vtx.spv",
		.ui_fragment_shader_file    = "ui-frg.spv"
	};


	struct DeviceInitInfo {
		std::string window_title;
		std::string application_name;
		uint32_t    app_version;
	};


	struct SwapchainImageData {
		VkImage image;
	};


	struct GframeData {
		VkDescriptorSet       frame_dset;
		vkutil::BufferDuplex  frame_ubo;
		vkutil::ManagedBuffer light_storage;
		vkutil::ManagedImage atch_color;
		vkutil::ManagedImage atch_depthstencil;
		VkImageView atch_color_view;
		VkImageView atch_depthstencil_view;
		VkCommandPool cmd_pool;
		VkCommandBuffer cmd_prepare;
		VkCommandBuffer cmd_draw;
		VkFramebuffer framebuffer;
		VkSemaphore sem_swapchain_image;
		VkSemaphore sem_prepare;
		VkSemaphore sem_draw;
		VkFence     fence_prepare;
		VkFence     fence_draw;
		uint_fast32_t light_storage_last_update_counter;
		uint32_t      light_storage_capacity;
	};


	class ConcurrentAccess {
	public:
		friend Engine;

		ConcurrentAccess() = default;
		ConcurrentAccess(Engine* e, bool is_thread_local): ca_engine(e), ca_threadLocal(is_thread_local) { }

		WorldRenderer& getWorldRenderer() noexcept;

		void setPresentExtent(VkExtent2D);

	private:
		Engine* ca_engine;
		bool    ca_threadLocal;
	};


	/// \brief Abstraction for the engine's loop.
	///
	/// The "async"-prefixed functions are called asynchronously
	/// by the engine, and their effects must be externally synchronized.
	///
	class LoopInterface {
	public:
		enum class LoopState {
			eShouldContinue, ///< The engine should continue running.
			eShouldStop,     ///< The engine should stop running.
		};

		virtual void      loop_processEvents (tickreg::delta_t delta_avg, tickreg::delta_t delta) = 0;
		virtual LoopState loop_pollState     () const noexcept = 0;
		virtual void      loop_async_preRender  (ConcurrentAccess, tickreg::delta_t delta_avg, tickreg::delta_t delta_previous) = 0;
		virtual void      loop_async_postRender (ConcurrentAccess, tickreg::delta_t delta_avg, tickreg::delta_t delta_current) = 0;
	};


	class AssetSupplier : public ModelSupplierInterface, public MaterialSupplierInterface {
	public:
		using Models           = std::unordered_map<std::string, DevModel>;
		using Materials        = std::unordered_map<std::string, Material>;
		using MissingMaterials = std::unordered_set<std::string>;

		AssetSupplier(): as_engine(nullptr) { }
		AssetSupplier(Engine& engine, std::string_view filename_prefix, float max_inactive_ratio);
		AssetSupplier(AssetSupplier&&);
		AssetSupplier& operator=(AssetSupplier&& mv) { this->~AssetSupplier(); return * new (this) AssetSupplier(std::move(mv)); }
		void destroy();
		~AssetSupplier();

		DevModel msi_requestModel(std::string_view locator) override;
		void     msi_releaseModel(std::string_view locator) noexcept override;
		void msi_releaseAllModels() noexcept override;

		Material msi_requestMaterial(std::string_view locator) override;
		void     msi_releaseMaterial(std::string_view locator) noexcept override;
		void msi_releaseAllMaterials() noexcept override;

	private:
		Engine* as_engine;
		Models    as_activeModels;
		Models    as_inactiveModels;
		Materials as_activeMaterials;
		Materials as_inactiveMaterials;
		Material  as_fallbackMaterial;
		MissingMaterials as_missingMaterials;
		std::string as_filenamePrefix;
		float       as_maxInactiveRatio;
	};


	class Engine {
	public:
		friend ConcurrentAccess;

		static constexpr uint32_t GFRAME_DSET_LOC       = 0;
		static constexpr uint32_t FRAME_UBO_BINDING     = 0;
		static constexpr uint32_t LIGHT_STORAGE_BINDING = 1;
		static constexpr uint32_t MATERIAL_DSET_LOC     = 1;
		static constexpr uint32_t DIFFUSE_TEX_BINDING   = 0;
		static constexpr uint32_t NORMAL_TEX_BINDING    = 1;
		static constexpr uint32_t SPECULAR_TEX_BINDING  = 2;
		static constexpr uint32_t EMISSIVE_TEX_BINDING  = 3;
		static constexpr uint32_t MATERIAL_UBO_BINDING  = 4;

		Engine() = default;
		Engine(const DeviceInitInfo&, const EnginePreferences&, std::unique_ptr<ShaderCacheInterface>);
		~Engine();

		VkShaderModule createShaderModuleFromFile(const std::string& file_path);

		template <ShaderCodeContainer ShaderCode>
		VkShaderModule createShaderModuleFromMemory(ShaderCode);

		void destroyShaderModule(VkShaderModule);

		VkPipeline createPipeline(std::string_view material_type_name);

		void run(LoopInterface&);
		bool isRunning() const noexcept;

		[[nodiscard]]
		std::unique_lock<std::mutex> pauseRenderPass();

		/// \brief Hints the Engine that large numbers of deallocations
		///        or removals are performed, or about to be.
		///
		/// When this happens, perpetually growing resources like command
		/// pools and descriptor pools may be shrinked with no opaque
		/// side-effect.
		///
		void hintContextChange() noexcept;

		const EnginePreferences& getPreferences() const noexcept { return mPrefs; }
		void setDesiredFramerate (float) noexcept;
		void setDesiredTickrate  (float) noexcept;
		void setRenderExtent     (VkExtent2D);
		void setUpscaleFactor    (float pixels_per_fragment);
		void setFullscreen       (bool should_be_fullscreen);

		void pushBuffer(vkutil::BufferDuplex&);
		void pullBuffer(vkutil::BufferDuplex&);

		auto getVmaAllocator       () noexcept { return mVma; }
		auto getDevice             () noexcept { return mDevice; }
		auto getPhysDevice         () noexcept { return mPhysDevice; }
		auto getTransferCmdPool    () noexcept { return mTransferCmdPool; }
		auto getMaterialDsetLayout () noexcept { return mMaterialDsetLayout; }
		auto getQueues             () noexcept { return mQueues; }

		const auto& getQueueInfo            () const noexcept { return mQueues; }
		const auto& getPhysDeviceFeatures   () const noexcept { return mDevFeatures; }
		const auto& getPhysDeviceProperties () const noexcept { return mDevProps; }

		auto& logger() const { return *mLogger; }

		MutexAccess<ConcurrentAccess> getConcurrentAccess() noexcept;

	private:
		class Implementation;
		class DeviceInitializer;
		class RpassInitializer;

		enum class QfamIndex : uint32_t { eInvalid = ~ uint32_t(0) };

		SDL_Window* mSdlWindow = nullptr;

		VkInstance       mVkInstance = nullptr;
		VkPhysicalDevice mPhysDevice = nullptr;
		VkDevice         mDevice     = nullptr;
		VmaAllocator     mVma        = nullptr;
		vkutil::Queues   mQueues     = { };
		VkPhysicalDeviceProperties mDevProps;
		VkPhysicalDeviceFeatures   mDevFeatures;

		VkCommandPool mTransferCmdPool;

		VkSurfaceKHR       mSurface          = nullptr;
		QfamIndex          mPresentQfamIndex = QfamIndex::eInvalid;
		VkQueue            mPresentQueue     = nullptr;
		VkSwapchainKHR     mSwapchainOld     = nullptr;
		VkSwapchainKHR     mSwapchain        = nullptr;
		VkSurfaceCapabilitiesKHR mSurfaceCapabs = { };
		VkSurfaceFormatKHR       mSurfaceFormat = { };
		VkFormat                 mDepthAtchFmt;
		std::vector<SwapchainImageData> mSwapchainImages;

		std::unique_ptr<LoopInterface>        mLoop = nullptr;
		std::unique_ptr<ShaderCacheInterface> mShaderCache;
		tickreg::Regulator mGraphicsReg;
		tickreg::Regulator mLogicReg;

		std::mutex                mGframeMutex = std::mutex();
		std::condition_variable   mGframeResumeCond;
		std::atomic_bool          mGframePriorityOverride = false;
		std::atomic_uint_fast32_t mGframeCounter;
		uint_fast32_t             mGframeSelector;
		std::vector<GframeData>   mGframes;
		std::thread               mGraphicsThread;

		VkExtent2D       mRenderExtent;
		VkExtent2D       mPresentExtent;
		VkRenderPass     mRpass;
		VkFramebuffer    mFramebuffer;
		VkPipelineLayout mPipelineLayout;
		VkPipelineCache  mPipelineCache;
		VkPipeline       mGenericGraphicsPipeline;

		VkDescriptorPool      mGframeDescPool;
		VkDescriptorSetLayout mGframeDsetLayout;
		VkDescriptorSetLayout mMaterialDsetLayout;

		std::mutex    mRendererMutex = std::mutex();
		AssetSupplier mAssetSupplier;
		WorldRenderer mWorldRenderer;
		Renderer      mUiRenderer;
		glm::mat4     mProjTransf;

		std::shared_ptr<spdlog::logger> mLogger;

		EnginePreferences mPrefs;
		RpassConfig       mRpassConfig;
		bool mSwapchainOod;
	};


	template <ShaderCodeContainer ShaderCode>
	VkShaderModule Engine::createShaderModuleFromMemory(ShaderCode code) {
		return createShaderModuleFromMemory<std::span<const uint32_t>>(mDevice, code);
	}

	template <>
	VkShaderModule Engine::createShaderModuleFromMemory(std::span<const uint32_t>);


	inline WorldRenderer& ConcurrentAccess::getWorldRenderer() noexcept { return ca_engine->mWorldRenderer; }

}
