#pragma once

#include <vector>
#include <type_traits>
#include <cstdint>
#include <stdfloat>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <skengine_fwd.hpp>

#include <vk-util/desc_proxy.hpp>



#define ALIGNF32(N_) alignas((N_) * sizeof(float))
#define ALIGNI32(N_) alignas((N_) * sizeof(uint32_t))



namespace SKENGINE_NAME_NS {

	using object_id_e         = uint_fast64_t;
	using model_instance_id_e = uint_fast64_t;
	using bone_id_e           = uint32_t;
	using material_id_e       = uint32_t;
	using model_id_e          = uint32_t;
	enum class ObjectId        : object_id_e         { };
	enum class BoneId          : bone_id_e           { };
	enum class ModelInstanceId : model_instance_id_e { };
	enum class MaterialId      : material_id_e       { };
	enum class ModelId         : model_id_e          { };


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
	};


	struct Bone {
		Mesh mesh;
		std::string material;
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


	/// This namespace defines structures as passed to
	/// the Vulkan device, which need to be carefully
	/// packed due to alignment shenanigans.
	///
	namespace dev {

		struct Instance {
			ALIGNF32(1) glm::mat4 model_transf;
			ALIGNF32(1) glm::vec4 color_mul;
			ALIGNF32(1) std::float32_t rnd;
		};


		struct Light {
			ALIGNF32(4) glm::vec4 m0;
			ALIGNF32(1) std::float32_t m1;
			ALIGNF32(1) std::float32_t m2;
			ALIGNF32(1) std::float32_t m3;
			ALIGNF32(1) std::float32_t m4;
		};

		struct RayLight {
			ALIGNF32(4) glm::vec4 direction;
			ALIGNF32(1) std::float32_t intensity;
			ALIGNF32(1) std::float32_t m2_unused;
			ALIGNF32(1) std::float32_t m3_unused;
			ALIGNF32(1) std::float32_t m4_unused;
		};
		static_assert(std::is_layout_compatible_v<Light, RayLight> && sizeof(Light) == sizeof(RayLight));

		struct PointLight {
			ALIGNF32(4) glm::vec4 position;
			ALIGNF32(1) std::float32_t intensity;
			ALIGNF32(1) std::float32_t falloff_exp;
			ALIGNF32(1) std::float32_t m3_unused;
			ALIGNF32(1) std::float32_t m4_unused;
		};
		static_assert(std::is_layout_compatible_v<Light, PointLight> && sizeof(Light) == sizeof(PointLight));


		struct FrameUniform {
			ALIGNF32(1) glm::mat4 projview_transf;
			ALIGNF32(1) glm::mat4 proj_transf;
			ALIGNF32(1) glm::mat4 view_transf;
			ALIGNF32(1) glm::vec4 view_pos;
			ALIGNI32(1) uint32_t  ray_light_count;
			ALIGNI32(1) uint32_t  point_light_count;
			ALIGNF32(1) uint32_t  shade_step_count;
			ALIGNF32(1) std::float32_t shade_step_smooth;
			ALIGNF32(1) std::float32_t shade_step_exp;
			ALIGNF32(1) std::float32_t rnd;
			ALIGNF32(1) std::float32_t time_delta;
		};


		struct MaterialUniform {
			ALIGNF32(1) float shininess;
		};

	}

}
