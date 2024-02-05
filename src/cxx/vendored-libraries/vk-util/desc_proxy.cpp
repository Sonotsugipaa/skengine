#include "desc_proxy.hpp"

#include "error.hpp"

#include <mutex>
#include <cmath>
#include <atomic>

#include <boost/thread/mutex.hpp>

#include <spdlog/spdlog.h>



namespace vkutil {

	std::atomic<dset_token_e> desc_gen = dset_token_e(0);


	constexpr uint32_t select_dpool_size_min = 16;
	const auto& select_dpool_size = [](uint32_t n) -> uint32_t {
		if(n < select_dpool_size_min) return select_dpool_size_min;
		uint_fast32_t log = std::log2<uint_fast32_t>(n);
		if(log > std::log2<uint_fast32_t>(n-1)) {
			++ log;
		}
		if(n <= select_dpool_size_min) [[likely]] {
			return select_dpool_size_min;
		} else {
			return std::pow<uint_fast32_t>(2, log);
		}
	};


	DescriptorProxy::DescriptorProxy(VkDevice dev):
			mDevice(dev),
			mDpool(nullptr),
			mInvalidated(true)
	{ }


	DescriptorProxy::~DescriptorProxy() {
		if(mDevice != nullptr) destroy();
	}


	DsetToken DescriptorProxy::makeUniqueToken() noexcept {
		return DsetToken(++ desc_gen);
	}


	void DescriptorProxy::registerDsetLayout(VkDescriptorSetLayout layout, DsetSizes sizes) {
		if(mLayoutSizes.contains(layout)) {
			spdlog::warn("Overriding layout {:016x} in the descriptor proxy (chaos may ensue)", size_t(layout));
		}
		mLayoutSizes.insert_or_assign(layout, std::move(sizes));
	}


	void DescriptorProxy::dropDsetLayout(VkDescriptorSetLayout layout) {
		auto found = mLayoutSizes.find(layout);

		if(found == mLayoutSizes.end()) {
			spdlog::warn("Removing non-existent layout {:016x} in the descriptor proxy", size_t(layout));
		} else {
			#ifndef NDEBUG
				// Dropping currently used layouts is illegal, you know!
				for(const auto& token : mTokens) {
					assert(token.second.layout != layout);
				}
			#endif
			mLayoutSizes.erase(found);
		}
	}


	DsetToken DescriptorProxy::createToken(VkDescriptorSetLayout layout) {
		increaseSizesFor(layout);
		auto r = makeUniqueToken();
		assert(! mTokens.contains(r));
		if(mInvalidated) {
			using MapEntry = std::pair<const vkutil::DsetToken, vkutil::DsetTokenInfo>; // VS Codium linting problems
			mTokens.insert(MapEntry(r, { layout, VK_NULL_HANDLE }));
		} else {
			mLazyAllocQueue.push_back({ r, layout });
		}
		return r;
	}


	DsetToken DescriptorProxy::createToken(
			VkDescriptorSetLayout layout,
			DsetResetCallback reset_callback
	) {
		auto r = createToken(layout);
		setResetCallback(r, std::move(reset_callback));
		return r;
	}


	void DescriptorProxy::writeToken(DsetToken, VkWriteDescriptorSet wr) {
		vkUpdateDescriptorSets(mDevice, 1, &wr, 0, nullptr);
		abort();
	}


	void DescriptorProxy::destroyToken(DsetToken token) {
		auto found_token = mTokens.find(token);
		assert(found_token != mTokens.end());

		if(found_token->second.dset != VK_NULL_HANDLE) {
			VK_CHECK(vkFreeDescriptorSets, mDevice, mDpool, 1, &found_token->second.dset);
		}

		auto found_sizes = mLayoutSizes.find(found_token->second.layout);
		assert(found_sizes != mLayoutSizes.end());

		for(auto& size : found_sizes->second) {
			auto found_size = mSizes.find(size.type);
			assert(found_size != mSizes.end());
			assert(found_size->second >= size.descriptorCount);
			/**/   found_size->second -= size.descriptorCount;
		}

		for(size_t i = 0; auto& lazy_alloc : mLazyAllocQueue) {
			if(lazy_alloc.token == token) {
				mLazyAllocQueue.erase(mLazyAllocQueue.begin() + i);
				break;
			}
			++ i;
		}

		mResetCallbacks.erase(token);
		mTokens.erase(found_token);
	}


	// Possible optimization: use `vkUpdateDescriptorSets` to copy old descriptors,
	// by remembering past updates for each descriptor and performing them;
	// although doing so is somewhat complex, storing all the callbacks is expensive.
	VkDescriptorSet DescriptorProxy::resolveToken(DsetToken token) {
		assert(select_dpool_size( 0) >=  0); assert(select_dpool_size( 0) <  1);
		assert(select_dpool_size( 1) >=  1); assert(select_dpool_size( 1) <  2);
		assert(select_dpool_size(16) >= 16); assert(select_dpool_size(16) < 32);
		assert(select_dpool_size(17) >= 17); assert(select_dpool_size(17) < 32);
		assert(select_dpool_size(32) >= 32); assert(select_dpool_size(32) < 64);
		assert(select_dpool_size(33) >= 33); assert(select_dpool_size(33) < 64);

		if(mInvalidated) {
			VkDescriptorPool new_pool;

			{ // Create the new pool
				VkDescriptorPoolCreateInfo dp_info = { };
				DsetSizes sizes;
				dp_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
				dp_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
				dp_info.poolSizeCount = 0;
				dp_info.maxSets       = 0;
				// Count size limits
				for(const auto& max_size : mMaxSizes) {
					dp_info.poolSizeCount += max_size.second;
					sizes.push_back({ max_size.first, max_size.second });
					spdlog::debug("[Dpool reset] Descriptor type {} count {}", size_t(sizes.back().type), sizes.back().descriptorCount);
				}
				spdlog::debug("[Dpool reset] Descriptor count {}", dp_info.poolSizeCount);
				dp_info.pPoolSizes = sizes.data();
				VK_CHECK(vkCreateDescriptorPool, mDevice, &dp_info, nullptr, &new_pool);
			}

			{ // Allocate all the lost descriptors
				VkDescriptorSetAllocateInfo dsa_info = { };
				std::vector<VkDescriptorSetLayout> layouts;
				auto token_derefs = std::unique_ptr<VkDescriptorSet[]>((VkDescriptorSet*) operator new[](mTokens.size() * sizeof(VkDescriptorSet)));
				layouts.reserve(mTokens.size());
				for(auto& token_info : mTokens) {
					layouts.push_back(token_info.second.layout);
				}
				dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				dsa_info.descriptorPool     = new_pool;
				dsa_info.descriptorSetCount = mTokens.size();
				dsa_info.pSetLayouts        = layouts.data();
				VK_CHECK(vkAllocateDescriptorSets, mDevice, &dsa_info, token_derefs.get());
				for(uint_fast32_t i = 0; auto& token_info : mTokens) {
					token_info.second.dset = token_derefs[i];
					auto callback_found = mResetCallbacks.find(token_info.first);
					if(callback_found != mResetCallbacks.end()) {
						callback_found->second(token_info.first, token_info.second);
					}
					++ i;
				}
			}

			vkDestroyDescriptorPool(mDevice, mDpool, nullptr);
			mDpool = new_pool;
		}

		{ // Allocate the queued insertions
			VkDescriptorSetAllocateInfo dsa_info = { };
			std::vector<VkDescriptorSetLayout> layouts;
			auto tokens = std::unique_ptr<VkDescriptorSet[]>((VkDescriptorSet*) operator new[](mLazyAllocQueue.size() * sizeof(VkDescriptorSet)));
			layouts.reserve(mLazyAllocQueue.size());
			for(auto& lazy_alloc_info : mLazyAllocQueue) {
				layouts.push_back(lazy_alloc_info.layout);
			}
			dsa_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			dsa_info.descriptorPool     = mDpool;
			dsa_info.descriptorSetCount = mLazyAllocQueue.size();
			dsa_info.pSetLayouts        = layouts.data();
			VK_CHECK(vkAllocateDescriptorSets, mDevice, &dsa_info, tokens.get());
			for(uint_fast32_t i = 0; auto& lazy_alloc_info : mLazyAllocQueue) {
				assert(mTokens.contains(lazy_alloc_info.token));
				assert(mTokens[lazy_alloc_info.token].layout = layouts[i]);
				mTokens[lazy_alloc_info.token].dset = tokens[i];
				++ i;
			}
		}

		mLazyAllocQueue.clear();
		mInvalidated = false;

		return mTokens.at(token).dset;
	}


	void DescriptorProxy::setResetCallback(DsetToken token, DsetResetCallback callback) {
		assert(mTokens.contains(token));
		mResetCallbacks[token] = std::move(callback);
	}


	void DescriptorProxy::invalidateTokens() noexcept {
		mInvalidated = true;
	}


	void DescriptorProxy::clear() {
		std::vector<DsetToken> token_erase_list;
		token_erase_list.reserve(mTokens.size());
		for(const auto& token_ln : mTokens) {
			token_erase_list.push_back(token_ln.first);
		}
		for(DsetToken token : token_erase_list) {
			destroyToken(token);
		}
	}


	void DescriptorProxy::destroy() {
		if(mDpool != nullptr) {
			vkDestroyDescriptorPool(mDevice, mDpool, nullptr);
		}
		mDevice = nullptr;
		mDpool  = nullptr;
	}


	void DescriptorProxy::increaseSizesFor(VkDescriptorSetLayout layout) {
		auto sizes_found = mLayoutSizes.find(layout);
		if(sizes_found == mLayoutSizes.end()) {
			throw std::out_of_range(
				fmt::format("Increasing descriptor counts for unregistered layout {:016x}", uint64_t(layout)) );
		}

		auto& layout_sizes_list = sizes_found->second;
		bool  do_invalidate     = false;
		for(const auto& layout_size : layout_sizes_list) {
			auto& global_size     = mSizes    [layout_size.type];
			auto& global_max_size = mMaxSizes [layout_size.type];
			global_size += layout_size.descriptorCount;
			if(global_size > global_max_size) {
				global_max_size = select_dpool_size(global_size);
				do_invalidate   = true;
			}
		}

		if(do_invalidate) {
			invalidateTokens();
		}
	}

}
