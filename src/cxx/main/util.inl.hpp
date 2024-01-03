#pragma once

#include <vector>
#include <string>
#include <posixfio_tl.hpp>

#include <engine/engine.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>



#ifdef TRANSF
	#error "Conflict on macro 'TRANSF'"
#endif
#define TRANSF(LEFT_, TOP_, DEPTH_, W_, H_) glm::scale(glm::translate(mat1, { float(LEFT_), float(TOP_), float(DEPTH_) }), { float(W_), float(H_), 1.0f })



inline namespace main_ns {

	constexpr glm::mat4 mat1 = glm::mat4(1.0f);


	const auto rectShape = std::make_shared<ske::Shape>(
		std::vector<ske::PolyVertex> {
			{{ -1.0f, -1.0f,  0.0f }},
			{{ -1.0f, +1.0f,  0.0f }},
			{{ +1.0f, +1.0f,  0.0f }},
			{{ +1.0f, -1.0f,  0.0f }} });


	std::vector<std::string> readObjectNameList(posixfio::FileView file) {
		auto fileBuf = posixfio::ArrayInputBuffer<>(file);

		std::string strBuf;
		auto rdln = [&]() {
			strBuf.clear();
			strBuf.reserve(64);
			char c;
			bool eol = false;
			ssize_t rd = fileBuf.read(&c, 1);
			while(rd > 0) {
				if(c == '\n') [[unlikely]] {
					eol = true;
					rd = 0;
				} else {
					strBuf.push_back(c);
					rd = fileBuf.read(&c, 1);
				}
			}
			return eol;
		};

		std::vector<std::string> r;
		bool eof    = ! rdln();
		bool nEmpty = ! strBuf.empty();
		while((! eof) || nEmpty) {
			if(nEmpty) {
				r.push_back(std::move(strBuf));
			}
			eof    = ! rdln();
			nEmpty = ! strBuf.empty();
		}
		return r;
	}


	ske::ShapeSet makeCrossShapeSet(float strokeWidth, float strokeHeight, float depth, const glm::vec4& color) {
		auto vbar = ske::ShapeReference(rectShape, color, TRANSF(0.0f, 0.0f, depth, 1.0f, strokeHeight));
		auto hbar = ske::ShapeReference(rectShape, color, TRANSF(0.0f, 0.0f, depth, strokeWidth, 1.0f));
		return ske::ShapeSet({ vbar, hbar });
	}


	ske::ShapeSet makeFrameShapeSet(float strokeWidth, float strokeHeight, float depth, const glm::vec4& color) {
		auto hbar0 = ske::ShapeReference(rectShape, color, TRANSF(0.0f, -1.0f, depth, 1.0f, strokeHeight));
		auto hbar1 = ske::ShapeReference(rectShape, color, TRANSF(0.0f, +1.0f, depth, 1.0f, strokeHeight));
		auto vbar0 = ske::ShapeReference(rectShape, color, TRANSF(-1.0f, 0.0f, depth, strokeWidth, 1.0f));
		auto vbar1 = ske::ShapeReference(rectShape, color, TRANSF(+1.0f, 0.0f, depth, strokeWidth, 1.0f));
		return ske::ShapeSet({ vbar0, vbar1, hbar0, hbar1 });
	}

}



#undef TRANSF
