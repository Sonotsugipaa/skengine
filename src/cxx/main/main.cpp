#include <numbers>
#include <random>

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
		static constexpr float   object_spacing    = 2.0f;
		static constexpr float   mouse_sensitivity = 0.25f;
		static constexpr float   movement_drag     = 8.0f;
		static constexpr float   movement_drag_mod = 0.2f;
		static constexpr float   movement_speed    = 1.1f * movement_drag;

		Engine* engine;
		std::unordered_set<SDL_KeyCode> pressedKeys;
		ObjectId  objects[obj_count_sqrt+obj_count_sqrt+1][obj_count_sqrt+obj_count_sqrt+1];
		ObjectId  floor;
		ObjectId  camLight;
		ObjectId  movingRayLight;
		ObjectId  lightGuide;
		glm::vec3 cameraSpeed;
		bool lightGuideVisible : 1;
		bool active            : 1;


		void setView() {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			glm::vec3 dir  = { 0.0f, glm::radians(20.0f), 0.0f };
			float     dist = object_spacing / 2.0f;
			wr.setViewRotation(dir);
			wr.setViewPosition({ dist * std::sin(dir.x), 0.45f, dist * std::cos(dir.x) });
		}


		void createGround() {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			Renderer::NewObject o = { };
			o.model_locator = "ground.fma";
			o.position_xyz  = { 0.0f, 0.0f, 0.0f };
			o.scale_xyz     = { 1.0f, 1.0f, 1.0f };

			floor = wr.createObject(o);
		}


		void createTestObjects() {
			using s_object_id_e = std::make_signed_t<object_id_e>;

			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();


			auto rng   = std::minstd_rand(size_t(this));
			auto disti = std::uniform_int_distribution<uint8_t>(0, 3);
			auto distf = std::uniform_real_distribution(0.0f, 2.0f * std::numbers::pi_v<float>);
			Renderer::NewObject o = { };
			o.scale_xyz = { 1.0f, 1.0f, 1.0f };
			for(s_object_id_e x = -obj_count_sqrt; x <= obj_count_sqrt; ++x)
			for(s_object_id_e y = -obj_count_sqrt; y <= obj_count_sqrt; ++y) {
				if(x == 0 && y == 0) [[unlikely]] {
					o.model_locator = "car.fma";
					o.position_xyz.y = 0.0f;
				} else switch(disti(rng)) {
					case 0:
						o.model_locator  = "gold-bars.fma";
						o.position_xyz.y = 0.0f;
						break;
					case 1:
						o.model_locator  = "car.fma";
						o.position_xyz.y = 0.0f;
						break;
					case 2:
						o.model_locator  = "test-model.fma";
						o.position_xyz.y = 0.5f;
						break;
					default: std::unreachable(); abort();
				}
				o.direction_ypr = { distf(rng), 0.0f, 0.0f };
				float ox = x * object_spacing;
				float oz = y * object_spacing;
				o.position_xyz.x = ox;
				o.position_xyz.z = oz;
				size_t xi = x + obj_count_sqrt;
				size_t yi = y + obj_count_sqrt;
				assert((void*)(&objects[yi][xi]) < (void*)(objects + std::size(objects)));
				objects[yi][xi] = wr.createObject(o);
			}
		}


		void createLights() {
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca.getWorldRenderer();

			WorldRenderer::NewRayLight rl = { };
			rl.intensity = 0.4f;
			rl.direction = { 1.8f, -0.2f, 0.0f };
			movingRayLight = wr.createRayLight(rl);

			WorldRenderer::NewPointLight pl = { };
			pl.intensity = 0.6f;
			pl.falloffExponent = 1.0f;
			pl.position = { 0.4f, 1.0f, 0.6f };
			camLight = wr.createPointLight(pl);

			Renderer::NewObject o = { };
			o.scale_xyz     = { 0.2f, 0.2f, 0.2f };
			o.model_locator = "gold-bars.fma";
			o.hidden        = true;
			lightGuide = wr.createObject(o);
		}


		explicit Loop(Engine& e):
			engine(&e),
			cameraSpeed { },
			lightGuideVisible(false),
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

			auto delta_integral = delta * delta / tickreg::delta_t(2.0);

			struct ResizeEvent {
				uint32_t width;
				uint32_t height;
				bool triggered = false;
			} resize_event;

			glm::vec2 rotate_camera = { };

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
						case SDL_WINDOWEVENT_FOCUS_LOST:
							pressedKeys.clear();
							break;
					} break;
					case SDL_EventType::SDL_KEYDOWN:
					switch(ev.key.keysym.sym) {
						case SDLK_LCTRL:
							SDL_SetRelativeMouseMode(mouse_rel_mode? SDL_FALSE : SDL_TRUE);
							mouse_rel_mode = ! mouse_rel_mode;
							break;
						default:
							pressedKeys.insert(SDL_KeyCode(ev.key.keysym.sym));
							break;
					} break;
					case SDL_EventType::SDL_KEYUP:
					switch(ev.key.keysym.sym) {
						default:
							pressedKeys.erase(SDL_KeyCode(ev.key.keysym.sym));
							break;
					} break;
				}

				if(mouse_rel_mode)
				switch(ev.type) {
					case SDL_EventType::SDL_MOUSEMOTION:
						rotate_camera.x -= glm::radians<float>(ev.motion.xrel) * mouse_sensitivity / delta;
						rotate_camera.y += glm::radians<float>(ev.motion.yrel) * mouse_sensitivity / delta;
						break;
				}
			}

			if(resize_event.triggered) ca.setPresentExtent({ resize_event.width, resize_event.height });

			{ // Rotate the camera
				constexpr auto pi_2 = std::numbers::pi_v<float> / 2.0f;
				auto dir = wr.getViewRotation();
				dir.x += rotate_camera.x * delta;
				dir.y += rotate_camera.y * delta;
				dir.y  = std::clamp(dir.y, -pi_2, +pi_2);
				wr.setViewRotation(dir);
			}

			glm::vec3 camera_pulse = [&]() {
				glm::vec3 r = { 0.0f, 0.0f, 0.0f };
				bool zero_mov = true;

				if(pressedKeys.contains(SDLK_w)) [[unlikely]] { zero_mov = false; r.z += -1.0f; }
				if(pressedKeys.contains(SDLK_s)) [[unlikely]] { zero_mov = false; r.z += +1.0f; }
				if(pressedKeys.contains(SDLK_a)) [[unlikely]] { zero_mov = false; r.x += -1.0f; }
				if(pressedKeys.contains(SDLK_d)) [[unlikely]] { zero_mov = false; r.x += +1.0f; }

				if(! zero_mov) {
					// Vertical movement only in relative mouse mode
					if(! mouse_rel_mode) {
						r.z = - r.z;
						std::swap(r.y, r.z);
					}

					auto&     view_transf4    = wr.getViewTransf();
					glm::mat3 view_transf     = view_transf4;
					glm::mat3 view_transf_inv = glm::inverse(view_transf);

					r  = view_transf_inv * r * movement_speed;
				}

				float drag = pressedKeys.contains(SDLK_LSHIFT)? movement_drag_mod : movement_drag;
				r -= cameraSpeed * drag;

				return r;
			} ();

			wr.setViewPosition(
				wr.getViewPosition()
				+ (cameraSpeed  * float(delta))
				+ (camera_pulse * float(delta_integral) ) );

			cameraSpeed += camera_pulse * float(delta);
		}


		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept override {
			return active? LoopState::eShouldContinue : LoopState::eShouldStop;
		}


		void loop_async_preRender(tickreg::delta_t, tickreg::delta_t) override { }


		void loop_async_postRender(tickreg::delta_t avg_delta, tickreg::delta_t last_delta) override {
			auto  ca  = engine->getConcurrentAccess();
			auto& wr  = ca.getWorldRenderer();
			auto& pos = wr.getViewPosition();

			avg_delta = std::min(avg_delta, last_delta);

			{ // Rotate the object at the center
				auto o = wr.modifyObject(objects[obj_count_sqrt][obj_count_sqrt]).value();
				o.direction_ypr.x -= glm::radians(71.0 * avg_delta);
			}

			if(! pressedKeys.contains(SDLK_SPACE)) {
				if(lightGuideVisible) {
					wr.modifyObject(lightGuide)->hidden = true;
					lightGuideVisible = false;
				}
			} else {
				// Rotate the moving light
				auto* rl = &wr.modifyRayLight(movingRayLight);
				glm::vec3 light_pos = [&]() {
					glm::mat3 view = wr.getViewTransf();
					return glm::transpose(view) * glm::vec3 { 0.0f, 0.0f, -1.0f };
				} ();
				rl->direction = light_pos;

				// Handle the light guide
				if(! lightGuideVisible) {
					auto o = wr.modifyObject(lightGuide).value();
					o.position_xyz = pos + light_pos;
					o.hidden       = false;
					lightGuideVisible = true;
				} else {
					wr.modifyObject(lightGuide)->position_xyz = pos + light_pos;
				}

				// Make the camera light follow the camera
				auto* pl = &wr.modifyPointLight(camLight);
				pl->position = pos + light_pos;
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
	prefs.max_render_extent     = { 0, 400 };
	prefs.asset_filename_prefix = "assets/";
	prefs.present_mode          = VK_PRESENT_MODE_MAILBOX_KHR;
	prefs.target_framerate      = 60.0f;
	prefs.target_tickrate       = 60.0f;
	prefs.fov_y                 = glm::radians(110.0f);
	prefs.shade_step_count      = 12;
	prefs.shade_step_smoothness = 0.7f;
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
