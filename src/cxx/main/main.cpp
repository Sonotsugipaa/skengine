#include <unistd.h>

#include <engine/engine.hpp>

#include <spdlog/spdlog.h>

#include <posixfio.hpp>



namespace {

	class Loop : public SKENGINE_NAME_NS_SHORT::LoopInterface {
	public:
		SKENGINE_NAME_NS_SHORT::Engine* engine;
		bool end = false;


		Loop(SKENGINE_NAME_NS_SHORT::Engine& e):
			engine(&e)
		{ }


		void loop_processEvents() override {
			end = true;
		}

		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept {
			return end? LoopState::eShouldStop : LoopState::eShouldContinue;
		}

		void loop_async_preRender() {
			spdlog::info("We did it, Lemmy!");
		}

		void loop_async_postRender() { }
	};

}



int main() {
	#ifdef NDEBUG
		spdlog::set_level(spdlog::level::info);
	#else
		spdlog::set_level(spdlog::level::debug);
	#endif

	auto prefs = SKENGINE_NAME_NS_SHORT::EnginePreferences::default_prefs;
	prefs.phys_device_uuid = "00000000-0300-0000-0000-000000000000";

	try {
		SKENGINE_NAME_NS_SHORT::BasicShaderCache shader_cache;

		auto engine = SKENGINE_NAME_NS_SHORT::Engine(
			SKENGINE_NAME_NS_SHORT::DeviceInitInfo {
				.window_title     = SKENGINE_NAME_CSTR " Test Window",
				.application_name = SKENGINE_NAME_PC_CSTR,
				.app_version = VK_MAKE_API_VERSION(0, 0, 0, 0) },
			prefs );

		Loop loop = engine;

		engine.run(shader_cache, loop);
	} catch(posixfio::Errcode& e) {
		spdlog::error("Uncaught posixfio error: {}", e.errcode);
	}
}
