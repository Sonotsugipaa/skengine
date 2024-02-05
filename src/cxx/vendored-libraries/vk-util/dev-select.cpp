#include "init.hpp"

#include <spdlog/logger.h>

#include <cstdint>
#include <cassert>
#include <cstring>
#include <span>
#include <string_view>
#include <charconv>



namespace vkutil {

	bool uuidToBytes(uint8_t dst[16], std::string_view sv) {
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


	void uuidFromBytes(std::string& dst, uint8_t src[16]) {
		dst = fmt::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
			src[0],  src[1],  src[2],  src[3],
			src[4],  src[5],  src[6],  src[7],
			src[8],  src[9],  src[10], src[11],
			src[12], src[13], src[14], src[15] );
	}


	float rankPhysDevice(float orderBias, const VkPhysicalDeviceProperties& props) {
		float r = 1.0f;

		{ // Rank version (0 < x <= [reasonable major version number])
			r +=
				VK_API_VERSION_MAJOR(props.driverVersion) +
				(VK_API_VERSION_MINOR(props.driverVersion) / 10.0f) +
				(VK_API_VERSION_PATCH(props.driverVersion) / 1000.0f) +
				(VK_API_VERSION_VARIANT(props.driverVersion) / 1000000.0f);
		} { // Rank device index
			/* Rationale: even if the driver's version is two minor numbers
			/* behind the lead, the user may prefer to use the first occurring device. */
			r -= orderBias / 5.0f;
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


	#define CKFTRS_ \
		CKFTR_(robustBufferAccess) \
		CKFTR_(fullDrawIndexUint32) \
		CKFTR_(imageCubeArray) \
		CKFTR_(independentBlend) \
		CKFTR_(geometryShader) \
		CKFTR_(tessellationShader) \
		CKFTR_(sampleRateShading) \
		CKFTR_(dualSrcBlend) \
		CKFTR_(logicOp) \
		CKFTR_(multiDrawIndirect) \
		CKFTR_(drawIndirectFirstInstance) \
		CKFTR_(depthClamp) \
		CKFTR_(depthBiasClamp) \
		CKFTR_(fillModeNonSolid) \
		CKFTR_(depthBounds) \
		CKFTR_(wideLines) \
		CKFTR_(largePoints) \
		CKFTR_(alphaToOne) \
		CKFTR_(multiViewport) \
		CKFTR_(samplerAnisotropy) \
		CKFTR_(textureCompressionETC2) \
		CKFTR_(textureCompressionASTC_LDR) \
		CKFTR_(textureCompressionBC) \
		CKFTR_(occlusionQueryPrecise) \
		CKFTR_(pipelineStatisticsQuery) \
		CKFTR_(vertexPipelineStoresAndAtomics) \
		CKFTR_(fragmentStoresAndAtomics) \
		CKFTR_(shaderTessellationAndGeometryPointSize) \
		CKFTR_(shaderImageGatherExtended) \
		CKFTR_(shaderStorageImageExtendedFormats) \
		CKFTR_(shaderStorageImageMultisample) \
		CKFTR_(shaderStorageImageReadWithoutFormat) \
		CKFTR_(shaderStorageImageWriteWithoutFormat) \
		CKFTR_(shaderUniformBufferArrayDynamicIndexing) \
		CKFTR_(shaderSampledImageArrayDynamicIndexing) \
		CKFTR_(shaderStorageBufferArrayDynamicIndexing) \
		CKFTR_(shaderStorageImageArrayDynamicIndexing) \
		CKFTR_(shaderClipDistance) \
		CKFTR_(shaderCullDistance) \
		CKFTR_(shaderFloat64) \
		CKFTR_(shaderInt64) \
		CKFTR_(shaderInt16) \
		CKFTR_(shaderResourceResidency) \
		CKFTR_(shaderResourceMinLod) \
		CKFTR_(sparseBinding) \
		CKFTR_(sparseResidencyBuffer) \
		CKFTR_(sparseResidencyImage2D) \
		CKFTR_(sparseResidencyImage3D) \
		CKFTR_(sparseResidency2Samples) \
		CKFTR_(sparseResidency4Samples) \
		CKFTR_(sparseResidency8Samples) \
		CKFTR_(sparseResidency16Samples) \
		CKFTR_(sparseResidencyAliased) \
		CKFTR_(variableMultisampleRate) \
		CKFTR_(inheritedQueries)
	// #define CKFTRS_


	bool checkDevMissingFeatures(
		const VkPhysicalDeviceFeatures& availableFeatures,
		const VkPhysicalDeviceFeatures& requiredFeatures
	) {
		#define CKFTR_(FTR_) { if(requiredFeatures.FTR_ && ! availableFeatures.FTR_) { return false; } }
		CKFTRS_
		#undef CKFTR_
		return true;
	}


	std::vector<std::string_view> listDevMissingFeatures(
		const VkPhysicalDeviceFeatures& availableFeatures,
		const VkPhysicalDeviceFeatures& requiredFeatures
	) {
		std::vector<std::string_view> r;
		#define CKFTR_(FTR_) { if(requiredFeatures.FTR_ && ! availableFeatures.FTR_) { r.push_back(#FTR_); } }
		CKFTRS_
		#undef CKFTR_
		return r;
	}


	#undef CKFTRS_


	void selectBestPhysDevice(
		spdlog::logger* logger,
		const SelectBestPhysDeviceDst&     dst,
		const std::span<VkPhysicalDevice>& devices,
		const VkPhysicalDeviceFeatures&    requiredFeatures,
		std::string* preferredDevUuidOpt
	) {
		dst.selectedDevice      = nullptr;
		dst.selectedDeviceProps = { };
		dst.index               = ~ unsigned(0);
		float   bestRank = rankPhysDevice(0.0f, dst.selectedDeviceProps);
		uint8_t uuidCmp[16];
		bool    preferUuid = uuidToBytes(uuidCmp, preferredDevUuidOpt == nullptr? "" : *preferredDevUuidOpt);

		VkPhysicalDeviceProperties2  props       = { };
		VkPhysicalDeviceIDProperties idProps     = { };
		VkPhysicalDeviceProperties2  bestProps   = { };
		VkPhysicalDeviceIDProperties bestIdProps = { };
		props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;

		const auto setBest = [&](
				VkPhysicalDevice physDev,
				unsigned         index,
				const VkPhysicalDeviceProperties2&  props,
				const VkPhysicalDeviceIDProperties& idProps
		) {
			dst.selectedDevice = physDev;
			dst.index          = index;
			bestProps          = props;
			bestIdProps        = idProps;
		};

		unsigned index = 0;
		for(VkPhysicalDevice physDev : devices) {
			VkPhysicalDeviceFeatures availableFeatures;
			props.pNext   = &idProps;
			idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
			vkGetPhysicalDeviceProperties2(physDev, &props);
			float rank = rankPhysDevice(index, props.properties);
			vkGetPhysicalDeviceProperties2(physDev, &props);
			vkGetPhysicalDeviceFeatures(physDev, &availableFeatures);
			if(! checkDevMissingFeatures(availableFeatures, requiredFeatures)) {
				if(logger) logger->info("Device [{}] {:x}:{:x} \"{}\" has missing required features",
					index,
					props.properties.vendorID, props.properties.deviceID,
					props.properties.deviceName );
				auto missing = listDevMissingFeatures(availableFeatures, requiredFeatures);
				for(std::string_view feat : missing) {
					if(logger) logger->info(" > \"{}\"", feat);
				}
			} else {
				if(logger) logger->info("Device [{}] {:x}:{:x} \"{}\" has rating {:.6g}",
					index,
					props.properties.vendorID, props.properties.deviceID,
					props.properties.deviceName, rank );
				if(preferUuid && 0 == memcmp(uuidCmp, idProps.deviceUUID, 16)) {
					setBest(physDev, index, props, idProps);
					break;
				} else {
					if(rank > bestRank) {
						setBest(physDev, index, props, idProps);
						bestRank = rank;
					}
					++ index;
				}
			}
		}

		if(dst.selectedDevice == nullptr) {
			throw std::runtime_error("No Vulkan device has all the required features");
		}

		dst.selectedDeviceProps = bestProps.properties;

		{
			std::string uuid;
			uuidFromBytes(uuid, bestIdProps.deviceUUID);
			if((preferredDevUuidOpt != nullptr)) {
				if(logger != nullptr && uuid == *preferredDevUuidOpt) {
					logger->info("Found preferred device [{}] {:04x}:{:04x} \"{}\"",
						dst.index,
						props.properties.vendorID, props.properties.deviceID, props.properties.deviceName );
					logger->info("                       {}", uuid);
				} else {
					*preferredDevUuidOpt = std::move(uuid);
				}
			}
			if(logger != nullptr) {
				logger->info("Selected device [{}] {:04x}:{:04x} \"{}\"",
					dst.index,
					props.properties.vendorID, props.properties.deviceID, props.properties.deviceName );
				logger->info("                {}", *preferredDevUuidOpt);
			}
		}
	}

}
