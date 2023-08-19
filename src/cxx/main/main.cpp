#include <numbers>

#include <engine/engine.hpp>

#include <spdlog/spdlog.h>

#include <posixfio.hpp>

#include <vk-util/error.hpp>

#include <glm/trigonometric.hpp>



namespace {

	class Loop : public SKENGINE_NAME_NS_SHORT::LoopInterface {
	public:
		SKENGINE_NAME_NS_SHORT::Engine* engine;
		bool active;


		Loop(SKENGINE_NAME_NS_SHORT::Engine& e):
			engine(&e),
			active(true)
		{ }


		void loop_processEvents(tickreg::delta_t, tickreg::delta_t delta) override {
			SDL_Event ev;
			while(1 == SDL_PollEvent(&ev))
			switch(ev.type) {
				case SDL_EventType::SDL_QUIT:
					active = false;
			}

			{ // Rotate the view at a frame-fixed pace
				auto& wr   = engine->getWorldRenderer();
				auto dir   = wr.getViewRotation();
				float dist = 2.4f;
				dir.x += glm::radians(45.0 * delta);
				wr.setViewPosition({ dist * std::sin(dir.x), 0.0f, dist * -std::cos(dir.x) });
				wr.setViewRotation(dir);
			}
		}

		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept override {
			return active? LoopState::eShouldContinue : LoopState::eShouldStop;
		}

		void loop_async_preRender(tickreg::delta_t, tickreg::delta_t) override { }

		void loop_async_postRender(tickreg::delta_t, tickreg::delta_t) override { }
	};

}



int main() {
	#ifdef NDEBUG
		spdlog::set_level(spdlog::level::info);
	#else
		spdlog::set_level(spdlog::level::debug);
	#endif

	auto prefs = SKENGINE_NAME_NS_SHORT::EnginePreferences::default_prefs;
	prefs.init_present_extent = { 700, 700 };
	prefs.max_render_extent   = { 200, 200 };
	prefs.present_mode        = VK_PRESENT_MODE_MAILBOX_KHR;
	prefs.target_framerate    = 60.0;
	prefs.target_tickrate     = 60.0;

	try {
		auto* shader_cache = new SKENGINE_NAME_NS_SHORT::BasicShaderCache("assets/");

		auto engine = SKENGINE_NAME_NS_SHORT::Engine(
			SKENGINE_NAME_NS_SHORT::DeviceInitInfo {
				.window_title     = SKENGINE_NAME_CSTR " Test Window",
				.application_name = SKENGINE_NAME_PC_CSTR,
				.app_version = VK_MAKE_API_VERSION(
					0,
					SKENGINE_VERSION_MAJOR,
					SKENGINE_VERSION_MINOR,
					SKENGINE_VERSION_PATCH ) },
			prefs,
			std::unique_ptr<SKENGINE_NAME_NS_SHORT::BasicShaderCache>(shader_cache) );

		Loop loop = engine;
		auto wr   = engine.getWorldRenderer();

		{
			float dist = 2.4f;
			auto& dir  = wr.getViewRotation();
			wr.setViewRotation({ 0.0f, 0.0f, 0.0f });
			wr.setViewPosition({ dist * std::sin(dir.x), 0.0f, dist * -std::cos(dir.x) });
		}

		engine.run(loop);
	} catch(posixfio::Errcode& e) {
		spdlog::error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		spdlog::error("Uncaught Vulkan error: {}", e.what());
	}

	spdlog::info("Successfully exiting the program.");
}
