#include "init.hpp"

#include <vk-util/error.hpp>
#include <vk-util/init.hpp>
#include <vk-util/command_pool.hpp>

#include <string>
#include <memory>
#include <charconv>
#include <unordered_set>



namespace SKENGINE_NAME_NS {

	#warning "Document how this works, since it's trippy, workaroundy and *definitely* UB (but it removes A LOT of boilerplate)"
	void Engine::DeviceInitializer::init(const DeviceInitInfo* dii) {
		assert(dii != nullptr);
		initSdl(dii);
		initVkInst(dii);
		initVkDev();
		initVma();
		initTransferContext();
		initDsetLayouts();
		initAssets();
	}


	void Engine::DeviceInitializer::destroy() {
		destroyAssets();
		destroyDsetLayouts();
		destroyTransferContext();
		destroyVma();
		destroyVkDev();
		destroyVkInst();
		destroySdl();
	}


	void Engine::DeviceInitializer::initVkInst(const DeviceInitInfo* device_init_info) {
		assert(mSdlWindow != nullptr);

		VkApplicationInfo a_info  = { };
		a_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		a_info.pEngineName        = SKENGINE_NAME_LC_CSTR;
		a_info.pApplicationName   = device_init_info->application_name.c_str();
		a_info.applicationVersion = device_init_info->app_version;
		a_info.apiVersion         = VK_API_VERSION_1_3;
		a_info.engineVersion = VK_MAKE_API_VERSION(
			0,
			SKENGINE_VERSION_MAJOR,
			SKENGINE_VERSION_MINOR,
			SKENGINE_VERSION_PATCH );

		std::vector<const char*> extensions;

		{ // Query SDL Vulkan extensions
			uint32_t extCount;
			if(SDL_TRUE != SDL_Vulkan_GetInstanceExtensions(mSdlWindow, &extCount, nullptr)) throw std::runtime_error("Failed to query SDL Vulkan extensions");
			extensions.resize(extCount);
			if(SDL_TRUE != SDL_Vulkan_GetInstanceExtensions(mSdlWindow, &extCount, extensions.data())) throw std::runtime_error("Failed to query SDL Vulkan extensions");

			for(uint32_t i=0; i < extCount; ++i) {
				logger().debug("SDL2 requires Vulkan extension \"{}\"", extensions[i]);
			}
		}

		VkInstanceCreateInfo r = { };
		r.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		r.pApplicationInfo = &a_info;
		r.enabledExtensionCount = extensions.size();
		r.ppEnabledExtensionNames = extensions.data();
		VK_CHECK(vkCreateInstance, &r, nullptr, &mVkInstance);
	}


	void Engine::DeviceInitializer::initVkDev() {
		assert(mVkInstance != nullptr);

		using namespace std::string_literals;
		std::vector<VkPhysicalDevice> devs;

		{ // Get physical devices
			uint32_t count;
			VK_CHECK(vkEnumeratePhysicalDevices, mVkInstance, &count, nullptr);
			if(count < 1) {
				throw std::runtime_error("Failed to find a Vulkan physical device");
			}
			devs.resize(count);
			VK_CHECK(vkEnumeratePhysicalDevices, mVkInstance, &count, devs.data());
		}

		{ // Select physical device
			VkPhysicalDeviceFeatures features = vkutil::commonFeatures;
			features.drawIndirectFirstInstance = true;
			features.fillModeNonSolid = true;

			unsigned best_dev_index;
			vkutil::SelectBestPhysDeviceDst best_dev = {
				mPhysDevice, mDevProps, best_dev_index };
			vkutil::selectBestPhysDevice(mLogger.get(), best_dev, devs, features, &mPrefs.phys_device_uuid);

			std::vector<std::string_view> missing_props;
			{ // Check for missing required properties
				VkPhysicalDeviceFeatures avail_ftrs;
				mDevFeatures = { };
				vkGetPhysicalDeviceFeatures(mPhysDevice, &avail_ftrs);
				missing_props = vkutil::listDevMissingFeatures(avail_ftrs, mDevFeatures);
			}

			if(! missing_props.empty()) {
				for(const auto& prop : missing_props) {
					logger().error("Selected device [{}]={:x}:{:x} \"{}\" is missing property `{}`",
						best_dev_index,
						mDevProps.vendorID, mDevProps.deviceID,
						mDevProps.deviceName,
						prop );
				}
				throw std::runtime_error("The chosen device is missing "s +
					std::to_string(missing_props.size()) + " features"s);
			}
		}

		auto avail_extensions = std::unordered_set<std::string_view>(0);
		{ // Enumerate extensions
			std::unique_ptr<VkExtensionProperties[]> avail_extensions_buf;
			uint32_t avail_extensions_count;
			{ // Get available device extensions
				VK_CHECK(vkEnumerateDeviceExtensionProperties, mPhysDevice, nullptr, &avail_extensions_count, nullptr);
				if(avail_extensions_count < 1) {
					throw std::runtime_error("Failed to find a Vulkan physical device");
				}
				avail_extensions_buf = std::make_unique_for_overwrite<VkExtensionProperties[]>(avail_extensions_count);
				VK_CHECK(vkEnumerateDeviceExtensionProperties, mPhysDevice, nullptr, &avail_extensions_count, avail_extensions_buf.get());
			}
			avail_extensions.max_load_factor(2.0f);
			avail_extensions.rehash(avail_extensions_count);
			for(uint32_t i = 0; i < avail_extensions_count; ++i) {
				auto* name = avail_extensions_buf[i].extensionName;
				mLogger->trace("Available device extension: {}", name);
				avail_extensions.insert(name);
			}
		}

		mDepthAtchFmt = vkutil::selectDepthStencilFormat(mLogger.get(), mPhysDevice, VK_IMAGE_TILING_OPTIMAL);

		{ // Create logical device
			auto dev_dst = vkutil::CreateDeviceDst { this->mDevice, mQueues };

			auto features = vkutil::commonFeatures;
			features.drawIndirectFirstInstance = true;
			features.fillModeNonSolid = true;

			std::vector<const char*> extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

			// Optional extensions
			#define INS_IF_AVAIL_(NM_) if(avail_extensions.contains(NM_)) extensions.push_back(NM_);
			INS_IF_AVAIL_("VK_EXT_pageable_device_local_memory")
			INS_IF_AVAIL_("VK_EXT_memory_priority")
			#undef INS_IF_AVAIL_

			// Required extensions
			auto request_ext = [&](std::string_view nm) {
				if(avail_extensions.contains(nm)) {
					extensions.push_back(nm.data());
				} else {
					mLogger->error("Desired device extension not available: {}", nm);
				}
			};
			request_ext("VK_EXT_hdr_metadata");

			vkutil::CreateDeviceInfo cd_info = { };
			cd_info.physDev           = mPhysDevice;
			cd_info.extensions        = std::move(extensions);
			cd_info.pPhysDevProps     = &mDevProps;
			cd_info.pRequiredFeatures = &features;

			vkutil::createDevice(mLogger.get(), dev_dst, cd_info);
		}
	}


	void Engine::DeviceInitializer::initVma() {
		assert(mDevice != nullptr);
		VmaAllocatorCreateInfo acInfo = { };
		acInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		acInfo.instance = mVkInstance;
		acInfo.device = mDevice;
		acInfo.physicalDevice = mPhysDevice;
		VK_CHECK(vmaCreateAllocator, &acInfo, &mVma);
	}


	void Engine::DeviceInitializer::initTransferContext() {
		#warning "TODO: intialize mTransferContext.cmdFence and use it in pushBuffer and pullBuffer"
		VkCommandPool pool;
		VkCommandPoolCreateInfo cpc_info = { };
		cpc_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpc_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		cpc_info.queueFamilyIndex = mQueues.families.graphicsIndex;
		VK_CHECK(vkCreateCommandPool, mDevice, &cpc_info, nullptr, &pool);
		mTransferContext = { .vma = mVma, .cmdPool = pool, .cmdFence = nullptr, .cmdQueue = mQueues.graphics, .cmdQueueFamily = mQueues.families.graphicsIndex };
	}


	void Engine::DeviceInitializer::initDsetLayouts() {
		{ // GUI dset layouts
			VkDescriptorSetLayoutBinding dslb[1] = { };
			dslb[0].binding = WorldRenderer::DIFFUSE_TEX_BINDING; // ??
			dslb[0].descriptorCount = 1;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			dslb[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo dslc_info = { };
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslc_info.bindingCount = std::size(dslb);
			dslc_info.pBindings = dslb;
			VK_CHECK(vkCreateDescriptorSetLayout, mDevice, &dslc_info, nullptr, &mImagePipelineDsetLayout);

			mGeometryPipelineDsetLayout = nullptr;
		}
	}


	void Engine::DeviceInitializer::initAssets() {
		auto newLogger = std::make_shared<spdlog::logger>(logger());
		mAssetSupplier = AssetSupplier(*this, std::move(newLogger), mAssetSource, 0.125f, mDevProps.limits.maxSamplerAnisotropy);
	}


	void Engine::DeviceInitializer::destroyAssets() {
		mAssetSupplier.destroy();
	}


	void Engine::DeviceInitializer::destroyDsetLayouts() {
		vkDestroyDescriptorSetLayout(mDevice, mImagePipelineDsetLayout, nullptr);
		assert(mGeometryPipelineDsetLayout == nullptr);
	}


	void Engine::DeviceInitializer::destroyTransferContext() {
		vkDestroyCommandPool(mDevice, mTransferContext.cmdPool, nullptr);
	}


	void Engine::DeviceInitializer::destroyVma() {
		assert(mDevice != nullptr);

		if(mVma != nullptr) {
			vmaDestroyAllocator(mVma);
			mVma = nullptr;
		}
	}


	void Engine::DeviceInitializer::destroyVkDev () {
		assert(mVkInstance != nullptr);

		if(mDevice != nullptr) {
			vkDestroyDevice(mDevice, nullptr);
			mDevice = nullptr;
		}
	}


	void Engine::DeviceInitializer::destroyVkInst() {
		if(mVkInstance != nullptr) {
			vkDestroyInstance(mVkInstance, nullptr);
		}
	}

}
