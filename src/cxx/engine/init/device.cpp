#include "init.hpp"

#include <vk-util/error.hpp>
#include <vk-util/init.hpp>
#include <vk-util/command_pool.hpp>

#include <string>
#include <memory>
#include <charconv>
#include <unordered_set>

#include <SDL2/SDL.h>



namespace SKENGINE_NAME_NS {

	unsigned long sdl_init_counter = 0;


	#warning "Document how this works, since it's trippy, workaroundy and *definitely* UB (but it removes A LOT of boilerplate)"
	void Engine::DeviceInitializer::init(const DeviceInitInfo* dii) {
		assert(dii != nullptr);
		initSdl(dii);
		initVkInst(dii);
		initVkDev();
		initVma();
		initTransferCmdPool();
		initDsetLayouts();
		initRenderer();
		initFreetype();
	}


	void Engine::DeviceInitializer::destroy() {
		destroyFreetype();
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
			if(0 != SDL_InitSubSystem(SDL_INIT_VIDEO)) {
				using namespace std::string_literals;
				const char* err = SDL_GetError();
				throw std::runtime_error(("failed initialize the SDL Video subsystem ("s + err) + ")");
			}

			if(0 != SDL_Vulkan_LoadLibrary(nullptr)) {
				using namespace std::string_literals;
				const char* err = SDL_GetError();
				throw std::runtime_error(("failed to load a Vulkan library ("s + err) + ")");
			}
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
			auto require_ext = [&](std::string_view nm) {
				if(! avail_extensions.contains(nm)) {
					mLogger->error("Required device extension not available: {}", nm);
				}
				extensions.push_back(nm.data());
			};
			require_ext("VK_EXT_hdr_metadata");

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
			VkDescriptorSetLayoutBinding dslb[5] = { };
			dslb[0].binding = DIFFUSE_TEX_BINDING;
			dslb[0].descriptorCount = 1;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			dslb[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
			dslb[1] = dslb[0];
			dslb[1].binding = NORMAL_TEX_BINDING;
			dslb[2] = dslb[0];
			dslb[2].binding = SPECULAR_TEX_BINDING;
			dslb[3] = dslb[0];
			dslb[3].binding = EMISSIVE_TEX_BINDING;
			dslb[4] = dslb[0];
			dslb[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			dslb[4].binding = MATERIAL_UBO_BINDING;

			VkDescriptorSetLayoutCreateInfo dslc_info = { };
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslc_info.bindingCount = std::size(dslb);
			dslc_info.pBindings = dslb;
			VK_CHECK(vkCreateDescriptorSetLayout, mDevice, &dslc_info, nullptr, &mMaterialDsetLayout);
		}

		{ // GUI dset layout
			VkDescriptorSetLayoutBinding dslb[1] = { };
			dslb[0].binding = DIFFUSE_TEX_BINDING;
			dslb[0].descriptorCount = 1;
			dslb[0].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			dslb[0].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

			VkDescriptorSetLayoutCreateInfo dslc_info = { };
			dslc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			dslc_info.bindingCount = std::size(dslb);
			dslc_info.pBindings = dslb;
			VK_CHECK(vkCreateDescriptorSetLayout, mDevice, &dslc_info, nullptr, &mGuiDsetLayout);
		}
	}


	void Engine::DeviceInitializer::initRenderer() {
		mAssetSupplier = AssetSupplier(*this, mAssetSource, 0.125f);
		mWorldRenderer = WorldRenderer::create(
			std::make_shared<spdlog::logger>(logger()),
			mVma,
			mMaterialDsetLayout,
			mAssetSupplier );
	}


	void Engine::DeviceInitializer::initFreetype() {
		auto error = FT_Init_FreeType(&mFreetype);
		if(error) throw FontError("failed to initialize FreeType", error);
{
auto cmd = [&](){
	VkCommandBufferAllocateInfo ca_info = { };
	ca_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	ca_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ca_info.commandPool = mTransferCmdPool;
	ca_info.commandBufferCount = 1;
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers, mDevice, &ca_info, &cmd);
	return cmd;
} ();
auto beginCmd = [&]() {
	VkCommandBufferBeginInfo cbbInfo = { };
	cbbInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cbbInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer, cmd, &cbbInfo);
};
auto endCmd = [&]() {
	VK_CHECK(vkEndCommandBuffer, cmd);
};
auto submitCmd = [&]() {
	auto cmdFence = [&]() {
		VkFence r;
		VkFenceCreateInfo fc_info = { };
		fc_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		vkCreateFence(mDevice, &fc_info, nullptr, &r);
		return r;
	} ();
	VkSubmitInfo s_info = { };
	s_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	s_info.commandBufferCount = 1;
	s_info.pCommandBuffers    = &cmd;
	VK_CHECK(vkQueueSubmit, mQueues.graphics, 1, &s_info, cmdFence);
	VK_CHECK(vkWaitForFences, mDevice, 1, &cmdFence, true, UINT64_MAX);
	vkDestroyFence(mDevice, cmdFence, nullptr);
};
mPlaceholderFont = FontFace::fromFile(mFreetype, "/usr/share/fonts/gnu-free/FreeMono.otf");
mPlaceholderGlyph = mPlaceholderFont.getGlyphBitmap('/', 32);
VkDescriptorPoolCreateInfo dpcInfo = { };
VkDescriptorPoolSize sizes[] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 } };
dpcInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
dpcInfo.maxSets = 1;
dpcInfo.poolSizeCount = std::size(sizes);
dpcInfo.pPoolSizes = sizes;
VK_CHECK(vkCreateDescriptorPool, mDevice, &dpcInfo, nullptr, &mPlaceholderGlyphDpool);
VkDescriptorSetAllocateInfo dsaInfo = { };
dsaInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
dsaInfo.descriptorPool = mPlaceholderGlyphDpool;
dsaInfo.descriptorSetCount = 1;
dsaInfo.pSetLayouts = &mGuiDsetLayout;
mLogger->critical("GLYPH BASELINE ({}, {}) SIZE {}x{}", mPlaceholderGlyph.xBaseline, mPlaceholderGlyph.yBaseline, mPlaceholderGlyph.width, mPlaceholderGlyph.height);

vkutil::BufferCreateInfo bcInfo = { };
size_t glyphPixCount = mPlaceholderGlyph.byteCount();
bcInfo.size  = glyphPixCount * 4; // The glyph has a 1-byte grayscale texel, the image wants a 4-byte RGBA texel because GLSL said so
bcInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
auto stagingBuffer = vkutil::ManagedBuffer::createStagingBuffer(mVma, bcInfo);
vkutil::ImageCreateInfo icInfo = { };
icInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
icInfo.extent = { mPlaceholderGlyph.width, mPlaceholderGlyph.height, 1 };
icInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
icInfo.type = VK_IMAGE_TYPE_2D;
icInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
icInfo.samples = VK_SAMPLE_COUNT_1_BIT;
icInfo.tiling = VK_IMAGE_TILING_LINEAR;
icInfo.arrayLayers = 1;
icInfo.mipLevels = 1;
vkutil::AllocationCreateInfo acInfo = { };
acInfo.requiredMemFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
acInfo.vmaFlags = { };
acInfo.vmaUsage = vkutil::VmaAutoMemoryUsage::eAutoPreferDevice;
mPlaceholderGlyphImage = vkutil::Image::create(mVma, icInfo, acInfo);
void* devGlyphPtr;
VK_CHECK(vmaMapMemory, mVma, stagingBuffer, &devGlyphPtr);
#define P_ reinterpret_cast<uint8_t*>(devGlyphPtr)
for(size_t i = 0; i < glyphPixCount; ++i) {
	auto v = uint8_t(mPlaceholderGlyph.bytes[i]);
	P_[(i*4)+0] = 0xff; P_[(i*4)+1] = 0xff; P_[(i*4)+2] = 0xff; P_[(i*4)+3] = v;
}
#undef P_
vmaUnmapMemory(mVma, stagingBuffer);
VkBufferImageCopy cp = { };
cp.bufferRowLength   = mPlaceholderGlyph.width;
cp.bufferImageHeight = mPlaceholderGlyph.height;
cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
cp.imageSubresource.layerCount = 1;
cp.imageExtent = icInfo.extent;
VkImageMemoryBarrier2 bar0 = { };
VkImageMemoryBarrier2 bar1;
bar0.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
bar0.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
bar0.srcAccessMask = VK_ACCESS_2_NONE;
bar0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
bar0.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
bar0.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
bar0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
bar0.srcQueueFamilyIndex = bar0.dstQueueFamilyIndex = mQueues.families.graphicsIndex;
bar0.image = mPlaceholderGlyphImage;
bar0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
bar0.subresourceRange.levelCount = 1;
bar0.subresourceRange.layerCount = 1;
bar1 = bar0;
bar1.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
bar1.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
bar1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
bar1.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
bar1.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
bar1.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
VkDependencyInfo depInfo = { };
depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
depInfo.imageMemoryBarrierCount = 1;
depInfo.pImageMemoryBarriers = &bar0;
beginCmd();
vkCmdPipelineBarrier2(cmd, &depInfo);
vkCmdCopyBufferToImage(cmd, stagingBuffer, mPlaceholderGlyphImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
depInfo.pImageMemoryBarriers = &bar1;
vkCmdPipelineBarrier2(cmd, &depInfo);
endCmd();
submitCmd();
vkutil::ManagedBuffer::destroy(mVma, stagingBuffer);
VkImageViewCreateInfo ivcInfo = { };
ivcInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
ivcInfo.image = mPlaceholderGlyphImage;
ivcInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
ivcInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
ivcInfo.components = { };
ivcInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
ivcInfo.subresourceRange.levelCount = 1;
ivcInfo.subresourceRange.layerCount = 1;
VK_CHECK(vkCreateImageView, mDevice, &ivcInfo, nullptr, &mPlaceholderGlyphImageView);
VkSamplerCreateInfo scInfo = { };
scInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
scInfo.magFilter = scInfo.minFilter = VK_FILTER_NEAREST;
scInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
scInfo.addressModeU = scInfo.addressModeV = scInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
scInfo.mipLodBias = 1.0f;
scInfo.anisotropyEnable = true;
scInfo.maxAnisotropy = mDevProps.limits.maxSamplerAnisotropy;
scInfo.maxLod = icInfo.mipLevels;
VK_CHECK(vkCreateSampler, mDevice, &scInfo, nullptr, &mPlaceholderGlyphSampler);

// IMAGE, IMAGEVIEW AND SAMPLER HAVE TO BE CREATED HERE OR SOONER
VK_CHECK(vkAllocateDescriptorSets, mDevice, &dsaInfo, &mPlaceholderGlyphDset);
VkDescriptorImageInfo diInfo = { };
diInfo.sampler = mPlaceholderGlyphSampler;
diInfo.imageView = mPlaceholderGlyphImageView;
diInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
VkWriteDescriptorSet wDset = { };
wDset.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
wDset.dstSet = mPlaceholderGlyphDset;
wDset.dstBinding = 0;
wDset.descriptorCount = 1;
wDset.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
wDset.pImageInfo = &diInfo;
vkUpdateDescriptorSets(mDevice, 1, &wDset, 0, nullptr);

vkFreeCommandBuffers(mDevice, mTransferCmdPool, 1, &cmd);
}
	}


	void Engine::DeviceInitializer::destroyFreetype() {
vkDestroyDescriptorPool(mDevice, mPlaceholderGlyphDpool, nullptr);
vkDestroySampler(mDevice, mPlaceholderGlyphSampler, nullptr);
vkDestroyImageView(mDevice, mPlaceholderGlyphImageView, nullptr);
vkutil::Image::destroy(mVma, mPlaceholderGlyphImage);
mPlaceholderFont = { };
		FT_Done_FreeType(mFreetype);
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
			SDL_Vulkan_UnloadLibrary();
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			SDL_Quit();
		}
	}

}
