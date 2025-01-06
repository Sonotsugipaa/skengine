#include "basic_render_process.hpp"

#include <engine/engine.hpp>
#include <engine/types.hpp>

#include <cassert>
#include <string_view>

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>



namespace SKENGINE_NAME_NS {

	void BasicRenderProcess::setup(
		BasicRenderProcess& brp,
		Logger logger,
		std::shared_ptr<AssetCacheInterface> aci,
		size_t obj_storage_count,
		float max_sampler_anisotropy
	) {
		assert(! brp.brp_assetSupplier.isInitialized());
		brp.brp_assetSupplier = AssetSupplier(std::move(logger), std::move(aci), max_sampler_anisotropy);
		brp.brp_objStorages = std::make_shared<std::vector<ObjectStorage>>();
		brp.brp_objStorages->resize(obj_storage_count);
	}


	void BasicRenderProcess::destroy(BasicRenderProcess& brp, TransferContext transfCtx) {
		assert(brp.brp_assetSupplier.isInitialized());
		brp.brp_assetSupplier.destroy(transfCtx);
	}


	#ifndef NDEBUG
		BasicRenderProcess::~BasicRenderProcess() {
			assert(! brp_assetSupplier.isInitialized());
			assert(! brp_worldRenderer);
			assert(! brp_uiRenderer);
			assert(! brp_objStorages);
			assert(! brp_worldRendererSs);
		}
	#endif


	inline namespace basic_render_process_impl {

		auto copyLogger(const Logger& cp, std::string_view sub) {
			using namespace std::string_view_literals;
			std::string cat = std::string(SKENGINE_NAME_PC_CSTR ":");
			cat.reserve(cat.size() + sub.size() + 1);
			cat.append(sub);
			cat.push_back(' ');
			return cloneLogger(cp, "["sv, cat, ""sv, "]  "sv);
		};

	}


	void BasicRenderProcess::rpi_createRenderers(ConcurrentAccess& ca) {
		auto& e = ca.engine();
		const auto& prefs = e.getPreferences();

		brp_worldRendererSs = std::make_shared_for_overwrite<WorldRendererSharedState>();
		WorldRenderer::initSharedState(e.getDevice(), *brp_worldRendererSs);

		assert(brp_objStorages);
		assert(! brp_objStorages->empty());
		for(auto& osSlot : *brp_objStorages) {
			osSlot = ObjectStorage::create(
				copyLogger(e.logger(), "ObjStorage"),
				brp_worldRendererSs,
				e.getVmaAllocator(),
				brp_assetSupplier );
		}

		const auto worldProj = WorldRenderer::ProjectionInfo {
			.verticalFov = prefs.fov_y,
			.zNear       = prefs.z_near,
			.zFar        = prefs.z_far };

		constexpr WorldRenderer::PipelineParameters outlinePlParams = [&]() {
			auto r = WorldRenderer::defaultPipelineParams;
			r.cullMode = VK_CULL_MODE_FRONT_BIT;
			r.shaderRequirement = ShaderRequirement { .name = "outline", .pipelineLayout = PipelineLayoutId::e3d };
			return r;
		} ();

		brp_worldRenderer = std::make_shared<WorldRenderer>(WorldRenderer::create(
			copyLogger(e.logger(), "WorldRdr"),
			e.getVmaAllocator(),
			brp_worldRendererSs,
			brp_objStorages,
			worldProj,
			{ WorldRenderer::defaultPipelineParams, outlinePlParams } ));

		brp_uiRenderer = std::make_shared<UiRenderer>(UiRenderer::create(
			e.getVmaAllocator(),
			copyLogger(e.logger(), "UiRdr"),
			prefs.font_location ));
	}


	void BasicRenderProcess::rpi_setupRenderProcess(ConcurrentAccess& ca, RenderProcess::DependencyGraph& depGraph) {
		using RtDesc = RenderTargetDescription;
		using ImgRef = RtDesc::ImageRef;
		using RpDesc = RenderPassDescription;
		using SpDesc = RpDesc::Subpass;
		using Atch = SpDesc::Attachment;
		using ClrValues = util::TransientArray<VkClearValue>;

		auto& e = ca.engine();
		auto& renderExt = e.getRenderExtent();
		auto& presentExt = e.getPresentExtent();
		auto  surfaceFmt = e.surfaceFormat().format;
		auto  depthFmt = e.depthFormat();
		auto  gframeCount = e.gframeCount();

		auto renderExt3d  = VkExtent3D { renderExt .width, renderExt .height, 1 };
		auto presentExt3d = VkExtent3D { presentExt.width, presentExt.height, 1 };
		auto depthExt3d   = VkExtent3D { std::max(renderExt.width, presentExt.width), std::max(renderExt.height, presentExt.height), 1 };
		auto scImgRefs = std::make_shared<std::vector<ImgRef>>();
		scImgRefs->reserve(gframeCount);
		for(auto gf : ca.getGframeData()) {
			scImgRefs->push_back(ImgRef {
				.image = gf.swapchain_image,
				.imageView = gf.swapchain_image_view });
		}
		auto mkRtDesc = [&](auto ref, auto& ext, VkImageUsageFlags usage, bool isDepth) {
			auto aspect = VkImageAspectFlags(isDepth? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT);
			return RtDesc { std::move(ref), ext, usage, isDepth?depthFmt:surfaceFmt, aspect, false, false, false, true };
		};
		RtDesc rtDesc[3] = {
			mkRtDesc(nullptr,              depthExt3d,   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, true),
			mkRtDesc(nullptr,              renderExt3d,  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false),
			mkRtDesc(std::move(scImgRefs), presentExt3d, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false) };
		RpDesc worldRpDesc, uiRpDesc;
		brp_depthRtarget = depGraph.addRtarget(rtDesc[0]);
		brp_worldRtarget = depGraph.addRtarget(rtDesc[1]);
		brp_uiRtarget = depGraph.addRtarget(rtDesc[2]);

		// THIS is the point of the circular dependency to cut
		brp_worldRenderer->setRtargetId_TMP_UGLY_NAME(brp_worldRtarget);
		brp_uiRenderer->setSrcRtargetId_TMP_UGLY_NAME(brp_worldRtarget);

		Atch worldColAtch0 = {
			.rtarget = brp_worldRtarget,
			.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,  .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,  .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
		Atch worldColAtch1 = {
			.rtarget = brp_worldRtarget,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,  .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,  .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
		Atch uiColAtch = {
			.rtarget = brp_uiRtarget,
			.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,  .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,  .storeOp = VK_ATTACHMENT_STORE_OP_STORE };
		auto worldSp1Dep = RpDesc::Subpass::Dependency {
			.srcSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // The first subpass is where values are cleared, otherwise the second subpass could begin as soon as the depth atch is written
			.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			.dependencyFlags = { } };
		worldRpDesc.subpasses.push_back(SpDesc {
			.inputAttachments = { }, .colorAttachments = { worldColAtch0 },
			.subpassDependencies = { },
			.depthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
			.depthInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .depthFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.depthRtarget = brp_depthRtarget });
		worldRpDesc.subpasses.push_back(SpDesc {
			.inputAttachments = { }, .colorAttachments = { worldColAtch1 },
			.subpassDependencies = { worldSp1Dep },
			.depthLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD, .depthStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
			.depthInitialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .depthFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.depthRtarget = brp_depthRtarget });
		worldRpDesc.framebufferSize = renderExt3d;
		uiRpDesc.subpasses.push_back(SpDesc {
			.inputAttachments = { }, .colorAttachments = { uiColAtch },
			.subpassDependencies = { SpDesc::Dependency {
				.srcSubpass = VK_SUBPASS_EXTERNAL,
				.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
				.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
				.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
				.dependencyFlags = { } }},
			.depthLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .depthStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
			.depthInitialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, .depthFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.depthRtarget = brp_depthRtarget });
		uiRpDesc.framebufferSize = presentExt3d;
		const auto& worldRpassId = depGraph.addRpass(worldRpDesc);
		const auto& uiRpassId    = depGraph.addRpass(uiRpDesc   );
		VkClearValue depthClr = { .depthStencil { .depth = 1.0f, .stencil = 0 } };
		VkClearValue worldClr[4] = {
			{ .color = { 0.035f, 0.062f, 0.094f, 1.0f } }, depthClr,
			{ .color = { 0.035f, 0.062f, 0.094f, 1.0f } }, depthClr };
		VkClearValue uiClr[2] = {
			{ .color = { 0.0f,   0.0f,   0.0f,   0.0f } }, depthClr };
		auto addStep =
			[&depGraph]
			(RenderPassId rpass, RendererId renderer, const VkExtent3D& ext, ClrValues clr)
		{
			RenderProcess::StepDescription desc = { };
			desc.rpass = rpass;
			desc.renderer = renderer;
			desc.renderArea.extent = { ext.width, ext.height };
			desc.clearColors = clr;
			return depGraph.addStep(std::move(desc));
		};
		RendererId renderers[] = {
			depGraph.addRenderer(brp_worldRenderer),
			depGraph.addRenderer(brp_uiRenderer) };
		auto step0 = addStep(worldRpassId, renderers[0], renderExt3d,  ClrValues::referenceTo(worldClr));
		;            addStep(uiRpassId,    renderers[1], presentExt3d, ClrValues::referenceTo(uiClr   )).after(step0);
	}


	void BasicRenderProcess::rpi_destroyRenderProcess(ConcurrentAccess&) { }


	void BasicRenderProcess::rpi_destroyRenderers(ConcurrentAccess& ca) {
		WorldRenderer::destroy(*brp_worldRenderer); brp_worldRenderer = { };
		UiRenderer::destroy(*brp_uiRenderer); brp_uiRenderer = { };
		for(auto& objStorage : *brp_objStorages) {
			ObjectStorage::destroy(ca.engine().getTransferContext(), objStorage);
		}
		brp_objStorages = { };
		WorldRenderer::destroySharedState(ca.engine().getDevice(), *brp_worldRendererSs); brp_worldRendererSs = { };
	}


	ObjectStorage& BasicRenderProcess::getObjectStorage(size_t index) noexcept {
		assert(brp_objStorages);
		assert(index < brp_objStorages->size());
		return (*brp_objStorages)[index];
	}

}
