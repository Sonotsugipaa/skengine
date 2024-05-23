#include "ui_renderer.hpp"

#include "engine.hpp"



namespace SKENGINE_NAME_NS {

	FontFace UiStorage::createFontFace() {
		return FontFace::fromFile(freetype, false, fontFilePath);
	}


	TextCache& UiStorage::getTextCache(Engine& e, unsigned short size) {
		using Caches = decltype(textCaches);
		auto& caches = textCaches;
		auto dev = [&]() { VmaAllocatorInfo r; vmaGetAllocatorInfo(vma, &r); return r.device; } ();
		auto found = caches.find(size);
		if(found == caches.end()) {
			found = caches.insert(Caches::value_type(size, TextCache(
				dev, vma,
				e.getImageDsetLayout(),
				std::make_shared<FontFace>(createFontFace()),
				size ))).first;
		}
		return found->second;
	}

}
