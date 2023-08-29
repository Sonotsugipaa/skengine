#include <numbers>

#include <engine/engine.hpp>

#include <spdlog/spdlog.h>

#include <posixfio.hpp>

#include <vk-util/error.hpp>

#include <glm/trigonometric.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>



inline namespace main_ns {
namespace {

	using namespace SKENGINE_NAME_NS;


	class Loop : public SKENGINE_NAME_NS_SHORT::LoopInterface {
	public:
		static constexpr ssize_t obj_count_sqrt = 7;

		Engine*  engine;
		ObjectId objects[obj_count_sqrt+obj_count_sqrt+1][obj_count_sqrt+obj_count_sqrt+1];
		ObjectId floor;
		bool active;


		void createGround() {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca->world_renderer;

			Renderer::NewObject o = { };
			o.model_locator = "ground.fma";
			o.position_xyz  = { 0.0f, -0.3f, 0.0f };
			o.scale_xyz     = { 1.0f, 1.0f, 1.0f };

			floor = wr.createObject(o);
		}


		void createTestObjects() {
			using s_object_id_e = std::make_signed_t<object_id_e>;

			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca->world_renderer;

			Renderer::NewObject o = { };
			float     dist     = 1.0f;
			glm::vec3 dir      = { 0.0f, glm::radians(20.0f), 0.0f };
			wr.setViewRotation(dir);
			wr.setViewPosition({ dist * std::sin(dir.x), 0.45f, dist * std::cos(dir.x) });
			o.model_locator = "test-model.fma";
			o.scale_xyz = { 0.6f, 0.6f, 0.6f };
			for(s_object_id_e x = -obj_count_sqrt; x < obj_count_sqrt; ++x)
			for(s_object_id_e y = -obj_count_sqrt; y < obj_count_sqrt; ++y) {
				float ox = x;
				float oz = y;
				o.position_xyz.x = ox;
				o.position_xyz.z = oz;
				o.position_xyz.y = std::sqrt((ox*ox) + (oz*oz)) * -0.4f / (obj_count_sqrt * obj_count_sqrt);
				size_t xi = x + obj_count_sqrt;
				size_t yi = y + obj_count_sqrt;
				assert((void*)(&objects[yi][xi]) < (void*)(objects + std::size(objects)));
				objects[yi][xi] = wr.createObject(o);
			}
		}


		explicit Loop(Engine& e):
			engine(&e),
			active(true)
		{
			createGround();
			createTestObjects();
		}


		void loop_processEvents(tickreg::delta_t, tickreg::delta_t) override {
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
		}


		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept override {
			return active? LoopState::eShouldContinue : LoopState::eShouldStop;
		}


		void loop_async_preRender(tickreg::delta_t, tickreg::delta_t) override { }


		void loop_async_postRender(tickreg::delta_t avg_delta, tickreg::delta_t last_delta) override {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca->world_renderer;

			avg_delta = std::min(avg_delta, last_delta);

			{ // Rotate the view
				auto  pos  = wr.getViewPosition();
				auto  dir  = wr.getViewRotation();
				float dist = 1.0f;
				float sin  = std::sin(dir.x);
				dir.x += glm::radians(15.0 * avg_delta);
				dir.y  = glm::radians(20.0f) + (glm::radians(20.0f) * sin);
				wr.setViewPosition({ dist * sin, pos.y, dist * std::cos(dir.x) });
				wr.setViewRotation(dir);
			}

			{ // Rotate the object at the center
				auto o = wr.modifyObject(objects[obj_count_sqrt][obj_count_sqrt]).value();
				o.direction_ypr.x -= glm::radians(71.0 * avg_delta);
			}
		}
	};

}}



int main() {
	auto logger = std::make_shared<spdlog::logger>(
		SKENGINE_NAME_CSTR,
		std::make_shared<spdlog::sinks::stdout_color_sink_mt>(spdlog::color_mode::automatic) );

	auto prefs = SKENGINE_NAME_NS_SHORT::EnginePreferences::default_prefs;
	prefs.init_present_extent   = { 700, 700 };
	prefs.max_render_extent     = { 0, 300 };
	prefs.asset_filename_prefix = "assets/";
	prefs.present_mode          = VK_PRESENT_MODE_MAILBOX_KHR;
	prefs.target_framerate      = 60.0;
	prefs.target_tickrate       = 60.0;
	prefs.fov_y                 = glm::radians(110.0f);

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

		auto loop = Loop(engine);

		engine.run(loop);

		logger->info("Successfully exiting the program.");
	} catch(posixfio::Errcode& e) {
		logger->error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		logger->error("Uncaught Vulkan error: {}", e.what());
	}
}
