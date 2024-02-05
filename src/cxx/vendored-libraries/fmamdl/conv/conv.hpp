#pragma once

#include <vector>
#include <string_view>

#include <posixfio_tl.hpp>



namespace fmamdl::conv {

	extern posixfio::OutputBuffer stdout_b;
	extern posixfio::OutputBuffer stderr_b;


	enum class SrcFormat {
		eObj
	};


	struct Options {
		std::string_view srcName;
		std::string_view dstName;
		std::string_view mainBone;
		std::string_view texturePrefix;
		bool noMaterials   : 1;
		bool onlyMaterials : 1;
	};

}
