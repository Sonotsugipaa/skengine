#pragma once

#include "renderprocess/render_process.hpp"
#include "renderprocess/interface.hpp"
#include "shader_cache.hpp"

#include <vk-util/init.hpp>
#include <vk-util/memory.hpp>

#include <tick-regulator.hpp>

#include <stdexcept>
#include <memory>
#include <mutex>
#include <stdfloat>
#include <condition_variable>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <ranges>

#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>

#include "mutex_ptr.inl.hpp"

#include <SDL2/SDL_vulkan.h>

#include <sflog.hpp>



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
/// - qfam    | queue family
/// - dset    | descriptor set
/// - dpool   | descriptor pool
/// - cpool   | command pool
/// - rstep   | render step
/// - rpass   | render pass
/// - rtarget | render target
///



namespace SKENGINE_NAME_NS {

	class Engine;


	class EngineRuntimeError : public std::runtime_error {
	public:
		template <typename... Args>
		EngineRuntimeError(Args... args): std::runtime_error::runtime_error(args...) { }
	};


	struct EnginePreferences {
		static const EnginePreferences default_prefs;
		std::string phys_device_uuid;
		VkExtent2D  init_present_extent;
		VkExtent2D  max_render_extent;
		VkPresentModeKHR present_mode;
		uint32_t       max_concurrent_frames;
		uint32_t       framerate_samples;
		std::float32_t upscale_factor;
		std::float32_t target_framerate;
		std::float32_t target_tickrate;
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


	struct TransferCmdBarrier {
		VkDevice vkDevice;
		VkCommandPool cmdPool;
		VkCommandBuffer cmdBuffer;
		VkFence cmdFence;

		TransferCmdBarrier(): vkDevice(nullptr), cmdPool(nullptr), cmdBuffer(nullptr), cmdFence(nullptr) { }
		TransferCmdBarrier(VkDevice, VkCommandPool, VkCommandBuffer, VkFence);
		~TransferCmdBarrier();
		void wait();
	};


	struct DeviceInitInfo {
		std::string window_title;
		std::string application_name;
		uint32_t    app_version;
	};


	struct GframeData {
		VkImage     swapchain_image;
		VkImageView swapchain_image_view;
		tickreg::delta_t frame_delta;
	};


	class ConcurrentAccess {
	public:
		friend Engine;

		ConcurrentAccess() = default;
		ConcurrentAccess(Engine* e, bool is_thread_local): ca_engine(e), ca_threadLocal(is_thread_local) { }

		GframeData& getGframeData(unsigned index) noexcept;
		std::span<GframeData> getGframeData() noexcept;
		RenderProcess& getRenderProcess() noexcept;

		void setPresentExtent(VkExtent2D);

		uint_fast32_t currentFrameNumber() const noexcept;

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

		virtual void      loop_begin() = 0;
		virtual void      loop_end() noexcept = 0;
		virtual void      loop_processEvents (tickreg::delta_t delta_avg, tickreg::delta_t delta) = 0;
		virtual LoopState loop_pollState     () const noexcept = 0;
		virtual void      loop_async_preRender  (ConcurrentAccess, tickreg::delta_t delta_avg, tickreg::delta_t delta_previous) = 0;
		virtual void      loop_async_postRender (ConcurrentAccess, tickreg::delta_t delta_avg, tickreg::delta_t delta_current) = 0;
	};


	class Engine {
	public:
		friend ConcurrentAccess;

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
			const Logger& );

		void run(LoopInterface&, std::shared_ptr<RenderProcessInterface>);
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
		static auto pushBufferAsync(const TransferContext&, vkutil::BufferDuplex&) -> TransferCmdBarrier;
		static auto pullBufferAsync(const TransferContext&, vkutil::BufferDuplex&) -> TransferCmdBarrier;

		auto getVmaAllocator  () noexcept { return mVma; }
		auto getDevice        () noexcept { return mDevice; }
		auto getPhysDevice    () noexcept { return mPhysDevice; }
		auto getQueues        () noexcept { return mQueues; }
		auto getPipelineCache () noexcept { return mPipelineCache; }

		auto& getTransferContext () const noexcept { return mTransferContext; }

		auto& getShaderCache () noexcept { return mShaderCache; }
		auto& getRenderProcess () noexcept { return mRenderProcess; }

		const auto& getRenderExtent         () const noexcept { return mRenderExtent; }
		const auto& getPresentExtent        () const noexcept { return mPresentExtent; }
		const auto& getQueueInfo            () const noexcept { return mQueues; }
		const auto& getPhysDeviceFeatures   () const noexcept { return mDevFeatures; }
		const auto& getPhysDeviceProperties () const noexcept { return mDevProps; }

		template <typename T> auto& logger(this T& self) noexcept { return self.mLogger; }

		auto surfaceFormat() const noexcept { return mSurfaceFormat; }
		auto depthFormat() const noexcept { return mDepthAtchFmt; };
		auto gframeCount() const noexcept { return mGframes.size(); }
		auto frameCounter() const noexcept { return mGframeCounter.load(std::memory_order_relaxed); }
		auto frameDelta() const noexcept { return mGraphicsReg.estDelta(); }
		auto tickDelta() const noexcept { return mLogicReg.estDelta(); }

		MutexAccess<ConcurrentAccess> getConcurrentAccess() noexcept;

	private:
		class Implementation;
		class DeviceInitializer;
		class RpassInitializer;

		enum class QfamIndex : uint32_t { eInvalid = ~ uint32_t(0) };

		SDL_Window* mSdlWindow = nullptr;

		Logger mLogger;

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
		std::vector<VkFence>      mWaveFencesWaitCache; // Not needed persistently across scopes, but keeping it here prevents frequent reallocations
		std::thread               mGraphicsThread;

		VkExtent2D      mRenderExtent;
		VkExtent2D      mPresentExtent;
		VkPipelineCache mPipelineCache;
		RenderProcess   mRenderProcess;

		std::mutex mRendererMutex = std::mutex(); // On the graphics thread, this is never locked outside of mGframeMutex lock/unlock periods; on external threads, lock/unlock sequences MUST span the entire lifetime of ConcurrentAccess objects
		Signal              mSignalGthread;
		std::atomic<Signal> mSignalXthread;
		uint_fast64_t mLastResizeTime; // Bandaid fix to drivers not telling me why they fail to vkCreateSwapchainKHR: don't resize twice in less than <TIMESPAN>
		static_assert(decltype(mSignalXthread)::is_always_lock_free);

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


	inline GframeData&           ConcurrentAccess::getGframeData      (unsigned i) noexcept { return ca_engine->mGframes[i]; }
	inline std::span<GframeData> ConcurrentAccess::getGframeData      ()           noexcept { return std::span(ca_engine->mGframes); }
	inline RenderProcess&        ConcurrentAccess::getRenderProcess   ()           noexcept { return ca_engine->mRenderProcess; }
	inline uint_fast32_t         ConcurrentAccess::currentFrameNumber ()     const noexcept { return ca_engine->mGframeCounter; }

}
