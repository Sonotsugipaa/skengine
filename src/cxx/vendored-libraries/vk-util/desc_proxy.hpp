#pragma once

#include <vulkan/vulkan.h>

#include <functional>
#include <stdexcept>
#include <vector>
#include <unordered_map>



namespace vkutil {

	using DsetSizes = std::vector<VkDescriptorPoolSize>;


	using dset_token_e = uint_fast64_t;
	enum class DsetToken : dset_token_e { };


	class DsetLayoutNotSubscribed : public std::logic_error {
	public:
		using std::logic_error::logic_error;
	};


   //  VkStructureType                  sType;
   //  const void*                      pNext;
   //  VkDescriptorSet                  dstSet;
   //  uint32_t                         dstBinding;
   //  uint32_t                         dstArrayElement;
   //  uint32_t                         descriptorCount;
   //  VkDescriptorType                 descriptorType;
   //  const VkDescriptorImageInfo*     pImageInfo;
   //  const VkDescriptorBufferInfo*    pBufferInfo;
   //  const VkBufferView*              pTexelBufferView;
	struct DsetWriteInfo {
		uint32_t                         dstBinding;
		uint32_t                         dstArrayElement;
		uint32_t                         descriptorCount;
		VkDescriptorType                 descriptorType;

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


	class [[deprecated]] DescriptorProxy {
	public:
		DescriptorProxy(): mDevice(nullptr), mDpool(nullptr), mInvalidated(true) { }
		DescriptorProxy(VkDevice);
		~DescriptorProxy();

		void invalidateTokens() noexcept;
		void clear();
		void destroy();

		[[nodiscard]] static DsetToken makeUniqueToken() noexcept;

		void registerDsetLayout(VkDescriptorSetLayout, DsetSizes);
		void dropDsetLayout(VkDescriptorSetLayout);

		[[nodiscard]] DsetToken createToken      (VkDescriptorSetLayout);
		[[nodiscard]] DsetToken createToken      (VkDescriptorSetLayout, DsetResetCallback);
		void                    writeToken       (DsetToken, VkWriteDescriptorSet);
		void                    destroyToken     (DsetToken);
		VkDescriptorSet         resolveToken     (DsetToken);
		void                    setResetCallback (DsetToken, DsetResetCallback);

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
