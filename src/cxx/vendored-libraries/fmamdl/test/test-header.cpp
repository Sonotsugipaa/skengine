#include <fmamdl/fmamdl.hpp>

#include <spdlog/spdlog.h>

#include "utils.cpp"

#include <cstring>
#include <random>
#include <vector>
#include <span>



bool cmpHeaders(const fmamdl::HeaderView& l, const fmamdl::HeaderView& r) {
	constexpr const char* spdlogIntPattern = "Header members `{}` differ: `{:016x}` != `{:016x}`";
	constexpr const char* spdlogStrPattern = "Header members `{}` differ: `{}` != `{}`";

	#define CMP_MEMBER_(PT_, M_) { \
		PT_ lm = PT_(l.M_()); \
		PT_ rm = PT_(r.M_()); \
		if(lm != rm) { \
			spdlog::error(spdlogIntPattern, #M_, lm, rm); \
			return false; \
		} \
	}
	CMP_MEMBER_(uint64_t, flags)
	CMP_MEMBER_(uint64_t, stringStorageOffset)
	CMP_MEMBER_(uint64_t, stringStorageSize)
	CMP_MEMBER_(uint64_t, stringCount)
	CMP_MEMBER_(uint64_t, materialTableOffset)
	CMP_MEMBER_(uint64_t, materialCount)
	CMP_MEMBER_(uint64_t, meshTableOffset)
	CMP_MEMBER_(uint64_t, meshCount)
	CMP_MEMBER_(uint64_t, boneTableOffset)
	CMP_MEMBER_(uint64_t, boneCount)
	CMP_MEMBER_(uint64_t, faceTableOffset)
	CMP_MEMBER_(uint64_t, faceCount)
	CMP_MEMBER_(uint64_t, indexTableOffset)
	CMP_MEMBER_(uint64_t, indexCount)
	CMP_MEMBER_(uint64_t, vertexTableOffset)
	CMP_MEMBER_(uint64_t, vertexCount)
	#undef CMP_MEMBER_

	auto ll = l.getVertexLayout().toStringView();
	auto rl = r.getVertexLayout().toStringView();

	if(ll != rl) {
		spdlog::error(spdlogStrPattern, "vertexLayout", ll, rl);
		return false;
	}

	return true;
}


bool checkString(const fmamdl::HeaderView& h, fmamdl::string_offset_e pos, std::string_view str) {
	auto hstr = h.getStringView(fmamdl::StringOffset(pos));
	if(hstr.size() != str.size()) {
		auto hstrSize0 = hstr.size();
		auto hstrSize1 = str.size();
		spdlog::error("String @{0} size mismatch: d{1}x{1:x} != d{2}x{2:x}", pos, hstrSize0, hstrSize1);
		return false;
	}
	if(hstr != str) {
		spdlog::error("String @{} mismatch: \"{}\" != \"{}\"", pos, hstr, str);
		return false;
	}
	return true;
}


int test(const char* headerStr, fmamdl::HeaderView& header) {
	auto headerBytes = utils::fromBase64(headerStr);
	fmamdl::HeaderView headerCmp = { headerBytes.data(), headerBytes.size() };
	if(! cmpHeaders(header, headerCmp))        return EXIT_FAILURE;
	if(! checkString(headerCmp, 0, "str"))     return EXIT_FAILURE;
	if(! checkString(headerCmp, 6, "nikolai")) return EXIT_FAILURE;
	return EXIT_SUCCESS;
}


int main(int argc, char** argv) {
	if(argc != 1) return 1;
	(void) argv;

	const char headerStr[] =
		"IyNmbWEAAAFmbGFncwAAAPAAAAAAAAAAAQAAAAAAAAAGAAAA"
		"AAAAAOgAAAAAAAAAAAAAAAAAAAAADAAAAAAAAAEAAAAAAAAA"
		"ABAAAAAAAAACAAAAAAAAAAAgAAAAAAAAAwAAAAAAAAAAQAAA"
		"AAAAAAQAAAAAAAAAAIAAAAAAAAAFAAAAAAAAAAsAZjQ0NHMx"
		"MTExMTExMTFmNDQAFIaqqltbWyBUaGlzIGlzIHJhbmRvbSBk"
		"YXRhIGJldHdlZW4gdGFibGVzIHRoYXQgZG9lc24ndCByZWFs"
		"bHkgZG8gYW55dGhpbmcuIF1dXaqqqqqqAwBzdHIABwBuaWtv"
		"bGFpAA==";

	auto layout    = fmamdl::Layout::fromCstring("f444s111111111f44");
	auto headerLen = fmamdl::HeaderView::requiredBytesFor(layout);
	auto headerMem = std::make_unique_for_overwrite<std::byte[]>(headerLen);
	fmamdl::HeaderView header = { headerMem.get(), headerLen };
	header.flags()               = fmamdl::HeaderFlags(0x73'67616c66);
	header.stringStorageOffset() = 0x00f0;
	header.stringCount()         = 0x0001;
	header.stringStorageSize()   = 0x0006;
	header.materialTableOffset() = 0x00e8;
	header.materialCount()       = 0x0000;
	header.meshTableOffset()     = 0x0c00;
	header.meshCount()           = 0x0001;
	header.boneTableOffset()     = 0x1000;
	header.boneCount()           = 0x0002;
	header.faceTableOffset()     = 0x2000;
	header.faceCount()           = 0x0003;
	header.indexTableOffset()    = 0x4000;
	header.indexCount()          = 0x0004;
	header.vertexTableOffset()   = 0x8000;
	header.vertexCount()         = 0x0005;
	header.setVertexLayout(layout);

	return test(headerStr, header);
}
