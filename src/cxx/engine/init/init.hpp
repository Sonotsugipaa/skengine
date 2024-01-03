#pragma once

#include <skengine_fwd.hpp>

#include <engine/engine.hpp>



namespace SKENGINE_NAME_NS {

	class Engine::DeviceInitializer : public Engine {
	public:
		void init(const DeviceInitInfo*);
		void destroy();

	private:
		void initSdl(const DeviceInitInfo*);
		void initVkInst(const DeviceInitInfo*);
		void initVkDev();
		void initVma();
		void initTransferCmdPool();
		void initDsetLayouts();
		void initFreetype();
		void initRenderer();
		void initGui();
		void destroyGui();
		void destroyRenderer();
		void destroyFreetype();
		void destroyDsetLayouts();
		void destroyTransferCmdPool();
		void destroyVma();
		void destroyVkDev();
		void destroyVkInst();
		void destroySdl();
	};


	struct Engine::RpassInitializer : public Engine {
	public:
		struct State;

		void init(const RpassConfig&);
		void reinit();
		void destroy();

	private:
		void initSurface();
		void initSwapchain(State&);
		void initGframeDescPool(State&);
		void initGframes(State&);
		void initRpasses(State&);
		void initFramebuffers(State&);
		void destroyFramebuffers(State&);
		void destroyRpasses(State&);
		void destroyGframes(State&);
		void destroyGframeDescPool(State&);
		void destroySwapchain(State&);
		void destroySurface();
	};

}
