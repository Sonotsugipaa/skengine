#include <numbers>
#include <random>

#include <engine/engine.hpp>
#include <ui/ui.hpp>

#include <spdlog/spdlog.h>

#include <posixfio_tl.hpp>

#include <vk-util/error.hpp>

#include <glm/trigonometric.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>



inline namespace main_ns {
namespace {

	using namespace SKENGINE_NAME_NS;


	const EnginePreferences engine_preferences = []() {
		auto prefs = EnginePreferences::default_prefs;
		prefs.init_present_extent   = { 700, 700 };
		prefs.max_render_extent     = { 0, 400 };
		prefs.asset_filename_prefix = "assets/";
		prefs.present_mode          = VK_PRESENT_MODE_MAILBOX_KHR;
		prefs.target_framerate      = 60.0f;
		prefs.target_tickrate       = 60.0f;
		prefs.fov_y                 = glm::radians(80.0f);
		prefs.shade_step_count      = 8;
		prefs.shade_step_smoothness = 0.7f;
		prefs.shade_step_exponent   = 4.0f;
		return prefs;
	} ();


	std::vector<std::string> readObjectNameList(posixfio::FileView file) {
		auto fileBuf = posixfio::ArrayInputBuffer<>(file);

		std::string strBuf;
		auto rdln = [&]() {
			strBuf.clear();
			strBuf.reserve(64);
			char c;
			bool eol = false;
			ssize_t rd = fileBuf.read(&c, 1);
			while(rd > 0) {
				if(c == '\n') [[unlikely]] {
					eol = true;
					rd = 0;
				} else {
					strBuf.push_back(c);
					rd = fileBuf.read(&c, 1);
				}
			}
			return eol;
		};

		std::vector<std::string> r;
		bool eof    = ! rdln();
		bool nEmpty = ! strBuf.empty();
		while((! eof) || nEmpty) {
			if(nEmpty) {
				r.push_back(std::move(strBuf));
			}
			eof    = ! rdln();
			nEmpty = ! strBuf.empty();
		}
		return r;
	}


	class Loop : public LoopInterface {
	public:
		static constexpr ssize_t obj_count_sqrt    = 5 * 2;
		static constexpr float   object_spacing    = 2.0f;
		static constexpr float   mouse_sensitivity = 0.25f;
		static constexpr float   movement_drag     = 8.0f;
		static constexpr float   movement_drag_mod = 0.2f;
		static constexpr float   movement_speed    = 1.1f * movement_drag;

		Engine* engine;
		std::mutex inputMutex;
		std::unordered_set<SDL_KeyCode> pressedKeys;
		ObjectId  objects[obj_count_sqrt+1][obj_count_sqrt+1];
		ObjectId  world;
		ObjectId  camLight;
		ObjectId  movingRayLight;
		ObjectId  lightGuide;
		glm::vec3 cameraSpeed;
		bool lightGuideVisible : 1;
		bool active            : 1;


		void setView(skengine::WorldRenderer& wr) {
			glm::vec3 dir  = { 0.0f, glm::radians(20.0f), 0.0f };
			float     dist = object_spacing / 2.0f;
			wr.setViewRotation(dir);
			wr.setViewPosition({ dist * std::sin(dir.x), 0.45f, dist * std::cos(dir.x) });
		}


		void rotateObject(skengine::WorldRenderer& wr, std::ranlux48& rng, float mul, ObjectId id) {
			auto dist = std::uniform_real_distribution(0.0f, 0.3f * std::numbers::pi_v<float>);
			auto obj = wr.modifyObject(id).value();
			obj.direction_ypr.r += mul * dist(rng);
		}


		void createGround(skengine::WorldRenderer& wr) {
			Renderer::NewObject o = { };
			o.model_locator = "world.fma";
			o.position_xyz  = { 0.0f, 0.0f, 0.0f };
			o.scale_xyz     = { 1.0f, 1.0f, 1.0f };

			world = wr.createObject(o);
		}


		void createTestObjects(skengine::WorldRenderer& wr) {
			using s_object_id_e = std::make_signed_t<object_id_e>;

			constexpr ssize_t obj_count_sqrt_half = obj_count_sqrt / 2;

			auto createListedObjects = [&](const std::vector<std::string>& nameList) {
				auto rng = std::minstd_rand(size_t(this));
				auto disti = std::uniform_int_distribution<uint8_t>(0, nameList.size() - 1);
				auto distf = std::uniform_real_distribution(0.0f, 2.0f * std::numbers::pi_v<float>);
				Renderer::NewObject o = { };
				o.scale_xyz = { 1.0f, 1.0f, 1.0f };
				for(s_object_id_e x = -obj_count_sqrt_half; x <= obj_count_sqrt_half; ++x)
				for(s_object_id_e y = -obj_count_sqrt_half; y <= obj_count_sqrt_half; ++y) {
					if(x == 0 && y == 0) [[unlikely]] {
						o.model_locator = "car.fma";
						o.position_xyz.y = 0.0f;
					} else {
						o.model_locator  = nameList[disti(rng)];
						o.position_xyz.y = 0.0f;
					}
					o.direction_ypr = { distf(rng), 0.0f, 0.0f };
					float ox = x * object_spacing;
					float oz = y * object_spacing;
					o.position_xyz.x = ox;
					o.position_xyz.z = oz;
					size_t xi = x + obj_count_sqrt_half;
					size_t yi = y + obj_count_sqrt_half;
					assert((void*)(&objects[yi][xi]) < (void*)(objects + std::size(objects)));
					objects[yi][xi] = wr.createObject(o);
				}
			};

			try {
				auto nameList = readObjectNameList(posixfio::File::open("assets/object-list.txt", O_RDONLY));
				if(nameList.empty()) {
					engine->logger().info("\"assets/object-list.txt\" is empty");
					throw std::runtime_error("empty object list");
				} else {
					std::unordered_map<std::string_view, size_t> names;
					for(auto& nm : nameList) ++ names[nm];
					engine->logger().info("Objects:");
					for(auto& nm : names) engine->logger().info("- {}x {}", nm.second, nm.first);
					createListedObjects(nameList);
				}
			} catch(posixfio::FileError& err) {
				engine->logger().error("File \"assets/object-list.txt\" not found");
			}
		}


		void createLights(skengine::WorldRenderer& wr) {
			WorldRenderer::NewRayLight rl = { };
			rl.intensity = 1.1f;
			rl.direction = { 1.8f, -0.2f, 0.0f };
			rl.color     = { 0.7f, 0.91f, 1.0f };
			movingRayLight = wr.createRayLight(rl);

			WorldRenderer::NewPointLight pl = { };
			pl.intensity = 4.0f;
			pl.falloffExponent = 1.0f;
			pl.position = { 0.4f, 1.0f, 0.6f };
			pl.color    = { 1.0f, 0.0f, 0.34f };
			camLight = wr.createPointLight(pl);

			Renderer::NewObject o = { };
			o.scale_xyz     = { 0.05f, 0.05f, 0.05f };
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
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca->getWorldRenderer();

			setView(wr);
			createGround(wr);
			createTestObjects(wr);
			createLights(wr);
		}


		void loop_processEvents(tickreg::delta_t delta, tickreg::delta_t) override {
			SDL_Event ev;

			bool mouse_rel_mode = SDL_GetRelativeMouseMode();
			bool request_ca     = false;

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
							request_ca = true;
							break;
						case SDL_WINDOWEVENT_FOCUS_LOST:
							inputMutex.lock();
							pressedKeys.clear();
							inputMutex.unlock();
							break;
					} break;
					case SDL_EventType::SDL_KEYDOWN:
					switch(ev.key.keysym.sym) {
						case SDLK_LCTRL:
							SDL_SetRelativeMouseMode(mouse_rel_mode? SDL_FALSE : SDL_TRUE);
							mouse_rel_mode = ! mouse_rel_mode;
							break;
						default:
							inputMutex.lock();
							pressedKeys.insert(SDL_KeyCode(ev.key.keysym.sym));
							inputMutex.unlock();
							break;
					} break;
					case SDL_EventType::SDL_KEYUP:
					switch(ev.key.keysym.sym) {
						default:
							inputMutex.lock();
							pressedKeys.erase(SDL_KeyCode(ev.key.keysym.sym));
							inputMutex.unlock();
							break;
					} break;
				}

				if(mouse_rel_mode) {
					using float_t = tickreg::delta_t;
					auto mouse_mov_magnitude = float_t(mouse_sensitivity) / delta;
					switch(ev.type) {
						default: break;
						case SDL_EventType::SDL_MOUSEMOTION:
							rotate_camera.x -= glm::radians<float_t>(ev.motion.xrel) * mouse_mov_magnitude;
							rotate_camera.y += glm::radians<float_t>(ev.motion.yrel) * mouse_mov_magnitude;
							request_ca = true;
							break;
					}
				}
			}

			if(request_ca) {
				auto  ca = engine->getConcurrentAccess();
				auto& wr = ca->getWorldRenderer();

				if(resize_event.triggered) {
					ca->setPresentExtent({ resize_event.width, resize_event.height });
				}

				if(rotate_camera != glm::vec2 { }) { // Rotate the camera
					constexpr auto pi_2 = std::numbers::pi_v<float> / 2.0f;
					auto dir = wr.getViewRotation();
					dir.x += rotate_camera.x * delta;
					dir.y += rotate_camera.y * delta;
					dir.y  = std::clamp(dir.y, -pi_2, +pi_2);
					wr.setViewRotation(dir, false);
				}
			}
		}


		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept override {
			return active? LoopState::eShouldContinue : LoopState::eShouldStop;
		}


		void loop_async_preRender(ConcurrentAccess, tickreg::delta_t, tickreg::delta_t) override { }


		void loop_async_postRender(ConcurrentAccess ca, tickreg::delta_t delta, tickreg::delta_t /*delta_last*/) override {
			auto& wr = ca.getWorldRenderer();

			auto delta_integral = delta * delta / tickreg::delta_t(2.0);
			auto rng = std::ranlux48(ca.currentFrameNumber() + (ca.currentFrameNumber() == 0));

			{ // Rotate the object at the center
				constexpr ssize_t obj_count_sqrt_half = obj_count_sqrt / 2;
				auto o = wr.modifyObject(objects[obj_count_sqrt_half][obj_count_sqrt_half]).value();
				o.direction_ypr.x -= glm::radians(71.0 * delta);
			}

			{ // Randomly rotate all the objects
				constexpr size_t obj_count_sqrt_half = obj_count_sqrt / 2;
				constexpr size_t actual_obj_count_sqrt = obj_count_sqrt+1;
				for(size_t x = 0; x < actual_obj_count_sqrt; ++x)
				for(size_t y = 0; y < actual_obj_count_sqrt; ++y) {
					if(x == obj_count_sqrt_half && y == obj_count_sqrt_half) [[unlikely]] /* Exclude the center object */ continue;
					rotateObject(wr, rng, delta, objects[x][y]);
				}
			}

			inputMutex.lock();

			glm::vec3 pos;
			{ // Move the camera
				bool mouse_rel_mode = SDL_GetRelativeMouseMode();

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
						glm::mat3 view_transf_inv = glm::transpose(view_transf);

						r = view_transf_inv * r * movement_speed;
					}

					float drag = pressedKeys.contains(SDLK_LSHIFT)? movement_drag_mod : movement_drag;
					r -= cameraSpeed * drag;

					return r;
				} ();

				pos =
					wr.getViewPosition()
					+ (cameraSpeed  * float(delta))
					+ (camera_pulse * float(delta_integral) );
				wr.setViewPosition(pos);

				cameraSpeed += camera_pulse * float(delta);
			}

			bool move_light_key_pressed = pressedKeys.contains(SDLK_SPACE);
			if(! move_light_key_pressed) {
				if(lightGuideVisible) {
					wr.modifyObject(lightGuide)->hidden = true;
					lightGuideVisible = false;
				}
			} else {
				// Rotate the moving light
				auto* rl = &wr.modifyRayLight(movingRayLight);
				glm::vec3 light_pos = [&]() {
					glm::mat3 view = wr.getViewTransf();
					return glm::transpose(view) * glm::vec3 { 0.0f, 0.0f, -0.2f };
				} ();
				rl->direction = light_pos;

				{ // Handle the light guide
					auto o = wr.modifyObject(lightGuide).value();
					o.position_xyz    = pos + light_pos;
					o.hidden          = false;
					lightGuideVisible = true;
				}

				// Make the camera light follow the camera
				{
					auto* pl = &wr.modifyPointLight(camLight);
					pl->position = pos + light_pos;
				}
			}

			inputMutex.unlock();
		}
	};

}}



int main() {
	auto logger = std::make_shared<spdlog::logger>(
		SKENGINE_NAME_CSTR,
		std::make_shared<spdlog::sinks::stdout_color_sink_mt>(spdlog::color_mode::automatic) );
	logger->set_pattern("[%^" SKENGINE_NAME_CSTR " %L%$] %v");

	auto prefs = engine_preferences;

	#ifdef NDEBUG
		spdlog::set_level(spdlog::level::info);
		logger->set_level(spdlog::level::info);
	#else
		spdlog::set_level(spdlog::level::debug);
		logger->set_level(spdlog::level::debug);
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
			std::shared_ptr<SKENGINE_NAME_NS_SHORT::BasicShaderCache>(shader_cache),
			logger );

		auto loop = Loop(engine);

		engine.run(loop);

		logger->info("Successfully exiting the program.");
	} catch(posixfio::Errcode& e) {
		logger->error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		logger->error("Uncaught Vulkan error: {}", e.what());
	}
}
