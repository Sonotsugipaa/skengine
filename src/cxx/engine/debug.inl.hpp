#pragma once

#ifndef NDEBUG
	extern "C" {
		#include <unistd.h>
	}
	#include <utility>
	#include <string_view>
	#include <sflog.hpp>
	#include <vulkan/vulkan.h>
	#include "types.hpp"
#endif



namespace SKENGINE_NAME_NS::debug {

	#ifndef NDEBUG
		inline Logger logger;
	#endif


	template <typename Logger>
	inline void setLogger(const Logger& l) {
		(void) l;
		#ifndef NDEBUG
			using namespace std::string_view_literals;
			logger = cloneLogger(l, "["sv, std::string_view(SKENGINE_NAME_PC_CSTR ":Debug "), ""sv, "]  "sv);
		#endif
	}


	template <typename BufferType, typename StringType>
	inline void createdBuffer(BufferType b, const StringType& usage) {
		(void) b; (void) usage;
		#ifndef NDEBUG
			assert(logger.sink());
			assert(logger.sink()->file());
			logger.debug("Created VkBuffer {:016x} : {}", size_t(VkBuffer(b)), usage);
		#endif
	}

	template <typename BufferType, typename StringType>
	inline void destroyedBuffer(BufferType b, const StringType& usage) {
		(void) b; (void) usage;
		#ifndef NDEBUG
			assert(logger.sink());
			assert(logger.sink()->file());
			logger.debug("Destroyed VkBuffer {:016x} : {}", size_t(VkBuffer(b)), usage);
		#endif
	}

}
