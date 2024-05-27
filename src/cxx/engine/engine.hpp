#pragma once

#include "world_renderer.hpp"
#include "ui_renderer.hpp"
#include "shader_cache.hpp"

#include <vk-util/init.hpp>
#include <vk-util/memory.hpp>

#include <input/input.hpp>

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
/// `LoopInterface::loop_async_preRender` and `LoopInterface::loop_async_postRender`.
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

	class GuiManager; // Defined in `engine_gui_manager.hpp`


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
		std::string asset_filename_prefix;
		std::string font_location;
		VkExtent2D  init_present_extent;
		VkExtent2D  max_render_extent;
		VkPresentModeKHR   present_mode;
		VkSampleCountFlags sample_count;
		uint32_t       max_concurrent_frames;
		uint32_t       framerate_samples;
		std::float32_t fov_y;
		std::float32_t z_near;
		std::float32_t z_far;
		uint32_t       shade_step_count;
		std::float32_t shade_step_smoothness;
		std::float32_t shade_step_exponent;
		std::float32_t dithering_steps;
		std::float32_t upscale_factor;
		std::float32_t target_framerate;
		std::float32_t target_tickrate;
		uint32_t       font_max_cache_size;
		bool           fullscreen      : 1;
		bool           composite_alpha : 1;
		bool           wait_for_gframe : 1;
	};


	struct RpassConfig {
		static const RpassConfig default_cfg;
		std::string world_vertex_shader_file;
		std::string world_fragment_shader_file;
	};

	inline const RpassConfig RpassConfig::default_cfg = RpassConfig {
		.world_vertex_shader_file   = "world-vtx.spv",
		.world_fragment_shader_file = "world-frg.spv"
	};


	struct DeviceInitInfo {
		std::string window_title;
		std::string application_name;
		uint32_t    app_version;
	};


	struct GframeData {
		vkutil::ManagedImage atch_color;
		vkutil::ManagedImage atch_depthstencil;
		VkImage     swapchain_image;
		VkImageView swapchain_image_view;
		VkImageView atch_color_view;
		VkImageView atch_depthstencil_view;
		VkCommandPool cmd_pool;
		VkCommandBuffer cmd_prepare;
		VkCommandBuffer cmd_draw[2] /* One for each render pass */;
		VkFramebuffer worldFramebuffer;
		VkFramebuffer uiFramebuffer;
		VkSemaphore sem_prepare;
		VkSemaphore sem_drawWorld;
		VkSemaphore sem_drawGui;
		VkFence     fence_prepare;
		VkFence     fence_draw;
		tickreg::delta_t frame_delta;
	};


	class ConcurrentAccess {
	public:
		friend Engine;

		ConcurrentAccess() = default;
		ConcurrentAccess(Engine* e, bool is_thread_local): ca_engine(e), ca_threadLocal(is_thread_local) { }

		ObjectStorage& getObjectStorage() noexcept;
		WorldRenderer& getWorldRenderer() noexcept;
		GframeData& getGframeData(unsigned index) noexcept;
		VkPipeline getPipeline(const ShaderRequirement&);

		void setPresentExtent(VkExtent2D);

		uint_fast32_t currentFrameNumber() const noexcept;

		GuiManager gui() const noexcept; // Defined in `engine_gui_manager.hpp`

		Engine& engine() noexcept { return *ca_engine; }

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


	class Engine {
	public:
		friend ConcurrentAccess;
		friend gui::DrawContext;
		friend GuiManager;

		using signal_e = unsigned;
		enum class Signal : signal_e { eNone = 0, eReinit = 1 };
		template <Signal signal> static const char signalChars[];
		static std::string_view signalString(Signal) noexcept;

		using GenericPipelineSet = std::unordered_map<ShaderRequirement, VkPipeline, ShaderRequirementHash, ShaderRequirementCompare>;

		Engine() = default;
		~Engine();

		Engine(
			const DeviceInitInfo&,
			const EnginePreferences&,
			std::shared_ptr<ShaderCacheInterface>,
			std::shared_ptr<AssetSourceInterface>,
			std::shared_ptr<spdlog::logger> );

		VkShaderModule createShaderModuleFromFile(const std::string& file_path);

		template <ShaderCodeContainer ShaderCode>
		VkShaderModule createShaderModuleFromMemory(ShaderCode);

		void destroyShaderModule(VkShaderModule);

		VkPipeline create3dPipeline(const ShaderRequirement&, uint32_t subpass, VkRenderPass);

		void run(LoopInterface&);
		bool isRunning() const noexcept;
		void signal(Signal, bool discardDuplicate = false) noexcept;

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
		void setWaitForGframe    (bool wait_for_gframe);

		static void pushBuffer(const TransferContext&, vkutil::BufferDuplex&);
		static void pullBuffer(const TransferContext&, vkutil::BufferDuplex&);

		auto getImageDsetLayout  () noexcept { return mImagePipelineDsetLayout; }
		auto getVmaAllocator     () noexcept { return mVma; }
		auto getDevice           () noexcept { return mDevice; }
		auto getPhysDevice       () noexcept { return mPhysDevice; }
		auto getQueues           () noexcept { return mQueues; }
		auto getPipelineLayout3d () noexcept { return m3dPipelineLayout; }

		auto getTransferContext() const noexcept { return mTransferContext; }

		const auto& getRenderExtent         () const noexcept { return mRenderExtent; }
		const auto& getPresentExtent        () const noexcept { return mPresentExtent; }
		const auto& getQueueInfo            () const noexcept { return mQueues; }
		const auto& getPhysDeviceFeatures   () const noexcept { return mDevFeatures; }
		const auto& getPhysDeviceProperties () const noexcept { return mDevProps; }

		auto& logger() const noexcept { return *mLogger; }
		auto  frameCount() const noexcept { return mGframeCounter.load(std::memory_order_relaxed); }
		auto  frameDelta() const noexcept { return mGraphicsReg.estDelta(); }
		auto  tickDelta() const noexcept { return mLogicReg.estDelta(); }

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

		TransferContext mTransferContext;

		VkSurfaceKHR       mSurface          = nullptr;
		QfamIndex          mPresentQfamIndex = QfamIndex::eInvalid;
		VkQueue            mPresentQueue     = nullptr;
		VkSwapchainKHR     mSwapchain        = nullptr;
		VkSurfaceCapabilitiesKHR mSurfaceCapabs = { };
		VkSurfaceFormatKHR       mSurfaceFormat = { };
		VkFormat                 mDepthAtchFmt;

		std::shared_ptr<ShaderCacheInterface> mShaderCache;
		tickreg::Regulator mGraphicsReg;
		tickreg::Regulator mLogicReg;

		std::mutex                mGframeMutex = std::mutex();
		std::condition_variable   mGframeResumeCond;
		std::atomic_bool          mGframePriorityOverride = false;
		std::atomic_uint_fast32_t mGframeCounter;
		uint_fast32_t             mGframeSelector;
		int_fast32_t              mGframeLast;
		std::vector<GframeData>   mGframes;
		std::vector<VkFence>      mGframeSelectionFences;
		std::thread               mGraphicsThread;

		VkExtent2D            mRenderExtent;
		VkExtent2D            mPresentExtent;
		VkRenderPass          mWorldRpass;
		VkRenderPass          mUiRpass;
		VkPipelineCache       mPipelineCache;
		VkPipelineLayout      m3dPipelineLayout;
		VkDescriptorSetLayout mGeometryPipelineDsetLayout;
		VkDescriptorSetLayout mImagePipelineDsetLayout;
		GenericPipelineSet    mPipelines;

		std::shared_ptr<WorldRendererSharedState> mWorldRendererSharedState_TMP_UGLY_NAME;
		std::shared_ptr<ObjectStorage> mObjectStorage;
		std::shared_ptr<UiStorage>     mUiStorage;
		WorldRenderer* mWorldRenderer_TMP_UGLY_NAME; // This is currently needed to reference one specific renderer of the many, but it should be managed by the Engine user in the future
		UiRenderer*    mUiRenderer_TMP_UGLY_NAME;    // Ditto

		std::mutex mRendererMutex = std::mutex(); // On the graphics thread, this is never locked outside of mGframeMutex lock/unlock periods; on external threads, lock/unlock sequences MUST span the entire lifetime of ConcurrentAccess objects
		Signal              mSignalGthread;
		std::atomic<Signal> mSignalXthread;
		uint_fast64_t mLastResizeTime; // Bandaid fix to drivers not telling me why they fail to vkCreateSwapchainKHR: don't resize twice in less than <TIMESPAN>
		static_assert(decltype(mSignalXthread)::is_always_lock_free);

		std::shared_ptr<AssetSourceInterface> mAssetSource;
		std::vector<std::unique_ptr<Renderer>> mRenderers;
		AssetSupplier mAssetSupplier;

		std::shared_ptr<spdlog::logger> mLogger;

		EnginePreferences mPrefs;
		RpassConfig       mRpassConfig;
		std::atomic_bool  mIsRunning;
		bool mSwapchainOod;
		bool mHdrEnabled;
	};


	template <> constexpr const char Engine::signalChars<Engine::Signal::eNone  >[4] = { 'N', 'O', 'N', 'E' };
	template <> constexpr const char Engine::signalChars<Engine::Signal::eReinit>[6] = { 'R', 'E', 'I', 'N', 'I', 'T' };
	inline std::string_view Engine::signalString(Engine::Signal signal) noexcept { switch(signal) {
		#define CASE_(S_) case S_: return std::string_view(signalChars<S_>, std::size(signalChars<S_>));
		CASE_(Signal::eNone);
		CASE_(Signal::eReinit);
		default: return "?";
		#undef CASE_
	}}


	template <ShaderCodeContainer ShaderCode>
	VkShaderModule Engine::createShaderModuleFromMemory(ShaderCode code) {
		return createShaderModuleFromMemory<std::span<const uint32_t>>(mDevice, code);
	}

	template <>
	VkShaderModule Engine::createShaderModuleFromMemory(std::span<const uint32_t>);


	inline ObjectStorage& ConcurrentAccess::getObjectStorage   ()           noexcept { return * ca_engine->mObjectStorage; }
	inline WorldRenderer& ConcurrentAccess::getWorldRenderer   ()           noexcept { return * ca_engine->mWorldRenderer_TMP_UGLY_NAME; }
	inline GframeData&    ConcurrentAccess::getGframeData      (unsigned i) noexcept { return ca_engine->mGframes[i]; }
	inline uint_fast32_t  ConcurrentAccess::currentFrameNumber ()     const noexcept { return ca_engine->mGframeCounter; }
	inline VkPipeline ConcurrentAccess::getPipeline(const ShaderRequirement& req) { return ca_engine->mPipelines.at(req); }

}



#include "engine_gui_manager.hpp"
