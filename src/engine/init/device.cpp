#include "init.hpp"

#include <vkutil/error.hpp>

#include <string>
#include <charconv>

#include <SDL2/SDL.h>

#include <spdlog/spdlog.h>



namespace SKENGINE_NAME_NS {

	unsigned long sdl_init_counter = 0;


	bool uuid_to_bytes(uint8_t dst[16], std::string_view sv) {
		static_assert(sizeof(uint8_t)  == 1);
		static_assert(sizeof(uint16_t) == 2);

		// 01234567-8901-2345-6789-012345678901
		if(sv.size() != 32+4) return false;

		bool err = false;

		uint8_t get16_buffer[2];
		const auto get16 = [&err, &get16_buffer](const char* beg) {
			uint16_t    r = 0xff;
			const char* end = beg + 4;
			auto        res = std::from_chars<>(beg, end, r, 0x10);
			if(res.ptr != end) err = true;
			get16_buffer[0] = (r >> 8) & 0xff;
			get16_buffer[1] = r & 0xff;
		};

		#define GET_(IOFF16_, STROFF_) \
			{ \
				get16(sv.data()+STROFF_); \
				if(err) return false; \
				assert((IOFF16_) < 8); \
				memcpy(dst + ((IOFF16_)*2), &get16_buffer, 2); \
			}
		GET_(0,  0+0)
		GET_(1,  4+0)
		GET_(2,  8+1)
		GET_(3, 12+2)
		GET_(4, 16+3)
		GET_(5, 20+4)
		GET_(6, 24+4)
		GET_(7, 28+4)
		#undef GET_

		return true;
	}


	void uuid_from_bytes(std::string& dst, uint8_t src[16]) {
		dst = fmt::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
			src[0],  src[1],  src[2],  src[3],
			src[4],  src[5],  src[6],  src[7],
			src[8],  src[9],  src[10], src[11],
			src[12], src[13], src[14], src[15] );
	}


	float rank_phys_device(float order_bias, const VkPhysicalDeviceProperties& props) {
		float r = 1.0f;

		{ // Rank version (0 < x <= [reasonable major version number])
			r +=
				VK_API_VERSION_MAJOR(props.driverVersion) +
				(VK_API_VERSION_MINOR(props.driverVersion) / 10.0f) +
				(VK_API_VERSION_PATCH(props.driverVersion) / 1000.0f) +
				(VK_API_VERSION_VARIANT(props.driverVersion) / 1000000.0f);
		} { // Rank device index
			/* Rationale: even if the driver's version is two minor numbers
				* behind the lead, the user may prefer to use the first occurring device. */
			//r -= orderBias / 5.0f;
			r += order_bias / 5.0f;
		} { // Rank the device type (0 < x <= 1)
			#define MKCASE_(TYPE_, R_) case TYPE_: r *= R_; break;
			switch(props.deviceType) {
				MKCASE_(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,   1.0f)
				MKCASE_(VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,    0.9f)
				MKCASE_(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, 0.5f)
				MKCASE_(VK_PHYSICAL_DEVICE_TYPE_OTHER,          0.3f)
				default: assert(false); [[fallthrough]];
				MKCASE_(VK_PHYSICAL_DEVICE_TYPE_CPU,            0.01f)
			}
			#undef MKCASE_
		}

		return r;
	}


	std::vector<std::string> check_dev_missing_features(
			const VkPhysicalDeviceFeatures& features,
			VkPhysicalDeviceFeatures&       dst_add_features
	) {
		std::vector<std::string> r;
		#define CKFTR_(FTR_) { if(! features.FTR_) { r.push_back(#FTR_); dst_add_features.FTR_ = true; } }
		CKFTR_(samplerAnisotropy)
		CKFTR_(sampleRateShading)
		CKFTR_(multiDrawIndirect)
		#undef CKFTR_
		return r;
	}


	using select_best_phys_device_t = std::tuple<VkPhysicalDevice&, unsigned&, VkPhysicalDeviceProperties&>;

	void select_best_phys_device(
			const select_best_phys_device_t&
				dst,
			const std::vector<VkPhysicalDevice>&
				devices,
			std::string&
				preferred_dev_dst,
			std::string_view
				preferred_dev
	) {
		std::get<0>(dst) = nullptr;
		std::get<1>(dst) = ~ unsigned(0);
		std::get<2>(dst) = { };
		float   bestRank    = rank_phys_device(1.0f, std::get<2>(dst));
		uint8_t uuid_cmp[16];
		bool    prefer_uuid = uuid_to_bytes(uuid_cmp, preferred_dev);

		VkPhysicalDeviceProperties2  props    = { };
		VkPhysicalDeviceIDProperties id_props = { };
		VkPhysicalDeviceProperties2  best_props    = { };
		VkPhysicalDeviceIDProperties best_id_props = { };
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

		const auto set_best = [&](
				VkPhysicalDevice physDev,
				unsigned index,
				const VkPhysicalDeviceProperties2& props,
				const VkPhysicalDeviceIDProperties& id_props
		) {
			std::get<0>(dst) = physDev;
			std::get<1>(dst) = index;
			best_props    = props;
			best_id_props = id_props;
		};

		unsigned index      = 0;
		for(VkPhysicalDevice physDev : devices) {
			float     rank = rank_phys_device(index, props.properties);
			props.pNext    = &id_props;
			id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
			vkGetPhysicalDeviceProperties2(physDev, &props);
			spdlog::info("Device [{}] {:x}:{:x} \"{}\" has rating {:.6g}",
				index,
				props.properties.vendorID, props.properties.deviceID,
				props.properties.deviceName, rank );
			if(prefer_uuid && 0 == memcmp(uuid_cmp, id_props.deviceUUID, 16)) {
				set_best(physDev, index, props, id_props);
				break;
			} else {
				if(rank > bestRank) {
					set_best(physDev, index, props, id_props);
					bestRank = rank;
				}
				++ index;
			}
		}

		std::get<2>(dst) = best_props.properties;

		{
			std::string uuid;
			uuid_from_bytes(uuid, best_id_props.deviceUUID);
			if(uuid == preferred_dev_dst) {
				spdlog::info("Found preferred device [{}] {:04x}:{:04x} \"{}\"",
					std::get<1>(dst),
					props.properties.vendorID, props.properties.deviceID, props.properties.deviceName );
				spdlog::info("                       {}", preferred_dev_dst);
			} else {
				spdlog::info("Selected device [{}] {:04x}:{:04x} \"{}\"",
					std::get<1>(dst),
					props.properties.vendorID, props.properties.deviceID, props.properties.deviceName );
				spdlog::info("                {}", preferred_dev_dst);
				preferred_dev_dst = std::move(uuid);
			}
		}
	}


	Engine::DeviceInitializer::DeviceInitializer(Engine& e, const DeviceInitInfo* dii, const EnginePreferences* ep):
			#define MIRROR_(MEMBER_) MEMBER_(e.MEMBER_)
			mSrcDeviceInitInfo (dii),
			mSrcPrefs          (ep),
			MIRROR_(mSdlWindow),
			MIRROR_(mVkInstance),
			MIRROR_(mPhysDevice),
			MIRROR_(mDevice),
			MIRROR_(mVma),
			MIRROR_(mQfams),
			MIRROR_(mGraphicsQueue),
			MIRROR_(mComputeQueue),
			MIRROR_(mTransferQueue),
			MIRROR_(mDevProps),
			MIRROR_(mDevFeatures),
			MIRROR_(mTransferCmdPool),
			MIRROR_(mRenderCmdPool),
			MIRROR_(mPrefs)
			#undef MIRROR_
	{ }


	void Engine::DeviceInitializer::init() {
		assert(mSrcDeviceInitInfo != nullptr);
		assert(mSrcPrefs          != nullptr);
		mPrefs = *mSrcPrefs;
		initSdl();
		initVkInst();
		initVkDev();
		initVma();
		initCmdPools();
	}


	void Engine::DeviceInitializer::destroy() {
		destroyCmdPools();
		destroyVma();
		destroyVkDev();
		destroyVkInst();
		destroySdl();
	}


	void Engine::DeviceInitializer::findQueueFamilies() {
		assert(mPhysDevice != nullptr);

		std::vector<VkQueueFamilyProperties> q_fam_props;
		{ // Query the queue family properties
			uint32_t prop_count;
			vkGetPhysicalDeviceQueueFamilyProperties(mPhysDevice, &prop_count, nullptr);
			q_fam_props.resize(prop_count);
			vkGetPhysicalDeviceQueueFamilyProperties(mPhysDevice, &prop_count, q_fam_props.data());
		}

		const auto find_idx = [&](
				const std::string& qtype,
				VkQueueFlags flag,
				unsigned offset
		) -> QfamIndex {
			constexpr const auto family_queue_str = [](VkQueueFlags f) { switch(f) {
				case VK_QUEUE_GRAPHICS_BIT: return "graphics";
				case VK_QUEUE_COMPUTE_BIT:  return "compute";
				case VK_QUEUE_TRANSFER_BIT: return "transfer";
				default: abort();
			} };

			for(uint32_t i=0; i < q_fam_props.size(); ++i) {
				uint32_t i_offset = (i+offset) % q_fam_props.size();
				assert(i_offset != uint32_t(QfamIndex::eInvalid));
				if(q_fam_props[i_offset].queueFlags & VkQueueFlagBits(flag)) {
					spdlog::info("Using queue family {} for {} queues", i_offset, family_queue_str(flag));
					return QfamIndex(i_offset);
				}
			}
			throw std::runtime_error(fmt::format(
				"No suitable queue for {} ops on device {:04x}:{:04x}",
				qtype,
				mDevProps.vendorID,
				mDevProps.deviceID ));
		};

		{ // Set the Engine members
			auto& dst = mQfams;
			dst.graphics_index = find_idx("graphics", VK_QUEUE_GRAPHICS_BIT, 0);
			dst.compute_index  = find_idx("compute",  VK_QUEUE_COMPUTE_BIT,  uint32_t(dst.graphics_index) + 1);
			dst.transfer_index = find_idx("transfer", VK_QUEUE_TRANSFER_BIT, uint32_t(dst.compute_index) + 1);
			dst.graphics_props = q_fam_props[std::size_t(dst.graphics_index)];
			dst.compute_props  = q_fam_props[std::size_t(dst.compute_index)];
			dst.transfer_props = q_fam_props[std::size_t(dst.transfer_index)];
		}
	}


	void Engine::DeviceInitializer::initSdl() {
		if(0 == mSrcPrefs->present_extent.width * mSrcPrefs->present_extent.height) {
			throw std::invalid_argument("Initial window area cannot be 0");
		}

		if(! sdl_init_counter) {
			SDL_Init(SDL_INIT_VIDEO);
			SDL_Vulkan_LoadLibrary(nullptr);
		}
		++ sdl_init_counter;

		uint32_t window_flags =
			SDL_WINDOW_SHOWN |
			SDL_WINDOW_VULKAN |
			SDL_WINDOW_RESIZABLE |
			(SDL_WINDOW_FULLSCREEN * mSrcPrefs->fullscreen);

		mSdlWindow = SDL_CreateWindow(
			mSrcDeviceInitInfo->window_title.c_str(),
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			mSrcPrefs->present_extent.width, mSrcPrefs->present_extent.height,
			window_flags );

		{ // Change the present extent, as the window decided it to be
			int w, h;
			auto& w0 = mSrcPrefs->present_extent.width;
			auto& h0 = mSrcPrefs->present_extent.height;
			SDL_Vulkan_GetDrawableSize(mSdlWindow, &w, &h);
			if(uint32_t(w) != w0 || uint32_t(h) != h0) {
				mPrefs.present_extent = { uint32_t(w), uint32_t(h) };
				spdlog::warn("Requested window size {}x{}, got {}x{}", w0, h0, w, h);
			}
		}

		if(mSdlWindow == nullptr) {
			using namespace std::string_literals;
			const char* err = SDL_GetError();
			throw std::runtime_error(("failed to create an SDL window ("s + err) + ")");
		}
	}


	void Engine::DeviceInitializer::initVkInst() {
		assert(mSdlWindow != nullptr);

		VkApplicationInfo a_info  = { };
		a_info.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		a_info.pEngineName        = SKENGINE_NAME_LC_CSTR;
		a_info.pApplicationName   = mSrcDeviceInitInfo->application_name.c_str();
		a_info.applicationVersion = mSrcDeviceInitInfo->app_version;
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
				spdlog::debug("SDL2 requires Vulkan extension \"{}\"", extensions[i]);
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
			unsigned best_dev_index;
			select_best_phys_device_t best_dev = {
				mPhysDevice, best_dev_index, mDevProps };
			select_best_phys_device(best_dev, devs, mPrefs.phys_device_uuid, mSrcPrefs->phys_device_uuid);

			std::vector<std::string> missing_props;
			{ // Check for missing required properties
				VkPhysicalDeviceFeatures avail_ftrs;
				mDevFeatures = { };
				vkGetPhysicalDeviceFeatures(mPhysDevice, &avail_ftrs);
				missing_props = check_dev_missing_features(avail_ftrs, mDevFeatures);
			}

			if(! missing_props.empty()) {
				for(const auto& prop : missing_props) {
					spdlog::error("Selected device [{}]={:x}:{:x} \"{}\" is missing property `{}`",
						best_dev_index,
						mDevProps.vendorID, mDevProps.deviceID,
						mDevProps.deviceName,
						prop );
				}
				throw std::runtime_error("The chosen device is missing "s +
					std::to_string(missing_props.size()) + " features"s);
			}
		}

		{ // Create logical device
			findQueueFamilies();

			#define MERGE_(SRC_, DST_) \
				assert(mQfams.DST_##_index != QfamIndex::eInvalid); \
				assert(mQfams.SRC_##_index != QfamIndex::eInvalid); \
				if(mQfams.DST_##_index == mQfams.SRC_##_index) { \
					DST_##_fam_q_count += SRC_##_fam_q_count; \
					SRC_##_q_index      = DST_##_q_index + 1; \
					SRC_##_fam_q_count  = 0; \
				}
			uint32_t graphics_fam_q_count = 1;
			uint32_t compute_fam_q_count  = 1;
			uint32_t transfer_fam_q_count = 1;
			uint32_t graphics_q_index = 0;
			uint32_t compute_q_index  = 0;
			uint32_t transfer_q_index = 0;
			MERGE_(transfer, compute)
			MERGE_(          compute, graphics)
			#undef MERGE_

			std::vector<VkDeviceQueueCreateInfo> dq_infos;
			dq_infos.reserve(3);
			const auto ins = [&](QfamIndex fam, uint32_t count) {
				constexpr float priorities[] = { .0f, .0f, .0f };
				assert(count <= std::size(priorities));
				if(count > 0) {
					VkDeviceQueueCreateInfo ins = { };
					ins.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					ins.queueFamilyIndex = uint32_t(fam);
					ins.queueCount       = count;
					ins.pQueuePriorities = priorities;
					dq_infos.push_back(ins);
					spdlog::info("Assigned {} queue{} to family {}", count, count == 1? "":"s", uint32_t(fam));
				}
			};
			ins(mQfams.graphics_index, graphics_fam_q_count);
			ins(mQfams.compute_index,  compute_fam_q_count);
			ins(mQfams.transfer_index, transfer_fam_q_count);

			VkDeviceCreateInfo d_info = { };
			const char* extensions[] = {
				VK_KHR_SWAPCHAIN_EXTENSION_NAME };
			d_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			d_info.pQueueCreateInfos       = dq_infos.data();
			d_info.queueCreateInfoCount    = dq_infos.size();
			d_info.pEnabledFeatures        = &mDevFeatures;
			d_info.enabledExtensionCount   = std::size(extensions);
			d_info.ppEnabledExtensionNames = extensions;
			VK_CHECK(vkCreateDevice, mPhysDevice, &d_info, nullptr, &mDevice);

			vkGetDeviceQueue(mDevice, uint32_t(mQfams.graphics_index), graphics_q_index, &mGraphicsQueue);
			vkGetDeviceQueue(mDevice, uint32_t(mQfams.compute_index),  compute_q_index,  &mComputeQueue);
			vkGetDeviceQueue(mDevice, uint32_t(mQfams.transfer_index), transfer_q_index, &mTransferQueue);
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


	void Engine::DeviceInitializer::initCmdPools () {
		mRenderCmdPool   = vkutil::CommandPool(mDevice, uint32_t(mQfams.graphics_index));
		mTransferCmdPool = vkutil::CommandPool(mDevice, uint32_t(mQfams.transfer_index));
	}


	void Engine::DeviceInitializer::destroyCmdPools () {
		assert(mDevice != nullptr);

		mTransferCmdPool = { };
		mRenderCmdPool   = { };
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
			SDL_Quit();
		}
	}

}
