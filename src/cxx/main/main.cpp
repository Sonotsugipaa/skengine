#include <unistd.h>

#include <engine/engine.hpp>

#include <spdlog/spdlog.h>

#include <posixfio.hpp>

#include <vk-util/error.hpp>



namespace {

	class Loop : public SKENGINE_NAME_NS_SHORT::LoopInterface {
	public:
		SKENGINE_NAME_NS_SHORT::Engine* engine;
		unsigned remainingFrames = 10;


		Loop(SKENGINE_NAME_NS_SHORT::Engine& e):
			engine(&e)
		{ }


		void loop_processEvents() override {
			SDL_Event ev;
			while(1 == SDL_PollEvent(&ev))
			switch(ev.type) {
				case SDL_EventType::SDL_QUIT:
					#warning "This exit is not graceful"
					exit(2);
			}

			spdlog::info("We did it, Lemmy!");
			-- remainingFrames;
		}

		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept override {
			return remainingFrames > 0? LoopState::eShouldContinue : LoopState::eShouldStop;
		}

		void loop_async_preRender() override { }

		void loop_async_postRender() override { }
	};

}



int main() {
	#ifdef NDEBUG
		spdlog::set_level(spdlog::level::info);
	#else
		spdlog::set_level(spdlog::level::debug);
	#endif

	auto prefs = SKENGINE_NAME_NS_SHORT::EnginePreferences::default_prefs;
	//prefs.phys_device_uuid    = "00000000-0900-0000-0000-000000000000";
	prefs.init_present_extent = { 700, 700 };
	prefs.max_render_extent   = { 100, 100 };

	try {
		SKENGINE_NAME_NS_SHORT::BasicShaderCache shader_cache = std::string("assets/");

		auto engine = SKENGINE_NAME_NS_SHORT::Engine(
			SKENGINE_NAME_NS_SHORT::DeviceInitInfo {
				.window_title     = SKENGINE_NAME_CSTR " Test Window",
				.application_name = SKENGINE_NAME_PC_CSTR,
				.app_version = VK_MAKE_API_VERSION(
					0,
					SKENGINE_VERSION_MAJOR,
					SKENGINE_VERSION_MINOR,
					SKENGINE_VERSION_PATCH ) },
			prefs );

		Loop loop = engine;

		engine.run(shader_cache, loop);
	} catch(posixfio::Errcode& e) {
		spdlog::error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		spdlog::error("Uncaught Vulkan error: {}", e.what());
	}

	spdlog::info("Successfully exiting the program.");
}
