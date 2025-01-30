#pragma once

#include <engine/types.hpp>

#include <vk-util/memory.hpp>

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>

#include <unordered_set>
#include <memory>
#include <span>
#include <thread>
#include <condition_variable>
#include <vector>
#include <type_traits>
#include <stdfloat>
#include <utility>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <vma/vk_mem_alloc.h>



namespace SKENGINE_NAME_NS {

	/// This namespace defines structures as passed to
	/// the Vulkan device, which need to be carefully
	/// packed due to alignment shenanigans.
	///
	namespace dev {

		using dev_flags_e = uint32_t;

		enum FrameUniformFlagBits : dev_flags_e {
			FRAME_UNI_ZERO        = 0b0,
			FRAME_UNI_HDR_ENABLED = 0b1
		};

		enum class FrameUniformFlags : dev_flags_e { };


		struct Object {
			ALIGNF32(1) glm::mat4 model_transf;
			ALIGNF32(1) glm::vec4 color_mul;
			ALIGNF32(1) glm::vec4 cull_sphere_xyzr;
			ALIGNF32(1) std::float32_t rnd;
			ALIGNI32(1) uint32_t draw_batch_idx;
			ALIGNI32(1) bool     visible;
			ALIGNI32(1) uint32_t padding[1];
		};


		struct Light {
			ALIGNF32(4) glm::vec4 m0;
			ALIGNF32(4) glm::vec4 m1;
			ALIGNF32(1) std::float32_t m2;
			ALIGNF32(1) std::float32_t m3;
			ALIGNF32(1) std::float32_t m4;
			ALIGNF32(1) std::float32_t m5;
		};

		struct RayLight {
			ALIGNF32(4) glm::vec4 direction;
			ALIGNF32(4) glm::vec4 color;
			ALIGNF32(1) std::float32_t aoa_threshold;
			ALIGNF32(1) std::float32_t m4_unused;
			ALIGNF32(1) std::float32_t m5_unused;
			ALIGNF32(1) std::float32_t m6_unused;
		};
		#ifndef VS_CODE_HEADER_LINTING_WORKAROUND
		static_assert(std::is_layout_compatible_v<Light, RayLight> && sizeof(Light) == sizeof(RayLight));
		#endif

		struct PointLight {
			ALIGNF32(4) glm::vec4 position;
			ALIGNF32(4) glm::vec4 color;
			ALIGNF32(1) std::float32_t falloff_exp;
			ALIGNF32(1) std::float32_t m4_unused;
			ALIGNF32(1) std::float32_t m5_unused;
			ALIGNF32(1) std::float32_t m6_unused;
		};
		#ifndef VS_CODE_HEADER_LINTING_WORKAROUND
		static_assert(std::is_layout_compatible_v<Light, PointLight> && sizeof(Light) == sizeof(PointLight));
		#endif


		struct FrameUniform {
			ALIGNF32(1) glm::mat4 projview_transf;
			ALIGNF32(1) glm::mat4 proj_transf;
			ALIGNF32(1) glm::mat4 view_transf;
			ALIGNF32(1) glm::vec4 view_pos;
			ALIGNF32(1) glm::vec4 ambient_lighting;
			ALIGNI32(1) uint32_t  ray_light_count;
			ALIGNI32(1) uint32_t  point_light_count;
			ALIGNF32(1) uint32_t  shade_step_count;
			ALIGNF32(1) std::float32_t shade_step_smooth;
			ALIGNF32(1) std::float32_t shade_step_exp;
			ALIGNF32(1) std::float32_t dithering_steps;
			ALIGNF32(1) std::float32_t rnd;
			ALIGNF32(1) std::float32_t time_delta;
			ALIGNF32(1) std::float32_t p_light_dist_threshold;
			ALIGNFLAGS(1) FrameUniformFlags flags;
		};


		struct MaterialUniform {
			ALIGNF32(1) float shininess;
		};


		struct ObjectId {
			ALIGNI32(1) uint32_t id;
		};

	}


	#define DECL_SCOPED_ENUM_(ENUM_, ALIAS_, UNDERLYING_) using ALIAS_ = UNDERLYING_; enum class ENUM_ : ALIAS_ { };
	DECL_SCOPED_ENUM_(ObjectId,        object_id_e,         uint_fast64_t)
	DECL_SCOPED_ENUM_(BoneId,          model_instance_id_e, uint_fast64_t)
	DECL_SCOPED_ENUM_(ModelInstanceId, bone_id_e,           uint32_t)
	DECL_SCOPED_ENUM_(MaterialId,      material_id_e,       uint32_t)
	DECL_SCOPED_ENUM_(ModelId,         model_id_e,          uint32_t)
	#undef DECL_SCOPED_ENUM_


	class Engine;
	struct WorldRendererSharedState;


	struct Object {
		ModelId model_id;
		glm::vec3 position_xyz;
		glm::vec3 direction_ypr;
		glm::vec3 scale_xyz;
		bool      hidden;
	};


	struct Mesh {
		uint32_t index_count;
		uint32_t first_index;
		glm::vec4 cull_sphere_xyzr;
	};


	struct Bone {
		Mesh mesh;
		MaterialId material_id;
		glm::vec3 position_xyz;
		glm::vec3 direction_ypr;
		glm::vec3 scale_xyz;
	};

	struct BoneInstance {
		ModelId    model_id;
		MaterialId material_id;
		ObjectId   object_id;
		glm::vec4 color_rgba;
		glm::vec3 position_xyz;
		glm::vec3 direction_ypr;
		glm::vec3 scale_xyz;
	};


	struct DrawBatch {
		ModelId    model_id;
		MaterialId material_id;
		uint32_t   vertex_offset;
		uint32_t   index_count;
		uint32_t   first_index;
		uint32_t   instance_count;
		uint32_t   first_instance;
	};


	struct RayLight {
		glm::vec3 direction;
		glm::vec3 color;
		float     intensity;
		float     aoa_threshold;
	};

	struct PointLight {
		glm::vec3 position;
		glm::vec3 color;
		float     intensity;
		float     falloff_exp;
	};


	struct BadObjectModelRefError { ModelId modelId; };


	struct DevModel {
		vkutil::BufferDuplex indices;
		vkutil::BufferDuplex vertices;
		std::vector<Bone>    bones;
		uint32_t index_count;
		uint32_t vertex_count;
	};


	// A draw batch, without object-specific data in favor of lists of references to them.
	struct UnboundDrawBatch {
		std::unordered_set<ObjectId> object_refs;
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


	class AssetCacheInterface {
	public:
		struct ModelDescription {
			fmamdl::HeaderView fmaHeader;
		};

		struct MaterialDescription {
			fmamdl::MaterialView fmaHeader;
			std::string texturePathPrefix;
		};

		virtual ModelDescription aci_requestModelData(ModelId) = 0;
		virtual MaterialDescription aci_requestMaterialData(MaterialId) = 0;
		virtual void aci_releaseModelData(ModelId) = 0;
		virtual void aci_releaseMaterialData(MaterialId) = 0;
		virtual MaterialId aci_materialIdFromName(std::string_view) = 0;
	};


	class AssetSupplier {
	public:
		using Models           = std::unordered_map<ModelId,    DevModel>;
		using Materials        = std::unordered_map<MaterialId, Material>;
		using MissingMaterials = std::unordered_set<MaterialId>;

		AssetSupplier(): as_initialized(false) { }
		AssetSupplier(Logger, std::shared_ptr<AssetCacheInterface>, float max_inactive_ratio);
		AssetSupplier(AssetSupplier&&);
		AssetSupplier& operator=(AssetSupplier&& mv) { this->~AssetSupplier(); return * new (this) AssetSupplier(std::move(mv)); }
		void destroy(TransferContext);

		#ifndef NDEBUG
			~AssetSupplier(); // Only asserts whether it has already been destroyed
		#endif

		DevModel requestModel(ModelId, TransferContext);
		void     releaseModel(ModelId, TransferContext) noexcept;
		void releaseAllModels(TransferContext) noexcept;

		Material requestMaterial(MaterialId, TransferContext);
		void     releaseMaterial(MaterialId, TransferContext) noexcept;
		void releaseAllMaterials(TransferContext) noexcept;

		bool isInitialized() const noexcept { return as_initialized; }

	private:
		Logger as_logger;
		std::shared_ptr<AssetCacheInterface> as_cacheInterface;
		Models    as_activeModels;
		Models    as_inactiveModels;
		Materials as_activeMaterials;
		Materials as_inactiveMaterials;
		Material  as_fallbackMaterial;
		MissingMaterials as_missingMaterials;
		float as_maxInactiveRatio;
		bool as_initialized;
		bool as_fallbackMaterialExists;
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
			ModelId   model_id;
			glm::vec3 position_xyz;
			glm::vec3 direction_ypr;
			glm::vec3 scale_xyz;
			bool      hidden;
		};

		template <typename Id> requires std::is_scoped_enum_v<Id>
		struct BadId : public std::runtime_error {
			Id id;
			BadId(Id id_): runtime_error(fmt::format("bad id {}", std::underlying_type_t<Id>(id_))), id(id_) { }
		};

		struct ModelData : DevModel {
			ModelId id;
		};

		struct MaterialData : Material {
			MaterialId id;
			VkDescriptorSet dset;
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
				struct {
					glm::vec4 cull_sphere;
				} mesh;
				struct {
					glm::mat4* model_transf;
					glm::vec4* cull_sphere;
				} dst;
			};
			using JobQueue = std::deque<Job>;
			std::mutex              mutex;
			std::condition_variable produce_cond;
			std::condition_variable consume_cond;
			std::thread             thread;
			JobQueue                queue;
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
		ObjectStorage& operator=(ObjectStorage&& mv) { this->~ObjectStorage(); return * new (this) ObjectStorage(std::move(mv)); }

		static ObjectStorage create(
			Logger,
			std::shared_ptr<WorldRendererSharedState>,
			VmaAllocator,
			AssetSupplier& );

		static void destroy(TransferContext, ObjectStorage&);

		[[nodiscard]] ObjectId createObject (TransferContext, const NewObject&);
		void                   removeObject (TransferContext, ObjectId);
		void                   clearObjects (TransferContext) noexcept;
		std::optional<ModifiableObject> modifyObject (ObjectId) noexcept;
		std::optional<const Object*>    getObject    (ObjectId) const noexcept;

		const ModelData* getModel  (ModelId) const noexcept;
		void             eraseModel(TransferContext, ModelId) noexcept;

		const MaterialData* getMaterial(MaterialId) const noexcept;

		VmaAllocator vma() const noexcept { return mVma; }

		auto  getObjectCount       () const noexcept { return mObjects.size(); }
		auto  getDrawCount         () const noexcept { return mDrawCount; }
		auto  getDrawBatchCount    () const noexcept { return mDrawBatchList.size(); }
		auto  getDrawBatches       () const noexcept { return std::span<const DrawBatch>(mDrawBatchList); };
		auto& getObjectBuffer      () const noexcept { return mObjectBuffer.first; }
		auto& getDrawCommandBuffer () const noexcept { return mBatchBuffer.first; }

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

		ModelMap         mModels;
		MaterialMap      mMaterials;
		Objects          mObjects;
		ObjectUpdates    mObjectUpdates;
		UnboundBatchMap  mUnboundDrawBatches;
		BatchList        mDrawBatchList;
		ModelDepCounters mModelDepCounters;
		VkDescriptorPool mMatDpool;
		size_t           mMatDpoolSize;
		size_t           mMatDpoolCapacity;
		size_t           mDrawCount;
		std::pair<vkutil::Buffer, size_t> mObjectBuffer;
		std::pair<vkutil::Buffer, size_t> mBatchBuffer;

		std::shared_ptr<MatrixAssembler> mMatrixAssembler;

		bool mMatrixAssemblerRunning : 1;
		bool mBatchesNeedUpdate      : 1; // `true` when objects have been added or removed
		bool mObjectsNeedRebuild     : 1; // `true` when the object buffer is completely out of date
		bool mObjectsNeedFlush       : 1; // `true` when the object buffer needs to be uploaded, but all objects already exist in it

		ModelData&    setModel      (ModelId,    DevModel);
		MaterialData& setMaterial   (MaterialId, Material);
		void          eraseMaterial (TransferContext, MaterialId) noexcept;
		void eraseModelNoObjectCheck (TransferContext, ModelId, ModelData&) noexcept;
	};

}
