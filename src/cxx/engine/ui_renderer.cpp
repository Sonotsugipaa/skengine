#include "ui_renderer.hpp"

#include "engine.hpp"
#include "debug.inl.hpp"

#include <vk-util/error.hpp>

#include <set>



namespace SKENGINE_NAME_NS {

	namespace ui { namespace {

		#define B_(BINDING_, DSET_N_, DSET_T_, STAGES_) VkDescriptorSetLayoutBinding { .binding = BINDING_, .descriptorType = DSET_T_, .descriptorCount = DSET_N_, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT, .pImmutableSamplers = nullptr }
		constexpr VkDescriptorSetLayoutBinding ui_dset_layout_bindings[] = {
		B_(0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT) };
		#undef B_

		constexpr ShaderRequirement ui_pipelines[] = {
			{ .name = "shape-fill",    .pipelineLayout = PipelineLayoutId::eGeometry },
			{ .name = "shape-outline", .pipelineLayout = PipelineLayoutId::eGeometry },
			{ .name = "text",          .pipelineLayout = PipelineLayoutId::eImage } };

		constexpr auto ui_renderer_shape_subpass_info = Renderer::Info {
			.dsetLayoutBindings = Renderer::Info::DsetLayoutBindings::referenceTo(ui_dset_layout_bindings),
			.shaderRequirements = Renderer::Info::ShaderRequirements::referenceTo(ui_pipelines),
			.rpass = Renderer::RenderPass::eUi };


		template <typename visit_fn_tp>
		void visitUi(
			ui::Canvas& canvas,
			visit_fn_tp visitFn
		) {
			using DfsList = std::deque<std::pair<LotId, Lot*>>;
			DfsList dfsQueue;

			for(auto& lot : canvas.lots()) dfsQueue.emplace_back(lot.first, lot.second.get());

			while(! dfsQueue.empty()) {
				auto extracted = std::move(dfsQueue.back());
				visitFn(extracted.first, *extracted.second);
				dfsQueue.pop_back();
				if(extracted.second->hasChildGrid()) {
					auto child = extracted.second->childGrid();
					for(auto& lot : child->lots()) dfsQueue.emplace_back(lot.first, lot.second.get());
				}
			}
		}

	}}



	UiRenderer::UiRenderer(): Renderer(ui_renderer_shape_subpass_info) { mState.initialized = false; }

	UiRenderer::UiRenderer(UiRenderer&& mv):
		Renderer(ui_renderer_shape_subpass_info),
		mState(std::move(mv.mState))
	{
		mv.mState.initialized = false;
	}

	UiRenderer::~UiRenderer() {
		if(mState.initialized) {
			UiRenderer::destroy(*this);
			mState.initialized = false;
		}
	}


	UiRenderer UiRenderer::create(
		VmaAllocator vma,
		Logger logger,
		std::string fontFilePath

	) {
		UiRenderer r;
		r.mState.logger = std::move(logger);
		r.mState.vma = vma;
		r.mState.fontFilePath = std::move(fontFilePath);
		r.mState.initialized = true;

		{ // Init freetype
			auto error = FT_Init_FreeType(&r.mState.freetype);
			if(error) throw FontError("failed to initialize FreeType", error);
		}

		{ // Hardcoded GUI canvas
			float ratio = 1.0f;
			float hSize = 0.1f;
			float wSize = hSize * ratio;
			float wComp = 0.5f * (hSize - wSize);
			float chBlank = (1.0 - hSize) / 2.0;
			auto& canvas = r.mState.canvas;
			canvas = std::make_unique<ui::Canvas>(ComputedBounds { 0.01, 0.01, 0.98, 0.98 });
			canvas->setRowSizes    ({ chBlank,       hSize, chBlank });
			canvas->setColumnSizes ({ chBlank+wComp, wSize, chBlank+wComp });
		}

		return r;
	}


	void UiRenderer::destroy(UiRenderer& r) {
		assert(r.mState.initialized);
		auto* vma = r.mState.vma;


		for(auto& gf : r.mState.gframes) {
			(void) gf; // NOP
			(void) vma;
		}
		r.mState.gframes.clear();

		r.mState.textCaches.clear();
		r.mState.canvas = { };
		FT_Done_FreeType(r.mState.freetype);

		r.mState.initialized = false;
	}


	void UiRenderer::afterSwapchainCreation(ConcurrentAccess&, unsigned gframeCount) {
		size_t oldGframeCount = mState.gframes.size();

		auto setGframeData = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];
			(void) wgf; // NOP
		};

		auto unsetGframeData = [&](unsigned gfIndex) {
			GframeData& wgf = mState.gframes[gfIndex];
			(void) wgf; // NOP
		};

		if(oldGframeCount < gframeCount) {
			mState.gframes.resize(gframeCount);
			for(size_t i = oldGframeCount; i < gframeCount; ++i) setGframeData(i);
		} else
		if(oldGframeCount > gframeCount) {
			for(size_t i = gframeCount; i < oldGframeCount; ++i) unsetGframeData(i);
			mState.gframes.resize(gframeCount);
		}
	}


	void UiRenderer::duringPrepareStage(ConcurrentAccess& ca, unsigned, VkCommandBuffer cmd) {
		auto& e = ca.engine();

		gui::DrawContext guiCtx = gui::DrawContext {
			.magicNumber = gui::DrawContext::magicNumberValue,
			.engine = &e,
			.prepareCmdBuffer = cmd,
			.drawJobs = { } };
		ui::DrawContext uiCtx = { &guiCtx };

		std::deque<std::tuple<LotId, Lot*, Element*>> repeatList;
		std::deque<std::tuple<LotId, Lot*, Element*>> repeatListSwap;
		unsigned repeatCount = 1;

		ui::visitUi(*mState.canvas, [&](LotId lotId, Lot& lot) {
			for(auto& elem : lot.elements()) {
				auto ps = elem.second->ui_elem_prepareForDraw(lotId, lot, 0, uiCtx);
				if(ps == ui::Element::PrepareState::eDefer) repeatList.push_back({ lotId, &lot, elem.second.get() });
			}
		});

		while(! repeatList.empty()) {
			for(auto& row : repeatList) {
				auto ps = std::get<2>(row)->ui_elem_prepareForDraw(std::get<0>(row), *std::get<1>(row), repeatCount, uiCtx);
				if(ps == ui::Element::PrepareState::eDefer) repeatListSwap.push_back(row);
			}
			repeatList = std::move(repeatListSwap);
			++ repeatCount;
		}
	}


	void UiRenderer::duringDrawStage(ConcurrentAccess& ca, unsigned gfIndex, VkCommandBuffer cmd) {
		auto& e   = ca.engine();
		auto& egf = ca.getGframeData(gfIndex);

		gui::DrawContext guiCtx = gui::DrawContext {
			.magicNumber = gui::DrawContext::magicNumberValue,
			.engine = &ca.engine(),
			.prepareCmdBuffer = nullptr,
			.drawJobs = { } };
		ui::DrawContext uiCtx = { &guiCtx };

		ui::visitUi(*mState.canvas, [&](LotId lotId, Lot& lot) {
			for(auto& elem : lot.elements()) elem.second->ui_elem_draw(lotId, lot, uiCtx);
		});

		// The caches will need for this draw op to finish before preparing for the next one
		// (unless they're up to date, in which case they won't do anything)
		for(auto& ln : mState.textCaches) ln.second.syncWithFence(egf.fence_draw);

		VkPipeline             lastPl = nullptr;
		const ViewportScissor* lastVs = nullptr;
		VkDescriptorSet        lastImageDset = nullptr;
		VkPipelineLayout       geomPipelineLayout = e.getGeomPipelines().layout;

		// This lambda ladder turns what follows into something less indented:
		//
		//    for(auto&          jobPl : guiCtx.drawJobs) {
		//       for(auto&       jobVs : jobPl.second) {
		//          for(auto&    jobDs : jobVs.second) {
		//             for(auto& job   : jobDs.second) { }
		//          }
		//       }
		//    }
		//
		// Note that the JobPl->JobVs->JobDs order appears to be reversed, but it isn't.
		using JobPl = gui::DrawJobSet::value_type;
		using JobVs = gui::DrawJobVsSet::value_type;
		using JobDs = gui::DrawJobDsetSet::value_type;
		auto forEachJob = [&](gui::DrawJob& job) {
			auto& shapeSet = *job.shapeSet;
			VkBuffer vtx_buffers[] = { shapeSet.vertexBuffer(), shapeSet.vertexBuffer() };
			VkDeviceSize offsets[] = { shapeSet.instanceCount() * sizeof(geom::Instance), 0 };
			vkCmdPushConstants(cmd, geomPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(geom::PushConstant), &job.transform);
			vkCmdBindVertexBuffers(cmd, 0, 2, vtx_buffers, offsets);
			vkCmdDrawIndirect(cmd, shapeSet.drawIndirectBuffer(), 0, shapeSet.drawCmdCount(), sizeof(VkDrawIndirectCommand));
		};

		auto forEachDs = [&](JobDs& jobDs) {
			if(lastImageDset != jobDs.first) {
				lastImageDset = jobDs.first;
				if(lastImageDset != nullptr) {
					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geomPipelineLayout, 0, 1, &lastImageDset, 0, nullptr);
				}
			}
			for(auto& job : jobDs.second) forEachJob(job);
		};

		auto forEachVs = [&](JobVs& jobVs) {
			if(lastVs != &jobVs.first) {
				lastVs = &jobVs.first;
				vkCmdSetViewport(cmd, 0, 1, &lastVs->viewport);
				vkCmdSetScissor(cmd, 0, 1, &lastVs->scissor);
			}
			for(auto& jobDs : jobVs.second) forEachDs(jobDs);
		};

		auto forEachPl = [&](JobPl& jobPl) {
			if(lastPl != jobPl.first) {
				lastPl = jobPl.first;
				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, lastPl);
			}
			for(auto& jobVs : jobPl.second) forEachVs(jobVs);
		};

		for(auto& jobPl : guiCtx.drawJobs) forEachPl(jobPl);
	}


	FontFace UiRenderer::createFontFace() {
		return FontFace::fromFile(mState.freetype, false, mState.fontFilePath.c_str());
	}


	TextCache& UiRenderer::getTextCache(Engine& e, unsigned short size) {
		using Caches = decltype(mState.textCaches);
		auto& caches = mState.textCaches;
		auto dev = vmaGetAllocatorDevice(mState.vma);
		auto found = caches.find(size);
		if(found == caches.end()) {
			found = caches.insert(Caches::value_type(size, TextCache(
				dev, mState.vma,
				e.getImageDsetLayout(),
				std::make_shared<FontFace>(createFontFace()),
				size ))).first;
		}
		return found->second;
	}


	void UiRenderer::trimTextCaches(codepoint_t maxCharCount) {
		for(auto& ln : mState.textCaches) ln.second.trimChars(maxCharCount);
	}


	void UiRenderer::forgetTextCacheFences() noexcept {
		for(auto& ln : mState.textCaches) ln.second.forgetFence();
	}

}
