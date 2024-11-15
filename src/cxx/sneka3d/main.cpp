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



namespace sneka {

	using std::byte;

	enum class GridObjectClass : uint8_t { eNoObject = 0, eBoost = 1, ePoint = 2, eObstacle = 3, eWall = 4 };


	struct World {
		struct Header {
			uint64_t width;
			uint64_t height;

			void toLittleEndian() noexcept {
				if constexpr (std::endian::native == std::endian::big) {
					width  = std::byteswap(width);
					height = std::byteswap(height);
				}
			}
		};

		struct BadFile {
			size_t atByte;
		};


		std::unique_ptr<byte[]> rawData;


		static World fromFile(const char* filename) {
			auto file = posixfio::File::open(filename, posixfio::OpenFlags::eRdonly);
			Header h;
			auto rd = posixfio::readAll(file, &h, sizeof(Header));
			if(rd != sizeof(Header)) throw BadFile { size_t(rd) };
			h.toLittleEndian();
			size_t gridBytes = sizeof(uint8_t) * h.width * h.height;
			auto r = World { .rawData = std::make_unique_for_overwrite<byte[]>(sizeof(Header) + gridBytes) };
			memcpy(r.rawData.get(), &h, sizeof(Header));
			rd = posixfio::readAll(file, r.rawData.get() + sizeof(Header), gridBytes);
			if(size_t(rd) != gridBytes) throw BadFile { sizeof(Header) + size_t(rd) };
			return r;
		}

		static World initEmpty(uint64_t width, uint64_t height) {
			size_t gridBytes = sizeof(uint8_t) * width * height;
			auto r = World { .rawData = std::make_unique_for_overwrite<byte[]>(sizeof(Header) + gridBytes) };
			Header& h = * reinterpret_cast<Header*>(r.rawData.get());
			h = { width, height };
			h.toLittleEndian();
			memset(r.rawData.get() + sizeof(Header), 0, gridBytes);
			return r;
		}

		void toFile(const char* filename) {
			assert(rawData);
			auto file = posixfio::File::open(filename, posixfio::OpenFlags::eWronly);
			Header& h = * reinterpret_cast<Header*>(rawData.get());
			posixfio::writeAll(file, rawData.get(), sizeof(Header) * h.width * h.height);
		}


		auto  width () const { assert(rawData); return reinterpret_cast<const Header*>(rawData.get())->width ; }
		auto  height() const { assert(rawData); return reinterpret_cast<const Header*>(rawData.get())->height; }
		auto& tile(uint64_t x, uint64_t y)       { assert(rawData); return * reinterpret_cast<GridObjectClass*>(rawData.get() + sizeof(Header) + (y * width()) + x); }
		auto  tile(uint64_t x, uint64_t y) const { return const_cast<World*>(this)->tile(x, y); }
	};


	class Loop : public ske::LoopInterface {
	public:
		struct CallbackSharedState {
			bool quit = false;
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
			world = World::initEmpty(5, 5);
			#define T_(X_, Y_) world.tile(X_, Y_) = eWall;
			T_(0, 4) T_(1, 4) T_(2, 4) T_(3, 4) T_(4, 4)
			T_(0, 3) T_(1, 3)                   T_(4, 3)
			T_(0, 2)                            T_(4, 2)
			T_(0, 1)          T_(2, 1) T_(3, 1) T_(4, 1)
			T_(0, 0) T_(1, 0) T_(2, 0) T_(3, 0) T_(4, 0)
			#undef T_
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
				auto bindKey = [&](SDL_KeyCode kc, std::string ctx) {
					auto key = ske::InputMapKey { ske::inputIdFromSdlKey(kc), ske::InputState::eActive };
					return inputMan.bindNewCommand(ske::Binding { key, std::move(ctx) }, nullptr);
				};
				cmdForward  = bindKey(SDLK_w, "general");
				cmdBackward = bindKey(SDLK_s, "general");
				cmdLeft     = bindKey(SDLK_a, "general");
				cmdRight    = bindKey(SDLK_d, "general");
				bindKeyCb(SDLK_q, "general", [sharedState](const ske::Context&, ske::Input) { sharedState->quit = true; });
			}

			auto& tc = engine->getTransferContext();
			auto newObject = ske::ObjectStorage::NewObject {
				"gold-bars.fma", { }, { }, { 0.95f, 0.95f, 0.95f }, false };
			for(size_t y = 0; y < world.height(); ++ y)
			for(size_t x = 0; x < world.width();  ++ x) {
				if(world.tile(x, y) == GridObjectClass::eWall) {
					newObject.position_xyz = { + float(x), 0.0f, - float(y) };
					(void) os.createObject(tc, newObject);
				}
			}
			wr.setViewRotation({ 0.0f, 0.4f, 0.0f });
			wr.setViewPosition({ 2.0f, 1.1f, 0.0f });
			wr.setAmbientLight({ 0.1f, 0.1f, 0.1f });
			light = wr.createPointLight(ske::WorldRenderer::NewPointLight {
				.position = { 2.0f, 1.4f, -2.0f },
				.color = { 0.9f, 0.9f, 1.0f },
				.intensity = 4.0f,
				.falloffExponent = 1.0f });

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

			{
				ske::WorldRenderer& wr = * rproc->worldRenderer();
				auto inputLock = std::unique_lock(inputManMutex);
				auto activeCmds = inputMan.getActiveCommands();
				for(const auto& [cmd, key] : activeCmds) {
					if     (cmd == cmdForward ) { auto pos = wr.getViewPosition(); pos.z -= delta_avg; wr.setViewPosition(pos); }
					else if(cmd == cmdBackward) { auto pos = wr.getViewPosition(); pos.z += delta_avg; wr.setViewPosition(pos); }
					else if(cmd == cmdLeft    ) { auto pos = wr.getViewPosition(); pos.x -= delta_avg; wr.setViewPosition(pos); }
					else if(cmd == cmdRight   ) { auto pos = wr.getViewPosition(); pos.x += delta_avg; wr.setViewPosition(pos); }
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
		prefs.init_present_extent   = { 300, 300 };
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
