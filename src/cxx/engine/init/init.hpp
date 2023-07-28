#pragma once

#include <skengine_fwd.hpp>

#include <engine/engine.hpp>



namespace SKENGINE_NAME_NS {

	class Engine::DeviceInitializer {
	public:
		DeviceInitializer(Engine&, const DeviceInitInfo*, const EnginePreferences*);

		void init();
		void destroy();

	private:
		const DeviceInitInfo*    mSrcDeviceInitInfo;
		const EnginePreferences* mSrcPrefs;
		SDL_Window*&      mSdlWindow;
		VkInstance&       mVkInstance;
		VkPhysicalDevice& mPhysDevice;
		VkDevice&         mDevice;
		VmaAllocator&     mVma;
		vkutil::Queues&   mQueues;
		VkPhysicalDeviceProperties& mDevProps;
		VkPhysicalDeviceFeatures&   mDevFeatures;
		vkutil::CommandPool& mTransferCmdPool;
		vkutil::CommandPool& mRenderCmdPool;
		EnginePreferences& mPrefs;

		void initSdl();
		void initVkInst();
		void initVkDev();
		void initVma();
		void initCmdPools();
		void destroyCmdPools();
		void destroyVma();
		void destroyVkDev();
		void destroyVkInst();
		void destroySdl();
	};


	struct Engine::RpassInitializer {
	public:
		RpassInitializer(Engine&);

		void init(const RpassConfig&);
		void reinit();
		void destroy();

	private:
		SDL_Window* const      mSdlWindow;
		const VkInstance       mVkInstance;
		const VkPhysicalDevice mPhysDevice;
		const VkDevice         mDevice;
		const VmaAllocator     mVma;
		const vkutil::Queues   mQueues;

		VkSurfaceKHR&   mSurface;
		QfamIndex&      mPresentQfamIndex;
		VkQueue&        mPresentQueue;
		VkSwapchainKHR& mSwapchain;
		VkSurfaceCapabilitiesKHR& mSurfaceCapabs;
		VkSurfaceFormatKHR&       mSurfaceFormat;
		std::vector<SwapchainImageData>& mSwapchainImages;
		std::vector<GframeData>&         mGframes;
		EnginePreferences& mPrefs;
		RpassConfig&       mRpassConfig;

		void initSurface();
		void initSwapchain(VkSwapchainKHR old_swapchain);
		void destroySwapchain(VkSwapchainKHR* dst_old_swapchain);
		void destroySurface();
	};

}
