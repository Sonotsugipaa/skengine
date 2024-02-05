#pragma once

#include "util.inl.hpp"
#include "conv.hpp"

#include <memory>
#include <filesystem>
#include <stdexcept>
#include <unordered_map>

#include <rapidobj/rapidobj.hpp>

#include <fmt/format.h>

#include <fmamdl/fmamdl.hpp>
#include <fmamdl/material.hpp>



namespace fmamdl::conv::obj {

	namespace {

		constexpr u8_t floatRgbToIntRgba(const float flt[3]) noexcept {
			return
				((u8_t(flt[0] * 255.0f) & u8_t(0xff)) <<  0) |
				((u8_t(flt[1] * 255.0f) & u8_t(0xff)) <<  8) |
				((u8_t(flt[2] * 255.0f) & u8_t(0xff)) << 16) |
				((u8_t(0xff)                        ) << 24);
		}

	}



	const auto vtxLayout  = Layout::fromCstring("f44444222222222");
	const auto headerSize = HeaderView::requiredBytesFor(vtxLayout);


	auto allocHeader() {
		auto r = std::make_unique_for_overwrite<std::byte[]>(headerSize);
		memset(r.get(), 0xaa, headerSize);
		return r;
	}


	struct ParsedMaterial {
		MaterialFlags flags;
		std::string name;
		std::string diffuseTexture;
		std::string normalTexture;
		std::string specularTexture;
		std::string emissiveTexture;
		u8_t id;
		u8_t diffuseValue;
		u8_t normalValue;
		u8_t specularValue;
		u8_t emissiveValue;
		f4_t specularExponent;
	};


	struct ReadObjDst {
		std::unordered_map<std::string, ParsedMaterial> parsedMaterials;
		std::vector<Material> materials;
		std::vector<Bone>     bones;
		std::vector<Mesh>     meshes;
		std::vector<Face>     faces;
		std::vector<Index>    indices;
		std::vector<Vertex>   vertices;
		StringStorage         strings;
	};

	void readObj(const Options& opt, ReadObjDst& dst) {
		rapidobj::Result result = rapidobj::ParseFile(opt.srcName, rapidobj::MaterialLibrary::Default());

		using vim_t  = std::unordered_map<Vertex, u4_t, VertexHash>;
		using ivm_t  = std::unordered_map<u4_t, const Vertex*>;
		using mf_ec  = MaterialFlags;
		using mf_e   = material_flags_e;
		using StrOff = fmamdl::StringOffset;

		vim_t vertexIndexMap;
		ivm_t indexVertexMap;
		u8_t  nullMaterialIdx = ~ u8_t(0);
		std::vector<Vertex> faceVertexCache;

		if(result.attributes.normals.empty()) {
			throw std::runtime_error("Model is missing normal data"); }
		if(result.attributes.texcoords.empty()) {
			throw std::runtime_error("Model is missing texture data"); }

		auto putVertex = [&](const rapidobj::Index& objIndex) {
			// When this function returns, tangents and bitangents are still yet to be calculated
			assert(objIndex.normal_index   >= 0);
			assert(objIndex.texcoord_index >= 0);
			Vertex vtx;
			u4_t   newIdx = dst.vertices.size();
			vtx.position[0] = result.attributes.positions[(objIndex.position_index*3)+0];
			vtx.position[1] = result.attributes.positions[(objIndex.position_index*3)+1];
			vtx.position[2] = result.attributes.positions[(objIndex.position_index*3)+2];
			vtx.normal[0]   = f2_t(result.attributes.normals[(objIndex.normal_index*3)+0]);
			vtx.normal[1]   = f2_t(result.attributes.normals[(objIndex.normal_index*3)+1]);
			vtx.normal[2]   = f2_t(result.attributes.normals[(objIndex.normal_index*3)+2]);
			vtx.texture[0]  = +result.attributes.texcoords[(objIndex.texcoord_index*2)+0];
			vtx.texture[1]  = -result.attributes.texcoords[(objIndex.texcoord_index*2)+1];
			auto iter0 = vertexIndexMap.insert(std::pair<const Vertex, u4_t>(vtx, newIdx));
			if(iter0.second /* Insertion happened */) {
				indexVertexMap.insert(std::pair<const u4_t, const Vertex*>(newIdx, &iter0.first->first));
				dst.vertices.push_back(vtx);
				return newIdx;
			} else {
				return iter0.first->second;
			}
		};

		auto addFace = [&](const rapidobj::Mesh& mesh, u8_t faceVtxOffset, u2_t faceSize, u4_t materialId) {
			assert(faceSize >= 3);
			Face face;
			faceVertexCache.clear();
			faceVertexCache.reserve(faceSize);
			face.firstIndex = dst.indices.size();
			for(size_t i = 0; i < faceSize; ++i) {
				std::size_t meshIndexIndex = faceVtxOffset + i;
				assert(faceVtxOffset + faceSize <= mesh.indices.size());
				u4_t insIndex = putVertex(mesh.indices[meshIndexIndex]);
				dst.indices.push_back(Index(insIndex));
				faceVertexCache.push_back(dst.vertices[insIndex]);
			}
			computeTangents(faceVertexCache.data(), faceVertexCache.size());
			face.indexCount    = faceSize;
			face.materialIndex = materialId;
			computeNormal(face.normal, faceVertexCache.data());
			for(size_t i = 0; i < faceSize; ++i) {
				std::size_t indexIndex = dst.indices.size() - faceSize + i;
				std::size_t index      = std::size_t(dst.indices[indexIndex]);
				memcpy(dst.vertices.data() + index, faceVertexCache.data() + i, sizeof(Vertex));
			}
			dst.indices.push_back(Index::ePrimitiveRestart);
			dst.faces.push_back(face);
		};

		auto addMaterial = [&](const rapidobj::Material& mat) {
			u8_t id = dst.materials.size();
			ParsedMaterial pmat = { };

			auto existingMat = dst.parsedMaterials.find(mat.name);
			if(existingMat != dst.parsedMaterials.end()) {
				return existingMat->second.id;
			}
			pmat.name = std::move(mat.name);
			pmat.id   = id;
			std::string texturePrefix = std::string(opt.texturePrefix);
			StrOff      nameIdx       = dst.strings.add(mat.name);

			if(! mat.diffuse_texname.empty()) {
				pmat.diffuseTexture = texturePrefix + mat.diffuse_texname;
			} else {
				pmat.diffuseValue = floatRgbToIntRgba(mat.diffuse.data());
				pmat.flags = mf_ec(mf_e(pmat.flags) | mf_e(mf_ec::eDiffuseInlinePixel));
			}
			if(! mat.bump_texname.empty()) {
				pmat.normalTexture = texturePrefix + mat.bump_texname;
			} else {
				pmat.normalValue = 0xff'ff'7f'7f;
				pmat.flags = mf_ec(mf_e(pmat.flags) | mf_e(mf_ec::eNormalInlinePixel));
			}
			if(! mat.specular_texname.empty()) {
				pmat.specularTexture = texturePrefix + mat.specular_texname;
			} else {
				pmat.specularValue = floatRgbToIntRgba(mat.specular.data());
				pmat.flags = mf_ec(mf_e(pmat.flags) | mf_e(mf_ec::eSpecularInlinePixel));
			}
			if(! mat.emissive_texname.empty()) {
				pmat.emissiveTexture = texturePrefix + mat.emissive_texname;
			} else {
				pmat.emissiveValue = floatRgbToIntRgba(mat.emission.data());
				pmat.flags = mf_ec(mf_e(pmat.flags) | mf_e(mf_ec::eEmissiveInlinePixel));
			}

			pmat.specularExponent = mat.shininess;

			dst.materials.push_back(Material {
				.name = nameIdx });
			dst.parsedMaterials.insert(decltype(dst.parsedMaterials)::value_type { pmat.name, pmat });

			return id;
		};

		auto addShape = [&](const rapidobj::Shape& shape) {
			auto& objMesh = shape.mesh;
			(void) shape.name;

			u8_t matId;
			if(shape.mesh.material_ids.empty() || result.materials.empty()) {
				if(nullMaterialIdx == ~ u8_t(0)) {
					rapidobj::Material mat = { };
					mat.name    = "default";
					mat.ambient = { 1.0f, 1.0f, 1.0f };
					matId = addMaterial(mat);
				} else {
					matId = nullMaterialIdx;
				}
			} else {
				matId = addMaterial(result.materials[shape.mesh.material_ids.front()]);
			}

			Mesh mesh          = { };
			mesh.materialIndex = matId;
			mesh.firstFace     = dst.faces.size();
			mesh.faceCount     = objMesh.num_face_vertices.size();
			u8_t indexCountBeforeInsertion = dst.indices.size();
			u8_t faceVtxOffset = 0;
			for(size_t i = 0; i < objMesh.num_face_vertices.size(); ++i) {
				u8_t faceSize = objMesh.num_face_vertices[i];
				addFace(objMesh, faceVtxOffset, faceSize, objMesh.material_ids[i]);
				faceVtxOffset += faceSize;
			}

			mesh.indexCount = dst.indices.size() - indexCountBeforeInsertion;
			dst.meshes.push_back(mesh);

			Bone bone = { };
			bone.name        = dst.strings.add(shape.name);
			bone.parent      = dst.strings.add("");
			bone.meshIndex   = dst.meshes.size() - 1;
			bone.relScale[0] = 1.0f;
			bone.relScale[1] = 1.0f;
			bone.relScale[2] = 1.0f;
			dst.bones.push_back(bone);
		};

		{
			u8_t indexCount = 0;
			for(auto& shape : result.shapes) indexCount += shape.mesh.indices.size() + shape.mesh.num_face_vertices.size();
			dst.materials.reserve(std::log2(std::max<size_t>(result.shapes.size(), 2)));
			dst.meshes.reserve(dst.materials.capacity());
			dst.vertices.reserve(result.attributes.positions.size() / 3);
			dst.faces.reserve(dst.vertices.capacity() / 2);
			dst.indices.reserve(indexCount + dst.faces.capacity());
			vertexIndexMap.reserve(dst.vertices.capacity());
			indexVertexMap.reserve(dst.indices.capacity());
		}

		for(auto& shape : result.shapes) {
			addShape(shape);
		}
	}


	void convert(const Options& opt) {
		auto       headerSpace = allocHeader();
		HeaderView h = { headerSpace.get(), headerSize };
		h.magicNumber() = fmamdl::currentMagicNumber;

		auto dstPath = std::filesystem::path(opt.dstName);

		StringStorage strings;

		auto& flags               = h.flags();
		auto& materialCount       = h.materialCount();
		auto& materialTableOffset = h.materialTableOffset();
		auto& meshCount           = h.meshCount();
		auto& meshTableOffset     = h.meshTableOffset();
		auto& boneCount           = h.boneCount();
		auto& boneTableOffset     = h.boneTableOffset();
		auto& faceCount           = h.faceCount();
		auto& faceTableOffset     = h.faceTableOffset();
		auto& indexCount          = h.indexCount();
		auto& indexTableOffset    = h.indexTableOffset();
		auto& vertexCount         = h.vertexCount();
		auto& vertexTableOffset   = h.vertexTableOffset();
		auto& stringCount         = h.stringCount();
		auto& stringsBytes        = h.stringStorageSize();
		auto& stringStorageOffset = h.stringStorageOffset();
		h.setVertexLayout(vtxLayout);

		flags = fmamdl::HeaderFlags::eTriangleFan;

		ReadObjDst dst;
		readObj(opt, dst);
		stringCount   = dst.strings.map.size();
		stringsBytes  = dst.strings.bytes.size();
		materialCount = dst.materials.size();
		meshCount     = dst.meshes.size();
		boneCount     = dst.bones.size();
		faceCount     = dst.faces.size();
		indexCount    = dst.indices.size();
		vertexCount   = dst.vertices.size();
		size_t stringStorageSize = align<8>(stringsBytes);
		size_t materialTableSize = align<8>(materialCount * sizeof(Material));
		size_t meshTableSize     = align<8>(meshCount * sizeof(Mesh));
		size_t boneTableSize     = align<8>(boneCount * sizeof(Bone));
		size_t faceTableSize     = align<8>(faceCount * sizeof(Face));
		size_t indexTableSize    = align<8>(indexCount * sizeof(Index));
		size_t vertexTableSize   = align<8>(vertexCount * sizeof(Vertex));
		stringStorageOffset = align<8>(headerSize);
		materialTableOffset = stringStorageOffset + stringStorageSize;
		meshTableOffset     = materialTableOffset + materialTableSize;
		boneTableOffset     = meshTableOffset + meshTableSize;
		faceTableOffset     = boneTableOffset + boneTableSize;
		indexTableOffset    = faceTableOffset + faceTableSize;
		vertexTableOffset   = indexTableOffset + indexTableSize;

		if(! opt.onlyMaterials) { // Write the model file
			size_t fileSize = vertexTableOffset + vertexTableSize;
			auto   output   = posixfio::File::open(opt.dstName.data(), O_CREAT | O_RDWR, 0660);
			output.ftruncate(fileSize);
			posixfio::MemMapping map = output.mmap(
				fileSize,
				posixfio::MemProtFlags::eWrite,
				posixfio::MemMapFlags::eShared,
				0 );
			auto* pmap = map.get<std::byte>();
			memcpy(pmap,                     h.data,                   headerSize);
			memcpy(pmap+stringStorageOffset, dst.strings.bytes.data(), stringStorageSize);
			memcpy(pmap+materialTableOffset, dst.materials.data(),     materialTableSize);
			memcpy(pmap+meshTableOffset,     dst.meshes.data(),        meshTableSize);
			memcpy(pmap+boneTableOffset,     dst.bones.data(),         boneTableSize);
			memcpy(pmap+faceTableOffset,     dst.faces.data(),         faceTableSize);
			memcpy(pmap+indexTableOffset,    dst.indices.data(),       indexTableSize);
			memcpy(pmap+vertexTableOffset,   dst.vertices.data(),      vertexTableSize);
		}

		if(! opt.noMaterials)
		for(auto& pmat : dst.parsedMaterials) { // Write the material files
			using mf_ec = MaterialFlags;
			using mf_e  = material_flags_e;
			StringStorage          strings;
			std::vector<std::byte> bytes;
			bytes.resize(10*8);

			MaterialView mat = { bytes.data(), bytes.size() };

			auto setTexture = [&](mf_ec neededFlag, u8_t& dst, u8_t val, const std::string& str) {
				if(! (mf_e(pmat.second.flags) & mf_e(neededFlag))) {
					dst = string_offset_e(strings.add(str));
				} else {
					dst = val;
				}
			};

			size_t endOfHead = align<8>(bytes.size());
			mat.magicNumber() = currentMagicNumber;
			mat.flags()       = mf_ec(pmat.second.flags);
			setTexture(mf_ec::eDiffuseInlinePixel,  mat.diffuseTexture(),  pmat.second.diffuseValue,  pmat.second.diffuseTexture);
			setTexture(mf_ec::eNormalInlinePixel,   mat.normalTexture(),   pmat.second.normalValue,   pmat.second.normalTexture);
			setTexture(mf_ec::eSpecularInlinePixel, mat.specularTexture(), pmat.second.specularValue, pmat.second.specularTexture);
			setTexture(mf_ec::eEmissiveInlinePixel, mat.emissiveTexture(), pmat.second.emissiveValue, pmat.second.emissiveTexture);
			mat.specularExponent()    = pmat.second.specularExponent;
			mat.stringStorageOffset() = endOfHead;
			mat.stringStorageSize()   = strings.bytes.size();
			mat.stringCount()         = strings.map.size();

			size_t fileSize = endOfHead + strings.bytes.size();
			std::string nameCstr =
				(dstPath.has_parent_path()?
					std::string(dstPath.parent_path()) :
					std::string(".")
				)
				+ std::string("/")
				+ std::string(pmat.first);

			auto output = posixfio::File::open(nameCstr.c_str(), O_CREAT | O_RDWR, 0660);
			output.ftruncate(fileSize);
			posixfio::MemMapping map = output.mmap(
				fileSize,
				posixfio::MemProtFlags::eWrite,
				posixfio::MemMapFlags::eShared,
				0 );
			auto* pmap = map.get<std::byte>();
			memcpy(pmap,             bytes.data(),         bytes.size());
			memcpy(pmap + endOfHead, strings.bytes.data(), strings.bytes.size());
		}
	}

}
