#pragma once

#ifndef NDEBUG
	extern "C" {
		#include <unistd.h>
	}
	#include <utility>
	#include <string_view>
	#include <sflog.hpp>
	#include <vulkan/vulkan.h>
#endif



namespace debug {

	#ifndef NDEBUG
		struct State {
			using Buffer = posixfio::ArrayOutputBuffer<>;
			Buffer buffer;
			sflog::Logger<Buffer*> logger;
		};
		inline State state = []() {
			using namespace std::string_view_literals;
			static auto buf = State::Buffer(STDOUT_FILENO);
			return State { std::move(buf), sflog::Logger<State::Buffer*>(&buf, sflog::Level::eTrace, sflog::AnsiSgr::eYes, ""sv, "[Skengine "sv, "]  "sv, ""sv) }; } ();
	#endif


	template <typename BufferType, typename StringType>
	inline void createdBuffer(BufferType b, const StringType& usage) {
		(void) b; (void) usage;
		#ifndef NDEBUG
			state.logger.trace("Created VkBuffer {:016x} : {}", size_t(VkBuffer(b)), usage);
		#endif
	}

	template <typename BufferType, typename StringType>
	inline void destroyedBuffer(BufferType b, const StringType& usage) {
		(void) b; (void) usage;
		#ifndef NDEBUG
			state.logger.trace("Destroyed VkBuffer {:016x} : {}", size_t(VkBuffer(b)), usage);
		#endif
	}

}
