#pragma once

#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include <skengine_fwd.hpp>

#include <vkutil/fwd.hpp>



#define ALIGNF32(N_) alignas((N_) * sizeof(float))
#define ALIGNI32(N_) alignas((N_) * sizeof(uint32_t))



namespace SKENGINE_NAME_NS {

	enum class UpdateCounter : uint_fast32_t { };

	enum class ObjectId   : uint_fast64_t  { };
	enum class MaterialId : uint_least32_t { };
	enum class MeshId     : uint_least32_t { };


	struct RenderObject {
		MeshId        mesh_id;
		MaterialId    material_id;
		glm::vec3     position_xyz;
		glm::vec3     direction_ypr;
		glm::vec3     scale_xyz;
		UpdateCounter update_ctr;
	};


	struct DrawBatch {
		MeshId     mesh_id;
		MaterialId material_id;
		uint32_t   first;
		uint32_t   count;
	};


	/// This namespace defines structures as passed to
	/// the Vulkan device, which need to be carefully
	/// packed due to alignment shenanigans.
	///
	namespace dev {

		struct RenderObject {
			ALIGNF32(4) glm::mat4 model_transf;
			ALIGNF32(4) glm::vec4 color_mul;
			ALIGNF32(1) float     rnd;
		};


		struct RayLight {
			ALIGNF32(1) glm::vec4 direction;
			ALIGNF32(1) float     intensity;
		};


		struct PointLight {
			ALIGNF32(1) glm::vec4 position;
			ALIGNF32(1) float     intensity;
			ALIGNF32(1) float     falloff_exp;
		};


		struct StaticUniform {
			ALIGNF32(4) glm::mat4 proj_transf;
			ALIGNI32(1) uint32_t  ray_light_count;
			ALIGNI32(1) uint32_t  point_light_count;
			ALIGNF32(1) float     rnd;
		};


		struct FrameUniform {
			ALIGNF32(4) glm::mat4 view_transf;
			ALIGNF32(1) float rnd;
			ALIGNF32(1) float time_delta;
		};


		struct MaterialUniform {
			ALIGNF32(1) float emissive_mul;
			ALIGNF32(1) float diffuse_mul;
			ALIGNF32(1) float specular_exp;
		};

	}

}
