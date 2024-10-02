#pragma once

#include <cstdint>
#include <cstdlib>
#include <memory>

#include <sflog.hpp>



inline namespace main_ns {

	using Logger = sflog::Logger<std::shared_ptr<posixfio::OutputBuffer>>;


	enum class PresentMode : uint32_t {
		eFifo,
		eMailbox,
		eImmediate
	};


	struct Extent {
		uint32_t width;
		uint32_t height;
	};


	struct Settings {
		Extent      initialPresentExtent;
		Extent      maxRenderExtent;
		PresentMode presentMode;
		uint32_t    shadeStepCount;
		float       shadeStepSmooth;
		float       shadeStepGamma;
		float       ditheringSteps;
		uint32_t    framerateSamples;
		float       targetFramerate;
		float       targetTickrate;
		float       fieldOfView;
		sflog::Level logLevel;
	};


	void parseSettings(
		Settings* dst,
		const posixfio::MemMapping& data,
		Logger& );

}
