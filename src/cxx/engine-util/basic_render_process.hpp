#pragma once

#include <skengine_fwd.hpp>

#include "world_renderer.hpp"
#include "ui_renderer.hpp"

#include <engine/renderprocess/interface.hpp>
#include <engine/types.hpp>



namespace SKENGINE_NAME_NS {

	class BasicRenderProcess : public RenderProcessInterface {
	public:
		static void setup(
			BasicRenderProcess&,
			Logger,
			WorldRenderer::RdrParams,
			UiRenderer::RdrParams,
			std::shared_ptr<AssetCacheInterface>,
			size_t objectStorageCount,
			float max_sampler_anisotropy );
		static void destroy(BasicRenderProcess&, TransferContext);

		#ifndef NDEBUG
			~BasicRenderProcess();
		#endif

		void rpi_createRenderers(ConcurrentAccess&) override;
		void rpi_setupRenderProcess(ConcurrentAccess&, RenderProcess::DependencyGraph&) override;
		void rpi_destroyRenderProcess(ConcurrentAccess&) override;
		void rpi_destroyRenderers(ConcurrentAccess&) override;

		#define M_ACCESS_(FN_, M_) template <typename T> auto& FN_ (this T& self) { return self.brp_ ## M_; }
		M_ACCESS_(worldRenderer, worldRenderer)
		M_ACCESS_(uiRenderer   , uiRenderer   )
		#undef M_ACCESS_

		ObjectStorage& getObjectStorage(size_t key) noexcept;

	private:
		AssetSupplier brp_assetSupplier;
		WorldRenderer::RdrParams brp_worldRdrParams;
		UiRenderer::RdrParams    brp_uiRdrParams;
		std::shared_ptr<WorldRendererSharedState> brp_worldRendererSs;
		std::shared_ptr<std::vector<ObjectStorage>> brp_objStorages;
		std::shared_ptr<WorldRenderer> brp_worldRenderer;
		std::shared_ptr<UiRenderer>    brp_uiRenderer;
		RenderTargetId brp_depthRtarget;
		RenderTargetId brp_worldRtarget;
		RenderTargetId brp_uiRtarget;
	};

}
