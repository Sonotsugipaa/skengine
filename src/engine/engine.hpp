#pragma once

#include "input.hpp"
#include "renderer.hpp"

#include "vkutil/buffer_alloc.hpp"
#include "vkutil/desc_proxy.hpp"
#include "vkutil/command_pool.hpp"

#include <tick-regulator.hpp>

#include <stdexcept>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <unordered_map>

#include <gap/unpacker.hpp>

#include <SDL2/SDL_vulkan.h>



/// \file impl.hpp
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
		VkExtent2D  present_extent;
		VkExtent2D  max_render_extent;
		VkPresentModeKHR   present_mode;
		VkSampleCountFlags sample_count;
		size_t max_concurrent_frames;
		float  target_framerate;
		float  target_tickrate;
		bool   fullscreen;
	};

	constexpr const EnginePreferences EnginePreferences::default_prefs = EnginePreferences {
		.phys_device_uuid      = "",
		.present_extent        = { 600, 400 },
		.max_render_extent     = { 0xffff, 0xffff },
		.present_mode          = VK_PRESENT_MODE_FIFO_KHR,
		.sample_count          = VK_SAMPLE_COUNT_1_BIT,
		.max_concurrent_frames = 2,
		.target_framerate      = 60.0f,
		.target_tickrate       = 60.0f,
		.fullscreen            = false
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
		vkutil::DsetToken  frame_dset;
		vkutil::Buffer     frame_ubo;
		dev::FrameUniform* frame_ubo_ptr;
		// ...
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

		virtual void      loop_processEvents () = 0;
		virtual LoopState loop_pollState     () const noexcept = 0;
		virtual void      loop_async_preRender  () = 0;
		virtual void      loop_async_postRender () = 0;
	};


	template <typename T>
	concept ShaderCodeContainer = requires(T t) {
		typename T::value_type;
		std::span<const uint32_t>(reinterpret_cast<const uint32_t*>(t.data()), t.size());
		{ t.size() } -> std::convertible_to<uint32_t>;
		{ t.data() } -> std::convertible_to<const void*>;
		requires 4 == sizeof(t.data()[0]);
	};

	struct ShaderModuleSet {
		VkShaderModule vertex;
		VkShaderModule fragment;
	};


	struct Material {
		VkPipeline        pipeline;
		VkPipelineLayout  pipeline_layout;
		vkutil::DsetToken texture_dset;
	};


	class Engine {
	public:
		class ConcurrentAccess;
		class ShaderCacheInterface;

		Engine() = default;
		Engine(const DeviceInitInfo&, const EnginePreferences&);
		~Engine();

		VkShaderModule createShaderModuleFromFile(const std::string& file_path);

		template <ShaderCodeContainer ShaderCode>
		VkShaderModule createShaderModuleFromMemory(ShaderCode);

		void destroyShaderModule(VkShaderModule);

		void run(ShaderCacheInterface&, LoopInterface&);
		bool isRunning() const noexcept;

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
		void setFullscreen       (bool should_be_fullscreen);

		const auto& getInputManager() const  { return mInputMgr; }
		auto&       getInputManager()        { return mInputMgr; }

		ConcurrentAccess getConcurrentAccess ();

	private:
		enum class QfamIndex : uint32_t { eInvalid = ~ uint32_t(0) };

		class DeviceInitializer;
		class RpassInitializer;

		struct QueueFamilies {
			VkQueueFamilyProperties graphics_props;
			VkQueueFamilyProperties compute_props;
			VkQueueFamilyProperties transfer_props;
			QfamIndex graphics_index;
			QfamIndex compute_index;
			QfamIndex transfer_index;
		};

		SDL_Window* mSdlWindow = nullptr;

		VkInstance       mVkInstance = nullptr;
		VkPhysicalDevice mPhysDevice = nullptr;
		VkDevice         mDevice     = nullptr;
		VmaAllocator     mVma        = nullptr;
		QueueFamilies    mQfams         = { { }, { }, { }, QfamIndex::eInvalid, QfamIndex::eInvalid, QfamIndex::eInvalid };
		VkQueue          mGraphicsQueue = nullptr;
		VkQueue          mComputeQueue  = nullptr;
		VkQueue          mTransferQueue = nullptr;
		VkPhysicalDeviceProperties mDevProps;
		VkPhysicalDeviceFeatures   mDevFeatures;

		VkSurfaceKHR   mSurface          = nullptr;
		QfamIndex      mPresentQfamIndex = QfamIndex::eInvalid;
		VkQueue        mPresentQueue     = nullptr;
		VkSwapchainKHR mSwapchain        = nullptr;
		VkSurfaceCapabilitiesKHR mSurfaceCapabs = { };
		VkSurfaceFormatKHR       mSurfaceFormat = { };
		std::vector<SwapchainImageData> mSwapchainImages;
		std::vector<GframeData>         mGframes;

		std::unique_ptr<LoopInterface> mLoop = nullptr;
		input::InputManager mInputMgr;

		tickreg::Regulator mGraphicsReg;
		tickreg::Regulator mLogicReg;

		std::counting_semaphore<> mGframeSem;
		uint_fast64_t             mFrameCounter;

		std::binary_semaphore   mDescProxyMutex;
		vkutil::DescriptorProxy mDescProxy;
		//vkutil::BufferAllocator mBufferAllocator;

		gap::PackageStorage mPackageStorage;
		//Renderer mWorldRenderer;
		//Renderer mUiRenderer;

		vkutil::CommandPool mTransferCmdPool;
		vkutil::CommandPool mRenderCmdPool;

		std::binary_semaphore mConcurrentAccessMutex;
		EnginePreferences     mPrefs;
		RpassConfig           mRpassConfig;
		size_t mConcurrentFrames;
		bool   mSwapchainOod;
	};


	template <ShaderCodeContainer ShaderCode>
	VkShaderModule Engine::createShaderModuleFromMemory(ShaderCode code) {
		return createShaderModuleFromMemory<std::span<const uint32_t>>(mDevice, code);
	}

	template <>
	VkShaderModule Engine::createShaderModuleFromMemory(std::span<const uint32_t>);


	class Engine::ConcurrentAccess final {
	public:
		ConcurrentAccess() = default;
		ConcurrentAccess(Engine&);

	private:
		Engine* mEngine;
	};


	struct WorldShaderRequirement {
		std::string_view material;
	};

	struct UiShaderRequirement {
		// TBW
	};


	struct ShaderRequirement {
		enum class Type {
			eWorld, eUi
		};

		union {
			WorldShaderRequirement world;
			UiShaderRequirement    ui;
		};

		Type type;
	};


	/// \brief Allows on-demand access to shader modules
	///        as desired by the user.
	///
	/// When a ShaderCacheInterface virtual function is called
	/// by the Engine, the latter is guaranteed to be able to load
	/// modules via `Engine::createShaderModuleFromFile` and
	/// `Engine::createShaderModuleFromMemory`, and said Engine
	/// is given as the first argument; although it is not necessarily
	/// fully constructed, and only those two functions are guaranteed
	/// to be accessible.
	///
	/// Before the ShaderCacheInterface is destroyed,
	/// `shader_cache_releaseAllModules` is always called, and no other
	/// function is called afterwards.
	///
	/// Furthermore, it may be assumed that the Engine will never
	/// try to release a module that was never given by the
	/// ShaderCacheInterface, and no module is released more
	/// (fewer) times than it is acquired.
	///
	class Engine::ShaderCacheInterface {
	public:
		virtual ShaderModuleSet shader_cache_requestModuleSet(Engine&, const ShaderRequirement&) = 0;
		virtual void shader_cache_releaseModuleSet  (Engine&, ShaderModuleSet&) = 0;
		virtual void shader_cache_releaseAllModules (Engine&) = 0;
	};


	#warning "Doxygen block TBW"
	class BasicShaderCache : public Engine::ShaderCacheInterface {
	public:
		#ifndef NDEBUG
			// The non-default default constructor isn't necessary,
			// and only used for runtime assertions
			~BasicShaderCache();
		#endif

		ShaderModuleSet shader_cache_requestModuleSet(Engine&, const ShaderRequirement&) override;
		void shader_cache_releaseModuleSet  (Engine&, ShaderModuleSet&) override;
		void shader_cache_releaseAllModules (Engine&) override;

	private:
		std::unordered_map<VkShaderModule, size_t> mModuleCounters;
	};

}
