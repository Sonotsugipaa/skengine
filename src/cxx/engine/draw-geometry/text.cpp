#include <vulkan/vulkan.h> // VK_NO_PROTOTYPES, defined in other headers, makes it necessary to include this one first

#include "gui.hpp"

#include <fmt/format.h>



namespace SKENGINE_NAME_NS {
inline namespace geom {

	inline namespace impl {

		constexpr const char* ft_error_string_or_unknown(FT_Error e) {
			auto r = FT_Error_String(e);
			if(r == nullptr) r = "unknown";
			return r;
		}

	}


	FontError::FontError(std::string ctx, FT_Error e):
		FontError(fmt::format("{}: {}", ctx, ft_error_string_or_unknown(e)))
	{ }


	FontFace FontFace::fromFile(FT_Library ft, const char* path) {
		FT_Face r;
		auto error = FT_New_Face(ft, path, 0, &r);
		if(error) throw FontError(fmt::format("failed to load font face \"{}\"", path), error);
		return FontFace(r);
	}


	FontFace::~FontFace() {
		if(font_face.value != nullptr) {
			auto error = FT_Done_Face(font_face.value);
			if(error) throw FontError(fmt::format("failed to unload a font face"), error); // Implementation defined: may or may not unwind the stack, but will always std::terminate
			font_face = nullptr;
		}
	}


	GlyphBitmap FontFace::getGlyphBitmap(codepoint_t c, unsigned pixelHeight) {
		auto error = FT_Set_Pixel_Sizes(font_face, 0, pixelHeight);
		if(error) throw FontError(fmt::format("failed to set font face size"), error);

		auto index = FT_Get_Char_Index(font_face, c);
		if(index == 0) index = FT_Get_Char_Index(font_face, '?');
		if(index == 0) [[unlikely]] { throw std::runtime_error("failed to map required character '?'"); }

		error = FT_Load_Glyph(font_face, index, FT_LOAD_DEFAULT);
		if(error) throw FontError(fmt::format("failed to load glyph 0x{:x}", uint_least32_t(c)), error);
		error = FT_Render_Glyph(font_face.value->glyph, FT_RENDER_MODE_NORMAL);
		if(error) throw FontError(fmt::format("failed to render glyph 0x{:x}", uint_least32_t(c)), error);
		assert(font_face.value->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY);

		GlyphBitmap ins;
		ins.xBaseline = font_face.value->glyph->bitmap_left;
		ins.yBaseline = font_face.value->glyph->bitmap_top;
		ins.width     = font_face.value->glyph->bitmap.width;
		ins.height    = font_face.value->glyph->bitmap.rows;
		auto byteCount = ins.byteCount();
		ins.bytes = std::make_unique_for_overwrite<std::byte[]>(byteCount);
		memcpy(ins.bytes.get(), font_face.value->glyph->bitmap.buffer, byteCount);
		///// This unused code block may be useful once I find out that bitmaps are padded in some way
		// for(unsigned i = 0; i < ins.height; ++i) {
		// 	auto rowCursor = i * ins.width;
		// 	memcpy(
		// 		ins.bytes.get() + rowCursor,
		// 		font_face.value->glyph->bitmap.buffer + rowCursor,
		// 		ins.width );
		// }
		return ins;
	}


	TextLine TextLine::create(VmaAllocator vma, FontFace& face, const std::vector<codepoint_t>& str) {

	}


	void TextLine::destroy(VmaAllocator vma, TextLine& ln) {}

}}
