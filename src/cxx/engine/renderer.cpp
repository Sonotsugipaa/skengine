#include "renderer.hpp"

#include "engine.hpp"

#include <bit>
#include <cassert>
#include <atomic>
#include <utility>
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
		VmaAllocator vma = engine.getVmaAllocator();

		vkutil::BufferCreateInfo bc_info = {
			.size  = size,
			.usage = usage,
			.qfamSharing = { } };
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


	void resizeBuffer(
			SKENGINE_NAME_NS_SHORT::Engine& engine,
			decltype(createObjectBuffer) createFn,
			vkutil::BufferDuplex& buffer,
			size_t                desired_obj_count
	) {
		using namespace SKENGINE_NAME_NS_SHORT;

		desired_obj_count = std::bit_ceil(desired_obj_count);

		VmaAllocator vma = engine.getVmaAllocator();
		vkutil::BufferDuplex::destroy(vma, buffer);
		buffer = createFn(engine, desired_obj_count);
	}


	// Returns whether the buffers have been resized.
	bool guaranteeBufferSize(
			SKENGINE_NAME_NS_SHORT::Engine& engine,
			decltype(createObjectBuffer) createFn,
			vkutil::BufferDuplex& buffer,
			size_t                desired_size_bytes
	) {
		using namespace SKENGINE_NAME_NS_SHORT;

		size_t size_bytes = buffer.size();

		auto too_big_threshold = desired_size_bytes / OBJECT_MAP_SHRINK_FACTOR;
		bool too_small = (size_bytes < desired_size_bytes);
		bool too_big   = (size_bytes > desired_size_bytes) && (size_bytes >= too_big_threshold);
		spdlog::trace("Renderer buffer resize: {} < {} <= {}", too_big_threshold, size_bytes, desired_size_bytes);

		if(too_small) [[unlikely]] {
			// The required size may be larger than what the growth factor guesses
			auto size_bytes_grown = (size_bytes == 0)? 1 : size_bytes * OBJECT_MAP_GROW_FACTOR;
			desired_size_bytes = std::max(size_bytes_grown, desired_size_bytes);
			resizeBuffer(engine, createFn, buffer, desired_size_bytes);
			return true;
		} else
		if(too_big) [[unlikely]] {
			assert(too_big);
			resizeBuffer(engine, createFn, buffer, desired_size_bytes);
			return true;
		}

		return false;
	}


	using BatchMap = std::unordered_map<SKENGINE_NAME_NS_SHORT::MeshId, std::unordered_map<SKENGINE_NAME_NS_SHORT::MaterialId, SKENGINE_NAME_NS_SHORT::DrawBatch>>;
	using Mpi      = SKENGINE_NAME_NS_SHORT::Renderer::MeshProviderInterface;

	void sortDrawCmds(
			vkutil::BufferDuplex& dst,
			const BatchMap&       batches,
			Mpi&                  mpi
	) {
		constexpr auto size_heuristic = [](const BatchMap& batches) -> size_t {
			// Take the first (log2(n)) meshes, take the average material count, double it,
			// return that times the number of meshes.
			size_t max_samples = std::max<size_t>(4, std::log2f(batches.size()));
			auto beg = batches.begin();
			auto end = batches.end();
			if(beg == end) return 0;

			auto   iter    = beg;
			size_t sum     = iter->second.size();
			size_t samples = 1;
			for(size_t i = 1; i < max_samples; ++i) {
				++ iter;
				++ samples;
				if(iter == end) break;
				sum += iter->second.size();
			}

			return batches.size() * 2 * size_t(float(sum) / float(samples));
		};

		std::vector<VkDrawIndexedIndirectCommand> cmds;
		cmds.reserve(size_heuristic(batches));

		for(auto& mesh_batches : batches) {
			auto mesh_info = mpi.getMeshInfo(mesh_batches.first);
			for(auto& batch : mesh_batches.second) {
				(void) dst; (void) batch; (void) mesh_info; abort(); // I'm tired, boss
			}
		}
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

		auto PLACEHOLDER = []() -> uint32_t { /* Create an instance and put it into a yet-to-exist instance buffer */ abort(); return 0; };

		auto gen_id = generateId<RenderObjectId>();
		mObjects.emplace(gen_id, o);
		mDrawBatches[o.mesh_id][o.material_id] = {
			.mesh_id       = o.mesh_id,
			.material_id   = o.material_id,
			.instanceCount = PLACEHOLDER(),
			.firstInstance = PLACEHOLDER() };

		// Grow the buffer, if necessary
		if(mState != State::eReconstructionNeeded) {
			bool resized = guaranteeBufferSize(*mEngine, createObjectBuffer, mDevObjectBuffer, obj_count + 1);
			if(resized) mState = State::eReconstructionNeeded;

			auto& obj_slot = mDevObjectBuffer.mappedPtr<RenderObject>()[obj_count /* The old size, which is the new last index */];
			obj_slot = o;

			mDevObjectDirtyBitset.push_back(true);

			switch(mState) {
				case State::eClean:
				case State::eObjectBufferDirty:
					mState = State::eDrawCmdBufferDirty;
					break;
				case State::eDrawCmdBufferDirty:
				case State::eReconstructionNeeded:
					break;
			}
		}

		abort(); // TBW
	}


	void Renderer::removeObject(RenderObjectId id) noexcept {
		ASSERT_IS_INITIALIZED_(this)

		if(0 == mObjects.erase(id)) return;

		mState = State::eReconstructionNeeded;
		mDrawBatches.clear();
		mDevObjectDirtyBitset.clear();
		spdlog::trace("Render object dirty bitset capacity after clear: {}", mDevObjectDirtyBitset.capacity());
		(void) sortDrawCmds; abort(); // TBW
	}

}

#undef ASSERT_IS_INITIALIZED_
