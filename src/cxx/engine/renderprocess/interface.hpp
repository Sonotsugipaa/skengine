#pragma once

#include "render_process.hpp"



namespace SKENGINE_NAME_NS {

	struct ConcurrentAccess;


	struct RenderProcessInterface {
		virtual void rpi_createRenderers(ConcurrentAccess&) = 0;
		virtual void rpi_setupRenderProcess(ConcurrentAccess&, RenderProcess::DependencyGraph&) = 0;
		virtual void rpi_destroyRenderProcess(ConcurrentAccess&) = 0;
		virtual void rpi_destroyRenderers(ConcurrentAccess&) = 0;
	};

}
