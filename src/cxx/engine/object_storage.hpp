#pragma once

#include "types.hpp"

#include <vk-util/memory.hpp>

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include <unordered_set>
#include <memory>
#include <span>
#include <thread>
#include <condition_variable>

#include <vma/vk_mem_alloc.h>



namespace SKENGINE_NAME_NS {

	class Engine;
	struct WorldRendererSharedState;


	struct DevModel {
		vkutil::BufferDuplex indices;
		vkutil::BufferDuplex vertices;
		std::vector<Bone>    bones;
		uint32_t index_count;
		uint32_t vertex_count;
	};


	// A draw batch, without object-specific data in favor of lists of references to them.
	struct UnboundDrawBatch {
		std::vector<ObjectId> object_refs;
		MaterialId material_id;
		bone_id_e  model_bone_index;
	};


	struct Material {
		struct Texture {
			vkutil::ManagedImage image;
			VkImageView          image_view;
			VkSampler            sampler;
			bool                 is_copy;
		};

		Texture texture_diffuse;
		Texture texture_normal;
		Texture texture_specular;
		Texture texture_emissive;
		vkutil::BufferDuplex mat_uniform;
	};


	class AssetSourceInterface {
	public:
		struct ModelSource {
			fmamdl::HeaderView fmaHeader;
		};

		struct MaterialSource {
			fmamdl::MaterialView fmaHeader;
			std::string texturePathPrefix;
		};

		virtual ModelSource asi_requestModelData(std::string_view locator) = 0;
		virtual MaterialSource asi_requestMaterialData(std::string_view locator) = 0;
		virtual void asi_releaseModelData(std::string_view locator) = 0;
		virtual void asi_releaseMaterialData(std::string_view locator) = 0;
	};


	/// \internal
	///
	class AssetSupplier {
	public:
		using Models           = std::unordered_map<std::string, DevModel>;
		using Materials        = std::unordered_map<std::string, Material>;
		using MissingMaterials = std::unordered_set<std::string>;

		AssetSupplier(): as_initialized(false) { }
		AssetSupplier(Engine&, Logger, std::shared_ptr<AssetSourceInterface> asi, float max_inactive_ratio, float max_sampler_anisotropy);
		AssetSupplier(AssetSupplier&&);
		AssetSupplier& operator=(AssetSupplier&& mv) { this->~AssetSupplier(); return * new (this) AssetSupplier(std::move(mv)); }
		void destroy();
		~AssetSupplier() { if(as_initialized) destroy(); }

		DevModel requestModel(std::string_view locator);
		void     releaseModel(std::string_view locator) noexcept;
		void releaseAllModels() noexcept;

		Material requestMaterial(std::string_view locator);
		void     releaseMaterial(std::string_view locator) noexcept;
		void releaseAllMaterials() noexcept;

		VmaAllocator vma() const noexcept { return as_transferContext.vma; }
		VkDevice     vkDevice() const noexcept { VmaAllocatorInfo i; vmaGetAllocatorInfo(vma(), &i); return i.device; }

	private:
		TransferContext as_transferContext;
		Logger as_logger;
		std::shared_ptr<AssetSourceInterface> as_srcInterface;
		Models    as_activeModels;
		Models    as_inactiveModels;
		Materials as_activeMaterials;
		Materials as_inactiveMaterials;
		Material  as_fallbackMaterial;
		MissingMaterials as_missingMaterials;
		float as_maxInactiveRatio;
		float as_maxSamplerAnisotropy;
		bool as_initialized;
	};


	/// \brief A collection of objects to be drawn, which may or
	///        may not be frequently modified.
	///
	/// The ObjectStorage abstracts the process of sorting objects by
	/// meshes and materials, and creating (indirect) draw commands.
	///
	/// It does own buffers for draw commands and object-specific data;
	/// it does NOT own mesh-specific or material-specific data,
	/// like vertices or textures.
	///
	class ObjectStorage {
	public:
		// This type should only be used for function parameters
		struct NewObject {
			std::string_view model_locator;
			glm::vec3 position_xyz;
			glm::vec3 direction_ypr;
			glm::vec3 scale_xyz;
			bool      hidden;
		};

		struct ModelData : DevModel {
			std::string locator;
		};

		struct MaterialData : Material {
			VkDescriptorSet dset;
			std::string locator;
		};

		struct ModifiableObject {
			std::span<BoneInstance> bones;
			glm::vec3& position_xyz;
			glm::vec3& direction_ypr;
			glm::vec3& scale_xyz;
			bool&      hidden;
		};

		struct MatrixAssembler {
			struct Job {
				struct {
					glm::vec3 object, bone, bone_instance;
				} position;
				struct {
					glm::vec3 object, bone, bone_instance;
				} direction;
				struct {
					glm::vec3 object, bone, bone_instance;
				} scale;
				glm::mat4* dst;
			};
			using JobQueue = std::deque<Job>;
			struct Worker {
				struct LockSet {
					std::mutex mutex;
					std::condition_variable produce_cond;
					std::condition_variable consume_cond;
				};
				std::unique_ptr<LockSet> cond;
				std::thread thread;
				JobQueue    queue;
				Worker(): cond(std::make_unique<decltype(cond)::element_type>()) { }
			};
			std::vector<Worker> workers;
		};

		template <typename K, typename V> using Umap = std::unordered_map<K, V>;
		template <typename T>             using Uset = std::unordered_set<T>;
		using DsetLayout = VkDescriptorSetLayout;
		using ModelLookup       = Umap<std::string_view, ModelId>;
		using MaterialLookup    = Umap<std::string_view, MaterialId>;
		using ModelMap          = Umap<ModelId,          ModelData>;
		using MaterialMap       = Umap<MaterialId,       MaterialData>;
		using Objects           = Umap<ObjectId,         std::pair<Object, std::vector<BoneInstance>>>;
		using ObjectUpdates     = Uset<ObjectId>;
		using UnboundBatchMap   = Umap<ModelId,          Umap<bone_id_e, Umap<MaterialId, UnboundDrawBatch>>>;
		using ModelDepCounters  = Umap<ModelId,          object_id_e>;
		using BatchList         = std::vector<DrawBatch>;

		ObjectStorage() = default;
		ObjectStorage(const ObjectStorage&) = delete;
		ObjectStorage(ObjectStorage&&) = default;

		static ObjectStorage create(
			Logger,
			std::shared_ptr<WorldRendererSharedState>,
			VmaAllocator,
			AssetSupplier& );

		static void destroy(ObjectStorage&);

		[[nodiscard]] ObjectId createObject (const NewObject&);
		void                   removeObject (ObjectId) noexcept;
		void                   clearObjects () noexcept;
		std::optional<const Object*>    getObject    (ObjectId) const noexcept;
		std::optional<ModifiableObject> modifyObject (ObjectId) noexcept;

		ModelId          getModelId (std::string_view locator);
		const ModelData* getModel   (ModelId) const noexcept;
		void             eraseModel (ModelId) noexcept;

		MaterialId          getMaterialId (std::string_view locator);
		const MaterialData* getMaterial   (MaterialId) const noexcept;

		VmaAllocator vma() const noexcept { return mVma; }
		VkDevice vkDevice() const noexcept { VmaAllocatorInfo ai; vmaGetAllocatorInfo(vma(), &ai); return ai.device; }

		auto     getDrawBatches       () const noexcept { return std::span<const DrawBatch>(mDrawBatchList); };
		VkBuffer getInstanceBuffer    () const noexcept { return const_cast<VkBuffer>(mObjectBuffer.value); }
		VkBuffer getDrawCommandBuffer () const noexcept { return const_cast<VkBuffer>(mBatchBuffer.value); }

		/// \brief Starts committing the objects to central memory, then to Vulkan buffers.
		/// \returns `true` only if any command was recorded into the command buffer parameter.
		///
		virtual bool commitObjects(VkCommandBuffer);

		/// \brief Wait until all worker threads are idle.
		///
		virtual void waitUntilReady();

		void reserve(size_t capacity);
		void shrinkToFit();

	private:
		VmaAllocator mVma = nullptr;
		Logger mLogger;
		std::shared_ptr<WorldRendererSharedState> mWrSharedState;
		AssetSupplier* mAssetSupplier;

		ModelLookup      mModelLocators;
		MaterialLookup   mMaterialLocators;
		ModelMap         mModels;
		MaterialMap      mMaterials;
		Objects          mObjects;
		ObjectUpdates    mObjectUpdates;
		UnboundBatchMap  mUnboundDrawBatches;
		BatchList        mDrawBatchList;
		ModelDepCounters mModelDepCounters;
		VkDescriptorPool mDpool;
		size_t           mDpoolSize;
		size_t           mDpoolCapacity;
		vkutil::BufferDuplex mObjectBuffer;
		vkutil::BufferDuplex mBatchBuffer;

		std::shared_ptr<MatrixAssembler> mMatrixAssembler;
		std::vector<size_t> mMatrixAssemblerRunningWorkers;

		bool mBatchesNeedUpdate  : 1; // `true` when objects have been added or removed
		bool mObjectsNeedRebuild : 1; // `true` when the object buffer is completely out of date
		bool mObjectsNeedFlush   : 1; // `true` when the object buffer needs to be uploaded, but all objects already exist in it

		ModelId    setModel      (std::string_view locator, DevModel);
		MaterialId setMaterial   (std::string_view locator, Material);
		void       eraseMaterial (MaterialId) noexcept;
		void       eraseModelNoObjectCheck (ModelId, ModelData&) noexcept;
	};

}
