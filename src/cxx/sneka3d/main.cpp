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

	constexpr size_t OBJSTG_SCENERY_IDX = 0;
	constexpr size_t OBJSTG_OBJECTS_IDX = 1;
	constexpr size_t OBJSTG_PLAYER_IDX  = 2;
	constexpr size_t OBJSTG_POINTS_IDX  = 3;
	constexpr size_t OBJSTG_COUNT       = 4;



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



	template <std::integral T>
	float discreteObjRotation(T x) {
		constexpr auto quarter = std::numbers::pi_v<float> / 2.0f;
		switch(x) {
			case  0: return           0.00f;
			case  1: return quarter * 0.05f;
			case  2: return quarter * 0.95f;
			case  3: return quarter * 1.00f;
			case  4: return quarter * 1.05f;
			case  5: return quarter * 1.95f;
			case  6: return quarter * 2.00f;
			case  7: return quarter * 2.05f;
			case  8: return quarter * 2.95f;
			case  9: return quarter * 3.00f;
			case 10: return quarter * 3.05f;
			case 11: return quarter * 3.95f;
			case 12: return           0.00f;
			case 13: return quarter * 0.1f;
			case 14: return quarter * 0.9f;
			case 15: return quarter * 1.1f;
			case 16: return quarter * 1.9f;
			case 17: return quarter * 2.1f;
			case 18: return quarter * 2.9f;
			case 19: return quarter * 3.1f;
			case 20: return quarter * 3.9f;
			default: assert(false /* Should not happen */); return 0.0f;
		}
	}


	std::string getenv(const char* nameCstr) {
		static std::mutex mtx;
		auto lock = std::unique_lock(mtx);
		char* env = std::getenv(nameCstr);
		if(env == nullptr) return "";
		auto len = strlen(env);
		std::string r;
		r.resize_and_overwrite(len, [&](auto* p, auto n) { assert(n == len); memcpy(p, env, n); return n; });
		return r;
	}


	class Loop : public ske::LoopInterface {
	public:
		static constexpr const char* worldFilename = "world.wrd";
		static constexpr float cameraDistance = 2.5f;
		static constexpr float cameraPitch = 0.75f;
		static constexpr float speedBaseDefault = 2.0f;
		static constexpr float speedBoostDecayDn = 0.5f;
		static constexpr float speedBoostDecayUp = 0.2f;
		static constexpr float speedBoostFromInput = speedBoostDecayDn * 5.0f;

		enum class QuitReason : unsigned char {
			eNoQuit = 1,
			eUserInput = 2,
			eGameEnd = 3
		};

		struct CallbackSharedState {
			std::mutex animMutex;
			ske::AnimationSet<glm::vec3> playerMovementAnim;
			ske::AnimationValue<glm::vec3> playerHeadPos;
			ske::AnimationValue<glm::vec3> camRotation;
			ske::AnimId   cameraAnimId;
			signed char   lastDir[2];
			unsigned char enableCulling;
			float         headYawTarget;
			float         speedBase;
			float         speedBoost;
			QuitReason    quitReason;
			bool          requestMapRegen;
			void init() {
				playerMovementAnim = { };
				playerHeadPos      = { };
				camRotation        = { };
				cameraAnimId       = idgen::invalidId<ske::AnimId>();
				lastDir[0]         =  0;
				lastDir[1]         = -1;
				enableCulling      = 0b11;
				headYawTarget      = 0.0f;
				speedBase          = speedBaseDefault;
				speedBoost         = -0.5f * speedBase;
				quitReason         = QuitReason::eNoQuit;
				requestMapRegen    = false;
			}
			CallbackSharedState() { init(); }
		};

		struct ModelIdStorage {
			static constexpr ske::ModelId noModel = idgen::invalidId<ske::ModelId>();
			ske::ModelId scenery    = noModel;
			ske::ModelId playerHead = noModel;
			std::vector<ske::ModelId> boost;
			std::vector<ske::ModelId> point;
			std::vector<ske::ModelId> obstacle;
			std::vector<ske::ModelId> wall;
		};

		ske::Engine* engine;
		ske::Logger logger;
		std::shared_ptr<ske::BasicAssetCache> assetCache;
		std::shared_ptr<ske::BasicRenderProcess> rproc;
		std::shared_ptr<CallbackSharedState> sharedState;
		ske::InputManager inputMan;
		ModelIdStorage mdlIds;
		BasicUmap<Vec2<int64_t>, std::pair<ske::ObjectId, ske::ObjectId>> pointObjects;
		std::mutex inputManMutex;
		std::mutex macrotickMutex;
		ske::AnimId playerHeadPosAnimId;
		ske::ObjectId light0;
		ske::ObjectId light1;
		ske::ObjectId skyLight;
		ske::ObjectId scenery;
		ske::ObjectId playerHead;
		ske::CommandId cmdBoost;
		float macrotickProgress;
		float macrotickFrequency;
		World world;
		Vec2<int64_t> worldOffset;


		auto gridToWorld(Vec2<int64_t> g, float height) {
			return glm::vec3 { +g.x - worldOffset.x, height, +g.y + worldOffset.y };
		}

		auto worldToGrid(glm::vec3 w) {
			w.x = std::floorf(+w.x + 0.5f);
			w.z = std::floorf(+w.z + 0.5f);
			return Vec2<int64_t> { int64_t(w.x) + worldOffset.x, int64_t(w.z) - worldOffset.y };
		}


		void createWorld(const char* worldFilename, Vec2<uint64_t> startPos = { UINT64_MAX, UINT64_MAX }) {
			auto sideLengthEnvvar = sneka::getenv("SNEKA_NEWWORLD_SIDE");
			auto* sideLengthEnvvarEnd = sideLengthEnvvar.data() + sideLengthEnvvar.size();
			uint64_t sideLength = std::strtoull(sideLengthEnvvar.data(), &sideLengthEnvvarEnd, 10);
			if(sideLengthEnvvar.data() == sideLengthEnvvarEnd) { sideLength = 53; }
			if(sideLength % 2 == 0) ++ sideLength;
			world = World::initEmpty(sideLength, sideLength);
			if(startPos.x == UINT64_MAX && startPos.y == UINT64_MAX) {
				startPos.x = sideLength / uint64_t(2);
				startPos.y = sideLength / uint64_t(2); }
			#define NOW_ std::chrono::steady_clock::now().time_since_epoch().count()
				generateWorld(logger, world, nullptr, startPos, std::minstd_rand(NOW_));
			#undef NOW_
			world.entryPointX() = startPos.x;
			world.entryPointY() = startPos.y;
			world.setSceneryModel("world1-scenery.fma");
			world.setPlayerHeadModel("default-player-head.fma");
			world.addObjBoostModel("default-boost.fma");
			world.addObjPointModel("default-point.fma");
			world.addObjObstacleModel("crate-obstacle.fma");
			world.addObjObstacleModel("chair-bundle.fma");
			world.addObjWallModel("crate-wall.fma");
			world.addObjWallModel("prism-wall.fma");
			world.toFile(worldFilename);
		}


		void updateViewPosRot(tickreg::delta_t deltaAvg) {
			auto& wr = * rproc->worldRenderer();
			auto& plrOs = rproc->getObjectStorage(OBJSTG_PLAYER_IDX);

			constexpr auto biasedAverage = [](float src, float target, float bias) -> float {
				return (src + (target * bias)) / (1.0f + bias); };

			{
				constexpr float headRotBias = 8.0f;
				constexpr float macrotickAnimRatio = 0.99f; // Used to encourage macrotick-tied animations to finish after the macrotick (ideally being interrupted)

				auto& state = *sharedState;
				auto inputLock = std::unique_lock(inputManMutex);
				auto viewRot = state.camRotation.getValue();
				const auto playerHeadPos = state.playerHeadPos.getValue();
				const auto playerHeadDir = [&]() { auto r = plrOs.getObject(this->playerHead); return (r.has_value()? r.value()->direction_ypr : glm::vec3 { }); } ();
				auto deltaSupertick = deltaAvg * macrotickFrequency;

				{
					auto macrotickLock = std::unique_lock(macrotickMutex);
					macrotickProgress += deltaSupertick;
				}

				{
					auto lock = std::unique_lock(state.animMutex);
					state.playerMovementAnim.fwd(deltaSupertick * macrotickAnimRatio);
				}

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
					{ auto mod = plrOs.modifyObject(this->playerHead);
						mod->position_xyz = playerHeadPos;
						mod->direction_ypr = newHeadRot; }
				}
			}
		}


		Loop(ske::Engine& e, ske::Logger loggerMv, decltype(assetCache) assetCache, decltype(rproc) rproc):
			engine(&e),
			logger(std::move(loggerMv)),
			assetCache(std::move(assetCache)),
			rproc(std::move(rproc)),
			sharedState(std::make_shared<CallbackSharedState>()),
			macrotickFrequency(1.0f)
		{
			using enum GridObjectClass;
			const auto onError = [&]() {
				createWorld(worldFilename);
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


		void reset() {
			sharedState->quitReason = QuitReason::eNoQuit;
		}


		void loop_begin() override {
			auto ca = engine->getConcurrentAccess();
			ske::ObjectStorage& sceneryOs = rproc->getObjectStorage(OBJSTG_SCENERY_IDX);
			ske::ObjectStorage& objectsOs = rproc->getObjectStorage(OBJSTG_OBJECTS_IDX);
			ske::ObjectStorage& playerOs  = rproc->getObjectStorage(OBJSTG_PLAYER_IDX);
			ske::ObjectStorage& pointOs   = rproc->getObjectStorage(OBJSTG_POINTS_IDX);
			ske::WorldRenderer& wr = * rproc->worldRenderer();
			sharedState->init();
			pointObjects.clear();

			{ // Input management
				auto inputLock = std::unique_lock(inputManMutex);
				auto sharedState = this->sharedState;
				auto bindKeyPressCb = [&](SDL_KeyCode kc, std::string ctx, ske::CommandCallbackFunction auto cb) {
					auto key = ske::InputMapKey { ske::inputIdFromSdlKey(kc), ske::InputState::eActivated };
					auto cbPtr = std::make_shared<ske::CommandCallbackWrapper<decltype(cb)>>(std::move(cb));
					return inputMan.bindNewCommand(
						ske::Binding { key, std::move(ctx) },
						std::move(cbPtr) );
				};
				auto bindKeyHoldCb = [&](SDL_KeyCode kc, std::string ctx, ske::CommandCallbackFunctionOptional auto cb) {
					auto key = ske::InputMapKey { ske::inputIdFromSdlKey(kc), ske::InputState::eActive };
					if constexpr (std::same_as<decltype(cb), nullptr_t>) {
						return inputMan.bindNewCommand(
							ske::Binding { key, std::move(ctx) },
							nullptr );
					} else {
						auto cbPtr = std::make_shared<ske::CommandCallbackWrapper<decltype(cb)>>(std::move(cb));
						return inputMan.bindNewCommand(
							ske::Binding { key, std::move(ctx) },
							std::move(cbPtr) );
					}
				};
				static constexpr auto rotate = [](CallbackSharedState& state, signed char dir) {
					#ifdef VS_CODE_HEADER_LINTING_WORKAROUND
						#define CONSTEXPR_ (void)0;
					#else
						#define CONSTEXPR_ constexpr
					#endif
					CONSTEXPR_ auto pi = std::numbers::pi_v<float>;
					CONSTEXPR_ auto pi2 = 2.0f * pi;
					#undef CONSTEXPR_
					signed char lastDir0 = state.lastDir[0];
					auto cam = state.camRotation.getValue();
					state.lastDir[0] = -dir * state.lastDir[1];
					state.lastDir[1] = +dir * lastDir0;
					auto yawTarget = std::atan2f(state.lastDir[0], -state.lastDir[1]);
					auto yawDiff = yawTarget - cam.x;
					while(yawDiff >= +pi) yawDiff -= pi2;
					while(yawDiff <= -pi) yawDiff += pi2;
					{
						auto lock = std::unique_lock(state.animMutex);
						state.playerMovementAnim.interrupt(state.cameraAnimId);
						state.cameraAnimId = state.playerMovementAnim.start<anim::target::EaseOut<glm::vec3>>(
							ske::AnimEndAction::eClampThenPause,
							state.camRotation,
							cam,
							glm::vec3 { yawDiff, 0.0f, 0.0f } );
					}
				};
				bindKeyPressCb(SDLK_a, "general", [sharedState](auto&, auto) { rotate(*sharedState, +1); });
				bindKeyPressCb(SDLK_d, "general", [sharedState](auto&, auto) { rotate(*sharedState, -1); });
				bindKeyPressCb(SDLK_q, "general", [sharedState](auto&, auto) { sharedState->quitReason = QuitReason::eUserInput; });
				bindKeyPressCb(SDLK_c, "general", [sharedState](auto&, auto) { sharedState->enableCulling = sharedState->enableCulling ^ 0b01; });
				bindKeyPressCb(SDLK_r, "general", [sharedState](auto&, auto) { sharedState->requestMapRegen = true; });
				cmdBoost = bindKeyHoldCb(SDLK_LSHIFT, "general", [sharedState](auto&, auto) { sharedState->speedBoost = speedBoostFromInput; });
			}

			{ // Load models
				auto checkModelListNotEmpty = [&](auto& mdls, std::string_view objTypeDesc) {
					if(! mdls.empty()) [[likely]] return;
					std::string msg = fmt::format("{} model list is empty", objTypeDesc);
					logger.error("{}", msg);
					throw std::runtime_error(std::move(msg));
				};
				auto modelLoadFail = [&](std::string_view mdl, posixfio::Errcode err) {
					logger.error("Failed to load file for model \"{}\" (errno {})", mdl, err.errcode); };
				auto boostMdls    = world.getObjBoostModels();    checkModelListNotEmpty(boostMdls,    "Boost");
				auto pointMdls    = world.getObjPointModels();    checkModelListNotEmpty(pointMdls,    "Point");
				auto obstacleMdls = world.getObjObstacleModels(); checkModelListNotEmpty(obstacleMdls, "Obstacle");
				auto wallMdls     = world.getObjWallModels();     checkModelListNotEmpty(wallMdls,     "Wall");
				#warning "TODO: `BasicAssetCache::setModelFromFile` should throw when the file cannot be loaded, but doesn't"
				auto trySetModel = [&](ske::ModelId* dst, std::string_view filename) {
					if(*dst != ModelIdStorage::noModel) return;
					*dst = ModelIdStorage::noModel;
					try { *dst = assetCache->setModelFromFile(filename); }
					catch(posixfio::Errcode& e) { modelLoadFail(filename, e.errcode); }
				};
				auto resetModelList = [&](std::vector<ske::ModelId>* dst) {
					for(auto& mdl : *dst) assetCache->unsetModel(mdl);
					dst->clear();
				};
				auto trySetModels = [&](std::vector<ske::ModelId>* dst, auto& mdls) {
					resetModelList(dst);
					for(auto& mdl : mdls) {
						try { dst->push_back(assetCache->setModelFromFile(mdl)); }
						catch(posixfio::Errcode& e) { modelLoadFail(mdl, e.errcode); }
					}
				};
				trySetModel(&mdlIds.scenery,    world.getSceneryModel());
				trySetModel(&mdlIds.playerHead, world.getPlayerHeadModel());
				trySetModels(&mdlIds.boost,    boostMdls   );
				trySetModels(&mdlIds.point,    pointMdls   );
				trySetModels(&mdlIds.obstacle, obstacleMdls);
				trySetModels(&mdlIds.wall,     wallMdls    );
			}

			{ // Setup animations
				macrotickProgress = 0.0f;
				playerHeadPosAnimId = idgen::invalidId<ske::AnimId>();
			}

			{ // Set up the world
				assert(world.width() * world.height() > 0);
				float xGridCenter = + (float(world.width()  - 1.0f) / 2.0f);
				float yGridCenter = - (float(world.height() - 1.0f) / 2.0f);
				worldOffset = { int64_t(xGridCenter), int64_t(yGridCenter) };
				auto& tc = engine->getTransferContext();
				auto rng = std::minstd_rand(std::chrono::system_clock::now().time_since_epoch().count());
				auto newObject = ske::ObjectStorage::NewObject {
					{ }, { }, { }, { 1.0f, 1.0f, 1.0f }, false };
				auto tryCreate = [&](ske::ObjectStorage& os, ske::ModelId mdl) {
					if(mdl == idgen::invalidId<ske::ModelId>()) return idgen::invalidId<ske::ObjectId>();
					newObject.model_id = mdl;
					return os.createObject(tc, newObject);
					return idgen::invalidId<ske::ObjectId>();
				};
				auto rndMdlFrom = [&](const std::vector<ske::ModelId>& mdls) {
					assert(! mdls.empty());
					return mdls[std::uniform_int_distribution<size_t>(0, mdls.size() - 1)(rng)];
				};
				auto insPoint = [&](ske::ObjectStorage& os) {
					using V = decltype(pointObjects)::value_type;
					auto p = tryCreate(os, rndMdlFrom(mdlIds.point));
					auto pos = worldToGrid(newObject.position_xyz);
					if(p != idgen::invalidId<ske::ObjectId>()) {
						auto l = wr.createPointLight(ske::WorldRenderer::NewPointLight {
							.position = {
								newObject.position_xyz.x,
								0.6f,
								newObject.position_xyz.z },
							.color = { 1.0f, 1.0f, 0.0f },
							.intensity = 0.15f,
							.falloffExponent = 3.0f });
						pointObjects.insert(V { pos, { p, l } });
					}
				};
				for(size_t y = 0; y < world.height(); ++ y)
				for(size_t x = 0; x < world.width();  ++ x) {
					bool invert = std::uniform_int_distribution<unsigned>(0, 1)(rng) == 1;
					newObject.position_xyz = { + float(x) - xGridCenter, 0.0f, - float(y) - yGridCenter };
					newObject.scale_xyz.x *= invert? -1.0f : +1.0f;
					newObject.scale_xyz.z *= invert? -1.0f : +1.0f;
					newObject.direction_ypr = {
						discreteObjRotation(std::uniform_int_distribution<uint_fast8_t>(0, 20)(rng)),
						0.0f,
						0.0f };
					assert(x < world.width());
					assert(y < world.height());
					switch(world.tile(x, y)) {
						case GridObjectClass::eBoost:    tryCreate(objectsOs, rndMdlFrom(mdlIds.boost)); break;
						case GridObjectClass::ePoint:    insPoint (pointOs); break;
						case GridObjectClass::eObstacle: tryCreate(objectsOs, rndMdlFrom(mdlIds.obstacle)); break;
						case GridObjectClass::eWall:     tryCreate(objectsOs, rndMdlFrom(mdlIds.wall)); break;
						default:
							logger.warn("World object at ({}, {}) has unknown type {}", x, y, grid_object_class_e(world.tile(x, y)));
							[[fallthrough]];
						case GridObjectClass::eNoObject: break;
					}
				}
				logger.info("World generated with {} points", pointObjects.size());
				newObject.position_xyz = gridToWorld({ int64_t(world.entryPointX()), int64_t(world.entryPointY()) }, 0.0f);
				newObject.direction_ypr = { };
				newObject.scale_xyz = { 1.0f, 1.0f, 1.0f };
				playerHead = tryCreate(playerOs, mdlIds.playerHead);
				newObject.position_xyz = { };
				newObject.direction_ypr = { };
				newObject.scale_xyz = { 1.0f, 1.0f, 1.0f };
				tryCreate(sceneryOs, mdlIds.scenery);
				sharedState->camRotation.setValue({ 0.0f, cameraPitch, 0.0f });
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

			sharedState->enableCulling = 0b11 * wr.isFrustumCullingEnabled();
			sharedState->quitReason = QuitReason::eNoQuit;
		}


		void loop_end() noexcept override {
			inputMan.clear();
		}


		void loop_processEvents(tickreg::delta_t deltaAvg, tickreg::delta_t delta) override {
			(void) deltaAvg;
			(void) delta;

			auto ca = engine->getConcurrentAccess();
			auto& shState = *sharedState;

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

				{ // This block is not macrotick-related, but it's a potential race condition nevertheless
					if(shState.requestMapRegen) {
						shState.requestMapRegen = false;
						createWorld(worldFilename);
						shState.quitReason = QuitReason::eGameEnd;
					}
				}

				if(macrotickProgress >= 1.0f) [[unlikely]] {
					#ifdef VS_CODE_HEADER_LINTING_WORKAROUND
						#define CONSTEXPR_ (void)0;
					#else
						#define CONSTEXPR_ constexpr
					#endif
					CONSTEXPR_ auto pi = std::numbers::pi_v<float>;
					CONSTEXPR_ auto pi2 = 2.0f * pi;
					#undef CONSTEXPR_

					-- macrotickProgress;
					if(inputMan.isCommandActive(cmdBoost)) shState.speedBoost = speedBoostFromInput;
					macrotickFrequency = shState.speedBase + shState.speedBoost;
					macrotickLock.unlock();

					if     (shState.speedBoost > 0.0f) shState.speedBoost = std::max(0.0f, shState.speedBoost - speedBoostDecayDn);
					else if(shState.speedBoost < 0.0f) shState.speedBoost = std::min(0.0f, shState.speedBoost + speedBoostDecayUp);

					const auto worldPos = shState.playerHeadPos.getValue();
					const auto gridPos = worldToGrid(worldPos);
					auto xApprox = std::floorf(worldPos.x + 0.5f);
					auto zApprox = std::floorf(worldPos.z + 0.5f);

					{ // Player-environment interaction
						auto obj = pointObjects.find({ gridPos.x, gridPos.y });
						if(obj != pointObjects.end()) [[unlikely]] {
							ske::ObjectStorage& pointsOs = rproc->getObjectStorage(OBJSTG_POINTS_IDX);
							pointsOs.removeObject(engine->getTransferContext(), obj->second.first);
							if(obj->second.second != idgen::invalidId<ske::ObjectId>())
								rproc->worldRenderer()->removeLight(obj->second.second);
							pointObjects.erase(obj);
							if(pointObjects.empty()) {
								logger.info("Conglaturations! Shine get!");
								createWorld(worldFilename);
								shState.quitReason = QuitReason::eGameEnd;
							}
						}
					}

					{ // Player movement animations
						auto xDiff = (xApprox - shState.lastDir[0]) - worldPos.x;
						auto zDiff = (zApprox + shState.lastDir[1]) - worldPos.z;
						auto yawDiff = std::atan2f(-xDiff, -zDiff) - shState.headYawTarget;
						while(yawDiff >= +pi) yawDiff -= pi2;
						while(yawDiff <= -pi) yawDiff += pi2;
						shState.headYawTarget += yawDiff;
						{
							auto lock = std::unique_lock(shState.animMutex);
							shState.playerMovementAnim.interrupt(playerHeadPosAnimId);
							playerHeadPosAnimId = shState.playerMovementAnim.start<anim::target::Linear<glm::vec3>>(
								ske::AnimEndAction::ePause,
								shState.playerHeadPos,
								worldPos,
								glm::vec3 { xDiff, 0.0f, zDiff } );
						}
					}
				}
			}
		}

		LoopState loop_pollState() const noexcept override {
			return (sharedState->quitReason == QuitReason::eNoQuit)?
				LoopState::eShouldContinue :
				LoopState::eShouldStop;
		}


		void loop_async_preRender(ske::ConcurrentAccess, tickreg::delta_t deltaAvg, tickreg::delta_t deltaPrevious) override {
			(void) deltaPrevious;

			deltaAvg = std::min(tickreg::delta_t(0.5), deltaAvg);

			bool cullingWasEnabled = (sharedState->enableCulling & 0b10) == 0b10;
			bool cullingIsEnabled  = (sharedState->enableCulling & 0b01) == 0b01;
			if(cullingWasEnabled != cullingIsEnabled) [[unlikely]] {
				sharedState->enableCulling = cullingIsEnabled | (cullingIsEnabled << 1);
				rproc->worldRenderer()->setFrustumCulling(cullingIsEnabled);
				logger.info("{}abled frustum culling", cullingIsEnabled? "En":"Dis");
			}

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
		prefs.init_present_extent = { 700, 500 };
		prefs.max_render_extent   = { 0, 0 };
		prefs.present_mode        = VK_PRESENT_MODE_MAILBOX_KHR;
		prefs.target_framerate    = 72.0f;
		prefs.target_tickrate     = 60.0f;
		prefs.wait_for_gframe     = false;
		prefs.framerate_samples   = 4;
		return prefs;
	} ();

	const auto worldRdrParams = []() {
		auto params = WorldRenderer::RdrParams::defaultParams;
		params.fovY                        = glm::radians(80.0f);
		params.shadeStepCount              = 7;
		params.pointLightDistanceThreshold = 1.0f / 64.0f;
		params.shadeStepSmoothness         = 1.0f;
		params.shadeStepExponent           = 4.0f;
		params.ditheringSteps              = 256.0f;
		return params;
	} ();

	const auto uiRdrParams = []() {
		auto params = UiRenderer::RdrParams::defaultParams;
		return params;
	} ();

	try {
		auto shader_cache   = std::make_shared<ske::BasicShaderCache>("assets/", logger);
		auto asset_cache    = std::make_shared<ske::BasicAssetCache>("assets/", logger);
		auto basic_rprocess = std::make_shared<ske::BasicRenderProcess>();
		BasicRenderProcess::setup(*basic_rprocess, logger, worldRdrParams, uiRdrParams, asset_cache, sneka::OBJSTG_COUNT, 0.125);

		auto engine = ske::Engine(
			ske::DeviceInitInfo {
				.window_title     = "Sneka 3D",
				.application_name = "Sneka 3D",
				.app_version = VK_MAKE_API_VERSION(0, 0, 1, 0) },
			enginePrefs,
			std::move(shader_cache),
			logger );

		auto loop = sneka::Loop(engine, logger, asset_cache, basic_rprocess);
		sneka::Loop::QuitReason loopQuitReason;

		do {
			engine.run(loop, basic_rprocess);
			loopQuitReason = loop.sharedState->quitReason;
			loop.reset();
		} while(loopQuitReason == sneka::Loop::QuitReason::eGameEnd);

		BasicRenderProcess::destroy(*basic_rprocess, engine.getTransferContext());

		logger.info("Successfully exiting the program.");
	} catch(posixfio::Errcode& e) {
		logger.error("Uncaught posixfio error: {}", e.errcode);
	} catch(vkutil::VulkanError& e) {
		logger.error("Uncaught Vulkan error: {}", e.what());
	}
}
