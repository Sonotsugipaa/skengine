#include "engine.hpp"

#include <vk-util/error.hpp>



namespace SKENGINE_NAME_NS {

	namespace {

		VkCommandBuffer create_cmd_buffer(VkDevice dev, VkCommandPool pool) {
			VkCommandBufferAllocateInfo ca_info = { };
			ca_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			ca_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			ca_info.commandPool = pool;
			ca_info.commandBufferCount = 1;
			VkCommandBuffer cmd;
			VK_CHECK(vkAllocateCommandBuffers, dev, &ca_info, &cmd);
			return cmd;
		}


		VkFence create_fence(VkDevice dev, bool signaled) {
			VkFence r;
			VkFenceCreateInfo fc_info = { };
			fc_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			fc_info.flags = VK_FENCE_CREATE_SIGNALED_BIT * signaled;
			vkCreateFence(dev, &fc_info, nullptr, &r);
			return r;
		}


		void submit_onetime_cmd(VkDevice dev, VkFence fence, VkQueue queue, VkCommandBuffer cmd, bool doReset) {
			VkSubmitInfo s_info = { };
			s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			s_info.commandBufferCount = 1;
			s_info.pCommandBuffers    = &cmd;
			if(doReset) VK_CHECK(vkResetFences, dev, 1, &fence);
			VK_CHECK(vkQueueSubmit, queue, 1, &s_info, fence);
		}

	}



	TransferCmdBarrier::TransferCmdBarrier(VkDevice d, VkCommandPool p, VkCommandBuffer c, VkFence f):
		vkDevice(d), cmdPool(p), cmdBuffer(c), cmdFence(f)
	{
		if(d != nullptr) {
			assert(p != nullptr);
			assert(c != nullptr);
			assert(f != nullptr);
		}
	}


	TransferCmdBarrier::~TransferCmdBarrier() {
		if(vkDevice == nullptr) return;
	}


	void TransferCmdBarrier::wait() {
		assert(vkDevice != nullptr);
		std::exception_ptr ex = { };
		try {
			VK_CHECK(vkWaitForFences, vkDevice, 1, &cmdFence, VK_TRUE, UINT64_MAX);
		} catch(...) { ex = std::current_exception(); }
		vkFreeCommandBuffers(vkDevice, cmdPool, 1, &cmdBuffer);
		vkDevice = nullptr;
		cmdPool = nullptr;
		cmdBuffer = nullptr;
		cmdFence = nullptr;
	}


	void Engine::pushBuffer(const TransferContext& tc, vkutil::BufferDuplex& b) {
		if(b.isHostVisible()) {
			b.flush(nullptr, tc.vma);
		} else {
			auto dev = vmaGetAllocatorDevice(tc.vma);
			auto cmd = create_cmd_buffer(dev, tc.cmdPool);
			b.flush(cmd, tc.vma);
			submit_onetime_cmd(dev, tc.cmdFence, tc.cmdQueue, cmd, true);
			VK_CHECK(vkWaitForFences, dev, 1, &tc.cmdFence, true, UINT64_MAX);
			vkFreeCommandBuffers(dev, tc.cmdPool, 1, &cmd);
		}
	}


	void Engine::pullBuffer(const TransferContext& tc, vkutil::BufferDuplex& b) {
		if(b.isHostVisible()) {
			b.invalidate(nullptr, tc.vma);
		} else {
			auto dev = vmaGetAllocatorDevice(tc.vma);
			auto cmd = create_cmd_buffer(dev, tc.cmdPool);
			b.invalidate(cmd, tc.vma);
			submit_onetime_cmd(dev, tc.cmdFence, tc.cmdQueue, cmd, true);
			VK_CHECK(vkWaitForFences, dev, 1, &tc.cmdFence, true, UINT64_MAX);
			vkFreeCommandBuffers(dev, tc.cmdPool, 1, &cmd);
		}
	}


	TransferCmdBarrier Engine::pushBufferAsync(const TransferContext& tc, vkutil::BufferDuplex& b) {
		if(b.isHostVisible()) {
			b.flush(nullptr, tc.vma);
			return { };
		} else {
			auto dev = vmaGetAllocatorDevice(tc.vma);
			auto cmd = create_cmd_buffer(dev, tc.cmdPool);
			auto fence = create_fence(dev, false);
			b.flush(cmd, tc.vma);
			submit_onetime_cmd(dev, fence, tc.cmdQueue, cmd, false);
			return TransferCmdBarrier(dev, tc.cmdPool, cmd, fence);
		}
	}


	TransferCmdBarrier Engine::pullBufferAsync(const TransferContext& tc, vkutil::BufferDuplex& b) {
		if(b.isHostVisible()) {
			b.invalidate(nullptr, tc.vma);
			return { };
		} else {
			auto dev = vmaGetAllocatorDevice(tc.vma);
			auto cmd = create_cmd_buffer(dev, tc.cmdPool);
			auto fence = create_fence(dev, false);
			b.invalidate(cmd, tc.vma);
			submit_onetime_cmd(dev, fence, tc.cmdQueue, cmd, false);
			return TransferCmdBarrier(dev, tc.cmdPool, cmd, fence);
		}
	}

}
