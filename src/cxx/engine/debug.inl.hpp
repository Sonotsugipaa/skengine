#pragma once

#ifdef NDEBUG
	extern "C" {
		#include <unistd.h>
	}
	#include <memory>
	#include <posixfio_tl.hpp>
	#include <spdlog/logger.h>
	#include <vulkan/vulkan.h>
#endif



namespace debug {

	#ifndef NDEBUG
		struct State {
			std::shared_ptr<spdlog::logger> logger;
		};
		inline State state = { };
	#endif


	template <typename Logger>
	inline void setLogger(Logger& logger) {
		(void) logger;
		#ifndef NDEBUG
			state.logger = logger;
		#endif
	}


	template <typename BufferType, typename StringType>
	inline void createdBuffer(BufferType b, const StringType& usage) {
		(void) b; (void) usage;
		#ifndef NDEBUG
			state.logger->trace("Created VkBuffer {:016x} : {}", size_t(VkBuffer(b)), usage);
		#endif
	}

	template <typename BufferType, typename StringType>
	inline void destroyedBuffer(BufferType b, const StringType& usage) {
		(void) b; (void) usage;
		#ifndef NDEBUG
			state.logger->trace("Destroyed VkBuffer {:016x} : {}", size_t(VkBuffer(b)), usage);
		#endif
	}

}
