#include "renderer.hpp"

#include "engine.hpp"

#include <glm/ext/matrix_transform.hpp>

#include <spdlog/spdlog.h>



namespace SKENGINE_NAME_NS {

	WorldRenderer WorldRenderer::create(VmaAllocator vma, MeshSupplierInterface& msi) {
		WorldRenderer r = Renderer::create(vma, msi);
		r.mViewPosXyz = { };
		r.mViewDirYpr = { };
		r.mViewTransfCacheOod = true;
		return r;
	}


	void WorldRenderer::destroy(WorldRenderer& r) {
		Renderer::destroy(r);
	}


	const glm::mat4& WorldRenderer::getViewTransf() noexcept {
		if(mViewTransfCacheOod) {
			constexpr glm::vec3 x = { 1.0f, 0.0f, 0.0f };
			constexpr glm::vec3 y = { 0.0f, 1.0f, 0.0f };
			constexpr glm::vec3 z = { 0.0f, 0.0f, 1.0f };
			constexpr glm::mat4 identity = glm::mat4(1.0f);
			glm::mat4 translate = glm::translate(identity, mViewPosXyz);
			glm::mat4 rot0      = glm::rotate(identity, mViewDirYpr.y, x);
			glm::mat4 rot1      = glm::rotate(identity, mViewDirYpr.x, y);
			glm::mat4 rot2      = glm::rotate(identity, mViewDirYpr.z, z);
			mViewTransfCache = rot0 * rot1 * rot2 * translate;
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


	WorldRenderer::WorldRenderer(Renderer&& mv):
		Renderer::Renderer(std::move(mv))
	{ }

}
