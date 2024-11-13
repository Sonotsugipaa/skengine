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
		void initAssets();
		void destroyAssets();
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
		void initGframes(State&);
		void initRpasses(State&);
		void initTop(State&);
		void destroyTop(State&);
		void destroyRpasses(State&);
		void destroyGframes(State&);
		void destroySwapchain(State&);
		void destroySurface();
	};

}
