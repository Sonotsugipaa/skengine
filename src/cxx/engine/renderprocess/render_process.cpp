#include "_render_process.inl.hpp"

#include "engine.hpp"

#include <cassert>
#include <unordered_set>

#include <vulkan/vulkan_format_traits.hpp>
#include <vulkan/vk_enum_string_helper.h>

#include <vk-util/error.hpp>



namespace SKENGINE_NAME_NS {

	namespace {

		template <typename T = size_t>
		constexpr auto imageByteSize(const VkExtent3D& ext, VkFormat format) {
			return T(ext.width) * T(ext.height) * T(ext.depth) * T(vk::blockSize(vk::Format(format)));
		}


		#define RP_STEP_ID_ RenderProcess::StepId
		auto detectGraphLoop(
			const std::map<RP_STEP_ID_, std::set<RP_STEP_ID_>>& fwdMap,
			const std::unordered_map<RP_STEP_ID_, std::set<RP_STEP_ID_>>& origins
		) {
			using StepId = RP_STEP_ID_;
			std::set<StepId> visited;
			std::vector<StepId> r; r.reserve(3);

			#define find_assert_(MAP_, KEY_) [&]() { auto r = MAP_.find(KEY_); assert(r != MAP_.end()); return r->second; } ()

			std::deque<StepId> q;
			std::unordered_map<StepId, StepId> bwdVisitTree;
			for(auto origin : origins) {
				q.clear();
				bwdVisitTree.clear();
				auto pop = [&]() { auto r = q.front(); q.pop_front(); return r; };
				q.push_back(origin.first);
				while(! q.empty()) {
					auto depender = pop();
					auto deps = find_assert_(fwdMap, depender);
					visited.insert(depender);
					for(auto& dependee : deps) {
						if(dependee == depender) [[unlikely /* and stupid */]] { r = { depender }; return r; }
						if(dependee != origin.first) {
							if(! visited.contains(dependee)) {
								q.push_back(dependee);
								bwdVisitTree[dependee] = depender;
							}
						} else {
							r.reserve(bwdVisitTree.size() + 1);
							do {
								r.push_back(depender);
								depender = find_assert_(bwdVisitTree, depender);
							} while(depender != origin.first);
							r.push_back(origin.first);
							return r;
						}
					}
				}
			}

			assert(false);
			std::unreachable();
			return r;
		}
		#undef RP_STEP_ID_


		void createSyncSet(VkDevice dev, RenderProcess::DrawSyncPrimitives& syncs) {
			static const auto fcInfo = []() { VkFenceCreateInfo r;
				r.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
				r.flags = VK_FENCE_CREATE_SIGNALED_BIT; return r; } ();
			static const auto scInfo = []() { VkSemaphoreCreateInfo r;
				r.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO; return r; } ();
			VK_CHECK(vkCreateFence,     dev, &fcInfo, nullptr, &syncs.fences    .prepare);
			VK_CHECK(vkCreateFence,     dev, &fcInfo, nullptr, &syncs.fences    .draw);
			VK_CHECK(vkCreateSemaphore, dev, &scInfo, nullptr, &syncs.semaphores.prepare);
			VK_CHECK(vkCreateSemaphore, dev, &scInfo, nullptr, &syncs.semaphores.draw);
		}

		void destroySyncSet(VkDevice dev, RenderProcess::DrawSyncPrimitives& syncs) {
			#define TRY_DESTROY_(WH_UC_, WH_LC_, WH_REST_, STAGE_) \
				if(syncs.WH_LC_##WH_REST_##s.STAGE_) \
				{ vkDestroy##WH_UC_##WH_REST_(dev, syncs.WH_LC_##WH_REST_##s.STAGE_, nullptr); \
				syncs.WH_LC_##WH_REST_##s.STAGE_ = nullptr; }
			TRY_DESTROY_(F,f,ence,     prepare)
			TRY_DESTROY_(F,f,ence,     draw)
			TRY_DESTROY_(S,s,emaphore, prepare)
			TRY_DESTROY_(S,s,emaphore, draw)
			#undef TRY_DESTROY_
		}


		void setWaveGframeCount(
			VkDevice dev,
			uint32_t queueFamIdx,
			std::vector<RenderProcess::WaveGframeData>* waveCmds,
			size_t newSize
		) {
			size_t oldSize = waveCmds->size();

			auto pushCmdBuffers = [dev, queueFamIdx, waveCmds, oldSize](size_t newSize) {
				assert(oldSize <= newSize);
				waveCmds->resize(newSize);
				auto bufferSquared = std::vector<VkCommandBuffer>((newSize - oldSize) * 2);
				VkCommandPoolCreateInfo cpcInfo = { };
				cpcInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				cpcInfo.queueFamilyIndex = queueFamIdx;
				VkCommandBufferAllocateInfo cbaInfo = { };
				cbaInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				cbaInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				cbaInfo.commandBufferCount = bufferSquared.size();
				for(size_t i = oldSize; i < newSize; ++i) { // For each wave
					auto& wave = (*waveCmds)[i];
					VK_CHECK(vkCreateCommandPool, dev, &cpcInfo, nullptr, &wave.cmdPool);
					cbaInfo.commandPool = wave.cmdPool;
					VK_CHECK(vkAllocateCommandBuffers, dev, &cbaInfo, bufferSquared.data());
					wave.cmdPrepare = bufferSquared[bufferSquared.size()-1];
					wave.cmdDraw    = bufferSquared[bufferSquared.size()-2];
					bufferSquared.resize(bufferSquared.size() - 2);
				}
				assert(bufferSquared.empty());
			};

			auto popCmdBuffers = [dev, waveCmds, oldSize](size_t newSize) {
				assert(oldSize >= newSize);
				if(newSize == 0) {
					for(auto& wave : *waveCmds) {
						vkDestroyCommandPool(dev, wave.cmdPool, nullptr);
					}
				} else {
					for(size_t i = oldSize-1; i > newSize; --i) {
						auto& wave = (*waveCmds)[i];
						VkCommandBuffer cmds[] = { wave.cmdPrepare, wave.cmdDraw };
						vkFreeCommandBuffers(dev, wave.cmdPool, std::size(cmds), cmds);
						vkTrimCommandPool(dev, wave.cmdPool, { });
					}
				}
				waveCmds->resize(newSize);
			};

			if(oldSize < newSize) {
				pushCmdBuffers(newSize);
			}
			else if(oldSize > newSize) {
				popCmdBuffers(newSize);
			}
		}


		template <typename Fn, typename... Args>
		void notifyRenderersOfSwapchainEvent(ConcurrentAccess& ca, std::string_view event, Fn&& fn, Args&&... args) {
			std::set<Renderer*> uniqueRdrs;
			auto& rProc = ca.getRenderProcess();
			for(auto& step : rProc.sortedStepRange()) {
				auto* renderer = rProc.getRenderer(step.second.renderer);
				if(renderer != nullptr) {
					ca.engine().logger().trace("Notifying renderer {} \"{}\" of swapchain {}", renderer_id_e(step.second.renderer), renderer->name(), event);
					uniqueRdrs.insert(renderer);
				} else {
					ca.engine().logger().trace("Not notifying null renderer {} of swapchain {}", renderer_id_e(step.second.renderer), event);
				}
			}
			for(auto* rdr : uniqueRdrs) (rdr->*fn)(std::forward<Args>(args)...);
		}

		void notifyRenderersOfSwapchainCreation(RenderProcess& rp, ConcurrentAccess& ca, std::string_view event, unsigned gframeCount) {
			notifyRenderersOfSwapchainEvent(ca, event, &Renderer::afterSwapchainCreation, ca, gframeCount);
			for(auto& step : rp.sortedStepRange()) {
				auto* renderer = rp.getRenderer(step.second.renderer);
				if(renderer != nullptr) {
					Renderer::SubpassSetupInfo ssInfo = {
						.phDevProps = &ca.engine().getPhysDeviceProperties(),
						.rpass = rp.getRenderPass(step.second.rpass).handle,
						.rpassId = step.second.rpass };
					renderer->prepareSubpasses(ssInfo, ca.engine().getPipelineCache(), ca.engine().getShaderCache().get());
				}
			}
		}

		void notifyRenderersOfSwapchainDestruction(RenderProcess& rp, ConcurrentAccess& ca, std::string_view event, unsigned gframeCount) {
			(void) gframeCount;
			for(auto& step : rp.sortedStepRange()) {
				auto* renderer = rp.getRenderer(step.second.renderer);
				if(renderer != nullptr) {
					Renderer::SubpassSetupInfo ssInfo = {
						.phDevProps = &ca.engine().getPhysDeviceProperties(),
						.rpass = rp.getRenderPass(step.second.rpass).handle,
						.rpassId = step.second.rpass };
					renderer->forgetSubpasses(ssInfo);
				}
			}
			notifyRenderersOfSwapchainEvent(ca, event, &Renderer::beforeSwapchainDestruction, ca);
		}
	}



	std::strong_ordering RenderProcess::WaveIterator::operator<=>(const WaveIterator&r) const noexcept {
		auto& l = *this;
		if(l.wi_rp == nullptr) {
			if(r.wi_rp == nullptr) return std::strong_ordering::equal;
			else return std::strong_ordering::greater;
		} else {
			assert(l.wi_validity == l.wi_rp->rp_waveIterValidity);
			if(r.wi_rp == nullptr) return std::strong_ordering::less;
			else if(l.wi_seqIdx  < r.wi_seqIdx) return std::strong_ordering::greater;
			else if(l.wi_seqIdx  > r.wi_seqIdx) return std::strong_ordering::less;
			else return std::strong_ordering::equal;
		}
	}


	RenderProcess::WaveIterator& RenderProcess::WaveIterator::operator++() {
		auto curStep = step_id_e(wi_firstStep);
		auto nxtStep = curStep + wi_stepCount;
		if constexpr (std::is_signed_v<step_id_e>) assert(std::make_signed_t<step_id_e>(curStep) >= 0); // make_signed is redundant, but makes GCC not nag without cause
		assert(wi_rp != nullptr);
		assert(wi_validity == wi_rp->rp_waveIterValidity);
		if(size_t(nxtStep) >= wi_rp->rp_steps.size()) {
			*this = WaveIterator();
		} else {
			wi_seqIdx = wi_rp->rp_steps[nxtStep].second.seqIndex;
			wi_firstStep = StepId(nxtStep);
			wi_stepCount = 1;
			for(step_id_e i = nxtStep + 1; size_t(i) < wi_rp->rp_steps.size(); ++i) {
				if(wi_rp->rp_steps[i].second.seqIndex != wi_seqIdx) break;
				++ wi_stepCount;
			}
		}
		return *this;
	}


	std::span<std::pair<RenderProcess::StepId, RenderProcess::Step>> RenderProcess::WaveIterator::operator*() {
		assert(wi_rp != nullptr);
		assert(wi_validity == wi_rp->rp_waveIterValidity);
		auto beg = wi_rp->rp_steps.begin() + size_t(wi_firstStep);
		auto end = beg + wi_stepCount;
		return std::span<std::pair<StepId, Step>>(beg, end);
	}


	#ifndef NDEBUG
		RenderProcess::~RenderProcess() {
			assert(! rp_initialized);
		}
	#endif


	void RenderProcess::setup(
		VmaAllocator vma,
		Logger logger,
		ConcurrentAccess& ca,
		VkFormat depthImageFormat,
		uint32_t queueFamIdx,
		unsigned gframeCount,
		const DependencyGraph& depGraph
	) {
		auto seqDesc = depGraph.assembleSequence();
		setup(vma, std::move(logger), ca, depthImageFormat, queueFamIdx, gframeCount, seqDesc);
	}


	void RenderProcess::setup(
		VmaAllocator vma,
		Logger logger,
		ConcurrentAccess& ca,
		VkFormat depthImageFormat,
		uint32_t queueFamIdx,
		unsigned gframeCount,
		DependencyGraph&& depGraph
	) {
		setup(vma, std::move(logger), ca, depthImageFormat, queueFamIdx, gframeCount, static_cast<DependencyGraph&>(depGraph));
	}


	void RenderProcess::setup(
		VmaAllocator vma,
		Logger mvLogger,
		ConcurrentAccess& ca,
		VkFormat depthImageFormat,
		uint32_t queueFamIdx,
		unsigned gframeCount,
		const SequenceDescription& seqDesc
	) {
		rp_gframeCount = gframeCount;
		rp_logger = std::move(mvLogger);
		rp_vkState = { vma, depthImageFormat, queueFamIdx };
		auto dev = rp_vkState.device();
		auto& rtsFac = * seqDesc.rtsFactory;

		rp_steps.resize(seqDesc.steps.size());
		rp_rpasses.resize(seqDesc.rpasses.size());
		rp_renderers.resize(seqDesc.renderers.size());

		for(size_t i = 0; i < rp_steps.size(); ++i) {
			auto& step = rp_steps[i];
			step.first = idFromIndex<StepId>(i);
			step.second = Step(seqDesc.steps[i]);
		}

		rp_rtargetStorage = rtsFac.finalize(vma);

		{ // Create waves and fences
			size_t waveCount = [&]() {
				size_t r = rp_steps.empty()? 0 : 1;
				seq_idx_e lastSeq = 0;
				for(size_t i = 0; i < rp_steps.size(); ++i) {
					auto seqIdx = seq_idx_e(rp_steps[i].second.seqIndex);
					if(seqIdx != lastSeq) {
						++r;
						lastSeq = seqIdx;
					}
					assert(r-1 == seqIdx);
				}
				return r;
			} ();
			rp_logger.debug("Creating Vulkan handles for {} wave{}", waveCount, (waveCount == 1)? "":"s");

			setWaveGframeCount(dev, rp_vkState.queueFamIdx, &rp_waveCmds, waveCount * gframeCount);

			rp_drawSyncPrimitives.reserve(rp_steps.size() * rp_gframeCount);
			for(size_t stepIdx = 0; stepIdx < rp_steps.size(); ++ stepIdx) {
				for(size_t gframeIdx = 0; gframeIdx < rp_gframeCount; ++ gframeIdx) {
					rp_drawSyncPrimitives.push_back({ });
					createSyncSet(dev, rp_drawSyncPrimitives.back());
				}
			}
		}

		{ // Create rpasses
			size_t maxSubpassCount = 0;
			for(auto& desc : seqDesc.rpasses) maxSubpassCount = std::max(maxSubpassCount, desc.subpasses.size());
			auto rprocRpcInfo = RprocRpassCreateInfo { rp_logger, vma, gframeCount, rp_rtargetStorage, rp_vkState.depthImageFormat };
			auto vectors = RprocRpassCreateVectorCache(maxSubpassCount, gframeCount);
			for(size_t rpassIdx = 0; rpassIdx < rp_rpasses.size(); ++ rpassIdx) {
				createRprocRpass(rp_rpasses.data() + rpassIdx, rpassIdx, &seqDesc.rpasses[rpassIdx], rprocRpcInfo, rp_rtargetStorage, vectors);
			}
		}

		for(size_t i = 0; i < rp_renderers.size(); ++i) rp_renderers[i] = seqDesc.renderers[i].lock();

		notifyRenderersOfSwapchainCreation(*this, ca, "creation", rp_gframeCount);

		rp_rtargetStorage.updateRtargetReferences();

		++ rp_waveIterValidity;
		rp_initialized = true;
	}


	void RenderProcess::destroy(ConcurrentAccess& ca) {
		assert(rp_initialized);
		auto dev = rp_vkState.device();
		std::exception_ptr exception = nullptr;

		{ // Destroy sync primitives
			assert(rp_drawSyncPrimitives.size() == rp_steps.size() * rp_gframeCount);
			{ // Wait for the fences first
				std::vector<VkFence> fences;
				fences.reserve(rp_steps.size());
				for(auto& syncs : rp_drawSyncPrimitives) {
					fences.push_back(syncs.fences.prepare);
					fences.push_back(syncs.fences.draw);
				}
				try {
					VK_CHECK(vkWaitForFences, dev, fences.size(), fences.data(), VK_TRUE, 30'000'000'000);
				} catch(vkutil::VulkanError& err) {
					using res_e = std::underlying_type_t<VkResult>;
					rp_logger.error("Failed to wait for gframe fences; trying to destroy them anyway.");
					rp_logger.error("Reason: vkWaitForFences returned VkResult {} ()", res_e(err.vkResult()), string_VkResult(err.vkResult()));
					exception = std::current_exception();
				}
			}
			for(auto& syncs : rp_drawSyncPrimitives) destroySyncSet(dev, syncs);
			rp_drawSyncPrimitives.clear();
		}

		notifyRenderersOfSwapchainDestruction(*this, ca, "destruction", rp_gframeCount);

		setWaveGframeCount(dev, rp_vkState.queueFamIdx, &rp_waveCmds, 0);

		for(size_t i = 0; i < rp_rpasses.size(); ++i) {
			auto& dst = rp_rpasses[i];
			if(dst) destroyRprocRpass(&dst, rp_vkState.vma);
		}
		rp_rpasses.clear();

		rp_rtargetStorage = { };

		rp_gframeCount = 0;
		rp_initialized = false;
		if(exception != nullptr) std::rethrow_exception(exception);
	}


	RenderProcess::Step& RenderProcess::getStep(StepId id) & {
		assert(idToIndex(id) < rp_renderers.size());
		return rp_steps.at(idToIndex(id)).second;
	}


	RenderPass& RenderProcess::getRenderPass(RenderPassId id) & {
		assert(idToIndex(id) < rp_renderers.size());
		return rp_rpasses.at(idToIndex(id));
	}


	Renderer* RenderProcess::getRenderer(RendererId id) & {
		assert(idToIndex(id) < rp_renderers.size());
		return rp_renderers.at(idToIndex(id)).get();
	}


	const RenderTargetDescription& RenderProcess::getRenderTargetDescription(RenderTargetId id) const & {
		return rp_rtargetStorage.getDescription(id);
	}


	const RenderTarget& RenderProcess::getRenderTarget(RenderTargetId id, size_t subIndex) const & {
		auto entrySet = rp_rtargetStorage.getEntrySet(id);
		assert(! entrySet.isExternal);
		assert(subIndex < entrySet.entries.size());
		#ifdef NDEBUG
			static constexpr RenderTarget nullRt = { };
			if(entrySet.isExternal) [[unlikely]] return nullRt; // Null pointers are more predictable UB than actual non-zero garbage
			else return entrySet.entries[subIndex].managed;
		#else
			return entrySet.entries[subIndex].managed;
		#endif
	}


	const RenderProcess::DrawSyncPrimitives& RenderProcess::getDrawSyncPrimitives(SequenceIndex wave, size_t gframe) const noexcept {
		auto idx = (size_t(wave) * rp_gframeCount) + gframe;
		assert(idx < rp_drawSyncPrimitives.size());
		auto& r = rp_drawSyncPrimitives[idx];
		if(! rp_steps.empty()) [[likely]] assert(size_t(rp_steps.back().second.seqIndex) < rp_drawSyncPrimitives.size());
		return r;
	}


	const RenderProcess::WaveGframeData& RenderProcess::getWaveGframeData(SequenceIndex wave, size_t gframe) const noexcept {
		auto idx = (size_t(wave) * rp_gframeCount) + gframe;
		assert(idx < rp_waveCmds.size());
		auto& r = rp_waveCmds[idx];
		if(! rp_steps.empty()) [[likely]] assert(size_t(rp_steps.back().second.seqIndex) < rp_waveCmds.size());
		return r;
	}


	void RenderProcess::reset(ConcurrentAccess& ca, unsigned newGframeCount, util::TransientArray<RtargetResizeInfo> resizes) {
		VkExtent3D maxResize = { 0, 0, 0 };

		bool doResize = resizes.size() > 0;
		bool doChangeGframeCount = newGframeCount == rp_gframeCount;
		bool doRecreateRpasses = doResize || doChangeGframeCount;
		std::vector<RenderPassDescription> rpassDescs;
		auto dev = rp_vkState.device();

		auto destroyRpasses = [&](std::vector<RenderPassDescription>* rpassDescs) {
			rpassDescs->reserve(rp_rpasses.size());
			notifyRenderersOfSwapchainDestruction(*this, ca, "creation", rp_gframeCount);
			for(size_t i = 0; i < rp_rpasses.size(); i++) {
				rpassDescs->emplace_back(std::move(rp_rpasses[i].description));
				destroyRprocRpass(rp_rpasses.data() + i, rp_vkState.vma);
			}
		};

		auto recreateRpasses = [&](const std::vector<RenderPassDescription>* rpassDescs) {
			size_t maxSubpassCount = 0;
			for(auto& desc : *rpassDescs) maxSubpassCount = std::max(maxSubpassCount, desc.subpasses.size());
			auto rprocRpcInfo = RprocRpassCreateInfo { rp_logger, rp_vkState.vma, newGframeCount, rp_rtargetStorage, rp_vkState.depthImageFormat };
			auto vectors = RprocRpassCreateVectorCache(maxSubpassCount, newGframeCount);
			for(size_t rpassIdx = 0; rpassIdx < rp_rpasses.size(); ++ rpassIdx) {
				createRprocRpass(rp_rpasses.data() + rpassIdx, rpassIdx, &(*rpassDescs)[rpassIdx], rprocRpcInfo, rp_rtargetStorage, vectors);
			}
			notifyRenderersOfSwapchainCreation(*this, ca, "recreation", rp_gframeCount);
		};

		if(doRecreateRpasses) rp_rtargetStorage.updateRtargetReferences();
		for(auto& resize : resizes) {
			maxResize.width  = std::max(maxResize.width , resize.newExtent.width );
			maxResize.height = std::max(maxResize.height, resize.newExtent.height);
			maxResize.depth  = std::max(maxResize.depth , resize.newExtent.depth );
			rp_rtargetStorage.setRtargetExtent(resize.rtarget, resize.newExtent);
		}

		if(doRecreateRpasses) destroyRpasses(&rpassDescs);

		if(doChangeGframeCount) {
			assert(! rp_steps.empty());
			size_t waveGframeCount = (size_t(rp_steps.back().second.seqIndex) + size_t(1)) * newGframeCount;
			rp_drawSyncPrimitives.reserve(waveGframeCount);
			// Create missing primitives ...
			for(size_t i = rp_drawSyncPrimitives.size(); i < waveGframeCount; ++i) {
				rp_drawSyncPrimitives.push_back({ });
				createSyncSet(dev, rp_drawSyncPrimitives.back());
			}
			// ... or destroy excessive ones
			for(size_t i = rp_drawSyncPrimitives.size(); i > waveGframeCount; --i) {
				auto& syncs = rp_drawSyncPrimitives.back();
				destroySyncSet(dev, syncs);
			}
			rp_drawSyncPrimitives.resize(waveGframeCount);

			setWaveGframeCount(dev, rp_vkState.queueFamIdx, &rp_waveCmds, waveGframeCount);

			rp_rtargetStorage.setGframeCount(newGframeCount);
			rp_gframeCount = newGframeCount;
		}

		if(doRecreateRpasses) recreateRpasses(&rpassDescs);
	}


	RenderProcess::WaveRange RenderProcess::waveRange() & {
		assert(rp_rtargetStorage.gframeCount() == rp_gframeCount);
		WaveRange r = WaveRange {
			.beginIter = WaveIterator(),
			.endIter = WaveIterator() };
		r.beginIter.wi_rp = this;
		r.beginIter.wi_validity = rp_waveIterValidity;
		++ r.beginIter;
		return r;
	}


	#define RP_DG_ RenderProcess::DependencyGraph


	RP_DG_::Subgraph& RP_DG_::Subgraph::before(const Subgraph& sg) {
		sg_graph->dg_dependencies_fwd.find(sg_step)->second.insert(sg.sg_step);
		sg_graph->dg_dependencies_bwd.find(sg.sg_step)->second.insert(sg_step);
		return *this;
	}

	RP_DG_::Subgraph& RP_DG_::Subgraph::after(const Subgraph& sg) {
		sg_graph->dg_dependencies_fwd.find(sg.sg_step)->second.insert(sg_step);
		sg_graph->dg_dependencies_bwd.find(sg_step)->second.insert(sg.sg_step);
		return *this;
	}


	RenderTargetId RP_DG_::addRtarget(RenderTargetDescription rtDesc) {
		return dg_rtsFactory->setRenderTarget(std::move(rtDesc));
	}

	RenderPassId RP_DG_::addRpass(RenderPassDescription rpDesc) {
		dg_rpasses.push_back(std::move(rpDesc));
		return idFromIndex<RenderPassId>(dg_rpasses.size() - 1);
	}

	RendererId RP_DG_::addRenderer(std::weak_ptr<Renderer> renderer) {
		dg_renderers.push_back(renderer.lock());
		return idFromIndex<RendererId>(dg_renderers.size() - 1);
	}


	RP_DG_::Subgraph RP_DG_::addDummyStep() {
		return addStep({ { }, idgen::invalidId<RenderPassId>(), idgen::invalidId<RendererId>(), { } });
	}

	RP_DG_::Subgraph RP_DG_::addStep(StepDescription stepDesc) {
		Subgraph r;
		stepDesc.clearColors.consolidate();
		r.sg_graph = this;
		r.sg_step = idFromIndex<StepId>(dg_steps.size());
		dg_steps.push_back(Step { stepDesc, idgen::invalidId<SequenceIndex>() });
		dg_dependencies_fwd.insert(DependencyMap::value_type { r.sg_step, { } });
		dg_dependencies_bwd.insert(DependencyMap::value_type { r.sg_step, { } });
		return r;
	}


	RenderProcess::SequenceDescription RP_DG_::assembleSequence() const {
		seq_idx_e seq = 0;
		SequenceDescription r;
		auto mkStepSet = [this]() { std::unordered_set<StepId> r; r.max_load_factor(1.2); r.reserve(dg_steps.size()); return r; };
		std::unordered_set<StepId> resolvedSteps;

		std::unordered_map<StepId, std::set<StepId>> unresolvedSteps;
		unresolvedSteps.max_load_factor(1.2);
		unresolvedSteps.reserve(dg_steps.size());
		for(auto& dep : dg_dependencies_bwd) unresolvedSteps.insert(dep);

		r.steps.reserve(dg_steps.size());
		r.rtsFactory = dg_rtsFactory;
		r.rpasses    = dg_rpasses;
		r.renderers  = dg_renderers;

		while(resolvedSteps.size() < dg_steps.size()) {
			auto localResolvedSteps = mkStepSet();
			auto localUnresolvedSteps = unresolvedSteps;
			size_t resolved = 0;
			for(auto& stepDeps : unresolvedSteps) {
				assert(! resolvedSteps.contains(stepDeps.first));
				bool skipDep = [&]() {
					for(StepId dep : stepDeps.second) { if(! resolvedSteps.contains(dep)) return true; }
					return false;
				} ();
				if(! skipDep) {
					auto stepIdx = idToIndex<StepId>(stepDeps.first);
					r.steps.push_back(dg_steps[stepIdx]);
					r.steps.back().seqIndex = SequenceIndex(seq);
					localResolvedSteps.insert(stepDeps.first);
					localUnresolvedSteps.erase(stepDeps.first);
					++ resolved;
				}
			}
			if(resolved == 0) {
				auto loop = detectGraphLoop(dg_dependencies_fwd, unresolvedSteps);
				throw RenderProcess::UnsatisfiableDependencyError(std::move(loop));
			}
			unresolvedSteps = std::move(localUnresolvedSteps);
			resolvedSteps.merge(localResolvedSteps);
			++ seq;
		}

		return r;
	}


	RenderProcess::UnsatisfiableDependencyError::UnsatisfiableDependencyError(std::vector<RenderProcess::StepId> dependencyChain):
		std::runtime_error("cyclic render step dependency"),
		dep_err_depChain(std::move(dependencyChain))
	{ }


	#undef RP_DG_

}
