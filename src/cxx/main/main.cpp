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
		static constexpr ssize_t obj_count_sqrt    = 7;
		static constexpr float   object_spacing    = 0.85f;
		static constexpr float   mouse_sensitivity = 2.0f;

		Engine*   engine;
		ObjectId  objects[obj_count_sqrt+obj_count_sqrt+1][obj_count_sqrt+obj_count_sqrt+1];
		ObjectId  floor;
		ObjectId  fixedLights[2];
		ObjectId  camLight;
		ObjectId  movingRayLight;
		float     movingRayLightYaw;
		glm::vec3 camAccel;
		bool active;


		void setView() {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			glm::vec3 dir = { 0.0f, glm::radians(20.0f), 0.0f };
			wr.setViewRotation(dir);
			wr.setViewPosition({ object_spacing * std::sin(dir.x), 0.45f, object_spacing * std::cos(dir.x) });
		}


		void createGround() {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			Renderer::NewObject o = { };
			o.model_locator = "ground.fma";
			o.position_xyz  = { 0.0f, -0.3f, 0.0f };
			o.scale_xyz     = { 1.0f, 1.0f, 1.0f };

			floor = wr.createObject(o);
		}


		void createTestObjects() {
			using s_object_id_e = std::make_signed_t<object_id_e>;

			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			Renderer::NewObject o = { };
			o.model_locator = "test-model.fma";
			o.scale_xyz = { 0.6f, 0.6f, 0.6f };
			for(s_object_id_e x = -obj_count_sqrt; x < obj_count_sqrt; ++x)
			for(s_object_id_e y = -obj_count_sqrt; y < obj_count_sqrt; ++y) {
				float ox = x * 1.3f;
				float oz = y * 1.3f;
				o.position_xyz.x = ox;
				o.position_xyz.z = oz;
				o.position_xyz.y = std::sqrt((ox*ox) + (oz*oz)) * -0.4f / (obj_count_sqrt * obj_count_sqrt);
				size_t xi = x + obj_count_sqrt;
				size_t yi = y + obj_count_sqrt;
				assert((void*)(&objects[yi][xi]) < (void*)(objects + std::size(objects)));
				objects[yi][xi] = wr.createObject(o);
			}
		}


		glm::vec3 computeMovingLightDir(){
			constexpr auto period = std::numbers::pi_v<float> * 2.0f;
			movingRayLightYaw -= period * std::floor(movingRayLightYaw / period);
			auto r = glm::vec3 { std::sin(movingRayLightYaw), -0.2f, std::cos(movingRayLightYaw) };
			return r;
		}


		void createLights() {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			auto& view_pos = wr.getViewPosition();

			WorldRenderer::NewRayLight rl = { };
			rl.intensity = 0.4f;
			rl.direction = { +1.0f, -0.0f, -2.0f };
			fixedLights[0] = wr.createRayLight(rl);
			rl.intensity = 0.2f;
			rl.direction = { -1.0f, -2.0f, +5.0f };
			fixedLights[1] = wr.createRayLight(rl);
			rl.intensity = 0.4f;
			rl.direction = glm::vec4(computeMovingLightDir(), 1.0f);
			movingRayLight = wr.createRayLight(rl);
			WorldRenderer::NewPointLight pl = { };
			pl.intensity = 1.0f;
			pl.falloffExponent = 4.0f;
			pl.position = { view_pos.x, 0.0f, view_pos.z };
			camLight = wr.createPointLight(pl);
			movingRayLightYaw = 0.0f;
		}


		explicit Loop(Engine& e):
			engine(&e),
			camAccel { },
			active(true)
		{
			setView();
			createGround();
			createTestObjects();
			createLights();
		}


		void loop_processEvents(tickreg::delta_t, tickreg::delta_t delta) override {
			SDL_Event ev;

			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			bool mouse_rel_mode = SDL_GetRelativeMouseMode();

			struct ResizeEvent {
				uint32_t width;
				uint32_t height;
				bool triggered = false;
			} resize_event;

			glm::vec2 rotate_camera = { };
			glm::vec3 move_camera   = { };

			// Consume events, but only the last one of each type; discard the rest
			while(1 == SDL_PollEvent(&ev)) {
				switch(ev.type) {
					case SDL_EventType::SDL_QUIT: {
						active = false;
					} return;
					case SDL_EventType::SDL_WINDOWEVENT:
					switch(ev.window.event) {
						case SDL_WINDOWEVENT_RESIZED:
							resize_event = { uint32_t(ev.window.data1), uint32_t(ev.window.data2), true };
							break;
					} break;
					case SDL_EventType::SDL_KEYDOWN:
					switch(ev.key.keysym.sym) {
						case SDLK_LCTRL:
							SDL_SetRelativeMouseMode(mouse_rel_mode? SDL_FALSE : SDL_TRUE);
							mouse_rel_mode = ! mouse_rel_mode;
							break;
					}
				}

				if(mouse_rel_mode)
				switch(ev.type) {
					case SDL_EventType::SDL_MOUSEMOTION:
						rotate_camera.x -= glm::radians<float>(ev.motion.xrel) * mouse_sensitivity;
						rotate_camera.y += glm::radians<float>(ev.motion.yrel) * mouse_sensitivity;
						break;
				}
			}

			if(resize_event.triggered) ca.setPresentExtent({ resize_event.width, resize_event.height });

			if(rotate_camera.x != 0.0f || rotate_camera.y != 0.0f) {
				constexpr auto pi_2 = std::numbers::pi_v<float> / 2.0f;
				auto dir = wr.getViewRotation();
				dir.x += rotate_camera.x * delta;
				dir.y += rotate_camera.y * delta;
				dir.y  = std::clamp(dir.y, -pi_2, +pi_2);
				wr.setViewRotation(dir);
			}

			if(move_camera.x != 0.0f || move_camera.y != 0.0f || move_camera.z != 0.0f) {

			}
		}


		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept override {
			return active? LoopState::eShouldContinue : LoopState::eShouldStop;
		}


		void loop_async_preRender(tickreg::delta_t, tickreg::delta_t) override { }


		void loop_async_postRender(tickreg::delta_t avg_delta, tickreg::delta_t last_delta) override {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			avg_delta = std::min(avg_delta, last_delta);
			auto& pos = wr.getViewPosition();

			{ // Rotate the object at the center
				auto o = wr.modifyObject(objects[obj_count_sqrt][obj_count_sqrt]).value();
				o.direction_ypr.x -= glm::radians(71.0 * avg_delta);
			}

			{ // Rotate the moving light
				movingRayLightYaw += glm::radians(120.0 * avg_delta);
				auto& l = wr.modifyRayLight(movingRayLight);
				l.direction = glm::vec4(computeMovingLightDir(), 1.0f);
			}

			{ // Make the camera light follow the camera
				auto& l = wr.modifyPointLight(camLight);
				l.position = glm::vec4(pos.z, pos.y, -pos.x, 1.0f);
				l.position.y = 1.0f;
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
	prefs.target_framerate      = 60.0f;
	prefs.target_tickrate       = 60.0f;
	prefs.fov_y                 = glm::radians(110.0f);
	prefs.shade_step_count      = 12;
	prefs.shade_step_smoothness = 0.75f;
	prefs.shade_step_exponent   = 4.0f;

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
