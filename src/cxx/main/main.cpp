#include <numbers>

#include <engine/engine.hpp>

#include <spdlog/spdlog.h>

#include <posixfio.hpp>

#include <vk-util/error.hpp>

#include <glm/trigonometric.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>



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

			// Consume events, but only the last one of each type; discard the rest
			while(1 == SDL_PollEvent(&ev)) {
				switch(ev.type) {
					case SDL_EventType::SDL_QUIT: {
						active = false;
					} break;
					case SDL_EventType::SDL_WINDOWEVENT:
					switch(ev.window.event) {
						case SDL_WINDOWEVENT_RESIZED:
							engine->setPresentExtent({ uint32_t(ev.window.data1), uint32_t(ev.window.data2) });
							break;
					} break;
				}
			}

			{ // Rotate the view at a frame-fixed pace
				auto& wr   = engine->getWorldRenderer();
				auto  pos  = wr.getViewPosition();
				auto  dir  = wr.getViewRotation();
				float dist = 1.0f;
				float sin  = std::sin(dir.x);
				dir.x += glm::radians(15.0 * delta);
				dir.y  = glm::radians(20.0f) + (glm::radians(20.0f) * sin);
				wr.setViewPosition({ dist * sin, pos.y, dist * std::cos(dir.x) });
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
	auto logger = std::make_shared<spdlog::logger>(
		SKENGINE_NAME_CSTR,
		std::make_shared<spdlog::sinks::stdout_color_sink_mt>(spdlog::color_mode::automatic) );

	auto prefs = SKENGINE_NAME_NS_SHORT::EnginePreferences::default_prefs;
	prefs.init_present_extent = { 700, 700 };
	prefs.max_render_extent   = { 0, 200 };
	prefs.present_mode        = VK_PRESENT_MODE_MAILBOX_KHR;
	prefs.target_framerate    = 60.0;
	prefs.target_tickrate     = 60.0;
	prefs.fov_y               = glm::radians(110.0f);

	prefs.logger = logger;
	#ifdef NDEBUG
		spdlog::set_level(spdlog::level::info);
		prefs.log_level = spdlog::level::info;
	#else
		spdlog::set_level(spdlog::level::debug);
		prefs.log_level = spdlog::level::debug;
	#endif

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
		auto& wr  = engine.getWorldRenderer();

		{
			SKENGINE_NAME_NS_SHORT::RenderObject ro = { };
			float     count_sq = 7.0f;
			float     dist     = 1.0f;
			glm::vec3 dir      = { 0.0f, glm::radians(20.0f), 0.0f };
			wr.setViewRotation(dir);
			wr.setViewPosition({ dist * std::sin(dir.x), 0.35f, dist * std::cos(dir.x) });
			ro.mesh_id     = wr.getMeshId("assets/test-model.fma");
			ro.material_id = wr.getMaterialId("assets/test-model.mtl.fma");
			ro.color_rgba = { 1.0f, 1.0f, 1.0f, 1.0f };
			ro.scale_xyz  = { 0.125f, 0.125f, 0.125f };
			for(float x = -count_sq; x < count_sq; ++x)
			for(float y = -count_sq; y < count_sq; ++y) {
				float ox = (0.5f + x) / 3.0f;
				float oz = (0.5f + y) / 3.0f;
				ro.position_xyz.x = ox;
				ro.position_xyz.z = oz;
				ro.position_xyz.y = std::sqrt((ox*ox) + (oz*oz)) * -0.4f / (count_sq * count_sq);
				wr.createObject(ro);
			}
		}

		engine.run(loop);

		logger->info("Successfully exiting the program.");
	} catch(posixfio::Errcode& e) {
		logger->error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		logger->error("Uncaught Vulkan error: {}", e.what());
	}
}
