#pragma once

#include <skengine_fwd.hpp>

#include <engine/engine.hpp>



namespace SKENGINE_NAME_NS {

	#warning "Document how this works, since it's trippy, workaroundy and probably UB (hopefully not) (but it removes A LOT of boilerplate)"
	class Engine::DeviceInitializer : public Engine {
	public:
		void init(const DeviceInitInfo*, const EnginePreferences*);
		void destroy();

	private:
		void initSdl(const DeviceInitInfo*);
		void initVkInst(const DeviceInitInfo*);
		void initVkDev();
		void initVma();
		void initCmdPools();
		void initDescProxy();
		void destroyDescProxy();
		void destroyCmdPools();
		void destroyVma();
		void destroyVkDev();
		void destroyVkInst();
		void destroySdl();
	};


	#warning "Document how this works, since it's trippy, workaroundy and probably UB (hopefully not) (but it removes A LOT of boilerplate)"
	struct Engine::RpassInitializer : public Engine {
	public:
		void init(const RpassConfig&);
		void reinit();
		void destroy();

	private:
		void initSurface();
		void initSwapchain(VkSwapchainKHR old_swapchain);
		void initGframes();
		void destroyGframes(size_t keep);
		void destroySwapchain(VkSwapchainKHR* dst_old_swapchain);
		void destroySurface();
	};

}
