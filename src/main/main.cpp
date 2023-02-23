#include <unistd.h>

#include <engine/engine.hpp>

#include <spdlog/spdlog.h>



int main() {
	#ifdef NDEBUG
		spdlog::set_level(spdlog::level::info);
	#else
		spdlog::set_level(spdlog::level::debug);
	#endif

	auto prefs = SKENGINE_NAME_NS::EnginePreferences::default_prefs;
	prefs.phys_device_uuid = "00000000-0300-0000-0000-000000000000";

	try {
		SKENGINE_NAME_NS::BasicShaderCache shader_cache;

		auto engine = SKENGINE_NAME_NS::Engine(
			SKENGINE_NAME_NS::DeviceInitInfo {
				.window_title     = SKENGINE_NAME_CSTR " Test Window",
				.application_name = SKENGINE_NAME_PC_CSTR,
				.app_version = VK_MAKE_API_VERSION(0, 0, 0, 0) },
			prefs );
	} catch(posixfio::Errcode& e) {
		spdlog::error("Uncaught posixfio error: {}", e.errcode);
	}
}
