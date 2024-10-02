#include "_render_process.inl.hpp"

#include <cassert>
#include <unordered_set>

#include <vulkan/vulkan_format_traits.hpp>

#include <timer.tpp>



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
		VkFormat depthImageFormat,
		unsigned gframeCount,
		const DependencyGraph& depGraph
	) {
		auto seqDesc = depGraph.assembleSequence();
		setup(vma, std::move(logger), depthImageFormat, gframeCount, seqDesc);
	}


	void RenderProcess::setup(
		VmaAllocator vma,
		Logger logger,
		VkFormat depthImageFormat,
		unsigned gframeCount,
		DependencyGraph&& depGraph
	) {
		setup(vma, std::move(logger), depthImageFormat, gframeCount, static_cast<DependencyGraph&>(depGraph));
	}


	void RenderProcess::setup(
		VmaAllocator vma,
		Logger mvLogger,
		VkFormat depthImageFormat,
		unsigned gframeCount,
		const SequenceDescription& seqDesc
	) {
		util::SteadyTimer<std::chrono::microseconds> timer;

		rp_gframeCount = gframeCount;
		rp_logger = std::move(mvLogger);
		rp_vkState = { vma, depthImageFormat };
		auto& rtsFac = * seqDesc.rtsFactory;

		rp_steps.resize(seqDesc.steps.size());
		rp_rpasses.resize(seqDesc.rpasses.size());
		rp_renderers.resize(seqDesc.renderers.size());

		for(size_t i = 0; i < rp_steps.size(); ++i) {
			rp_steps[i] = std::pair(idFromIndex<StepId>(i), seqDesc.steps[i]);
		}

		rp_rtargetStorage = rtsFac.finalize(vma);

		{ // Create rpasses
			size_t maxSubpassCount = 0;
			for(auto& desc : seqDesc.rpasses) maxSubpassCount = std::max(maxSubpassCount, desc.subpasses.size());
			auto rprocRpcInfo = RprocRpassCreateInfo { rp_logger, vma, gframeCount, rp_rtargetStorage, rp_vkState.depthImageFormat };
			auto vectors = RprocRpassCreateVectorCache(maxSubpassCount, gframeCount);
			for(size_t rpassIdx = 0; rpassIdx < rp_rpasses.size(); ++ rpassIdx) {
				createRprocRpass(rp_rpasses.data() + rpassIdx, rpassIdx, &seqDesc.rpasses[rpassIdx], rprocRpcInfo, vectors);
			}
		}

		for(size_t i = 0; i < rp_renderers.size(); ++i) rp_renderers[i] = seqDesc.renderers[i].lock();

		++ rp_waveIterValidity;
		rp_initialized = true;

		rp_logger.debug("render_process: setup took {}ms", timer.count<float>() / 1000.0f);
	}


	void RenderProcess::destroy() {
		assert(rp_initialized);

		for(size_t i = 0; i < rp_rpasses.size(); ++i) {
			auto& dst = rp_rpasses[i];
			if(dst) destroyRprocRpass(&dst, rp_vkState.vma);
		}
		rp_rpasses.clear();

		rp_rtargetStorage = { };

		rp_gframeCount = 0;
		rp_initialized = false;
	}


	void RenderProcess::reset(unsigned newGframeCount, util::TransientPtrRange<RtargetResizeInfo> resizes) {
		util::SteadyTimer<std::chrono::microseconds> timer;
		VkExtent3D maxResize = { 0, 0, 0 };

		bool doResize = resizes.size() > 0;
		bool doChangeGframeCount = newGframeCount == rp_gframeCount;
		bool doRecreateRpasses = doResize || doChangeGframeCount;
		std::vector<RenderPassDescription> rpassDescs;

		auto destroyRpasses = [&](std::vector<RenderPassDescription>* rpassDescs) {
			rpassDescs->reserve(rp_rpasses.size());
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
				createRprocRpass(rp_rpasses.data() + rpassIdx, rpassIdx, &(*rpassDescs)[rpassIdx], rprocRpcInfo, vectors);
			}
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
			rp_rtargetStorage.setGframeCount(newGframeCount);
			rp_gframeCount = newGframeCount;
		}

		if(doRecreateRpasses) recreateRpasses(&rpassDescs);

		rp_logger.debug("render_process: reset operation took {}ms", timer.count<float>() / 1000.0f);
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
		r.sg_graph = this;
		r.sg_step = idFromIndex<StepId>(dg_steps.size());
		dg_steps.push_back(stepDesc);
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

		auto getTargetSizes = [&](RenderTargetId id) {
			struct R {
				uint_fast32_t bytes;
				decltype(VkExtent3D::width ) width ;
				decltype(VkExtent3D::height) height;
				decltype(VkExtent3D::depth ) depth ;
			};
			auto& target = r.rtsFactory->dst.getDescription(id);
			return R { imageByteSize<uint_fast32_t>(target.extent, target.format), target.extent.width, target.extent.height, target.extent.depth };
		};

		auto subpassDepthSizes = [&](
			VkExtent3D& dstExt,
			uint_fast32_t& dstSize,
			uint_fast32_t& dstCount,
			const std::vector<RenderPassDescription::Subpass>& subpasses
		) {
			dstSize = 0;
			dstExt = { 0, 0, 0 };
			for(auto& subpass : subpasses) {
				if(subpass.requiresDepthAttachments) {
					for(auto& atch : subpass.colorAttachments) {
						auto sizes = getTargetSizes(atch.rtarget);
						dstSize = std::max(dstSize, sizes.bytes);
						dstExt.width  = std::max(dstExt.width , sizes.width );
						dstExt.height = std::max(dstExt.height, sizes.height);
						dstExt.depth  = std::max(dstExt.depth , sizes.depth );
					}
					++ dstCount;
				}
			}
		};

		while(resolvedSteps.size() < dg_steps.size()) {
			auto localResolvedSteps = mkStepSet();
			auto localUnresolvedSteps = unresolvedSteps;
			uint_fast32_t depthImageSize = 0;
			uint_fast32_t depthImageCount = 0;
			VkExtent3D    depthImageExtent = { 0, 0, 0 };
			size_t resolved = 0;
			for(auto& stepDeps : unresolvedSteps) {
				assert(! resolvedSteps.contains(stepDeps.first));
				bool skipDep = [&]() {
					for(StepId dep : stepDeps.second) { if(! resolvedSteps.contains(dep)) return true; }
					return false;
				} ();
				if(! skipDep) {
					auto  stepIdx  = idToIndex<StepId>(stepDeps.first);
					auto& stepDesc = dg_steps[stepIdx];
					if(stepDesc.rpass != idgen::invalidId<RenderPassId>()) { // Scan required depth images
						auto& rpass = dg_rpasses[idToIndex<RenderPassId>(stepDesc.rpass)];
						subpassDepthSizes(depthImageExtent, depthImageSize, depthImageCount, rpass.subpasses);
					}
					Step step = { std::move(stepDesc), SequenceIndex(seq) };
					r.steps.push_back(std::move(step));
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
