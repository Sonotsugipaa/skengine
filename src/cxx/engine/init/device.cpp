#include "init.hpp"

#include <vk-util/error.hpp>
#include <vk-util/init.hpp>
#include <vk-util/command_pool.hpp>

#include <string>
#include <charconv>

#include <SDL2/SDL.h>



namespace SKENGINE_NAME_NS {

	unsigned long sdl_init_counter = 0;


	#warning "Document how this works, since it's trippy, workaroundy and probably UB (hopefully not) (but it removes A LOT of boilerplate)"
	void Engine::DeviceInitializer::init(const DeviceInitInfo* dii) {
		assert(dii != nullptr);
		initSdl(dii);
		initVkInst(dii);
		initVkDev();
		initVma();
		initTransferCmdPool();
		initDsetLayouts();
		initRenderer();
	}


	void Engine::DeviceInitializer::destroy() {
		destroyRenderer();
		destroyDsetLayouts();
		destroyTransferCmdPool();
		destroyVma();
		destroyVkDev();
		destroyVkInst();
		destroySdl();
	}


	void Engine::DeviceInitializer::initSdl(const DeviceInitInfo* device_init_info) {
		if(0 == mPrefs.init_present_extent.width * mPrefs.init_present_extent.height) {
			throw std::invalid_argument("Initial window area cannot be 0");
		}

		if(! sdl_init_counter) {
			SDL_InitSubSystem(SDL_INIT_VIDEO);
			SDL_Vulkan_LoadLibrary(nullptr);
		}
		++ sdl_init_counter;

		uint32_t window_flags =
			SDL_WINDOW_SHOWN |
			SDL_WINDOW_VULKAN |
			SDL_WINDOW_RESIZABLE |
			(SDL_WINDOW_FULLSCREEN * mPrefs.fullscreen);

		mSdlWindow = SDL_CreateWindow(
			device_init_info->window_title.c_str(),
			0, 0,
			mPrefs.init_present_extent.width, mPrefs.init_present_extent.height,
			window_flags );

		{ // Change the present extent, as the window decided it to be
			int w, h;
			auto& w0 = mPrefs.init_present_extent.width;
			auto& h0 = mPrefs.init_present_extent.height;
			SDL_Vulkan_GetDrawableSize(mSdlWindow, &w, &h);
			if(uint32_t(w) != w0 || uint32_t(h) != h0) {
				mPrefs.init_present_extent = { uint32_t(w), uint32_t(h) };
				logger().warn("Requested window size {}x{}, got {}x{}", w0, h0, w, h);
			}
		}

		if(mSdlWindow == nullptr) {
			using namespace std::string_literals;
			const char* err = SDL_GetError();
			throw std::runtime_error(("failed to create an SDL window ("s + err) + ")");
		}
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

		mDepthAtchFmt = vkutil::selectDepthStencilFormat(mLogger.get(), mPhysDevice, VK_IMAGE_TILING_OPTIMAL);

		{ // Create logical device
			auto dev_dst = vkutil::CreateDeviceDst { this->mDevice, mQueues };

			auto features = vkutil::commonFeatures;
			features.drawIndirectFirstInstance = true;

			vkutil::CreateDeviceInfo cd_info = { };
			cd_info.physDev           = mPhysDevice;
			cd_info.extensions        = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
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


	void Engine::DeviceInitializer::initTransferCmdPool() {
		VkCommandPoolCreateInfo cpc_info = { };
		cpc_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cpc_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		cpc_info.queueFamilyIndex = mQueues.families.graphicsIndex;
		vkCreateCommandPool(mDevice, &cpc_info, nullptr, &mTransferCmdPool);
	}


	void Engine::DeviceInitializer::initDsetLayouts() {
		{ // Gframe dset layout
			VkDescriptorSetLayoutBinding dslb[2] = { { }, { } };
			dslb[0].binding = FRAME_UBO_BINDING;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			dslb[0].descriptorCount = 1;
			dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			dslb[1] = dslb[0];
			dslb[1].binding = LIGHT_STORAGE_BINDING;
			dslb[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

			VkDescriptorSetLayoutCreateInfo dslc_info = { };
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslc_info.bindingCount = std::size(dslb);
			dslc_info.pBindings = dslb;
			VK_CHECK(vkCreateDescriptorSetLayout, mDevice, &dslc_info, nullptr, &mGframeDsetLayout);
		}

		{ // Material dset layout
			VkDescriptorSetLayoutBinding dslb[4] = { { }, { }, { }, { } };
			dslb[0].binding = DIFFUSE_TEX_BINDING;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			dslb[0].descriptorCount = 1;
			dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			dslb[1] = dslb[0];
			dslb[1].binding = NORMAL_TEX_BINDING;
			dslb[2] = dslb[0];
			dslb[2].binding = SPECULAR_TEX_BINDING;
			dslb[3] = dslb[0];
			dslb[3].binding = EMISSIVE_TEX_BINDING;

			VkDescriptorSetLayoutCreateInfo dslc_info = { };
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslc_info.bindingCount = std::size(dslb);
			dslc_info.pBindings = dslb;
			VK_CHECK(vkCreateDescriptorSetLayout, mDevice, &dslc_info, nullptr, &mMaterialDsetLayout);
		}
	}


	void Engine::DeviceInitializer::initRenderer() {
		mAssetSupplier = AssetSupplier(*this, 0.125f);
		mWorldRenderer = WorldRenderer::create(
			std::make_shared<spdlog::logger>(logger()),
			mVma,
			mMaterialDsetLayout,
			mPrefs.asset_filename_prefix,
			mAssetSupplier,
			mAssetSupplier );
	}


	void Engine::DeviceInitializer::destroyRenderer() {
		WorldRenderer::destroy(mWorldRenderer);
		mAssetSupplier.destroy();
	}


	void Engine::DeviceInitializer::destroyDsetLayouts() {
		vkDestroyDescriptorSetLayout(mDevice, mMaterialDsetLayout, nullptr);
		vkDestroyDescriptorSetLayout(mDevice, mGframeDsetLayout, nullptr);
	}


	void Engine::DeviceInitializer::destroyTransferCmdPool() {
		vkDestroyCommandPool(mDevice, mTransferCmdPool, nullptr);
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


	void Engine::DeviceInitializer::destroySdl() {
		if(mSdlWindow != nullptr) {
			SDL_DestroyWindow(mSdlWindow);
			mSdlWindow = nullptr;
		}

		assert(sdl_init_counter > 0);
		-- sdl_init_counter;
		if(sdl_init_counter == 0) {
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			SDL_Quit();
		}
	}

}
