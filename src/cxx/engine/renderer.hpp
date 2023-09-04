#pragma once
/// \file
/// This entire thing is wrong.
///
/// The understanding-oriented documentation in /doc treats objects as
/// sets of meshes (as it should), but this header treats them as instances.
///
/// Here's what *should* happen:\n
/// the Renderer should hold a buffer of indirect draw commands, and one
/// of instances; it should also break given objects (from models) into
/// instances, *then* turn them into draw commands.
///
/// It does not have to worry about vertex buffers, because those belong
/// to the model (you know, memory-mapped model files and such) and the engine
/// does (will do) all the vertex buffer binding itself.\n
/// The entire point of the Renderer is for the engine to tell it
/// "I have these objects (with these locations) that have these models
/// (I already know where the vertices are), please turn them into instances
/// and draw commands."
///
/// I will worry about bones in the future, I can't see how it could
/// possibly go wrong.
///

#include "types.hpp"

#include "vk-util/memory.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <thread>
#include <memory>
#include <optional>
#include <condition_variable>
#include <unordered_set>
#include <unordered_map>

#include <spdlog/logger.h>



namespace SKENGINE_NAME_NS {

	class Engine;
	class WorldRenderer;
	class UiRenderer;


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


	class ModelSupplierInterface {
	public:
		virtual DevModel msi_requestModel(std::string_view locator) = 0;
		virtual void     msi_releaseModel(std::string_view locator) noexcept = 0;
		virtual void msi_releaseAllModels() noexcept = 0;
	};


	class MaterialSupplierInterface {
	public:
		virtual Material msi_requestMaterial(std::string_view locator) = 0;
		virtual void     msi_releaseMaterial(std::string_view locator) noexcept = 0;
		virtual void msi_releaseAllMaterials() noexcept = 0;
	};


	/// \brief A collection of objects to be drawn, which may or
	///        may not be frequently modified.
	///
	/// The Renderer abstracts the process of sorting objects by
	/// meshes and materials, and creating (indirect) draw commands.
	///
	/// It does own buffers for draw commands and object-specific data;
	/// it does NOT own mesh-specific or material-specific data,
	/// like vertices or textures.
	///
	class Renderer {
	public:
		friend WorldRenderer;
		friend UiRenderer;

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
				glm::vec3  positions[3];
				glm::vec3  directions[3];
				glm::vec3  scales[3];
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

		Renderer() = default;

		static Renderer create(
			std::shared_ptr<spdlog::logger>,
			VmaAllocator,
			DsetLayout material_dset_layout,
			ModelSupplierInterface&,
			MaterialSupplierInterface& );

		static void destroy(Renderer&);

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

		auto     getDrawBatches       () const noexcept { return std::span<const DrawBatch>(mDrawBatchList); };
		VkBuffer getInstanceBuffer    () const noexcept { return const_cast<VkBuffer>(mObjectBuffer.value); }
		VkBuffer getDrawCommandBuffer () const noexcept { return const_cast<VkBuffer>(mBatchBuffer.value); }

		/// \returns `true` only if any command was recorded into the command buffer parameter.
		///
		virtual bool commitObjects(VkCommandBuffer);

		void reserve(size_t capacity);
		void shrinkToFit();

	private:
		VkDevice     mDevice = nullptr;
		VmaAllocator mVma;
		std::shared_ptr<spdlog::logger> mLogger;
		ModelSupplierInterface*    mModelSupplier;
		MaterialSupplierInterface* mMaterialSupplier;

		ModelLookup      mModelLocators;
		MaterialLookup   mMaterialLocators;
		ModelMap         mModels;
		MaterialMap      mMaterials;
		Objects          mObjects;
		ObjectUpdates    mObjectUpdates;
		UnboundBatchMap  mUnboundDrawBatches;
		BatchList        mDrawBatchList;
		ModelDepCounters mModelDepCounters;
		DsetLayout       mDsetLayout;
		VkDescriptorPool mDpool;
		size_t           mDpoolSize;
		size_t           mDpoolCapacity;
		vkutil::BufferDuplex mObjectBuffer;
		vkutil::BufferDuplex mBatchBuffer;

		std::shared_ptr<MatrixAssembler> mMatrixAssembler;

		bool   mBatchesNeedUpdate  : 1; // `true` when objects have been added or removed
		bool   mObjectsNeedRebuild : 1; // `true` when the object buffer is completely out of date
		bool   mObjectsNeedFlush   : 1; // `true` when the object buffer needs to be uploaded, but all objects already exist in it

		ModelId    setModel      (std::string_view locator, DevModel);
		MaterialId setMaterial   (std::string_view locator, Material);
		void       eraseMaterial (MaterialId) noexcept;
		void       eraseModelNoObjectCheck (ModelId, ModelData&) noexcept;
	};


	/// \brief A specialisation of Renderer, for drawing objects
	///        in a generic 3D space.
	///
	class WorldRenderer : public Renderer {
	public:
		struct LightStorage {
			vkutil::ManagedBuffer buffer;
			dev::Light*           mappedPtr;
			uint_fast32_t updateCounter;
			uint32_t      bufferCapacity;
			uint32_t      rayCount;
			uint32_t      pointCount;
		};

		struct NewRayLight {
			glm::vec3 direction;
			float     intensity;
		};

		struct NewPointLight {
			glm::vec3 position;
			float     intensity;
			float     falloffExponent;
		};

		template <typename K, typename V> using Umap = std::unordered_map<K, V>;
		using RayLights   = Umap<ObjectId, dev::RayLight>;
		using PointLights = Umap<ObjectId, dev::PointLight>;

		WorldRenderer() = default;

		static WorldRenderer create(
			std::shared_ptr<spdlog::logger>,
			VmaAllocator,
			DsetLayout material_dset_layout,
			ModelSupplierInterface&,
			MaterialSupplierInterface& );

		static void destroy(WorldRenderer&);

		const glm::mat4& getViewTransf() noexcept;

		const glm::vec3& getViewPosition () const noexcept { return mViewPosXyz; }
		const glm::vec3& getViewRotation () const noexcept { return mViewDirYpr; }
		void setViewPosition  (const glm::vec3& xyz) noexcept;
		void setViewRotation  (const glm::vec3& ypr) noexcept; ///< Sets the yaw, pitch and roll of the world-view transformation.
		void setViewDirection (const glm::vec3& xyz) noexcept; ///< Rotates the view so that `xyz - pos` in world space equals (0, 0, -1) in view space.
		void rotate           (const glm::vec3& ypr) noexcept; ///< Similar to `WorldRenderer::setViewRotation`, but it's relative to the current position and rotation.
		void rotateTowards    (const glm::vec3& xyz) noexcept; ///< Similar to `WorldRenderer::setViewDirection`, but it's relative to the current position.

		[[nodiscard]] ObjectId createRayLight   (const NewRayLight&);
		[[nodiscard]] ObjectId createPointLight (const NewPointLight&);
		void                   removeLight      (ObjectId);
		const dev::RayLight&   getRayLight      (ObjectId) const;
		const dev::PointLight& getPointLight    (ObjectId) const;
		dev::RayLight&         modifyRayLight   (ObjectId);
		dev::PointLight&       modifyPointLight (ObjectId);

		const LightStorage& lightStorage() const noexcept { return mLightStorage; };

		bool commitObjects(VkCommandBuffer) override;

	private:
		RayLights    mRayLights;
		PointLights  mPointLights;
		LightStorage mLightStorage;
		glm::mat4 mViewTransfCache;
		glm::vec3 mViewPosXyz;
		glm::vec3 mViewDirYpr;
		bool      mViewTransfCacheOod : 1;
		bool      mLightStorageOod    : 1;

		WorldRenderer(Renderer&&);
	};


	/// \brief A specialisation of Renderer, for drawing objects
	///        user interface elements.
	///
	/* Placeholder */ struct UiRenderer : public WorldRenderer {
		using WorldRenderer::WorldRenderer;
	};

}
