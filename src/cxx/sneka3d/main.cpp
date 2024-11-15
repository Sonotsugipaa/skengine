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

	class Loop : public ske::LoopInterface {
	public:
		struct CallbackSharedState {
			signed char lastDir[2] = { 0, -1 };
			float       yawTarget  = 0.0f;
			bool        quit       = false;
		};

		ske::Engine* engine;
		std::shared_ptr<ske::BasicRenderProcess> rproc;
		std::shared_ptr<CallbackSharedState> sharedState;
		ske::InputManager inputMan;
		std::mutex inputManMutex;
		ske::ObjectId light;
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
			using enum GridObjectClass;
			world = World::initEmpty(100, 100);
			ske::Logger& logger = engine->logger();
			#define NOW_ std::chrono::steady_clock::now().time_since_epoch().count()
			generateWorldNoise(logger, world, std::minstd_rand(NOW_));
			generateWorldPath(logger, world, std::minstd_rand(NOW_), UINT_MAX);
			generateWorldPath(logger, world, std::minstd_rand(NOW_), 100);
			generateWorldPath(logger, world, std::minstd_rand(NOW_), 100);
			for(unsigned i = 0; i < 10; ++i) {
				generateWorldPath(logger, world, std::minstd_rand(NOW_), 10); }
			#undef NOW_
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
				"gold-bars.fma", { }, { }, { 1.0f, 1.0f, 1.0f }, false };
			for(size_t y = 0; y < world.height(); ++ y)
			for(size_t x = 0; x < world.width();  ++ x) {
				if(world.tile(x, y) == GridObjectClass::eWall) {
					newObject.position_xyz = { + float(x), 0.0f, - float(y) };
					(void) os.createObject(tc, newObject);
				}
			}
			wr.setViewRotation({ 0.0f, 0.7f, 0.0f });
			wr.setViewPosition({ xGridCenter, 1.5f, 0.0f });
			wr.setAmbientLight({ 0.1f, 0.1f, 0.1f });
			light = wr.createPointLight(ske::WorldRenderer::NewPointLight {
				.position = { xGridCenter, 10.0f, yGridCenter },
				.color = { 0.9f, 0.9f, 1.0f },
				.intensity = 4.0f,
				.falloffExponent = 0.2f });

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

			SDL_Event ev;
			while(1 == SDL_PollEvent(&ev)) {
				auto inputLock = std::unique_lock(inputManMutex);
				inputMan.feedSdlEvent("general", ev);
			}
		}

		LoopState loop_pollState() const noexcept override {
			return sharedState->quit? LoopState::eShouldStop : LoopState::eShouldContinue;
		}


		void loop_async_preRender(ske::ConcurrentAccess, tickreg::delta_t delta_avg, tickreg::delta_t delta_previous) override {
			(void) delta_previous;

			delta_avg = std::min(tickreg::delta_t(0.5), delta_avg);

			float speed = 4.0f * delta_avg;

			{
				ske::WorldRenderer& wr = * rproc->worldRenderer();
				auto inputLock = std::unique_lock(inputManMutex);
				auto activeCmds = inputMan.getActiveCommands();
				signed char shift = 0;
				for(const auto& [cmd, key] : activeCmds) {
					if     (cmd == cmdForward ) { shift -= 1; }
					else if(cmd == cmdBackward) { shift += 1; }
				}
				auto rot = wr.getViewRotation();
				auto* vec = sharedState->lastDir;
				auto yawDiff = sharedState->yawTarget - rot.x;
				if(yawDiff < +0.001f || yawDiff > -0.001f) {
					constexpr float rotBias = 0.1f;
					constexpr float pi = std::numbers::pi_v<float>;
					if     (yawDiff > +pi) rot.x += pi*2.0f;
					else if(yawDiff < -pi) rot.x -= pi*2.0f;
					rot.x = (rot.x + (sharedState->yawTarget * rotBias)) / (1.0f + rotBias);
					wr.setViewRotation(rot);
				}
				if(shift != 0) {
					auto pos = wr.getViewPosition();
					pos.x += float(vec[0] * shift) * speed;
					pos.z -= float(vec[1] * shift) * speed;
					wr.setViewPosition(pos);
				}
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
