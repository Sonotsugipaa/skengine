#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern "C" {
	#include <unistd.h>
}
#include <posixfio_tl.hpp>

#include <cassert>
#include <cerrno>
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <bit>
#include <exception>



using String = std::string;
using StringView = std::string_view;



namespace {

	void trimFileExtension(String& str) {
		char c;
		auto size = str.size();

		auto shift = [&]() {
			if(size == 0) [[unlikely]] return false;
			-- size;
			c = str[size];
			return true;
		};

		#define SHIFT_FOR(C_) if(shift() && (c == C_)) [[likely]]
		/**/         SHIFT_FOR('g') goto end_g; else goto no_match;
		end_g:       SHIFT_FOR('n') goto end_gn; else goto no_match;
		end_gn:      SHIFT_FOR('p') goto end_gnp; else goto no_match;
		end_gnp:     SHIFT_FOR('.') goto end_gnp_dot; else goto no_match;
		end_gnp_dot: str.resize(str.size() - 4); return;
		#undef SHIFT_FOR

		no_match: /* NOP */
	}


	size_t getFileSize(posixfio::FileView file) {
		try {
			auto cur = file.lseek(0, posixfio::Whence::eCur);
			auto end = file.lseek(0, posixfio::Whence::eEnd);
			file.lseek(cur, posixfio::Whence::eSet);
			return size_t(end);
		} catch(posixfio::FileError& err) {
			if(err.errcode == ESPIPE) { /* NOP */ }
			else std::rethrow_exception(std::current_exception());
			return 0;
		}
	}


	void convert(const char* src, String& dst) {
		auto srcFile = posixfio::File::open(src, posixfio::OpenFlags::eRdonly);
		auto srcMap = srcFile.mmap(getFileSize(srcFile), posixfio::MemProtFlags::eRead, posixfio::MemMapFlags::ePrivate, 0);
		int w;
		int h;
		int d;

		auto* stbImage = stbi_load_from_memory(srcMap.get<const stbi_uc>(), srcMap.size(), &w, &h, &d, 0);
		switch(d) {
			default: break;
			case 1: dst.append(".r8u"); break;
			case 2: dst.append(".rg8u"); break;
			case 3: dst.append(".rgb8u"); break;
			case 4: dst.append(".rgba8u"); break;
		}
		auto dstFile = posixfio::File::open(dst.c_str(), posixfio::OpenFlags::eRdwr | posixfio::OpenFlags::eCreat);
		auto dstFileBuffer = posixfio::ArrayOutputBuffer<>(dstFile);

		try {
			uint64_t w64 = w;
			uint64_t h64 = h;
			static_assert((std::endian::native == std::endian::big) || (std::endian::native == std::endian::little));
			if constexpr (std::endian::native == std::endian::big) {
				w64 = std::byteswap(w64);
				h64 = std::byteswap(h64);
			}

			dstFileBuffer.writeAll(&w64, sizeof(uint64_t));
			dstFileBuffer.writeAll(&h64, sizeof(uint64_t));
			dstFileBuffer.writeAll(stbImage, size_t(w) * size_t(h) * size_t(d));
			dstFileBuffer.flush();
			dstFile.ftruncate(dstFile.lseek(0, posixfio::Whence::eCur));
		} catch(...) {
			stbi_image_free(stbImage);
			std::rethrow_exception(std::current_exception());
		}

		stbi_image_free(stbImage);
	}

}



int main(int argn, char** argv) {
	constexpr char newline = '\n';

	std::vector<const char*> args;
	assert(argn > 0);
	args.reserve(argn-1);
	for(int i = 1; i < argn; ++i) { args.push_back(argv[i]); }

	for(const auto& arg : args) {
		auto dst = String(arg);
		trimFileExtension(dst);
		dst.reserve(dst.size() + 6 /* dot + "fmat" + null character */);
		try {
			convert(arg, dst);
		} catch(posixfio::FileError err) {
			auto output = posixfio::ArrayOutputBuffer<>(STDERR_FILENO);
			auto msg = StringView(strerror(err.errcode));
			output.writeAll("Error: ", sizeof("Error: ")-1);
			output.writeAll(msg.data(), msg.size());
			output.write(&newline, 1);
		}
	}

	return EXIT_SUCCESS;
}
