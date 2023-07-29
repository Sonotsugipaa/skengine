#include "engine.hpp"

#include "init/init.hpp"

#include <posixfio_tl.hpp>

#include <spdlog/spdlog.h>

#include <vk-util/error.hpp>



namespace SKENGINE_NAME_NS {

	Engine::Engine(const DeviceInitInfo& di, const EnginePreferences& ep):
			mGframeSem      (0),
			mDescProxyMutex (1)
	{
		{
			auto init = Engine::DeviceInitializer(*this, &di, &ep);
			init.init();
		}

		{
			auto rpass_cfg = RpassConfig::default_cfg;
			auto init = Engine::RpassInitializer(*this);
			init.init(rpass_cfg);
		}
	}


	Engine::~Engine() {
		{
			auto init = Engine::RpassInitializer(*this);
			init.destroy();
		}

		{
			auto init = Engine::DeviceInitializer(*this, nullptr, nullptr);
			init.destroy();
		}
	}


	template <>
	VkShaderModule Engine::createShaderModuleFromMemory(
			std::span<const uint32_t> code
	) {
		VkShaderModuleCreateInfo sm_info = { };
		sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		sm_info.pCode    = code.data();
		sm_info.codeSize = code.size_bytes();
		VkShaderModule r;
		VK_CHECK(vkCreateShaderModule, mDevice, &sm_info, nullptr, &r);
		spdlog::debug("Loaded shader module from memory");
		return r;
	}


	VkShaderModule Engine::createShaderModuleFromFile(
			const std::string& file_path
	) {
		static_assert(4 == sizeof(uint32_t));

		VkShaderModuleCreateInfo    sm_info = { };
		std::unique_ptr<uint32_t[]> buffer;
		try {
			auto file    = posixfio::File::open(file_path.c_str(), O_RDONLY);
			size_t lsize = file.lseek(0, SEEK_END);
			if(lsize > UINT32_MAX) throw ShaderModuleReadError("Shader file is too long");
			if(lsize % 4 != 0)     throw ShaderModuleReadError("Misaligned shader file size");
			file.lseek(0, SEEK_SET);
			buffer    = std::make_unique_for_overwrite<uint32_t[]>(lsize / 4);
			size_t rd = posixfio::readAll(file, buffer.get(), lsize);
			if(rd != lsize) throw ShaderModuleReadError("Shader file partially read");
			sm_info.codeSize = uint32_t(lsize);
		} catch(const posixfio::FileError& e) {
			switch(e.errcode) {
				using namespace std::string_literals;
				case ENOENT: throw ShaderModuleReadError("Shader file not found: \""s      + file_path + "\""s); break;
				case EACCES: throw ShaderModuleReadError("Shader file not accessible: \""s + file_path + "\""s); break;
				default: throw e;
			}
		}
		sm_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		sm_info.pCode = buffer.get();
		VkShaderModule r;
		VK_CHECK(vkCreateShaderModule, mDevice, &sm_info, nullptr, &r);
		spdlog::debug("Loaded shader module from file \"{}\"", file_path);
		return r;
	}


	void Engine::destroyShaderModule(VkShaderModule module) {
		vkDestroyShaderModule(mDevice, module, nullptr);
	}


	void Engine::run(
			ShaderCacheInterface& shaders,
			LoopInterface&        loop
	) {
		loop_begin:

		auto loop_state = loop.loop_pollState();
		if(loop_state == LoopInterface::LoopState::eShouldStop) return;

		if(loop_state == LoopInterface::LoopState::eShouldDelay) {
			spdlog::warn("Engine instructed to delay the loop, but the functionality isn't implemented yet");
		}

		spdlog::info("zzzzzz");
		loop.loop_processEvents();
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		spdlog::info("I slept!");

		goto loop_begin;
	}

}
