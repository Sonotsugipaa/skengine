#pragma once

#include <skengine_fwd.hpp>

#include <engine/engine.hpp>



namespace SKENGINE_NAME_NS {

	class Engine::DeviceInitializer : public Engine {
	public:
		void init(const DeviceInitInfo*, const EnginePreferences*);
		void destroy();

	private:
		void initSdl(const DeviceInitInfo*);
		void initVkInst(const DeviceInitInfo*);
		void initVkDev();
		void initVma();
		[[deprecated]] void initCmdPools();
		void initDescProxy();
		void destroyDescProxy();
		void destroyCmdPools();
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
		void initGframes(State&);
		void initRpass(State&);
		void initFramebuffers(State&);
		void destroyFramebuffers(State&);
		void destroyRpass(State&);
		void destroyGframes(State&, size_t keep);
		void destroySwapchain(State&);
		void destroySurface();
	};

}
