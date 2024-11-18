#include <numbers>
#include <random>
#include <bit>

#include <engine/types.hpp>

#include <engine-util/basic_shader_cache.hpp>
#include <engine-util/basic_asset_source.hpp>
#include <engine-util/basic_render_process.hpp>
#include <engine-util/gui_manager.hpp>

#include <input/input.hpp>

#include <posixfio_tl.hpp>

#include <vk-util/error.hpp>

extern "C" {
	#include <sys/stat.h>
}

#include "worldgen.inl.hpp"



namespace sneka {

	std::string getenv(const char* nameCstr) {
		static std::mutex mtx;
		mtx.lock();
		char* env = std::getenv(nameCstr);
		if(env == nullptr) return "";
		auto len = strlen(env);
		std::string r;
		r.resize_and_overwrite(len, [&](auto* p, auto n) { assert(n == len); memcpy(p, env, n); return n; });
		return r;
	}


	class Loop : public ske::LoopInterface {
	public:
		struct CallbackSharedState {
			signed char lastDir[2] = { 0, -1 };
			float       yawTarget  = 0.0f;
			float       speed      = 0.0f;
			bool        quit       = false;
		};

		ske::Engine* engine;
		std::shared_ptr<ske::BasicRenderProcess> rproc;
		std::shared_ptr<CallbackSharedState> sharedState;
		ske::InputManager inputMan;
		std::mutex inputManMutex;
		ske::ObjectId light;
		ske::ObjectId scenery;
		ske::CommandId cmdForward;
		ske::CommandId cmdBackward;
		ske::CommandId cmdLeft;
		ske::CommandId cmdRight;
		World world;


		Loop(ske::Engine& e, decltype(rproc) rproc):
			engine(&e),
			rproc(std::move(rproc)),
			sharedState(std::make_shared<CallbackSharedState>())
		{
			constexpr const char* worldFilename = "world.wrd";
			using enum GridObjectClass;
			ske::Logger& logger = engine->logger();
			const auto onError = [&]() {
				auto sideLengthEnvvar = sneka::getenv("SNEKA_NEWWORLD_SIDE");
				auto* sideLengthEnvvarEnd = sideLengthEnvvar.data() + sideLengthEnvvar.size();
				uint64_t sideLength = std::strtoull(sideLengthEnvvar.data(), &sideLengthEnvvarEnd, 10);
				if(sideLengthEnvvar.data() == sideLengthEnvvarEnd) { sideLength = 51; }
				world = World::initEmpty(sideLength, sideLength);
				#define NOW_ std::chrono::steady_clock::now().time_since_epoch().count()
				generateWorldNoise(logger, world, std::minstd_rand(NOW_));
				generateWorldPath(logger, world, std::minstd_rand(NOW_), sideLength);
				generateWorldPath(logger, world, std::minstd_rand(NOW_), sideLength / 2);
				generateWorldPath(logger, world, std::minstd_rand(NOW_), sideLength / 2);
				for(unsigned i = 0; i < sideLength / 5; ++i) {
					generateWorldPath(logger, world, std::minstd_rand(NOW_), sideLength / 4); }
				#undef NOW_
				world.setSceneryModel("world1-scenery.fma");
				world.setPlayerHeadModel("world1-player-head.fma");
				world.setObjBoostModel("default-boost.fma");
				world.setObjPointModel("default-point.fma");
				world.setObjObstacleModel("default-obstacle.fma");
				world.setObjWallModel("default-wall.fma");
				world.toFile(worldFilename);
			};
			try {
				world = World::fromFile(worldFilename);
			} catch(posixfio::Errcode& e) {
				if(e.errcode == ENOENT) logger.error("World \"{}\" does not exist, creating a new one", worldFilename);
				else                    logger.error("Failed to read world file \"{}\" (errno {}), creating a new one", worldFilename, e.errcode);
				onError();
			} catch(World::BadFile& e) {
				logger.error("Bad world file at byte {0}, 0x{0:x}: reason {1}", e.errorOffset, size_t(e.reason));
				onError();
			}
		}


		void loop_begin() override {
			auto ca = engine->getConcurrentAccess();
			ske::ObjectStorage& os = * rproc->objectStorage();
			ske::WorldRenderer& wr = * rproc->worldRenderer();

			{
				std::shared_ptr<ske::WorldRenderer> wrPtr = rproc->worldRenderer();
				auto inputLock = std::unique_lock(inputManMutex);
				auto sharedState = this->sharedState;
				auto bindKeyCb = [&](SDL_KeyCode kc, std::string ctx, ske::CommandCallbackFunction auto cb) {
					auto key = ske::InputMapKey { ske::inputIdFromSdlKey(kc), ske::InputState::eActivated };
					auto cbPtr = std::make_shared<ske::CommandCallbackWrapper<decltype(cb)>>(std::move(cb));
					return inputMan.bindNewCommand(
						ske::Binding { key, std::move(ctx) },
						std::move(cbPtr) );
				};
				static constexpr auto rotate = [](CallbackSharedState& state, signed char dir) {
					signed char lastDir0 = state.lastDir[0];
					state.lastDir[0] = -dir * state.lastDir[1];
					state.lastDir[1] = +dir * lastDir0;
					state.yawTarget = std::atan2f(state.lastDir[0], -state.lastDir[1]);
					state.speed = std::min(0.4f, state.speed * 0.7f);
				};
				auto bindKey = [&](SDL_KeyCode kc, std::string ctx) {
					auto key = ske::InputMapKey { ske::inputIdFromSdlKey(kc), ske::InputState::eActive };
					return inputMan.bindNewCommand(ske::Binding { key, std::move(ctx) }, nullptr);
				};
				cmdForward  = bindKey(SDLK_w, "general");
				cmdBackward = bindKey(SDLK_s, "general");
				bindKeyCb(SDLK_a, "general", [sharedState](const ske::Context&, ske::Input) { rotate(*sharedState, +1); });
				bindKeyCb(SDLK_d, "general", [sharedState](const ske::Context&, ske::Input) { rotate(*sharedState, -1); });
				bindKeyCb(SDLK_q, "general", [sharedState](const ske::Context&, ske::Input) { sharedState->quit = true; });
			}

			assert(world.width() * world.height() > 0);
			float xGridCenter = + (float(world.width()  - 1.0f) / 2.0f);
			float yGridCenter = - (float(world.height() - 1.0f) / 2.0f);
			auto& tc = engine->getTransferContext();
			auto newObject = ske::ObjectStorage::NewObject {
				{ }, { }, { }, { 0.95f, 0.95f, 0.95f }, false };
			for(size_t y = 0; y < world.height(); ++ y)
			for(size_t x = 0; x < world.width();  ++ x) {
				newObject.position_xyz = { + float(x) - xGridCenter, 0.0f, - float(y) - yGridCenter };
				auto tryCreate = [&](std::string_view mdl) {
					newObject.model_locator = mdl;
					try { return os.createObject(tc, newObject); }
					catch(posixfio::Errcode& e) { engine->logger().error("Failed to load file for model \"{}\" (errno {})", newObject.model_locator, e.errcode); }
					return idgen::invalidId<ske::ObjectId>();
				};
				switch(world.tile(x, y)) {
					case GridObjectClass::eBoost: tryCreate(world.getObjBoostModel()); break;
					case GridObjectClass::ePoint: tryCreate(world.getObjPointModel()); break;
					case GridObjectClass::eObstacle: tryCreate(world.getObjObstacleModel()); break;
					case GridObjectClass::eWall: tryCreate(world.getObjWallModel()); break;
					default:
						engine->logger().warn("World object at ({}, {}) has unknown type {}", x, y, grid_object_class_e(world.tile(x, y)));
						[[fallthrough]];
					case GridObjectClass::eNoObject: break;
				}
			}
			try {
				auto obj = ske::ObjectStorage::NewObject {
					.model_locator = world.getSceneryModel(),
					.position_xyz = { },
					.direction_ypr = { },
					.scale_xyz = { 1.0f, 1.0f, 1.0f },
					.hidden = false };
				scenery = os.createObject(engine->getTransferContext(), obj);
			} catch(posixfio::Errcode& e) {
				engine->logger().error("Failed to load file for model \"{}\" (errno {})", world.getSceneryModel(), e.errcode);
			}
			wr.setViewRotation({ 0.0f, 0.65f, 0.0f });
			wr.setViewPosition({ 0.0f, 1.6f, 0.0f });
			wr.setAmbientLight({ 0.1f, 0.1f, 0.1f });
			light = wr.createPointLight(ske::WorldRenderer::NewPointLight {
				.position = { 1.4f * xGridCenter, 10.0f, 1.1f * yGridCenter },
				.color = { 0.9f, 0.9f, 1.0f },
				.intensity = 4.0f,
				.falloffExponent = 0.5f });

			sharedState->quit = false;
		}


		void loop_end() noexcept override {
			sharedState->quit = false;
			inputMan.clear();
		}


		void loop_processEvents(tickreg::delta_t delta_avg, tickreg::delta_t delta) override {
			(void) delta_avg;
			(void) delta;

			auto ca = engine->getConcurrentAccess();

			struct ResizeEvent {
				Sint32 width;
				Sint32 height;
				bool triggered;
			} resizeEvent = { 0, 0, false };

			SDL_Event ev;
			while(1 == SDL_PollEvent(&ev)) {
				auto inputLock = std::unique_lock(inputManMutex);
				inputMan.feedSdlEvent("general", ev);
				if(ev.type == SDL_WINDOWEVENT) {
					if(ev.window.event == SDL_WINDOWEVENT_RESIZED) {
						resizeEvent = { ev.window.data1, ev.window.data2, true };
					}
				}
			}

			if(resizeEvent.triggered) ca->setPresentExtent(VkExtent2D { uint32_t(resizeEvent.width), uint32_t(resizeEvent.height) });
		}

		LoopState loop_pollState() const noexcept override {
			return sharedState->quit? LoopState::eShouldStop : LoopState::eShouldContinue;
		}


		void loop_async_preRender(ske::ConcurrentAccess, tickreg::delta_t delta_avg, tickreg::delta_t delta_previous) override {
			(void) delta_previous;

			delta_avg = std::min(tickreg::delta_t(0.5), delta_avg);

			constexpr auto biasedAverage = [](float src, float target, float bias) -> float {
				return (src + (target * bias)) / (1.0f + bias); };

			const auto accelerate = [delta_avg](float value, float delta, float accel) -> float {
				return value + (delta * delta_avg) + (2.0f * accel * delta_avg);
			};

			{
				ske::WorldRenderer& wr = * rproc->worldRenderer();
				auto state = *sharedState;
				auto inputLock = std::unique_lock(inputManMutex);
				auto activeCmds = inputMan.getActiveCommands();
				signed char cmdShift = 0;
				for(const auto& [cmd, key] : activeCmds) {
					if     (cmd == cmdForward ) { cmdShift -= 1; }
					else if(cmd == cmdBackward) { cmdShift += 1; }
				}
				auto rot = wr.getViewRotation();
				auto* direction = state.lastDir;
				auto yawDiff = state.yawTarget - rot.x;
				if(std::abs(yawDiff) > 0.0001f) {
					constexpr float rotBias = 8.0f;
					constexpr float pi = std::numbers::pi_v<float>;
					if(std::abs(yawDiff) < 0.001) {
						wr.setViewRotation({ state.yawTarget, rot.y, rot.z });
					} else {
						if     (yawDiff > +pi) rot.x += pi*2.0f;
						else if(yawDiff < -pi) rot.x -= pi*2.0f;
						rot.x = biasedAverage(rot.x, state.yawTarget, rotBias * delta_avg);
						wr.setViewRotation(rot);
					}
				}
				if(cmdShift == 0) {
					state.speed = 0.0f;
				} else {
					constexpr float speedBias = 4.0f;
					constexpr float maxSpeed = 6.0f;
					const auto cmdShiftf = float(cmdShift);
					auto speedTarget = biasedAverage(state.speed, maxSpeed, speedBias * delta_avg);
					auto accel = (speedTarget - state.speed) * cmdShiftf;
					auto vel = state.speed * cmdShiftf;
					auto pos = wr.getViewPosition();
					pos.x = accelerate(pos.x, +direction[0] * vel, +direction[0] * accel);
					pos.z = accelerate(pos.z, -direction[1] * vel, -direction[1] * accel);
					wr.setViewPosition(pos);
					state.speed = speedTarget;
				}
				*sharedState = state;
			}
		}


		virtual void loop_async_postRender(ske::ConcurrentAccess, tickreg::delta_t delta_avg, tickreg::delta_t delta_current) {
			(void) delta_avg;
			(void) delta_current;
			;
		}

	};

}



int main(int argn, char** argv) {
	using namespace std::string_view_literals;
	using namespace ske;
	(void) argn;
	(void) argv;

	auto logger = Logger(
		std::make_shared<posixfio::OutputBuffer>(STDOUT_FILENO, 512),
		sflog::Level::eInfo,
		sflog::OptionBit::eUseAnsiSgr | sflog::OptionBit::eAutoFlush,
		"["sv, SKENGINE_NAME_CSTR " Sneka : "sv, ""sv, "]  "sv );

	#ifndef NDEBUG
		logger.setLevel(sflog::Level::eDebug);
	#endif

	const auto enginePrefs = []() {
		auto prefs = EnginePreferences::default_prefs;
		prefs.init_present_extent   = { 700, 500 };
		prefs.max_render_extent     = { 0, 0 };
		prefs.asset_filename_prefix = "assets/";
		prefs.present_mode          = VK_PRESENT_MODE_MAILBOX_KHR;
		prefs.target_framerate      = 72.0f;
		prefs.target_tickrate       = 60.0f;
		prefs.fov_y                 = glm::radians(80.0f);
		prefs.shade_step_count      = 12;
		prefs.shade_step_smoothness = 0.3f;
		prefs.shade_step_exponent   = 4.0f;
		prefs.dithering_steps       = 256.0f;
		prefs.font_location         = "assets/font.otf";
		prefs.wait_for_gframe       = false;
		prefs.framerate_samples     = 4;
		return prefs;
	} ();

	try {
		auto shader_cache   = std::make_shared<ske::BasicShaderCache>("assets/", logger);
		auto asset_source   = std::make_shared<ske::BasicAssetSource>("assets/", logger);
		auto basic_rprocess = std::make_shared<ske::BasicRenderProcess>();
		BasicRenderProcess::setup(*basic_rprocess, logger, asset_source, 0.125);

		auto engine = ske::Engine(
			ske::DeviceInitInfo {
				.window_title     = "Sneka 3D",
				.application_name = "Sneka 3D",
				.app_version = VK_MAKE_API_VERSION(0, 0, 1, 0) },
			enginePrefs,
			std::move(shader_cache),
			logger );

		auto loop = sneka::Loop(engine, basic_rprocess);

		engine.run(loop, basic_rprocess);

		BasicRenderProcess::destroy(*basic_rprocess, engine.getTransferContext());

		logger.info("Successfully exiting the program.");
	} catch(posixfio::Errcode& e) {
		logger.error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		logger.error("Uncaught Vulkan error: {}", e.what());
	}
}
