#include "engine.hpp"
#include "debug.inl.hpp"

#include "init/init.hpp"

#include <posixfio_tl.hpp>

#include <fmamdl/fmamdl.hpp>

#include <vk-util/error.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <deque>

#include <vulkan/vk_enum_string_helper.h>



namespace SKENGINE_NAME_NS {
namespace {

	constexpr tickreg::delta_t choose_delta(tickreg::delta_t avg, tickreg::delta_t last) {
		using tickreg::delta_t;
		constexpr auto tolerance_factor = delta_t(1.0) / delta_t(2.0);
		delta_t diff = (avg > last)? (avg - last) : (last - avg);
		if(diff > last * tolerance_factor) avg = last;
		return avg;
	}

}}



struct SKENGINE_NAME_NS::Engine::Implementation {

	using frame_counter_t = decltype(Engine::mGframeSelector);


	struct DrawInfo {
		ConcurrentAccess& concurrent_access;
		GframeData* gframe;
		uint32_t sc_img_idx;
	};


	static void setupRprocess(Engine& e, RenderProcessInterface& rpi) {
		auto depGraph = RenderProcess::DependencyGraph(e.mLogger, e.gframeCount());
		auto ca = ConcurrentAccess(&e, true);

		rpi.rpi_setupRenderProcess(ca, depGraph);

		try {
			e.mRenderProcess.setup(e.mVma, e.mLogger, ca, e.mDepthAtchFmt, uint32_t(e.mPresentQfamIndex), e.mGframes.size(), depGraph.assembleSequence());
			e.mLogger.debug("Renderer list:");
			size_t waveIdx = 0;
			for(auto wave : e.mRenderProcess.waveRange()) {
				for(auto& step : wave) {
					auto& ra = step.second.renderArea;
					e.mLogger.debug("  Wave {}: step {} renderer {} renderArea ({},{})x({},{})",
						waveIdx,
						RenderProcess::step_id_e(step.first),
						render_target_id_e(step.second.renderer),
						ra.offset.x, ra.offset.y, ra.extent.width, ra.extent.height );
				}
				++ waveIdx;
			}
			if(waveIdx == 0) e.mLogger.debug("  (empty)");
		} catch(RenderProcess::UnsatisfiableDependencyError& err) {
			e.mLogger.error("{}:", err.what());
			auto& chain = err.dependencyChain();
			for(auto step : chain) e.mLogger.error("  Renderer {:3}", RenderProcess::step_id_e(step));
			e.mLogger.error("  Renderer {:3} ...", RenderProcess::step_id_e(chain.front()));
			std::rethrow_exception(std::current_exception());
		}
	}

	static void destroyRprocess(Engine& e, RenderProcessInterface& rpi) {
		auto ca = ConcurrentAccess(&e, true);
		e.mRenderProcess.destroy(ca);
		rpi.rpi_destroyRenderProcess(ca);
	}


	static VkFence& selectGframeFence(Engine& e) {
		auto i = (++ e.mGframeSelector) % frame_counter_t(e.mGframes.size());
		auto& r = e.mGframeSelectionFences[i];
		VK_CHECK(vkResetFences, e.mDevice, 1, &r);
		return r;
	}


	static void prepareStep(
		Renderer& renderer,
		RenderPass& rpass,
		VkCommandBuffer cmd,
		const Renderer::DrawSyncPrimitives& syncs,
		DrawInfo& draw
	) {
		(void) rpass;
		assert(draw.sc_img_idx < rpass.framebuffers.size());
		auto  rdrDrawInfo = Renderer::DrawInfo { .syncPrimitives = syncs, .gframeIndex = draw.sc_img_idx };
		renderer.duringPrepareStage(draw.concurrent_access, rdrDrawInfo, cmd);
	}

	static void drawStep(
		RenderProcess::Step& step,
		Renderer& renderer,
		RenderPass& rpass,
		VkCommandBuffer cmd,
		const Renderer::DrawSyncPrimitives& syncs,
		DrawInfo& draw
	) {
		auto rdrDrawInfo = Renderer::DrawInfo { .syncPrimitives = syncs, .gframeIndex = draw.sc_img_idx };
		VkRenderPassBeginInfo rpb_info = { };
		rpb_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		rpb_info.framebuffer = rpass.framebuffers[draw.sc_img_idx].handle;
		rpb_info.renderPass  = rpass.handle;
		rpb_info.clearValueCount = step.clearColors.size();
		rpb_info.pClearValues    = step.clearColors.begin();
		rpb_info.renderArea      = step.renderArea;

		vkCmdBeginRenderPass(cmd, &rpb_info, VK_SUBPASS_CONTENTS_INLINE);
		renderer.duringDrawStage(draw.concurrent_access, rdrDrawInfo, cmd);
		vkCmdEndRenderPass(cmd);
		renderer.afterRenderPass(draw.concurrent_access, rdrDrawInfo, cmd);
	}


	static void draw(Engine& e, LoopInterface& loop) {
		#define IF_WORLD_RPASS_(RENDERER_) if(RENDERER_->pipelineInfo().rpass == Renderer::RenderPass::eWorld)
		#define IF_UI_RPASS_(RENDERER_)    if(RENDERER_->pipelineInfo().rpass == Renderer::RenderPass::eUi)
		using SeqIdx = RenderProcess::SequenceIndex;
		using seq_idx_e = RenderProcess::seq_idx_e;
		GframeData* gframe;
		uint32_t    sc_img_idx = ~ uint32_t(0);
		auto        delta_avg  = e.mGraphicsReg.estDelta();
		auto        delta_last = e.mGraphicsReg.lastDelta();
		auto        delta      = choose_delta(delta_avg, delta_last);
		auto        concurrent_access = ConcurrentAccess(&e, true);
		auto        waves = e.mRenderProcess.waveRange();
		auto        steps = e.mRenderProcess.sortedStepRange();

		auto ifRenderer = [&] <typename Fn, typename... Args> (Renderer* rdr, Fn&& fn, Args&&... args) {
			if(rdr != nullptr) (rdr->*std::forward<Fn>(fn))(std::forward<Args>(args)...);
		};

		auto stepRdrDrawInfo = [&](const RenderProcess::Step& step, decltype(sc_img_idx) gfIdx) {
			return Renderer::DrawInfo { e.mRenderProcess.getDrawSyncPrimitives(step.seqIndex, gfIdx), gfIdx };
		};

		e.mGraphicsReg.beginCycle();

		{ // Acquire image
			VkFence sc_img_fence;
			try {
				sc_img_fence = selectGframeFence(e);
			} catch(vkutil::VulkanError& err) {
				auto str = std::string_view(string_VkResult(err.vkResult()));
				e.logger().error("Failed to acquire gframe fence ({})", str);
				return;
			}
			VkResult res = vkAcquireNextImageKHR(e.mDevice, e.mSwapchain, UINT64_MAX, nullptr, sc_img_fence, &sc_img_idx);
			switch(res) {
				case VK_SUCCESS:
					break;
				case VK_ERROR_OUT_OF_DATE_KHR:
					e.logger().trace("Swapchain is out of date");
					e.signal(Signal::eReinit);
					return;
				case VK_SUBOPTIMAL_KHR:
					e.logger().trace("Swapchain is suboptimal");
					e.signal(Signal::eReinit);
					break;
				case VK_TIMEOUT:
					e.logger().trace("Swapchain image request timed out");
					return;
				default:
					assert(res < VkResult(0));
					throw vkutil::VulkanError("vkAcquireNextImage2KHR", res);
			}
			gframe = e.mGframes.data() + sc_img_idx;
			assert(! steps.empty());
			VK_CHECK(vkWaitForFences, e.mDevice, 1, &sc_img_fence, VK_TRUE, UINT64_MAX);
		}
		auto draw_info = DrawInfo { concurrent_access, gframe, sc_img_idx };

		e.mGframeCounter.fetch_add(1, std::memory_order_relaxed);

		VkCommandBufferBeginInfo cbb_info = { };
		cbb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		gframe->frame_delta = delta;

		for(auto wave : waves) for(auto& step : wave) {
			auto* renderer = e.mRenderProcess.getRenderer(step.second.renderer);
			ifRenderer(renderer, &Renderer::beforePreRender, concurrent_access, stepRdrDrawInfo(step.second, sc_img_idx));
		}
		loop.loop_async_preRender(concurrent_access, delta, delta_last);

		e.mRendererMutex.lock();

		// Prepare and draw calls for each renderer
		RenderProcess::DrawSyncPrimitives lastSyncs = { };
		for(auto seqIdx = SeqIdx(0); auto wave : waves) {
			#ifndef NDEBUG
				for(auto& step : wave) assert(seqIdx == step.second.seqIndex);
			#endif

			auto& syncs = e.mRenderProcess.getDrawSyncPrimitives(seqIdx, sc_img_idx);
			auto& waveGframe = e.mRenderProcess.getWaveGframeData(seqIdx, sc_img_idx);

			VK_CHECK(vkResetCommandPool, e.mDevice, waveGframe.cmdPool, 0);
			VK_CHECK(vkBeginCommandBuffer, waveGframe.cmdPrepare, &cbb_info);
			VK_CHECK(vkBeginCommandBuffer, waveGframe.cmdDraw, &cbb_info);

			for(auto& step : wave) {
				auto* renderer = e.mRenderProcess.getRenderer(step.second.renderer);
				if(renderer != nullptr) {
					auto& rpass = e.mRenderProcess.getRenderPass(step.second.rpass);
					prepareStep(*renderer, rpass, waveGframe.cmdPrepare, syncs, draw_info);
				}
			}
			VK_CHECK(vkEndCommandBuffer, waveGframe.cmdPrepare);
			for(auto& step : wave) {
				auto* renderer = e.mRenderProcess.getRenderer(step.second.renderer);
				if(renderer != nullptr) {
					auto& rpass = e.mRenderProcess.getRenderPass(step.second.rpass);
					drawStep(step.second, *renderer, rpass, waveGframe.cmdDraw, syncs, draw_info);
				}
			}
			VK_CHECK(vkEndCommandBuffer, waveGframe.cmdDraw);

			VkSubmitInfo subm = { };

			{ // Submit the prepare and draw commands
				assert(! wave.empty());
				constexpr VkPipelineStageFlags waitStages[2] = {
					0,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT };
				bool waitForLastDraw = seq_idx_e(seqIdx) > 0;
				subm.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
				subm.commandBufferCount = 1;
				subm.pCommandBuffers    = &waveGframe.cmdPrepare;
				subm.pWaitDstStageMask  = waitStages + 0;
				subm.waitSemaphoreCount = waitForLastDraw? 1 : 0;
				subm.pWaitSemaphores    = waitForLastDraw? &lastSyncs.semaphores.draw : nullptr;
				subm.signalSemaphoreCount = 1;
				subm.pSignalSemaphores    = &syncs.semaphores.prepare;
				VK_CHECK(vkResetFences, e.mDevice,          1,       &syncs.fences.prepare);
				VK_CHECK(vkQueueSubmit, e.mQueues.graphics, 1, &subm, syncs.fences.prepare);
				subm.waitSemaphoreCount = 1;
				subm.pCommandBuffers    = &waveGframe.cmdDraw;
				subm.pWaitDstStageMask  = waitStages + 1;
				subm.pWaitSemaphores    = &syncs.semaphores.prepare;
				subm.pSignalSemaphores  = &syncs.semaphores.draw;
				VK_CHECK(vkResetFences, e.mDevice,          1,       &syncs.fences.draw);
				VK_CHECK(vkQueueSubmit, e.mQueues.graphics, 1, &subm, syncs.fences.draw);

				lastSyncs = syncs;
			}

			seqIdx = SeqIdx(seq_idx_e(seqIdx) + 1);
		}

		{ // Here's a present!
			VkResult res;
			VkPresentInfoKHR p_info = { };
			p_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			p_info.pResults = &res;
			p_info.swapchainCount = 1;
			p_info.pSwapchains    = &e.mSwapchain;
			p_info.pImageIndices  = &sc_img_idx;
			p_info.waitSemaphoreCount = 1;
			p_info.pWaitSemaphores    = &lastSyncs.semaphores.draw;
			VK_CHECK(vkQueuePresentKHR, e.mPresentQueue, &p_info);
		}

		for(auto wave : waves) for(auto& step : wave) {
			auto* renderer = e.mRenderProcess.getRenderer(step.second.renderer);
			ifRenderer(renderer, &Renderer::afterPresent, concurrent_access, stepRdrDrawInfo(step.second, sc_img_idx));
		}

		e.mGframeLast = int_fast32_t(sc_img_idx);
		e.mWaveFencesWaitCache.clear();
		e.mWaveFencesWaitCache.reserve(e.mRenderProcess.waveCount());
		if(e.mPrefs.wait_for_gframe) {
			for(auto seqIdx = SeqIdx(0); auto wave : waves) {
				auto& syncs = e.mRenderProcess.getDrawSyncPrimitives(seqIdx, sc_img_idx);
				e.mWaveFencesWaitCache.push_back(syncs.fences.prepare);
				e.mWaveFencesWaitCache.push_back(syncs.fences.draw);
				seqIdx = SeqIdx(seq_idx_e(seqIdx) + 1);
			}
		} else {
			for(auto seqIdx = SeqIdx(0); auto wave : waves) {
				auto& syncs = e.mRenderProcess.getDrawSyncPrimitives(seqIdx, sc_img_idx);
				e.mWaveFencesWaitCache.push_back(syncs.fences.prepare);
				seqIdx = SeqIdx(seq_idx_e(seqIdx) + 1);
			}
		}
		e.mRendererMutex.unlock();

		for(auto wave : waves) for(auto& step : wave) {
			auto* renderer = e.mRenderProcess.getRenderer(step.second.renderer);
			ifRenderer(renderer, &Renderer::beforePostRender, concurrent_access, stepRdrDrawInfo(step.second, sc_img_idx));
		}
		loop.loop_async_postRender(concurrent_access, delta, e.mGraphicsReg.lastDelta());
		for(auto wave : waves) for(auto& step : wave) {
			auto* renderer = e.mRenderProcess.getRenderer(step.second.renderer);
			ifRenderer(renderer, &Renderer::afterPostRender, concurrent_access, stepRdrDrawInfo(step.second, sc_img_idx));
		}

		e.mGraphicsReg.endCycle();

		#undef IF_WORLD_RPASS_
		#undef IF_UI_RPASS_
	}


	static LoopInterface::LoopState runLogicIteration(Engine& e, LoopInterface& loop) {
		e.mLogicReg.beginCycle();
		auto delta_last = e.mLogicReg.lastDelta();
		auto delta = choose_delta(e.mLogicReg.estDelta(), delta_last);

		e.mGframePriorityOverride.store(true, std::memory_order_seq_cst);
		loop.loop_processEvents(delta, delta_last);
		e.mGframePriorityOverride.store(false, std::memory_order_seq_cst);
		e.mGframeResumeCond.notify_one();

		e.mLogicReg.endCycle();

		auto r = loop.loop_pollState();

		e.mLogicReg.awaitNextTick();
		return r;
	}


	static void handleSignals(Engine& e, RenderProcessInterface& rpi) {
		bool reinitGate = false;
		auto reinit = [&]() {
			using tickreg::delta_t;

			if(reinitGate) return;
			reinitGate = true;

			auto time = std::chrono::duration_cast<std::chrono::duration<decltype(Engine::mLastResizeTime), std::milli>>(std::chrono::steady_clock::now().time_since_epoch()).count();
			if(e.mLastResizeTime + 1000 > time) return;

			// Some compositors resize the window as soon as it appears, and this seems to cause problems
			e.mGraphicsReg.resetEstimates (delta_t(1.0) / delta_t(e.mPrefs.target_framerate));
			e.mLogicReg.resetEstimates    (delta_t(1.0) / delta_t(e.mPrefs.target_tickrate));

			auto init = reinterpret_cast<Engine::RpassInitializer*>(&e);
			auto ca = ConcurrentAccess(&e, true);
			Implementation::destroyRprocess(e, rpi);
			init->reinit(ca);
			Implementation::setupRprocess(e, rpi);
		};

		std::unique_lock<std::mutex> gframeLock;

		bool gframeLockExists = false;
		auto awaitRpass = [&]() {
			if(! gframeLockExists) {
				gframeLock = e.pauseRenderPass();
				gframeLockExists = true;
			}
		};

		auto extractSignal = [&]() {
			auto r = e.mSignalXthread.exchange(Signal::eNone, std::memory_order::seq_cst);
			return Signal(r);
		};

		auto handleSignal = [&](Signal sig) {
			switch(sig) {
				default: [[fallthrough]];
				case Signal::eNone: return false; break;
				case Signal::eReinit: reinit(); break;
			}
			return true;
		};

		// Handle the local signal once
		Signal curSignal = Signal::eNone;
		std::swap(curSignal, e.mSignalGthread);
		if(curSignal != Signal::eNone) {
			handleSignal(curSignal);
			e.mLogger.trace("Handled signal {} from graphics thread", signalString(curSignal));
		}

		awaitRpass();
		curSignal = extractSignal();
		if(curSignal != Signal::eNone) {
			do {
				handleSignal(curSignal);
				e.mLogger.trace("Handled signal {} from external thread, polling new immediate signal", signalString(curSignal));
				curSignal = extractSignal();
			} while(curSignal != Signal::eNone);
		}
		assert(e.mSignalGthread == Signal::eNone);
		assert(e.mSignalXthread.load(std::memory_order::relaxed) == Signal::eNone);
	}

};



namespace SKENGINE_NAME_NS {

	const EnginePreferences EnginePreferences::default_prefs = EnginePreferences {
		.phys_device_uuid               = "",
		.init_present_extent            = { 600, 400 },
		.max_render_extent              = { 0, 0 },
		.present_mode                   = VK_PRESENT_MODE_FIFO_KHR,
		.max_concurrent_frames          = 2,
		.framerate_samples              = 16,
		.upscale_factor                 = 1.0f,
		.target_framerate               = 60.0f,
		.target_tickrate                = 60.0f,
		.fullscreen                     = false,
		.composite_alpha                = false,
		.wait_for_gframe                = true
	};


	constexpr auto regulator_params = tickreg::RegulatorParams {
		.deltaTolerance     = 0.2,
		.burstTolerance     = 0.05,
		.compensationFactor = 0.0,
		.strategyMask       = tickreg::strategy_flag_t(tickreg::WaitStrategyFlags::eSleepUntil) };



	void ConcurrentAccess::setPresentExtent(VkExtent2D ext) {
		bool extChanged =
			(ext.width  != ca_engine->mPresentExtent.width) ||
			(ext.height != ca_engine->mPresentExtent.height);
		if(extChanged) {
			ca_engine->mPrefs.init_present_extent = ext;

			auto gframeLock = std::unique_lock(ca_engine->mGframeMutex, std::defer_lock_t());
			bool gframeNotRunning = gframeLock.try_lock();
			if(gframeNotRunning) {
				ca_engine->signal(Engine::Signal::eReinit);
			} else {
				// A gframe is already running, and would deadlock on mRendererMutex;
				// we need to wait for the gframe to end, and the state of the engine
				// protected by mRendererMutex may change unbeknownst to the caller.
				ca_engine->mGframePriorityOverride.store(true, std::memory_order::seq_cst);
				ca_engine->mRendererMutex.unlock();
				ca_engine->signal(Engine::Signal::eReinit, true);
				ca_engine->mGframePriorityOverride.store(false, std::memory_order::seq_cst);
				ca_engine->mGframeResumeCond.notify_one();
				ca_engine->mRendererMutex.lock();
			}
		}
	}



	Engine::Engine(
			const DeviceInitInfo& di,
			const EnginePreferences& ep,
			std::shared_ptr<ShaderCacheInterface> sci,
			const Logger& logger
	):
		mLogger(cloneLogger(logger, std::string_view("["), std::string_view(SKENGINE_NAME_PC_CSTR ":Engine "), std::string_view(""), std::string_view("]  "))),
		mShaderCache(std::move(sci)),
		mGraphicsReg(
			ep.framerate_samples,
			decltype(ep.target_framerate)(1.0) / ep.target_framerate,
			tickreg::WaitStrategyState::eSleepUntil,
			regulator_params ),
		mLogicReg(
			ep.framerate_samples,
			decltype(ep.target_tickrate)(1.0) / ep.target_tickrate,
			tickreg::WaitStrategyState::eSleepUntil,
			regulator_params ),
		mGframePriorityOverride(false),
		mGframeCounter(0),
		mGframeSelector(0),
		mSignalGthread(Signal::eNone),
		mSignalXthread(Signal::eNone),
		mLastResizeTime(0),
		mPrefs(ep),
		mIsRunning(false)
	{
		{
			auto init = reinterpret_cast<Engine::DeviceInitializer*>(this);
			init->init(&di);
		}

		{
			auto rpass_cfg = RpassConfig::default_cfg;
			auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
			auto ca = ConcurrentAccess(this, true);
			init->init(ca, rpass_cfg);
		}
	}


	Engine::~Engine() {
		{
			auto init = reinterpret_cast<Engine::RpassInitializer*>(this);
			auto ca = ConcurrentAccess(this, true);
			init->destroy(ca);
		}

		{
			auto init = reinterpret_cast<Engine::DeviceInitializer*>(this);
			init->destroy();
		}
	}


	void Engine::run(LoopInterface& loop, std::shared_ptr<RenderProcessInterface> rpi) {
		auto loop_state = loop.loop_pollState();
		auto exception  = std::exception_ptr(nullptr);

		auto handle_exception = [&]() {
			auto lock = std::unique_lock(mGframeMutex, std::try_to_lock);
			exception = std::current_exception();
			loop_state = LoopInterface::LoopState::eShouldStop;
		};

		{
			auto ca = ConcurrentAccess(this, true);
			rpi->rpi_createRenderers(ca);
			Implementation::setupRprocess(*this, *rpi);
		}

		auto gframeLock = std::unique_lock(mGframeMutex);

		mGraphicsThread = std::thread([&]() {
			#ifdef NDEBUG
				mIsRunning.store(true, std::memory_order::seq_cst);
			#else
				bool wasRunningBeforeBeginning = mIsRunning.exchange(true, std::memory_order::seq_cst);
				assert(! wasRunningBeforeBeginning);
			#endif
			while(loop_state != LoopInterface::LoopState::eShouldStop) {
				try {
					auto gframeLock = std::unique_lock(mGframeMutex, std::defer_lock);
					if(mGframePriorityOverride.load(std::memory_order_consume)) [[unlikely]] {
						gframeLock.lock();
						if(mGframePriorityOverride.load(std::memory_order_consume)) { // Need to check again, after potentially waiting for the lock
							mGframeResumeCond.wait(gframeLock);
						}
					} else {
						gframeLock.lock(); // This must be done for both branches, but before waiting on the cond var AND after checking the atomic var
					}
					Implementation::draw(*this, loop);
					Implementation::handleSignals(*this, *rpi);
					gframeLock.unlock();
					mGraphicsReg.awaitNextTick();
				} catch(...) {
					handle_exception();
				}
			}
			mIsRunning.store(false, std::memory_order::seq_cst);
		});

		try { loop.loop_begin(); } catch(...) { handle_exception(); }
		gframeLock.unlock();
		while((exception == nullptr) && (loop_state != LoopInterface::LoopState::eShouldStop)) {
			try {
				loop_state = Implementation::runLogicIteration(*this, loop);
			} catch(...) {
				handle_exception();
			}
		}
		mGframePriorityOverride.store(true, std::memory_order_seq_cst);
		gframeLock.lock();
		loop.loop_end();
		gframeLock.unlock();

		assert(mGraphicsThread.joinable());
		auto renderPassLock = pauseRenderPass();
		mGframePriorityOverride.store(false, std::memory_order_seq_cst);
		mGframeResumeCond.notify_one();
		renderPassLock.unlock();
		mGraphicsThread.join();
		mGraphicsThread = { };

		{
			auto ca = ConcurrentAccess(this, true);
			Implementation::destroyRprocess(*this, *rpi);
			rpi->rpi_destroyRenderers(ca);
		}

		if(exception) {
			std::rethrow_exception(exception);
		}
	}


	bool Engine::isRunning() const noexcept {
		return mIsRunning.load(std::memory_order::seq_cst);
	}


	void Engine::signal(Signal newSig, bool discardDuplicate) noexcept {
		using Seconds = std::chrono::duration<float, std::ratio<1>>;
		Signal oldSig;
		bool thisIsGraphicsThread = mGraphicsThread.get_id() == std::this_thread::get_id();
		auto exchangeSig = [&]() { auto r = mSignalXthread.exchange(newSig, std::memory_order::seq_cst); return Signal(r); };
		auto discardBecauseDuplicateFn = [&]() { return discardDuplicate && (oldSig == newSig); };

		if(thisIsGraphicsThread) {
			mLogger.trace("Graphics thread signaling {}", signalString(newSig));
			oldSig = Signal(mSignalGthread);
			mSignalGthread = newSig;
			assert(oldSig == Signal::eNone);
		} else {
			auto gframeLock = std::unique_lock(mGframeMutex, std::defer_lock_t());
			oldSig = exchangeSig();
			bool discardBecauseDuplicate = discardBecauseDuplicateFn();
			while((oldSig != Signal::eNone) && (! discardBecauseDuplicate)) {
				mSignalXthread.store(oldSig, std::memory_order::seq_cst);
				mGframePriorityOverride.store(false, std::memory_order::release);
				if(gframeLock.try_lock()) {
					gframeLock.unlock();
					oldSig = exchangeSig();
					discardBecauseDuplicate = discardBecauseDuplicateFn();
				} else {
					auto sleepTime = Seconds(100.0f / (mPrefs.target_tickrate * float(mPrefs.max_concurrent_frames)));
					mLogger.trace("Signal {} enqueued, sleeping for {}s", signalString(newSig), sleepTime.count());
					mGframeResumeCond.notify_one();
					std::this_thread::sleep_for(sleepTime);
				}
			}
			if(discardBecauseDuplicate) {
				assert(oldSig == newSig);
				mLogger.trace("Discarded duplicate signal {}", signalString(newSig));
			} else {
				assert(oldSig == Signal::eNone);
				mLogger.trace("External thread signaling {}", signalString(newSig));
			}
		}
	}


	std::unique_lock<std::mutex> Engine::pauseRenderPass() {
		auto lock = std::unique_lock(mGframeMutex, std::defer_lock_t());

		auto wait_for_fences = [&]() {
			// Wait for all fences, in this order: selection -> prepare -> draw
			#define ITERATE_STEP_GFRAMES_ for(auto& step : mRenderProcess.sortedStepRange()) for(size_t gfIdx = 0; gfIdx < mGframes.size(); ++gfIdx)
			#define INSERT_FENCE_(M_) fences.push_back(mRenderProcess.getDrawSyncPrimitives(step.second.seqIndex, gfIdx).fences.M_);
			std::vector<VkFence> fences;
			for(auto& gff : mGframeSelectionFences) vkWaitForFences(mDevice, 1, &gff, true, UINT64_MAX);
			fences.reserve(mGframes.size());
			ITERATE_STEP_GFRAMES_ { INSERT_FENCE_(prepare) }
			vkWaitForFences(mDevice, fences.size(), fences.data(), true, UINT64_MAX);
			fences.clear();
			fences.reserve(mGframes.size());
			ITERATE_STEP_GFRAMES_ { INSERT_FENCE_(draw) }
			vkWaitForFences(mDevice, fences.size(), fences.data(), true, UINT64_MAX);
			#undef ITERATE_STEP_GFRAMES_
			#undef INSERT_FENCE_
		};

		bool is_graphics_thread = std::this_thread::get_id() == mGraphicsThread.get_id();
		if(! is_graphics_thread) {
			mGframePriorityOverride.store(true, std::memory_order_seq_cst);
			lock.lock();
			wait_for_fences();
			mGframePriorityOverride.store(false, std::memory_order_seq_cst);
			mGframeResumeCond.notify_one();
		} else {
			wait_for_fences();
		}
		vkDeviceWaitIdle(mDevice);

		return lock;
	}


	void Engine::setWaitForGframe(bool wait_for_gframe) {
		mPrefs.wait_for_gframe = wait_for_gframe;
	}


	MutexAccess<ConcurrentAccess> Engine::getConcurrentAccess() noexcept {
		assert(mGraphicsThread.get_id() != std::this_thread::get_id() && "This *will* cause a deadlock");
		auto gframeLock = std::unique_lock(mGframeMutex, std::defer_lock);
		auto rendererLock = std::unique_lock(mRendererMutex);
		if(this->isRunning()) {
			mGframeResumeCond.notify_one();
		}
		auto ca = ConcurrentAccess(this, false);
		return MutexAccess(std::move(ca), std::move(rendererLock));
	}

}
