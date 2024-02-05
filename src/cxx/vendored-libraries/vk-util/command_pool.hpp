#pragma once

#include <vulkan/vulkan.h>

#include <rll-alloc/static_allocator.hpp>

#include <functional>
#include <concepts>
#include <vector>
#include <deque>



namespace vkutil {

	struct CommandInfo {
		VkQueue queue;
		VkPipelineStageFlags* wait_dst_stage_masks;
		uint32_t     wait_semaphore_count;
		VkSemaphore* wait_semaphore_ptr;
		uint32_t     signal_semaphore_count;
		VkSemaphore* signal_semaphore_ptr;
	};


	struct CommandBuffer {
		VkCommandBuffer value;
		size_t          token;
		operator VkCommandBuffer() const noexcept { return value; }
	};


	struct CommandPoolBase {
	protected:
		enum class FenceIdx : size_t { };
		enum class CmdIdx   : size_t { };

		CommandPoolBase():             mVkDevice(nullptr) { }
		CommandPoolBase(VkDevice dev): mVkDevice(dev)     { }

		struct AsyncCmd {
			FenceIdx fence_idx;
			CmdIdx   cmd_idx;
		};

		VkDevice      mVkDevice;
		VkCommandPool mPool;
		std::vector<VkFence>         mFences;
		std::vector<VkCommandBuffer> mCmds;
		std::vector<AsyncCmd>        mAsyncCmds;
		std::vector<VkFence>         mFlushFenceBuffers;
		rll_alloc::StaticAllocator<size_t> mUsedFences;
		rll_alloc::StaticAllocator<size_t> mUsedCmds;
	};


	class CommandPool : private CommandPoolBase {
	public:
		using RunFn      = std::function<void (VkCommandBuffer)>;
		using RunAsyncFn = std::function<void (VkCommandBuffer, VkFence)>;
		using Allocator  = rll_alloc::StaticAllocator<size_t>;

		CommandPool() = default;
		CommandPool(VkDevice, uint32_t queue_family_index, bool transient = false);

		CommandPool(CommandPool&&);
		CommandPool& operator=(CommandPool&& mv) { this->~CommandPool(); return * new (this) CommandPool(std::move(mv)); }

		~CommandPool();

		void flushCommands();

		void run      (const CommandInfo&, VkFence, const RunFn&);
		void runAsync (const CommandInfo&,          const RunAsyncFn&);

		CommandBuffer allocateBuffer   ();
		void          deallocateBuffer (CommandBuffer&);

	private:
		void     createSomeFences ();
		FenceIdx allocFence       ();
		void     createSomeCmds ();
		CmdIdx   allocCmd       ();
		void     runCmd         (const CommandInfo&, VkFence, VkCommandBuffer, const RunFn&);
	};

}
