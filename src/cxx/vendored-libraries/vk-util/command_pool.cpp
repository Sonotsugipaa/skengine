#include "command_pool.hpp"

#include "error.hpp"

#include <cassert>



namespace vkutil {

	void CommandPool::createSomeFences() {
		size_t old_size          = mFences.size();
		VkFenceCreateInfo f_info = { };
		f_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		f_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		mFences.reserve(old_size + 1);
		assert(mFences.capacity() > old_size);
		for(size_t i = old_size; i < mFences.capacity(); ++i) {
			VkFence f;
			VK_CHECK(vkCreateFence, mVkDevice, &f_info, nullptr, &f);
			mFences.push_back(f);
		}
	}


	CommandPool::FenceIdx CommandPool::allocFence() {
		auto alloc = mUsedFences.try_alloc(1);
		assert(alloc.pageCount < 2);
		if(alloc.pageCount < 1) {
			flushCommands();
			createSomeFences();
			alloc = mUsedFences.try_alloc(1);
			assert(alloc.pageCount == 1);
		}
		assert(alloc.base < mFences.size());
		return FenceIdx(alloc.base);
	}


	void CommandPool::createSomeCmds() {
		size_t old_size = mCmds.size();
		VkCommandBufferAllocateInfo cba_info = { };
		cba_info.sType       = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		cba_info.level       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cba_info.commandPool = mPool;
		mCmds.reserve(old_size + 1);
		assert(mCmds.capacity() > old_size);
		cba_info.commandBufferCount = mCmds.capacity() - old_size;
		mCmds.resize(mCmds.capacity());
		VK_CHECK(vkAllocateCommandBuffers, mVkDevice, &cba_info, mCmds.data() + old_size);
	}


	CommandPool::CmdIdx CommandPool::allocCmd() {
		auto alloc = mUsedCmds.try_alloc(1);
		assert(alloc.pageCount < 2);
		if(alloc.pageCount < 1) {
			flushCommands();
			createSomeCmds();
			alloc = mUsedCmds.try_alloc(1);
			assert(alloc.pageCount == 1);
		}
		assert(alloc.base < mCmds.size());
		return CmdIdx(alloc.base);
	}



	void CommandPool::runCmd(
			const CommandInfo& cmd_info_ref,
			VkFence            fence,
			VkCommandBuffer    cmd,
			const RunFn&       fn
	) {
		auto cmd_info = cmd_info_ref;

		{
			VkCommandBufferBeginInfo cb_info = { };
			cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			cb_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			VK_CHECK(vkBeginCommandBuffer, cmd, &cb_info);
		}
		fn(cmd);
		VK_CHECK(vkEndCommandBuffer, cmd);

		{
			VkSubmitInfo s_info = { };
			s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			s_info.commandBufferCount   = 1;
			s_info.pCommandBuffers      = &cmd;
			s_info.pWaitDstStageMask    = cmd_info.wait_dst_stage_masks;
			s_info.waitSemaphoreCount   = cmd_info.wait_semaphore_count;
			s_info.pWaitSemaphores      = cmd_info.wait_semaphore_ptr;
			s_info.signalSemaphoreCount = cmd_info.signal_semaphore_count;
			s_info.pSignalSemaphores    = cmd_info.signal_semaphore_ptr;
			VK_CHECK(vkQueueSubmit, cmd_info.queue, 1, &s_info, fence);
		}
		if(fence != VK_NULL_HANDLE) vkWaitForFences(mVkDevice, 1, &fence, VK_TRUE, UINT64_MAX);
	}


	CommandPool::CommandPool(VkDevice dev, uint32_t fam_index, bool transient):
			CommandPoolBase(dev)
	{
		VkCommandPoolCreateInfo cp_info = { };
		cp_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cp_info.queueFamilyIndex = fam_index;
		cp_info.flags = transient? VK_COMMAND_POOL_CREATE_TRANSIENT_BIT : 0;
		vkCheck("vkCreateCommandPool", vkCreateCommandPool, mVkDevice, &cp_info, nullptr, &mPool);
	}


	CommandPool::~CommandPool() {
		if(mVkDevice != nullptr) {
			flushCommands();
			vkDestroyCommandPool(mVkDevice, mPool, nullptr);
			for(VkFence fence : mFences) {
				vkDestroyFence(mVkDevice, fence, nullptr);
			}
		}
	}


	CommandPool::CommandPool(CommandPool&& mv):
			CommandPoolBase(std::move(mv))
	{
		mv.mVkDevice = nullptr;
	}


	void CommandPool::flushCommands() {
		if(! mFlushFenceBuffers.empty()) {
			VK_CHECK(vkWaitForFences, mVkDevice, mFlushFenceBuffers.size(), mFlushFenceBuffers.data(), VK_TRUE, UINT64_MAX);
		}
		mFlushFenceBuffers.clear();
		for(const auto& acmd : mAsyncCmds) {
			mUsedFences.dealloc(size_t(acmd.fence_idx));
			mUsedCmds.dealloc(size_t(acmd.cmd_idx));
		}
	}


	// void CommandPool::run(const CommandInfo& cmd_info, VkFence fence, const RunFn& fn) {
	// 	assert(fence != nullptr);
	// 	CmdIdx cmd_idx = allocCmd();
	// 	auto&  cmd     = mCmds[size_t(cmd_idx)];
	// 	runCmd(cmd_info, fence, cmd, fn);
	// 	mUsedCmds.dealloc(size_t(cmd_idx));
	// }


	void CommandPool::runAsync(const CommandInfo& cmd_info, const RunAsyncFn& fn) {
		FenceIdx fence_idx = allocFence();
		CmdIdx   cmd_idx   = allocCmd();
		auto&    fence     = mFences[size_t(fence_idx)];
		auto&    cmd       = mCmds[size_t(cmd_idx)];

		auto wrapper = [fence, &fn](VkCommandBuffer cmd) { fn(cmd, fence); };
		runCmd(cmd_info, nullptr, cmd, wrapper);

		mAsyncCmds.push_back({ fence_idx, cmd_idx });
		mFlushFenceBuffers.push_back(fence);
	}

	CommandBuffer CommandPool::allocateBuffer() {
		CommandBuffer r;
		r.token = size_t(allocCmd());
		r.value = mCmds[r.token];
		return r;
	}


	void CommandPool::deallocateBuffer(CommandBuffer& cmd) {
		mUsedCmds.dealloc(cmd.token);
	}

}
