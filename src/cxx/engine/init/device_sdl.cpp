#include "init.hpp"

#include <SDL2/SDL.h>



namespace SKENGINE_NAME_NS {

	unsigned long sdl_init_counter = 0;


	void Engine::DeviceInitializer::initSdl(const DeviceInitInfo* device_init_info) {
		if(0 == mPrefs.init_present_extent.width * mPrefs.init_present_extent.height) {
			throw std::invalid_argument("Initial window area cannot be 0");
		}

		if(! sdl_init_counter) {
			if(0 != SDL_InitSubSystem(SDL_INIT_VIDEO)) {
				using namespace std::string_literals;
				const char* err = SDL_GetError();
				throw std::runtime_error(("failed initialize the SDL Video subsystem ("s + err) + ")");
			}

			if(0 != SDL_Vulkan_LoadLibrary(nullptr)) {
				using namespace std::string_literals;
				const char* err = SDL_GetError();
				throw std::runtime_error(("failed to load a Vulkan library ("s + err) + ")");
			}
		}
		++ sdl_init_counter;

		uint32_t window_flags =
			SDL_WINDOW_SHOWN |
			SDL_WINDOW_VULKAN |
			SDL_WINDOW_RESIZABLE |
			(SDL_WINDOW_FULLSCREEN * mPrefs.fullscreen);

		mSdlWindow = SDL_CreateWindow(
			device_init_info->window_title.c_str(),
			0, 0,
			mPrefs.init_present_extent.width, mPrefs.init_present_extent.height,
			window_flags );

		{ // Change the present extent, as the window decided it to be
			int w, h;
			auto& w0 = mPrefs.init_present_extent.width;
			auto& h0 = mPrefs.init_present_extent.height;
			SDL_Vulkan_GetDrawableSize(mSdlWindow, &w, &h);
			if(uint32_t(w) != w0 || uint32_t(h) != h0) {
				mPrefs.init_present_extent = { uint32_t(w), uint32_t(h) };
				logger().warn("Requested window size {}x{}, got {}x{}", w0, h0, w, h);
			}
		}

		if(mSdlWindow == nullptr) {
			using namespace std::string_literals;
			const char* err = SDL_GetError();
			throw std::runtime_error(("failed to create an SDL window ("s + err) + ")");
		}
	}


	void Engine::DeviceInitializer::destroySdl() {
		if(mSdlWindow != nullptr) {
			SDL_DestroyWindow(mSdlWindow);
			mSdlWindow = nullptr;
		}

		assert(sdl_init_counter > 0);
		-- sdl_init_counter;
		if(sdl_init_counter == 0) {
			SDL_Vulkan_UnloadLibrary();
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			SDL_Quit();
		}
	}

}
