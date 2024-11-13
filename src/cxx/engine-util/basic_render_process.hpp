#pragma once

#include <skengine_fwd.hpp>

#include "world_renderer.hpp"
#include "ui_renderer.hpp"

#include <engine/renderprocess/interface.hpp>
#include <engine/types.hpp>



namespace SKENGINE_NAME_NS {

	class BasicRenderProcess : public RenderProcessInterface {
	public:
		static void setup(BasicRenderProcess&, Logger, std::shared_ptr<AssetSourceInterface>, float max_sampler_anisotropy);
		static void destroy(BasicRenderProcess&, TransferContext);

		#ifndef NDEBUG
			~BasicRenderProcess();
		#endif

		void rpi_createRenderers(ConcurrentAccess&) override;
		void rpi_setupRenderProcess(ConcurrentAccess&, RenderProcess::DependencyGraph&) override;
		void rpi_destroyRenderProcess(ConcurrentAccess&) override;
		void rpi_destroyRenderers(ConcurrentAccess&) override;

		#define M_ACCESS_(FN_, M_) template <typename T> auto& FN_ (this T& self) { return self.brp_ ## M_; }
		M_ACCESS_(objectStorage, objStorage   )
		M_ACCESS_(worldRenderer, worldRenderer)
		M_ACCESS_(uiRenderer   , uiRenderer   )
		#undef M_ACCESS_

	private:
		AssetSupplier brp_assetSupplier;
		std::shared_ptr<WorldRendererSharedState> brp_worldRendererSs;
		std::shared_ptr<ObjectStorage> brp_objStorage;
		std::shared_ptr<WorldRenderer> brp_worldRenderer;
		std::shared_ptr<UiRenderer>    brp_uiRenderer;
		RenderTargetId brp_depthRtarget;
		RenderTargetId brp_worldRtarget;
		RenderTargetId brp_uiRtarget;
	};

}
