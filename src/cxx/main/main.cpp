#include <numbers>
#include <random>

#include <engine-util/basic_asset_source.hpp>

#include <spdlog/spdlog.h>

#include <posixfio_tl.hpp>

#include <vk-util/error.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>

#include "util.inl.hpp"



inline namespace main_ns {
namespace {

	using namespace SKENGINE_NAME_NS;


	const EnginePreferences engine_preferences = []() {
		auto prefs = EnginePreferences::default_prefs;
		prefs.init_present_extent   = { 100, 100 };
		prefs.max_render_extent     = { 500, 500 };
		prefs.asset_filename_prefix = "assets/";
		prefs.present_mode          = VK_PRESENT_MODE_MAILBOX_KHR;
		prefs.target_framerate      = 60.0f;
		prefs.target_tickrate       = 60.0f;
		prefs.fov_y                 = glm::radians(80.0f);
		prefs.shade_step_count      = 4;
		prefs.shade_step_smoothness = 4.0f;
		prefs.shade_step_exponent   = 4.0f;
		prefs.font_location         = "assets/font.otf";
		prefs.wait_for_gframe       = true;
		prefs.framerate_samples     = 4;
		return prefs;
	} ();


	class Loop : public LoopInterface {
	public:
		static constexpr ssize_t obj_count_sqrt     = 20;
		static constexpr float   object_spacing     = 50.0f / float(obj_count_sqrt + 2);
		static constexpr float   mouse_sensitivity  = 0.25f;
		static constexpr float   movement_speed     = 1.1f;
		static constexpr float   movement_accel     = 2.0f;
		static constexpr float   movement_drag      = 8.0f * movement_accel;
		static constexpr float   movement_drag_mod  = 0.2f * movement_accel;
		static constexpr float   movement_pulse     = movement_speed * movement_drag;
		static constexpr auto    cursor_offset      = glm::vec3 { 0.0f, 0.0f, -0.2f };
		static constexpr float   crosshair_size_px  = 40.0f;
		static constexpr float   crosshair_width_px = 3.0f;
		static_assert(obj_count_sqrt % 2 == 0);
		static_assert(obj_count_sqrt > 0);

		Engine* engine;
		std::mutex inputMutex;
		std::unordered_set<SDL_KeyCode> pressedKeys;
		std::vector<ObjectId> createdLights;
		std::weak_ptr<ui::BasicGrid> crosshairGrid;
		std::weak_ptr<ui::Lot> crosshairLot;
		std::weak_ptr<gui::BasicPolygon> crosshair;
		std::weak_ptr<gui::TextLine> speedGauge;
		std::weak_ptr<gui::TextLine> lightCounter;
		std::weak_ptr<gui::TextLine> fpsGauge;
		ObjectId  objects[obj_count_sqrt+1][obj_count_sqrt+1];
		ObjectId  spaceship;
		ObjectId  world;
		ObjectId  camLight;
		ObjectId  lightGuide;
		glm::vec3 cameraSpeed;
		bool lightGuideVisible : 1;
		bool active            : 1;
		bool lightCreated      : 1;


		void setView(skengine::WorldRenderer& wr) {
			glm::vec3 dir  = { 0.0f, glm::radians(20.0f), 0.0f };
			float     dist = object_spacing / 2.0f;
			wr.setViewRotation(dir);
			wr.setViewPosition({ dist * std::sin(dir.x), 0.45f, dist * std::cos(dir.x) });
			wr.setAmbientLight({ 0.06f, 0.06f, 0.06f });
		}


		void rotateObject(skengine::WorldRenderer& wr, std::ranlux48& rng, float mul, ObjectId id) {
			auto dist = std::uniform_real_distribution(-0.9f, +0.9f * std::numbers::pi_v<float>);
			auto obj = wr.modifyObject(id).value();
			obj.direction_ypr.r += mul * dist(rng);
		}


		void setCrosshairGridSize() {
			float ext[2]   = { float(engine->getPresentExtent().width), float(engine->getPresentExtent().height) };
			float size[2]  = { crosshair_size_px / ext[0], crosshair_size_px / ext[1] };
			float space[2] = { (1.0f - size[0]) / 2.0f, (1.0f - size[1]) / 2.0f };
			auto chGrid = crosshairGrid.lock();
			chGrid->setColumnSizes({ space[0], size[0] });
			chGrid->setRowSizes({ space[1], size[1] });
		}


		ShapeSet makeCrosshairShapeSet() {
			constexpr float pixelWidth = crosshair_width_px / crosshair_size_px;
			return makeCrossShapeSet(pixelWidth, pixelWidth, 0.1f, { 1.0f, 1.0f, 1.0f, 1.0f });
		}


		void createGround(skengine::WorldRenderer& wr) {
			Renderer::NewObject o = { };
			o.model_locator = "world.fma";
			o.position_xyz  = { 0.0f, 0.0f, 0.0f };
			o.scale_xyz     = { 1.0f, 1.0f, 1.0f };
			world = wr.createObject(o);
		}


		void createSpaceship(skengine::WorldRenderer& wr) {
			Renderer::NewObject o = { };
			o.model_locator = "spaceship.fma";
			o.position_xyz  = { 0.0f, 10.0f, 0.0f };
			o.scale_xyz     = { 1.0f, 1.0f, 1.0f };
			spaceship = wr.createObject(o);
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
				o.position_xyz.y = 0.0f;
				for(s_object_id_e x = -obj_count_sqrt_half; x <= obj_count_sqrt_half; ++x)
				for(s_object_id_e y = -obj_count_sqrt_half; y <= obj_count_sqrt_half; ++y) {
					o.model_locator = nameList[disti(rng)];
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
			WorldRenderer::NewPointLight pl = { };
			pl.intensity = 2.0f;
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


		void createGui(skengine::GuiManager& gui) {
			static constexpr auto textInfo0 = TextInfo {
				.alignment = TextAlignment::eLeftCenter,
				.fontSize = 20,
				.textSize = 0.05f };
			static constexpr auto textInfo1 = TextInfo {
				.alignment = TextAlignment::eRightCenter,
				.fontSize = textInfo0.fontSize,
				.textSize = textInfo0.textSize };
			auto& canvas = gui.canvas();

			crosshairGrid =
				canvas.createLot({ 0, 0 }, { 3, 3 }).second
				->setChildBasicGrid({ }, { }, { });
			setCrosshairGridSize();
			auto chGrid  = crosshairGrid.lock();
			crosshairLot = chGrid->createLot({ 1, 1 }, { 1, 1 }).second;
			auto chLot   = crosshairLot.lock();
			crosshair    = gui.createBasicShape(*chLot, makeCrosshairShapeSet(), true).second;

			auto textGridLot = canvas.createLot({ 0, 0 }, { 3, 3 }).second;
			auto& textGrid = * textGridLot->setChildBasicGrid({ }, { textInfo0.textSize }, { 1.0f });
			lightCounter = gui.createTextLine(*textGrid.createLot({ 0, 0 }, { 1, 1 }).second, 0.0f, textInfo0, "Lights: 0").second;
			speedGauge   = gui.createTextLine(*textGrid.createLot({ 1, 0 }, { 1, 1 }).second, 0.0f, textInfo0, "Speed: 0.00").second;
			fpsGauge     = gui.createTextLine(*textGrid.createLot({ 0, 0 }, { 1, 1 }).second, 0.0f, textInfo1, "Framerate: 0.00").second;
		}


		explicit Loop(Engine& e):
			engine(&e),
			cameraSpeed { },
			lightGuideVisible(false),
			active(true),
			lightCreated(false)
		{
			auto  ca = engine->getConcurrentAccess();
			auto& wr = ca->getWorldRenderer();
			auto  gui = ca->gui();

			setView(wr);
			createGround(wr);
			createSpaceship(wr);
			createTestObjects(wr);
			createLights(wr);
			createGui(gui);
		}


		~Loop() {
			crosshairGrid = { };
			crosshairLot = { };
			crosshair = { };
		}


		void loop_processEvents(tickreg::delta_t delta, tickreg::delta_t) override {
			SDL_Event ev;

			bool mouse_rel_mode  = SDL_GetRelativeMouseMode();
			bool request_ca      = false;
			bool do_create_light = false;
			bool destroy_light   = false;
			bool light_is_ray    = false;

			struct ResizeEvent {
				uint32_t width;
				uint32_t height;
				bool triggered = false;
			} resize_event;

			glm::vec2 rotate_camera = { };

			auto imLock = std::unique_lock(inputMutex);

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
							pressedKeys.clear();
							break;
					} break;
					case SDL_EventType::SDL_KEYDOWN:
					if(! ev.key.repeat) switch(ev.key.keysym.sym) {
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

			{ // Handle light creation / reset within ticks, not frames
				auto found = pressedKeys.find(SDLK_f);
				if(found != pressedKeys.end()) [[unlikely]] {
					do_create_light = true;
					request_ca      = true;
				} else
				if((found = pressedKeys.find(SDLK_l)) != pressedKeys.end()) [[unlikely]] {
					do_create_light = true;
					light_is_ray    = true;
					request_ca      = true;
				} else {
					lightCreated = false;
					found = pressedKeys.find(SDLK_c);
					if(found != pressedKeys.end()) [[unlikely]] {
						destroy_light = true;
						request_ca    = true;
					}
				}
			}

			imLock.unlock();

			if(request_ca) {
				auto  ca = engine->getConcurrentAccess();
				auto& wr = ca->getWorldRenderer();

				if(resize_event.triggered) {
					ca->setPresentExtent({ resize_event.width, resize_event.height });
					setCrosshairGridSize();
				}

				if(rotate_camera != glm::vec2 { }) { // Rotate the camera
					constexpr auto pi_2 = std::numbers::pi_v<float> / 2.0f;
					auto dir = wr.getViewRotation();
					dir.x += rotate_camera.x * delta;
					dir.y += rotate_camera.y * delta;
					dir.y  = std::clamp(dir.y, -pi_2, +pi_2);
					wr.setViewRotation(dir, false);
				}

				if(do_create_light) [[unlikely]] {
					glm::mat3 iview = glm::inverse(wr.getViewTransf());
					auto      dir   = iview * cursor_offset;
					auto      pos   = wr.getViewPosition() + dir;
					if(lightCreated) [[likely]] {
						assert(! createdLights.empty());
						auto& lightId = createdLights.back();
						if(light_is_ray) {
							auto& light = wr.modifyRayLight(lightId);
							light.direction = dir;
						} else {
							auto& light = wr.modifyPointLight(lightId);
							light.position = pos;
						}
					} else {
						if(light_is_ray) {
							WorldRenderer::NewRayLight rl = { };
							rl.intensity    = 0.1f;
							rl.aoaThreshold = 2.0f;
							rl.direction    = dir;
							rl.color        = { 0.52f, 0.80f, 0.92f };
							createdLights.push_back(wr.createRayLight(rl));
						} else {
							WorldRenderer::NewPointLight pl = { };
							pl.intensity = 0.5f;
							pl.falloffExponent = 1.0f;
							pl.position = pos;
							pl.color    = glm::vec3 { 0.1f, 0.1f, 1.0f };
							createdLights.push_back(wr.createPointLight(pl));
						}
						lightCounter.lock()->setText(fmt::format("Lights: {}", createdLights.size()));
						engine->logger().info("Creating light {}", object_id_e(createdLights.back()));
						lightCreated = true;
					}
				}

				if(destroy_light) [[unlikely]] {
					if(! createdLights.empty()) {
						for(auto& light : createdLights) wr.removeLight(light);
						engine->logger().info("Destroyed lights");
						createdLights.clear();
						lightCounter.lock()->setText(fmt::format("Lights: {}", createdLights.size()));
					}
				}
			}
		}


		SKENGINE_NAME_NS_SHORT::LoopInterface::LoopState loop_pollState() const noexcept override {
			return active? LoopState::eShouldContinue : LoopState::eShouldStop;
		}


		void loop_async_preRender(ConcurrentAccess, tickreg::delta_t, tickreg::delta_t) override {
			{ // Update the GUI
				speedGauge.lock()->setText(fmt::format("Speed: {:.2f}", glm::length(cameraSpeed)));
			}
		}


		void loop_async_postRender(ConcurrentAccess ca, tickreg::delta_t delta, tickreg::delta_t /*delta_last*/) override {
			auto& wr = ca.getWorldRenderer();

			auto delta_integral = delta * delta / tickreg::delta_t(2.0);
			auto rng = std::ranlux48(size_t(this));

			auto inputLock = std::unique_lock(inputMutex);
			bool move_light_key_pressed, move_fwd, move_bkw, move_lft, move_rgt, move_drag_mod; {
				move_light_key_pressed = pressedKeys.contains(SDLK_SPACE);
				move_fwd = pressedKeys.contains(SDLK_w);
				move_bkw = pressedKeys.contains(SDLK_s);
				move_lft = pressedKeys.contains(SDLK_a);
				move_rgt = pressedKeys.contains(SDLK_d);
				move_drag_mod = pressedKeys.contains(SDLK_LSHIFT);
			}

			{ // Randomly rotate all the objects
				constexpr size_t actual_obj_count_sqrt = obj_count_sqrt+1;
				for(size_t x = 0; x < actual_obj_count_sqrt; ++x)
				for(size_t y = 0; y < actual_obj_count_sqrt; ++y) {
					rotateObject(wr, rng, delta, objects[x][y]);
				}
			}

			glm::vec3 pos;
			{ // Move the camera
				bool mouse_rel_mode = SDL_GetRelativeMouseMode();

				glm::vec3 camera_pulse = [&]() {
					glm::vec3 r = { 0.0f, 0.0f, 0.0f };
					bool zero_mov = true;

					if(move_fwd) [[unlikely]] { zero_mov = false; r.z += -1.0f; }
					if(move_bkw) [[unlikely]] { zero_mov = false; r.z += +1.0f; }
					if(move_lft) [[unlikely]] { zero_mov = false; r.x += -1.0f; }
					if(move_rgt) [[unlikely]] { zero_mov = false; r.x += +1.0f; }

					if(! zero_mov) {
						// Vertical movement only in relative mouse mode
						if(! mouse_rel_mode) {
							r.z = - r.z;
							std::swap(r.y, r.z);
						}

						auto&     view_transf4    = wr.getViewTransf();
						glm::mat3 view_transf     = view_transf4;
						glm::mat3 view_transf_inv = glm::transpose(view_transf);

						r = view_transf_inv * r * movement_pulse;
					}

					float drag = move_drag_mod? movement_drag_mod : movement_drag;
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

			if(! move_light_key_pressed) {
				if(lightGuideVisible) {
					wr.modifyObject(lightGuide)->hidden = true;
					lightGuideVisible = false;
				}
			} else {
				// Rotate the moving light
				glm::vec3 light_pos = [&]() {
					glm::mat3 view = wr.getViewTransf();
					return glm::transpose(view) * cursor_offset;
				} ();

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

			fpsGauge.lock()->setText(fmt::format("Frame rate: {:.2f}", tickreg::delta_t(1) / (engine->frameDelta())));
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
		auto* asset_source = new SKENGINE_NAME_NS_SHORT::BasicAssetSource("assets/", logger);

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
			std::shared_ptr<SKENGINE_NAME_NS_SHORT::AssetSourceInterface>(asset_source),
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
