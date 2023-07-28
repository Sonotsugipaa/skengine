#include "renderer.hpp"

#include "engine.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <type_traits>

#include <spdlog/spdlog.h>


#ifdef NDEBUG
	#define ASSERT_IS_INITIALIZED_ (void) 0;
#else
	#define ASSERT_IS_INITIALIZED_(PTR_) assert((PTR_)->mIsInitialized);
#endif

namespace {

	constexpr size_t OBJECT_MAP_INITIAL_CAPACITY = 16;
	constexpr float  OBJECT_MAP_MAX_LOAD_FACTOR  = 4.0;
	constexpr size_t OBJECT_MAP_GROW_FACTOR      = 4; // Should be a power of 2
	constexpr size_t OBJECT_MAP_SHRINK_FACTOR    = 16; // Should be a power of 2


	template <VkBufferUsageFlags usage, vkutil::HostAccess host_access>
	vkutil::BufferDuplex createBuffer(
			SKENGINE_NAME_NS_SHORT::Engine& engine,
			size_t size
	) {
		using namespace SKENGINE_NAME_NS_SHORT;
		VmaAllocator vma    = engine.getVmaAllocator();
		auto&        queues = engine.getQueueInfo();
		uint32_t qfam_sharing[] = {
			queues.families.transferIndex,
			queues.families.graphicsIndex };

		vkutil::BufferCreateInfo bc_info = {
			.size  = size,
			.usage = usage,
			.qfam_sharing = std::span<uint32_t>(qfam_sharing, std::size(qfam_sharing)) };
		return vkutil::BufferDuplex::createStorageBuffer(vma, bc_info, host_access);
	}


	vkutil::BufferDuplex createObjectBuffer(
			SKENGINE_NAME_NS_SHORT::Engine& engine,
			size_t count
	) {
		using namespace SKENGINE_NAME_NS_SHORT;
		return
			createBuffer <
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				vkutil::HostAccess::eWr
			> (engine, count * sizeof(dev::RenderObject));
	}


	vkutil::BufferDuplex createDrawCommandBuffer(
			SKENGINE_NAME_NS_SHORT::Engine& engine,
			size_t count
	) {
		using namespace SKENGINE_NAME_NS_SHORT;
		return
			createBuffer <
				VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
				vkutil::HostAccess::eWr
			> (engine, count * sizeof(VkDrawIndexedIndirectCommand));
	}


	void resizeBuffers(
			SKENGINE_NAME_NS_SHORT::Engine& engine,
			vkutil::BufferDuplex& objBuffer,
			vkutil::BufferDuplex& drawCmdBuffer,
			size_t                desired_obj_count
	) {
		using namespace SKENGINE_NAME_NS_SHORT;

		desired_obj_count = std::bit_ceil(desired_obj_count);

		VmaAllocator vma = engine.getVmaAllocator();
		vkutil::BufferDuplex::destroy(vma, objBuffer);
		vkutil::BufferDuplex::destroy(vma, drawCmdBuffer);
		objBuffer     = createObjectBuffer(engine, desired_obj_count);
		drawCmdBuffer = createDrawCommandBuffer(engine, desired_obj_count);
	}


	// Returns whether the buffers have been resized.
	bool guaranteeBufferSizes(
			SKENGINE_NAME_NS_SHORT::Engine& engine,
			vkutil::BufferDuplex& objBuffer,
			vkutil::BufferDuplex& drawCmdBuffer,
			size_t                desired_obj_count
	) {
		using namespace SKENGINE_NAME_NS_SHORT;

		size_t obj_count = objBuffer.size() / sizeof(dev::RenderObject);
		assert(objBuffer.size() % sizeof(dev::RenderObject) == 0);

		auto too_big_threshold = desired_obj_count / OBJECT_MAP_SHRINK_FACTOR;
		bool too_small = (obj_count < desired_obj_count);
		bool too_big   = (obj_count > desired_obj_count) && (obj_count >= too_big_threshold);
		spdlog::trace("Renderer buffer resize: {} < {} <= {}", too_big_threshold, obj_count, desired_obj_count);

		if(too_small) [[unlikely]] {
			// The desired count may be larger than what the growth factor guesses
			auto obj_count_grown = (obj_count == 0)? 1 : obj_count * OBJECT_MAP_GROW_FACTOR;
			desired_obj_count = std::max(obj_count_grown, desired_obj_count);
			resizeBuffers(engine, objBuffer, drawCmdBuffer, desired_obj_count);
			return true;
		} else
		if(too_big) [[unlikely]] {
			assert(too_big);
			resizeBuffers(engine, objBuffer, drawCmdBuffer, desired_obj_count);
			return true;
		}

		return false;
	}


	template <typename T>
	concept ScopedEnum = requires(T t) {
		requires std::integral<std::underlying_type_t<T>>;
	};


	template <typename T>
	T generateId() {
		using int_t = std::underlying_type_t<T>;
		static std::atomic<int_t> last = 0;
		int_t r = last.fetch_add(1, std::memory_order_relaxed);
		return T(r);
	}

}



namespace SKENGINE_NAME_NS {

	Renderer Renderer::create(Engine& engine) {
		Renderer r;
		r.mEngine = &engine;
		r.mDevObjectDirtyBitset = decltype(mDevObjectDirtyBitset)(OBJECT_MAP_INITIAL_CAPACITY, false);
		r.mDevObjectBuffer = createObjectBuffer(engine, OBJECT_MAP_INITIAL_CAPACITY);
		r.mDrawCmdBuffer   = createDrawCommandBuffer(engine, OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects     = decltype(mObjects)     (OBJECT_MAP_INITIAL_CAPACITY);
		r.mDrawBatches = decltype(mDrawBatches) (OBJECT_MAP_INITIAL_CAPACITY);
		r.mObjects.max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mDrawBatches.max_load_factor(OBJECT_MAP_MAX_LOAD_FACTOR);
		r.mState = State::eClean;

		#ifndef NDEBUG
			r.mIsInitialized = true;
		#endif

		return r;
	}


	void Renderer::destroy(Renderer& r) {
		#ifndef NDEBUG
			ASSERT_IS_INITIALIZED_(&r)
			r.mIsInitialized = false;
		#endif

		auto vma = r.mEngine->getVmaAllocator();
		vkutil::BufferDuplex::destroy(vma, r.mDevObjectBuffer);
		vkutil::BufferDuplex::destroy(vma, r.mDevObjectBuffer);
		r.mDevObjectDirtyBitset = { };
	}


	RenderObjectId Renderer::createObject(RenderObject o) {
		ASSERT_IS_INITIALIZED_(this)

		auto obj_count = mObjects.size();

		// Grow the buffers, if necessary
		guaranteeBufferSizes(*mEngine, mDevObjectBuffer, mDrawCmdBuffer, obj_count + 1);

		auto gen_id = generateId<RenderObjectId>();
		mObjects.emplace(gen_id, o);
		abort(); // TBW
	}


	void Renderer::removeObject(RenderObjectId id) {
		if(0 == mObjects.erase(id)) return;

		mState = State::eReconstructionNeeded;
		mDrawBatches.clear();
		mDevObjectDirtyBitset.clear();
		spdlog::trace("Render object dirty bitset capacity after clear: {}", mDevObjectDirtyBitset.capacity());

	}

}

#define ASSERT_IS_INITIALIZED_
