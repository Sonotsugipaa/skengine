#include <numbers>
#include <random>
#include <bit>

#include <engine/types.hpp>

#include <glm/ext/matrix_transform.hpp>

#include <engine-util/basic_shader_cache.hpp>
#include <engine-util/basic_asset_cache.hpp>
#include <engine-util/basic_render_process.hpp>
#include <engine-util/gui_manager.hpp>
#include <engine-util/animation.inl.hpp>

#include <input/input.hpp>

#include <posixfio_tl.hpp>

#include <vk-util/error.hpp>

extern "C" {
	#include <sys/stat.h>
}

#include "worldgen.inl.hpp"



namespace sneka {

	namespace anim::target {

		template <typename T>
		class Linear : public ske::Animation<T> {
		public:
			T beginning;
			T dir;

			Linear(ske::AnimationValue<T>& v, T beginning, T dir):
				ske::Animation<T>(v),
				beginning(beginning),
				dir(dir)
			{ }

			void animation_setProgress(T& dst, ske::anim_x_t x) noexcept override {
				dst = beginning + (dir * x);
			}
		};


		template <typename T>
		class EaseOut : public ske::Animation<T> {
		public:
			T beginning;
			T dir;

			EaseOut(ske::AnimationValue<T>& v, T beginning, T dir):
				ske::Animation<T>(v),
				beginning(beginning),
				dir(dir)
			{ }

			void animation_setProgress(T& dst, ske::anim_x_t x) noexcept override {
				constexpr auto f = [](ske::anim_x_t x) { auto x2 = x*x; return (ske::anim_x_t(2) * x) - x2; };
				dst = beginning + (dir * float(f(x)));
			}
		};

	}


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
		static constexpr float cameraDistance = 2.0f;
		static constexpr float cameraPitch = 0.75f;

		struct CallbackSharedState {
			ske::AnimationSet<glm::vec3> playerMovementAnim = { };
			ske::AnimationValue<glm::vec3> playerHeadPos = { };
			ske::AnimationValue<glm::vec3> camRotation = { };
			ske::AnimId cameraAnimId    = idgen::invalidId<ske::AnimId>();
			signed char lastDir[2]    = { 0, -1 };
			float       headYawTarget = 0.0f;
			float       speedBase     = 1.5f;
			float       speedBoost    = 0.0f;
			bool        quit          = false;
		};

		struct ModelIdStorage {
			ske::ModelId scenery;
			ske::ModelId playerHead;
			ske::ModelId boost;
			ske::ModelId point;
			ske::ModelId obstacle;
			ske::ModelId wall;
		};

		ske::Engine* engine;
		std::shared_ptr<ske::BasicAssetCache> assetCache;
		std::shared_ptr<ske::BasicRenderProcess> rproc;
		std::shared_ptr<CallbackSharedState> sharedState;
		ske::InputManager inputMan;
		ModelIdStorage mdlIds;
		std::mutex inputManMutex;
		std::mutex macrotickMutex;
		ske::AnimId playerHeadPosAnimId;
		ske::ObjectId light0;
		ske::ObjectId light1;
		ske::ObjectId skyLight;
		ske::ObjectId scenery;
		ske::ObjectId playerHead;
		ske::CommandId cmdLeft;
		ske::CommandId cmdRight;
		float macrotickProgress;
		float macrotickFrequency;
		World world;


		void updateViewPosRot(tickreg::delta_t deltaAvg) {
			auto& wr = * rproc->worldRenderer();
			auto& os = * rproc->objectStorage();

			constexpr auto biasedAverage = [](float src, float target, float bias) -> float {
				return (src + (target * bias)) / (1.0f + bias); };

			{
				constexpr float headRotBias = 8.0f;
				constexpr float macrotickAnimRatio = 0.99f; // Used to encourage macrotick-tied animations to finish after the macrotick (ideally being interrupted)

				auto& state = *sharedState;
				auto inputLock = std::unique_lock(inputManMutex);
				auto viewRot = state.camRotation.getValue();
				const auto playerHeadPos = state.playerHeadPos.getValue();
				const auto playerHeadDir = [&]() { auto r = os.getObject(this->playerHead); return (r.has_value()? r.value()->direction_ypr : glm::vec3 { }); } ();
				auto deltaSupertick = deltaAvg * macrotickFrequency;

				{
					auto macrotickLock = std::unique_lock(macrotickMutex);
					macrotickProgress += deltaSupertick;
				}

				state.playerMovementAnim.fwd(deltaSupertick * macrotickAnimRatio);

				{
					glm::mat4 viewRotTransf = glm::mat4(1.0f);
					viewRotTransf = glm::rotate(viewRotTransf, +viewRot.x, { 0.0f, 1.0f, 0.0f });
					viewRotTransf = glm::rotate(viewRotTransf, -viewRot.y, { 1.0f, 0.0f, 0.0f });
					auto viewPos = playerHeadPos;
					auto viewPosOff4 = viewRotTransf * glm::vec4 { 0.0f, 0.0f, -cameraDistance, 1.0f };
					viewPos -= glm::vec3(viewPosOff4);
					wr.setViewPosition(viewPos);
					wr.setViewRotation(viewRot);
				}

				if(this->playerHead != idgen::invalidId<ske::ObjectId>()) {
					auto newHeadRot = playerHeadDir;
					newHeadRot.x = biasedAverage(newHeadRot.x, state.headYawTarget, headRotBias * deltaAvg);
					{ auto mod = os.modifyObject(this->playerHead);
						mod->position_xyz = playerHeadPos;
						mod->direction_ypr = newHeadRot; }
				}
			}
		}


		Loop(ske::Engine& e, decltype(assetCache) assetCache, decltype(rproc) rproc):
			engine(&e),
			assetCache(std::move(assetCache)),
			rproc(std::move(rproc)),
			sharedState(std::make_shared<CallbackSharedState>()),
			macrotickFrequency(1.0f)
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
				generateWorld(logger, world, std::minstd_rand(NOW_));
				world.setSceneryModel("world1-scenery.fma");
				world.setPlayerHeadModel("default-player-head.fma");
				world.setObjBoostModel("default-boost.fma");
				world.setObjPointModel("default-point.fma");
				world.setObjObstacleModel("crate-obstacle.fma");
				world.setObjWallModel("crate-wall.fma");
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

			{ // Input management
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
					constexpr auto pi = std::numbers::pi_v<float>;
					constexpr auto pi2 = 2.0f * pi;
					signed char lastDir0 = state.lastDir[0];
					auto cam = state.camRotation.getValue();
					state.lastDir[0] = -dir * state.lastDir[1];
					state.lastDir[1] = +dir * lastDir0;
					auto yawTarget = std::atan2f(state.lastDir[0], -state.lastDir[1]);
					auto yawDiff = yawTarget - cam.x;
					while(yawDiff >= +pi) yawDiff -= pi2;
					while(yawDiff <= -pi) yawDiff += pi2;
					state.playerMovementAnim.interrupt(state.cameraAnimId);
					state.cameraAnimId = state.playerMovementAnim.start<anim::target::EaseOut<glm::vec3>>(
						ske::AnimEndAction::eClampThenPause,
						state.camRotation,
						cam,
						glm::vec3 { yawDiff, 0.0f, 0.0f } );
				};
				bindKeyCb(SDLK_a, "general", [sharedState](const ske::Context&, ske::Input) { rotate(*sharedState, +1); });
				bindKeyCb(SDLK_d, "general", [sharedState](const ske::Context&, ske::Input) { rotate(*sharedState, -1); });
				bindKeyCb(SDLK_q, "general", [sharedState](const ske::Context&, ske::Input) { sharedState->quit = true; });
			}

			{ // Load models
				auto trySetModel = [&](std::string_view filename) {
					try { return assetCache->setModelFromFile(filename); }
					catch(posixfio::Errcode& e) { engine->logger().error("Failed to load file for model \"{}\" (errno {})", filename, e.errcode); }
					return idgen::invalidId<ske::ModelId>();
				};
				mdlIds.scenery    = trySetModel(world.getSceneryModel());
				mdlIds.playerHead = trySetModel(world.getPlayerHeadModel());
				mdlIds.boost      = trySetModel(world.getObjBoostModel());
				mdlIds.point      = trySetModel(world.getObjPointModel());
				mdlIds.obstacle   = trySetModel(world.getObjObstacleModel());
				mdlIds.wall       = trySetModel(world.getObjWallModel());
			}

			{ // Setup animations
				auto state = *this->sharedState;
				macrotickProgress = 0.0f;
				playerHeadPosAnimId = idgen::invalidId<ske::AnimId>();
			}

			{ // Create world
				auto& state = *this->sharedState;
				state = CallbackSharedState();
				assert(world.width() * world.height() > 0);
				float xGridCenter = + (float(world.width()  - 1.0f) / 2.0f);
				float yGridCenter = - (float(world.height() - 1.0f) / 2.0f);
				auto& tc = engine->getTransferContext();
				auto newObject = ske::ObjectStorage::NewObject {
					{ }, { }, { }, { 1.0f, 1.0f, 1.0f }, false };
				auto tryCreate = [&](ske::ModelId mdl) {
					if(mdl == idgen::invalidId<ske::ModelId>()) return idgen::invalidId<ske::ObjectId>();
					newObject.model_id = mdl;
					return os.createObject(tc, newObject);
					return idgen::invalidId<ske::ObjectId>();
				};
				for(size_t y = 0; y < world.height(); ++ y)
				for(size_t x = 0; x < world.width();  ++ x) {
					auto rng = std::minstd_rand(std::chrono::system_clock::now().time_since_epoch().count());
					bool invert = std::uniform_int_distribution<unsigned>(0, 1)(rng) == 1;
					newObject.position_xyz = { + float(x) - xGridCenter, 0.0f, - float(y) - yGridCenter };
					newObject.scale_xyz.x *= invert? -1.0f : +1.0f;
					newObject.scale_xyz.z *= invert? -1.0f : +1.0f;
					newObject.direction_ypr = {
						0.5f * std::numbers::pi_v<float> * float(std::uniform_int_distribution<unsigned>(0, 3)(rng)),
						0.0f,
						0.0f };
					switch(world.tile(x, y)) {
						case GridObjectClass::eBoost:    tryCreate(mdlIds.boost); break;
						case GridObjectClass::ePoint:    tryCreate(mdlIds.point); break;
						case GridObjectClass::eObstacle: tryCreate(mdlIds.obstacle); break;
						case GridObjectClass::eWall:     tryCreate(mdlIds.wall); break;
						default:
							engine->logger().warn("World object at ({}, {}) has unknown type {}", x, y, grid_object_class_e(world.tile(x, y)));
							[[fallthrough]];
						case GridObjectClass::eNoObject: break;
					}
				}
				newObject.position_xyz = state.playerHeadPos.getValue();
				newObject.direction_ypr = { };
				newObject.scale_xyz = { 1.0f, 1.0f, 1.0f };
				playerHead = tryCreate(mdlIds.playerHead);
				newObject.position_xyz = { };
				newObject.direction_ypr = { };
				newObject.scale_xyz = { 1.0f, 1.0f, 1.0f };
				tryCreate(mdlIds.scenery);
				state.camRotation.setValue({ 0.0f, cameraPitch, 0.0f });
				wr.setAmbientLight({ 0.1f, 0.1f, 0.1f });
				light0 = wr.createPointLight(ske::WorldRenderer::NewPointLight {
					.position = { },
					.color = { 0.4f, 0.4f, 1.0f },
					.intensity = 0.8f,
					.falloffExponent = 0.8f });
				light1 = wr.createPointLight(ske::WorldRenderer::NewPointLight {
					.position = { -0.9f * xGridCenter, 10.0f, -0.8f * yGridCenter },
					.color = { 0.9f, 0.9f, 1.0f },
					.intensity = 12.0f,
					.falloffExponent = 0.9f });
				skyLight = wr.createRayLight(ske::WorldRenderer::NewRayLight {
					.direction = { 0.0f, -1.0f, 0.0f },
					.color = { 0.9f, 0.9f, 1.0f },
					.intensity = 0.7f,
					.aoaThreshold = 0.3f });
				updateViewPosRot(0.0);
			}

			sharedState->quit = false;
		}


		void loop_end() noexcept override {
			sharedState->quit = false;
			inputMan.clear();
		}


		void loop_processEvents(tickreg::delta_t deltaAvg, tickreg::delta_t delta) override {
			(void) deltaAvg;
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

			{
				auto macrotickLock = std::unique_lock(macrotickMutex);
				if(macrotickProgress >= 1.0f) [[unlikely]] {
					constexpr auto pi = std::numbers::pi_v<float>;
					constexpr auto pi2 = 2.0f * pi;
					auto& shState = *sharedState;

					-- macrotickProgress;
					macrotickFrequency = shState.speedBase + shState.speedBoost;
					macrotickLock.unlock();

					const auto pos = shState.playerHeadPos.getValue();
					auto xApprox = std::floorf(pos.x + 0.5f);
					auto zApprox = std::floorf(pos.z + 0.5f);
					auto xDiff = (xApprox - shState.lastDir[0]) - pos.x;
					auto zDiff = (zApprox + shState.lastDir[1]) - pos.z;
					auto yawDiff = std::atan2f(-xDiff, -zDiff) - shState.headYawTarget;
					while(yawDiff >= +pi) yawDiff -= pi2;
					while(yawDiff <= -pi) yawDiff += pi2;
					shState.headYawTarget += yawDiff;
					shState.playerMovementAnim.interrupt(playerHeadPosAnimId);
					playerHeadPosAnimId = shState.playerMovementAnim.start<anim::target::Linear<glm::vec3>>(
						ske::AnimEndAction::ePause,
						shState.playerHeadPos,
						pos,
						glm::vec3 { xDiff, 0.0f, zDiff } );
				}
			}
		}

		LoopState loop_pollState() const noexcept override {
			return sharedState->quit? LoopState::eShouldStop : LoopState::eShouldContinue;
		}


		void loop_async_preRender(ske::ConcurrentAccess, tickreg::delta_t deltaAvg, tickreg::delta_t deltaPrevious) override {
			(void) deltaPrevious;

			deltaAvg = std::min(tickreg::delta_t(0.5), deltaAvg);

			updateViewPosRot(deltaAvg);
		}


		virtual void loop_async_postRender(ske::ConcurrentAccess, tickreg::delta_t deltaAvg, tickreg::delta_t deltaCurrent) {
			(void) deltaAvg;
			(void) deltaCurrent;
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
		prefs.fov_y                 = glm::radians(90.0f);
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
		auto asset_cache   = std::make_shared<ske::BasicAssetCache>("assets/", logger);
		auto basic_rprocess = std::make_shared<ske::BasicRenderProcess>();
		BasicRenderProcess::setup(*basic_rprocess, logger, asset_cache, 0.125);

		auto engine = ske::Engine(
			ske::DeviceInitInfo {
				.window_title     = "Sneka 3D",
				.application_name = "Sneka 3D",
				.app_version = VK_MAKE_API_VERSION(0, 0, 1, 0) },
			enginePrefs,
			std::move(shader_cache),
			logger );

		auto loop = sneka::Loop(engine, asset_cache, basic_rprocess);

		engine.run(loop, basic_rprocess);

		BasicRenderProcess::destroy(*basic_rprocess, engine.getTransferContext());

		logger.info("Successfully exiting the program.");
	} catch(posixfio::Errcode& e) {
		logger.error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		logger.error("Uncaught Vulkan error: {}", e.what());
	}
}
