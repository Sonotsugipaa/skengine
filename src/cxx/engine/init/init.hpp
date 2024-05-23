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
		void initTransferContext();
		void initDsetLayouts();
		void initAssets();
		void initGui();
		void destroyGui();
		void destroyAssets();
		void destroyDsetLayouts();
		void destroyTransferContext();
		void destroyVma();
		void destroyVkDev();
		void destroyVkInst();
		void destroySdl();
	};


	struct Engine::RpassInitializer : public Engine {
	public:
		struct State;

		void init(ConcurrentAccess&, const RpassConfig&);
		void reinit(ConcurrentAccess&);
		void destroy(ConcurrentAccess&);

	private:
		void unwind(State&);
		void initSurface();
		void initSwapchain(State&);
		void initRenderers(State&);
		void initGframes(State&);
		void initRpasses(State&);
		void initFramebuffers(State&);
		void initTop(State&);
		void destroyTop(State&);
		void destroyFramebuffers(State&);
		void destroyRpasses(State&);
		void destroyGframes(State&);
		void destroyRenderers(State&);
		void destroySwapchain(State&);
		void destroySurface();
	};

}
