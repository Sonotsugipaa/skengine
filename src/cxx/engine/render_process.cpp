#include "render_process.hpp"

#include <cassert>
#include <unordered_set>



namespace SKENGINE_NAME_NS {

	namespace {

		template <idgen::ScopedEnum T>
		constexpr auto idFromIndex(T idx) noexcept {
			return T(std::underlying_type_t<T>(idx) + idgen::baseId<std::underlying_type_t<T>>());
		}

		template <idgen::ScopedEnum T>
		constexpr auto idFromIndex(std::underlying_type_t<T> idx) noexcept {
			return T(idx + idgen::baseId<std::underlying_type_t<T>>());
		}

		template <idgen::ScopedEnum T>
		constexpr auto idToIndex(T id) noexcept {
			return T(std::underlying_type_t<T>(id) - idgen::baseId<std::underlying_type_t<T>>());
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
		assert(curStep >= 0);
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


	void RenderProcess::setup(VmaAllocator vma, VkPhysicalDevice physDev, const SequenceDescription& seqDesc) {
		rp_vkState = { vma, physDev };

		rp_steps.resize(seqDesc.steps.size());
		rp_rtargets.resize(seqDesc.rtargets.size());
		rp_rpasses.resize(seqDesc.rpasses.size());
		rp_renderers.resize(seqDesc.renderers.size());
		for(size_t i = 0; i < rp_steps.size();     ++i) rp_steps[i]     = { idFromIndex<StepId>(i), seqDesc.steps[i] };
		for(size_t i = 0; i < rp_rtargets.size();  ++i) rp_rtargets[i]  = RenderTarget { seqDesc.rtargets[i], { }, { } };
		for(size_t i = 0; i < rp_rpasses.size();   ++i) rp_rpasses[i]   = RenderPass { seqDesc.rpasses[i], nullptr };
		for(size_t i = 0; i < rp_renderers.size(); ++i) rp_renderers[i] = seqDesc.renderers[i].lock();

		++ rp_waveIterValidity;
		rp_initialized = true;
	}


	void RenderProcess::destroy() {
		rp_initialized = false;
	}


	RenderProcess::WaveRange RenderProcess::waveRange() & {
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
		dg_rtargets.push_back(std::move(rtDesc));
		return idFromIndex<RenderTargetId>(dg_rtargets.size() - 1);
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
		Subgraph r;
		r.sg_graph = this;
		r.sg_step = idFromIndex<StepId>(dg_steps.size());
		dg_steps.push_back({ idgen::invalidId<RenderPassId>(), idgen::invalidId<RendererId>() });
		dg_dependencies_fwd.insert(DependencyMap::value_type { r.sg_step, { } });
		dg_dependencies_bwd.insert(DependencyMap::value_type { r.sg_step, { } });
		return r;
	}

	RP_DG_::Subgraph RP_DG_::addStep(RenderPassId rpass, RendererId renderer) {
		Subgraph r;
		r.sg_graph = this;
		r.sg_step = idFromIndex<StepId>(dg_steps.size());
		dg_steps.push_back({ rpass, renderer });
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

		r.rtargets  = dg_rtargets;
		r.rpasses   = dg_rpasses;
		r.renderers = dg_renderers;
		r.steps.reserve(dg_steps.size());

		size_t resolved;
		while(resolvedSteps.size() < dg_steps.size()) {
			auto localResolvedSteps = mkStepSet();
			auto localUnresolvedSteps = unresolvedSteps;
			resolved = 0;
			for(auto& stepDeps : unresolvedSteps) {
				assert(! resolvedSteps.contains(stepDeps.first));
				bool skipDep = [&]() {
					for(StepId dep : stepDeps.second) {
						if(! resolvedSteps.contains(dep)) return true;
					}
					return false;
				} ();
				if(! skipDep) {
					auto  stepIdx = step_id_e(idToIndex<StepId>(stepDeps.first));
					auto& step    = dg_steps[stepIdx];
					r.steps.push_back(Step { SequenceIndex(seq), step.rpass, step.renderer });
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
			resolvedSteps.merge(std::move(localResolvedSteps));
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
