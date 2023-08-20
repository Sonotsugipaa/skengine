#pragma once

#include "input.hpp"
#include "renderer.hpp"
#include "shader_cache.hpp"

#include <vk-util/init.hpp>
#include <vk-util/memory.hpp>

#include <tick-regulator.hpp>

#include <stdexcept>
#include <memory>
#include <string>
#include <unordered_map>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

#include <boost/thread/mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

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

	using scoped_lock = boost::interprocess::scoped_lock<boost::mutex>;


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


	struct EnginePreferences {
		static const EnginePreferences default_prefs;
		std::string phys_device_uuid;
		VkExtent2D  init_present_extent;
		VkExtent2D  max_render_extent;
		VkPresentModeKHR   present_mode;
		VkSampleCountFlags sample_count;
		std::shared_ptr<spdlog::logger> logger;
		spdlog::level::level_enum       log_level;
		uint32_t max_concurrent_frames;
		float    fov_y;
		float    z_near;
		float    z_far;
		float    upscale_factor;
		float    target_framerate;
		float    target_tickrate;
		bool     fullscreen;
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
		VkDescriptorSet      frame_dset;
		vkutil::BufferDuplex frame_ubo;
		vkutil::BufferDuplex light_storage;
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
		VkFence     fence_draw;
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
			eShouldDelay     ///< The engine should stop rendering for one lframe, then continue running.
		};

		virtual void      loop_processEvents (tickreg::delta_t delta_avg, tickreg::delta_t delta) = 0;
		virtual LoopState loop_pollState     () const noexcept = 0;
		virtual void      loop_async_preRender  (tickreg::delta_t delta_avg, tickreg::delta_t delta_previous) = 0;
		virtual void      loop_async_postRender (tickreg::delta_t delta_avg, tickreg::delta_t delta_current) = 0;
	};


	class MeshSupplier : public MeshSupplierInterface {
	public:
		using Meshes = std::unordered_map<std::string, DevMesh>;

		MeshSupplier() = default;
		MeshSupplier(Engine& engine, float max_inactive_ratio);
		void destroy();
		~MeshSupplier();

		DevMesh msi_requestMesh(std::string_view locator) override;
		void    msi_releaseMesh(std::string_view locator) noexcept override;
		void msi_releaseAllMeshes() noexcept override;

	private:
		Engine* ms_engine;
		Meshes  ms_active;
		Meshes  ms_inactive;
		float   ms_maxInactiveRatio;
	};


	class Engine {
	public:
		static constexpr uint32_t GFRAME_DSET            = 0;
		static constexpr uint32_t FRAME_UBO_BINDING      = 0;
		static constexpr uint32_t LIGHT_STORAGE_BINDING  = 1;

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
		scoped_lock pauseRenderPass();

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
		void setPresentExtent    (VkExtent2D);
		void setUpscaleFactor    (float pixels_per_fragment);
		void setFullscreen       (bool should_be_fullscreen);

		void pushBuffer(vkutil::BufferDuplex&);
		void pullBuffer(vkutil::BufferDuplex&);

		auto getVmaAllocator () noexcept { return mVma; }
		auto getDevice       () noexcept { return mDevice; }
		auto getPhysDevice   () noexcept { return mPhysDevice; }

		const auto& getWorldRenderer() const noexcept { return mWorldRenderer; }
		auto&       getWorldRenderer()       noexcept { return mWorldRenderer; }

		const auto& getQueueInfo            () const noexcept { return mQueues; }
		const auto& getPhysDeviceFeatures   () const noexcept { return mDevFeatures; }
		const auto& getPhysDeviceProperties () const noexcept { return mDevProps; }

		auto& logger() const { return *mLogger; }

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
		VkSwapchainKHR     mSwapchain        = nullptr;
		VkSurfaceCapabilitiesKHR mSurfaceCapabs = { };
		VkSurfaceFormatKHR       mSurfaceFormat = { };
		VkFormat                 mDepthAtchFmt;
		std::vector<SwapchainImageData> mSwapchainImages;

		std::unique_ptr<LoopInterface>        mLoop = nullptr;
		std::unique_ptr<ShaderCacheInterface> mShaderCache;
		tickreg::Regulator mGraphicsReg;
		tickreg::Regulator mLogicReg;

		boost::mutex mGframeMutex = boost::mutex();
		uint_fast64_t           mFrameCounter;
		uint_fast32_t           mGframeSelector;
		std::vector<GframeData> mGframes;

		VkExtent2D       mRenderExtent;
		VkExtent2D       mPresentExtent;
		VkRenderPass     mRpass;
		VkFramebuffer    mFramebuffer;
		VkPipelineLayout mPipelineLayout;
		VkPipelineCache  mPipelineCache;
		VkPipeline       mGenericGraphicsPipeline;

		VkDescriptorPool      mGframeDescPool;
		VkDescriptorSetLayout mGframeDsetLayout;

		MeshSupplier  mMeshSupplier;
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

}
