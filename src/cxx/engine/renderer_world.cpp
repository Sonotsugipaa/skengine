#include "renderer.hpp"

#include "engine.hpp"
#include "enum_class_gen.inl.hpp"

#include <glm/ext/matrix_transform.hpp>



#ifdef NDEBUG
	#define assert_not_nullptr_(V_) (V_)
	#define assert_not_end_(M_, K_) (M_.find((K_)))
#else
	#define assert_not_nullptr_(V_) ([&]() { assert(V_ != nullptr); return V_; } ())
	#define assert_not_end_(M_, K_) ([&]() { auto& m__ = (M_); auto r__ = m__.find((K_)); assert(r__!= m__.end()); return r__; } ())
#endif



namespace SKENGINE_NAME_NS {

	namespace {

		constexpr auto light_storage_create_info(size_t light_count) {
			vkutil::BufferCreateInfo r = { };
			r.size  = light_count * sizeof(dev::Light);
			r.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			return r;
		};

		constexpr auto light_storage_allocate_info = []() {
			vkutil::AllocationCreateInfo r = { };
			r.requiredMemFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
			r.vmaFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			r.vmaUsage = vkutil::VmaAutoMemoryUsage::eAutoPreferHost;
			return r;
		} ();


		uint32_t set_light_buffer_capacity(VmaAllocator vma, WorldRenderer::LightStorage* dst, uint32_t desired) {
			static_assert(std::bit_ceil(0u) == 1u);
			desired = std::bit_ceil(desired);
			if(desired != dst->bufferCapacity) {
				if(dst->bufferCapacity > 0) {
					dst->buffer.unmap(vma);
					vkutil::ManagedBuffer::destroy(vma, dst->buffer);
					dst->bufferCapacity = 0;
				}

				auto  bc_info = light_storage_create_info(desired);
				auto& ac_info = light_storage_allocate_info;
				dst->buffer    = vkutil::ManagedBuffer::create(vma, bc_info, ac_info);
				dst->mappedPtr = dst->buffer.map<dev::Light>(vma);

				dst->bufferCapacity = desired;
			}
			return desired;
		}

	}



	WorldRenderer WorldRenderer::create(
			std::shared_ptr<spdlog::logger> logger,
			VmaAllocator vma,
			DsetLayout dset_layout,
			ModelSupplierInterface&    model_supplier,
			MaterialSupplierInterface& mat_supplier
	) {
		WorldRenderer r = Renderer::create(std::move(logger), vma, dset_layout, model_supplier, mat_supplier);
		r.mViewPosXyz = { };
		r.mViewDirYpr = { };
		r.mLightStorage = { };
		r.mViewTransfCacheOod = true;
		set_light_buffer_capacity(r.mVma, &r.mLightStorage, 0);
		return r;
	}


	void WorldRenderer::destroy(WorldRenderer& r) {
		if(r.mLightStorage.bufferCapacity > 0) {
			r.mLightStorage.bufferCapacity = 0;
			r.mLightStorage.buffer.unmap(r.mVma);
			vkutil::ManagedBuffer::destroy(r.mVma, r.mLightStorage.buffer);
		}
		Renderer::destroy(r);
	}


	const glm::mat4& WorldRenderer::getViewTransf() noexcept {
		if(mViewTransfCacheOod) [[unlikely]] {
			constexpr glm::vec3 x = { 1.0f, 0.0f, 0.0f };
			constexpr glm::vec3 y = { 0.0f, 1.0f, 0.0f };
			constexpr glm::vec3 z = { 0.0f, 0.0f, 1.0f };
			constexpr glm::mat4 identity = glm::mat4(1.0f);

			glm::mat4 translate = glm::translate(identity, -mViewPosXyz);
			glm::mat4 rot0      = glm::rotate(identity, +mViewDirYpr.z, z);
			glm::mat4 rot1      = glm::rotate(identity, -mViewDirYpr.x, y); // I have no idea why this has to be negated for the right-hand rule to apply
			glm::mat4 rot2      = glm::rotate(identity, +mViewDirYpr.y, x);
			mViewTransfCache = rot2 * rot1 * rot0 * translate;
			mViewTransfCacheOod = false;
		}

		return mViewTransfCache;
	}


	void WorldRenderer::setViewPosition(const glm::vec3& xyz) noexcept {
		mViewPosXyz = xyz;
		mViewTransfCacheOod = true;
	}


	void WorldRenderer::setViewRotation(const glm::vec3& ypr) noexcept {
		{ // Normalize the direction values
			constexpr auto pi2 = 2.0f * std::numbers::pi_v<float>;
			#define NORMALIZE_(I_) { if(mViewDirYpr[I_] >= pi2) [[unlikely]] { \
				mViewDirYpr[I_] = std::floor(mViewDirYpr[I_] / pi2) * pi2; }}
			NORMALIZE_(0)
			NORMALIZE_(1)
			NORMALIZE_(2)
			#undef NORMALIZE_
		}

		mViewDirYpr = ypr;
		mViewTransfCacheOod = true;
		mLightStorageOod    = true;
	}


	void WorldRenderer::setViewDirection(const glm::vec3& xyz) noexcept {
		/*       -x         '  '        +y         '
		'         |         '  '         |         '
		'         |         '  '         |         '
		'  +z ----O---- -z  '  '  +z ----O---- -z  '
		'         |         '  '         |         '
		'         |         '  '         |         '
		'        +x         '  '        -y        */
		glm::vec3 ypr;
		ypr[0] = std::atan2(-xyz.x, -xyz.z);
		ypr[1] = std::atan2(+xyz.y, -xyz.z);
		ypr[2] = 0;
		mViewDirYpr = ypr;
		mViewTransfCacheOod = true;
	}


	[[nodiscard]]
	ObjectId WorldRenderer::createRayLight(const NewRayLight& nrl) {
		auto r = generate_id<ObjectId>();
		dev::RayLight rl = { };
		rl.direction = glm::vec4(glm::normalize(nrl.direction), 1.0f);
		rl.intensity = std::max(nrl.intensity, 0.0f);
		mRayLights.insert(RayLights::value_type { r, std::move(rl) });
		mLightStorageOod = true;
		return r;
	}


	[[nodiscard]]
	ObjectId WorldRenderer::createPointLight(const NewPointLight& npl) {
		auto r = generate_id<ObjectId>();
		dev::PointLight pl = { };
		pl.position    = glm::vec4(npl.position, 1.0f);
		pl.intensity   = std::max(npl.intensity, 0.0f);
		pl.falloff_exp = std::max(npl.falloffExponent, 0.0f);
		mPointLights.insert(PointLights::value_type { r, std::move(pl) });
		mLightStorageOod = true;
		return r;
	}


	void WorldRenderer::removeLight(ObjectId id) {
		assert(mPointLights.contains(id) || mRayLights.contains(id));
		mLightStorageOod = true;
		if(0 == mRayLights.erase(id)) mPointLights.erase(id);
	}


	const dev::RayLight& WorldRenderer::getRayLight(ObjectId id) const {
		return assert_not_end_(mRayLights, id)->second;
	}


	const dev::PointLight& WorldRenderer::getPointLight(ObjectId id) const {
		return assert_not_end_(mPointLights, id)->second;
	}


	dev::RayLight& WorldRenderer::modifyRayLight(ObjectId id) {
		mLightStorageOod = true;
		return assert_not_end_(mRayLights, id)->second;
	}


	dev::PointLight& WorldRenderer::modifyPointLight(ObjectId id) {
		mLightStorageOod = true;
		return assert_not_end_(mPointLights, id)->second;
	}


	bool WorldRenderer::commitObjects(VkCommandBuffer cmd) {
		auto r = Renderer::commitObjects(cmd);

		if(! mLightStorageOod) return r;

		uint32_t new_ls_size = mRayLights.size() + mPointLights.size();
		set_light_buffer_capacity(mVma, &mLightStorage, new_ls_size);

		mLightStorage.rayCount   = mRayLights.size();
		mLightStorage.pointCount = mPointLights.size();
		auto& ray_count = mLightStorage.rayCount;
		for(uint32_t i = 0; auto& rl : mRayLights) {
			auto& dst = *reinterpret_cast<dev::RayLight*>(mLightStorage.mappedPtr + i);
			dst = rl.second;
			dst.direction = - dst.direction;
			++ i;
		}
		for(uint32_t i = ray_count; auto& pl : mPointLights) {
			auto& dst = *reinterpret_cast<dev::PointLight*>(mLightStorage.mappedPtr + i);
			dst = pl.second;
			++ i;
		}

		++ mLightStorage.updateCounter;
		mLightStorage.buffer.flush(mVma);

		mLightStorageOod = false;
		return r;
	}


	WorldRenderer::WorldRenderer(Renderer&& mv):
		Renderer::Renderer(std::move(mv))
	{ }

}



#undef assert_not_end_
#undef assert_not_nullptr_

