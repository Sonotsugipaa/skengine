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


		void submit_onetime_cmd(VkDevice dev, VkQueue queue, VkCommandBuffer cmd) {
			auto fence = create_fence(dev, false);
			VkSubmitInfo s_info = { };
			s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			s_info.commandBufferCount = 1;
			s_info.pCommandBuffers    = &cmd;
			VK_CHECK(vkQueueSubmit, queue, 1, &s_info, fence);
			VK_CHECK(vkWaitForFences, dev, 1, &fence, true, UINT64_MAX);
			vkDestroyFence(dev, fence, nullptr);
		}

	}



	void Engine::pushBuffer(const TransferContext& tc, vkutil::BufferDuplex& b) {
		if(b.isHostVisible()) {
			b.flush(nullptr, tc.vma);
		} else {
			VmaAllocatorInfo vaInfo;
			vmaGetAllocatorInfo(tc.vma, &vaInfo);
			auto cmd = create_cmd_buffer(vaInfo.device, tc.cmdPool);
			b.flush(cmd, tc.vma);
			submit_onetime_cmd(vaInfo.device, tc.cmdQueue, cmd);
			vkFreeCommandBuffers(vaInfo.device, tc.cmdPool, 1, &cmd);
		}
	}


	void Engine::pullBuffer(const TransferContext& tc, vkutil::BufferDuplex& b) {
		if(b.isHostVisible()) {
			b.invalidate(nullptr, tc.vma);
		} else {
			VmaAllocatorInfo vaInfo;
			vmaGetAllocatorInfo(tc.vma, &vaInfo);
			auto cmd = create_cmd_buffer(vaInfo.device, tc.cmdPool);
			b.invalidate(cmd, tc.vma);
			submit_onetime_cmd(vaInfo.device, tc.cmdQueue, cmd);
			vkFreeCommandBuffers(vaInfo.device, tc.cmdPool, 1, &cmd);
		}
	}

}
