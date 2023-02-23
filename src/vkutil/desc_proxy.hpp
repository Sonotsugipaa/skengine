#pragma once

#include "fwd.hpp"

#include <vulkan/vulkan.h>

#include <functional>
#include <stdexcept>
#include <vector>
#include <unordered_map>



namespace vkutil {

	using DsetSizes = std::vector<VkDescriptorPoolSize>;


	using PoolResetSyncCallback = std::function<void ()>;


	class DsetLayoutNotSubscribed : public std::logic_error {
	public:
		using std::logic_error::logic_error;
	};


	struct DsetTokenInfo {
		VkDescriptorSetLayout layout;
		VkDescriptorSet       dset;
	};


	using DsetResetCallback = std::function<void (DsetToken, const DsetTokenInfo&)>;


	struct DsetLazyAllocInfo {
		DsetToken             token;
		VkDescriptorSetLayout layout;
	};


	class DescriptorProxy {
	public:
		DescriptorProxy(): mDevice(nullptr), mInvalidated(true) { }
		~DescriptorProxy();

		[[nodiscard]] static DsetToken makeUniqueToken() noexcept;

		void registerDsetLayout(VkDescriptorSetLayout, DsetSizes);
		void dropDsetLayout(VkDescriptorSetLayout);

		[[nodiscard]] DsetToken createToken      (VkDescriptorSetLayout);
		[[nodiscard]] DsetToken createToken      (VkDescriptorSetLayout, DsetResetCallback);
		void                    destroyToken     (DsetToken);
		VkDescriptorSet         resolveToken     (DsetToken, const PoolResetSyncCallback&);
		void                    setResetCallback (DsetToken, DsetResetCallback);

		void writeDescriptorSets(uint32_t desc_count, const VkWriteDescriptorSet* writes);

		void invalidateTokens() noexcept;
		void clear();

		/// \brief Equivalent to `shrink_to_fit` funtions of STL containers.
		///
		void shrinkToFit();

	private:
		void increaseSizesFor(VkDescriptorSetLayout);

		std::unordered_map<VkDescriptorSetLayout, DsetSizes> mLayoutSizes;
		std::unordered_map<DsetToken,         DsetTokenInfo> mTokens;
		std::unordered_map<DsetToken,     DsetResetCallback> mResetCallbacks;
		std::unordered_map<VkDescriptorType,       uint32_t> mSizes;
		std::unordered_map<VkDescriptorType,       uint32_t> mMaxSizes;
		std::vector<DsetLazyAllocInfo> mLazyAllocQueue;
		VkDevice         mDevice;
		VkDescriptorPool mDpool;
		bool             mInvalidated;
	};

}
