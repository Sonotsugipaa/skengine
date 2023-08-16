#include "engine.hpp"

#include <spdlog/spdlog.h>

#include <fmamdl/fmamdl.hpp>

#include <posixfio.hpp>



namespace SKENGINE_NAME_NS {

	MeshSupplier::MeshSupplier(Engine& e, float max_inactive_ratio):
			ms_engine(&e),
			ms_maxInactiveRatio(max_inactive_ratio)
	{ }


	void MeshSupplier::destroy() {
		assert(ms_engine != nullptr);
		auto vma = ms_engine->getVmaAllocator();
		msi_releaseAllMeshes();
		for(auto& mesh : ms_inactive) {
			vkutil::BufferDuplex::destroy(vma, mesh.second.indices);
			vkutil::BufferDuplex::destroy(vma, mesh.second.vertices);
		}
		ms_inactive.clear();
		ms_engine = nullptr;
	}


	MeshSupplier::~MeshSupplier() {
		if(ms_engine != nullptr) destroy();
	}


	DevMesh MeshSupplier::msi_requestMesh(std::string_view locator) {
		std::string locator_s = std::string(locator);
		auto        existing  = ms_active.find(locator_s);
		auto        vma       = ms_engine->getVmaAllocator();

		if(existing != ms_active.end()) {
			return existing->second;
		} else {
			using posixfio::MemProtFlags;
			using posixfio::MemMapFlags;

			DevMesh r;

			auto file = posixfio::File::open(locator_s.c_str(), O_RDONLY);
			auto len  = file.lseek(0, SEEK_END);
			auto mmap = file.mmap(len, MemProtFlags::eRead, MemMapFlags::ePrivate, 0);
			auto h    = fmamdl::Header { mmap.get<std::byte>(), mmap.size() };
			auto indices  = h.indices();
			auto vertices = h.vertices();

			vkutil::BufferCreateInfo bc_info = { };
			bc_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			bc_info.size  = indices.size_bytes();
			r.indices = vkutil::BufferDuplex::createIndexInputBuffer(vma, bc_info);
			bc_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			bc_info.size  = vertices.size_bytes();
			r.vertices = vkutil::BufferDuplex::createVertexInputBuffer(vma, bc_info);

			memcpy(r.indices.mappedPtr<void>(),  indices.data(),  indices.size_bytes());
			memcpy(r.vertices.mappedPtr<void>(), vertices.data(), vertices.size_bytes());
			ms_engine->pushBuffer(r.indices);
			ms_engine->pushBuffer(r.vertices);

			ms_active.insert(Meshes::value_type(std::move(locator_s), r));
			double size_kib = indices.size_bytes() + vertices.size_bytes();
			spdlog::trace("Loaded mesh \"{}\" ({:.3f} KiB)", locator, size_kib / 1000.0);
			return r;
		}
	}

	void MeshSupplier::msi_releaseMesh(std::string_view locator) noexcept {
		std::string locator_s = std::string(locator);
		auto        existing  = ms_active.find(locator_s);
		auto        vma       = ms_engine->getVmaAllocator();

		if(existing != ms_active.end()) {
			// Move to the inactive map
			ms_inactive.insert(*existing);
			ms_active.erase(existing);
			if(ms_maxInactiveRatio < float(ms_inactive.size()) / float(ms_active.size())) {
				auto victim = ms_inactive.begin();
				vkutil::BufferDuplex::destroy(vma, victim->second.indices);
				vkutil::BufferDuplex::destroy(vma, victim->second.vertices);
				ms_inactive.erase(victim);
			}
			spdlog::trace("Released mesh \"{}\"", locator);
		} else {
			spdlog::debug("Tried to release mesh \"{}\", but it's not loaded", locator);
		}
	}

	void MeshSupplier::msi_releaseAllMeshes() noexcept {
		std::vector<std::string> queue;
		queue.reserve(ms_active.size());
		for(auto& mesh : ms_active) queue.push_back(mesh.first);
		for(auto& loc  : queue)     msi_releaseMesh(loc);
	}

}
