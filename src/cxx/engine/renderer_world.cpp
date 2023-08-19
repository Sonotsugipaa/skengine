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
			mViewTransfCache = translate * rot0 * rot1 * rot2;
			mViewTransfCacheOod = false;
		}

		return mViewTransfCache;
	}


	void WorldRenderer::setViewPos(const glm::vec3& pos) noexcept {
		mViewPosXyz = pos;
		mViewTransfCacheOod = true;
	}


	void WorldRenderer::setViewDir(const glm::vec3& dir) noexcept {
		{ // Normalize the direction values
			constexpr auto& pi = std::numbers::pi_v<float>;
			#define NORMALIZE_(M_) { if(mViewDirYpr.M_ >= pi) [[unlikely]] { \
				mViewDirYpr.M_ = std::floor(mViewDirYpr.M_ / pi) * pi; }}
			NORMALIZE_(x)
			NORMALIZE_(y)
			NORMALIZE_(z)
			#undef NORMALIZE_
		}

		mViewDirYpr = dir;
		mViewTransfCacheOod = true;
	}


	WorldRenderer::WorldRenderer(Renderer&& mv):
		Renderer::Renderer(std::move(mv))
	{ }

}
