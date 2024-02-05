#pragma once

#include <cstdlib>
#include <cmath>
#include <cassert>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <unordered_map>

#include <fmt/format.h>

#include <fmamdl/fmamdl.hpp>



namespace std {

	template <>
	struct equal_to<const fmamdl::Vertex> {
		constexpr bool operator()(const fmamdl::Vertex& l, const fmamdl::Vertex& r) const noexcept {
			return 0 == (
				memcmp(&l.position, &r.position, sizeof(fmamdl::Vertex::position)) |
				memcmp(&l.texture,  &r.texture,  sizeof(fmamdl::Vertex::texture))  |
				memcmp(&l.normal,   &r.normal,   sizeof(fmamdl::Vertex::normal)) );
		}
	};

	template <>
	struct equal_to<fmamdl::Vertex> {
		constexpr bool operator()(const fmamdl::Vertex& l, const fmamdl::Vertex& r) const noexcept {
			return equal_to<const fmamdl::Vertex>()(l, r);
		}
	};

}



namespace fmamdl::conv {

	template <size_t alignment>
	constexpr size_t align(size_t x) {
		size_t mod = x % alignment;
		size_t inv = (mod == 0)? 0 : alignment - mod;
		return x + inv;
	}

	static_assert(align<8>( 0) == 8*0);
	static_assert(align<8>( 1) == 8*1);
	static_assert(align<8>( 8) == 8*1);
	static_assert(align<8>(16) == 8*2);
	static_assert(align<8>(17) == 8*3);


	struct VertexHash {
		std::size_t operator()(const Vertex& v) const noexcept {
			#define BC4_(V_) std::size_t(std::bit_cast<u4_t>(V_))
			std::size_t r;
			std::size_t mask = ~ std::size_t(0b111);
			r  = BC4_(v.position[0]);
			r ^= std::rotr<std::size_t>(BC4_(v.position[1]) & mask, 1*7);
			r ^= std::rotr<std::size_t>(BC4_(v.position[2]) & mask, 2*7);
			r ^= std::rotr<std::size_t>(BC4_(v.texture[0]) & mask,  3*7);
			r ^= std::rotr<std::size_t>(BC4_(v.texture[1]) & mask,  4*7);
			r ^= std::rotr<std::size_t>(BC4_(v.normal[0]) & mask,   5*7);
			r ^= std::rotr<std::size_t>(BC4_(v.normal[1]) & mask,   6*7);
			r ^= std::rotr<std::size_t>(BC4_(v.normal[2]) & mask,   7*7);
			return r;
			#undef BC4_
		}
	};


	template <size_t dim, std::floating_point T, std::floating_point Tv>
	inline void vecSet(T dst[dim], Tv set) {
		for(size_t i = 0; i < dim; ++i) dst[i] = set;
	}

	template <size_t dim, std::floating_point T, std::floating_point Tv>
	inline void vecSet(T dst[dim], const Tv set[dim]) {
		if constexpr(std::is_same_v<std::remove_const_t<T>, std::remove_const_t<Tv>>) {
			memcpy(dst, set, sizeof(dst));
		} else {
			for(size_t i = 0; i < dim; ++i) dst[i] = T(set[i]);
		}
	}


	template <size_t dim, std::floating_point T, std::floating_point Tl, std::floating_point Tr>
	inline void vecAdd(T dst[dim], const Tl lho[dim], const Tr rho[dim]) {
		for(size_t i = 0; i < dim; ++i) dst[i] = lho[i] + rho[i];
	}

	template <size_t dim, std::floating_point T, std::floating_point Tv>
	inline void vecAdd(T dst[dim], const Tv add[dim]) {
		for(size_t i = 0; i < dim; ++i) dst[i] += add[i];
	}

	template <size_t dim, std::floating_point T, std::floating_point Tl, std::floating_point Tr>
	inline void vecSub(T dst[dim], const Tl lho[dim], const Tr rho[dim]) {
		for(size_t i = 0; i < dim; ++i) dst[i] = lho[i] - rho[i];
	}

	template <size_t dim, std::floating_point T, std::floating_point Vl, std::floating_point Tr>
	inline void vecMul(T dst[dim], Vl lho, const Tr rho[dim]) {
		for(size_t i = 0; i < dim; ++i) dst[i] = T(lho) * T(rho[i]);
	}

	template <size_t dim, std::floating_point T, std::floating_point Vl>
	inline void vecMul(T dst[dim], Vl mul) {
		for(size_t i = 0; i < dim; ++i) dst[i] *= mul;
	}


	template <size_t dim, std::floating_point T, std::floating_point Tl, std::floating_point Tr>
	inline T vecDot(const Tl lho[dim], const Tr rho[dim]) {
		T dot = 0.0;
		for(size_t i = 0; i < dim; ++i) dot += lho[i] * rho[i];
		return dot;
	}


	template <size_t dim, std::floating_point T, std::floating_point Tv>
	inline T vecNorm(const Tv vec[dim]) {
		T norm = 0.0;
		for(size_t i = 0; i < dim; ++i) norm += T(vec[i]) * T(vec[i]);
		return std::sqrt(norm);
	}


	template <size_t dim, std::floating_point T, std::floating_point Tl, std::floating_point Tr>
	inline T vecAngle(const Tl lho[dim], const Tr rho[dim]) {
		// acos(dot(A, B) / (len(A) * len(B)))
		long double dot  = vecDot<3>(lho, rho);
		long double r    = dot / (vecNorm(lho) * vecNorm(rho));
		return std::acos(r);
	}


	template <std::floating_point T>
	void computeNormal(T dst[3], const Vertex vtx[3]) {
		long double dst_precise[3];
		long double edge_0[3]; vecSub<3>(edge_0, vtx[1].position, vtx[0].position);
		long double edge_1[3]; vecSub<3>(edge_1, vtx[2].position, vtx[0].position);
		dst_precise[0] = (edge_0[1] * edge_1[2]) - (edge_0[2] * edge_1[1]);
		dst_precise[1] = (edge_0[2] * edge_1[0]) - (edge_0[0] * edge_1[2]);
		dst_precise[2] = (edge_0[0] * edge_1[1]) - (edge_0[1] * edge_1[0]);
		vecMul<3>(dst, (long double)(1.0) / vecNorm<3, long double>(dst_precise), dst_precise);
	}


	template <std::floating_point Tl, std::floating_point Tp, std::floating_point Tt>
	void computeTangents(Tl dst_tanu[3], Tl dst_tanv[3], Tp pos0[3], Tp pos1[3], Tp pos2[3], Tt uv0[2], Tt uv1[2], Tt uv2[2]) {
		long double dst_precise[3];
		long double edge_0[3];      vecSub<3>(edge_0,     pos1, pos0);
		long double edge_1[3];      vecSub<3>(edge_1,     pos2, pos0);
		long double delta_uv_0[2];  vecSub<2>(delta_uv_0, uv1,  uv0);
		long double delta_uv_1[2];  vecSub<2>(delta_uv_1, uv2,  uv0);
		long double determinant = (delta_uv_0[0] * delta_uv_1[1]) - (delta_uv_0[1] * delta_uv_1[0]);
		long double acc0[3];
		long double acc1[3];
		{
			vecMul<3>(acc0, delta_uv_1[1], edge_0);
			vecMul<3>(acc1, delta_uv_0[1], edge_1);
			vecSub<3>(dst_precise, acc0, acc1);
			vecMul<3>(dst_precise, (long double)(1.0) / determinant, dst_precise);
			vecMul<3>(dst_tanu, (long double)(1.0) / vecNorm<3, long double>(dst_precise), dst_precise);
		} {
			vecMul<3>(acc0, delta_uv_0[0], edge_1);
			vecMul<3>(acc1, delta_uv_1[0], edge_0);
			vecSub<3>(dst_precise, acc0, acc1);
			vecMul<3>(dst_precise, (long double)(1.0) / determinant, dst_precise);
			vecMul<3>(dst_tanv, (long double)(1.0) / vecNorm<3, long double>(dst_precise), dst_precise);
		}
	}


	void computeTangents(Vertex* vtx, std::size_t n) {
		assert(n >= 3);

		for(std::size_t i0 = 0; i0 < n; ++i0) {
			long double dst_tanu[3];
			long double dst_tanv[3];
			std::size_t i1 = (i0 + 1) % n;
			std::size_t i2 = (i0 + 2) % n;

			computeTangents(
				dst_tanu,
				dst_tanv,
				vtx[i0].position, vtx[i1].position, vtx[i2].position,
				vtx[i0].texture,  vtx[i1].texture,  vtx[i2].texture );

			vecSet<3>(vtx[i0].tangent,   dst_tanu);
			vecSet<3>(vtx[i0].bitangent, dst_tanv);
		}
	}


	struct StringStorage {
		using map_t = std::unordered_map<std::string_view, u8_t>;

		std::vector<std::byte> bytes;
		map_t                  map;

		StringOffset add(std::string_view str) {
			auto iter = map.find(str);
			if(iter == map.end()) {
				auto curOff   = bytes.size();
				auto byteDiff = 3 + str.length();
				if(byteDiff > 0xffff) throw std::length_error(fmt::format(
					"String exceeds the 16-bit character limit ({} vs 65533)",
					byteDiff ));
				bytes.resize(bytes.size() + byteDiff);
				auto* cur  = bytes.data() + curOff;
				u2_t  len2 = str.length();
				memcpy(cur,   &len2,      2);
				memcpy(cur+2, str.data(), str.size());
				cur[byteDiff-1] = std::byte('\0');
				if(byteDiff % 2 != 0) bytes.push_back(std::byte(0));
				str = std::string_view(reinterpret_cast<char*>(bytes.data()) + curOff + 2, str.length());
				map.insert(std::pair<const std::string_view, u8_t>(str, u8_t(curOff)));
				return StringOffset(curOff);
			} else {
				return StringOffset(iter->second);
			}
		}
	};

}
