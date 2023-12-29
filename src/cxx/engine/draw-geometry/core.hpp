#pragma once

#include <misc-util.tpp>

#include <skengine_fwd.hpp>

#define VK_NO_PROTOTYPES // Don't need those in the header
#include <vulkan/vulkan.h>

#include <vk-util/memory.hpp>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <vector>
#include <memory>

#include <freetype2/ft2build.h>
#include FT_FREETYPE_H



namespace SKENGINE_NAME_NS {
inline namespace geom {

	using codepoint_t = char32_t;


	class FontError : public std::runtime_error {
	public:
		template <typename... Args>
		FontError(Args... args): std::runtime_error::runtime_error(args...) { }

		FontError(std::string, FT_Error);
		FontError(const char* c, FT_Error e): FontError(std::string(c), e) { }
	};


	enum class PipelineType { ePoly, eText };


	struct PolyVertex {
		alignas(4 * sizeof(float)) glm::vec3 position;
	};

	struct TextVertex {
		alignas(4 * sizeof(float)) glm::vec3 position;
		alignas(2 * sizeof(float)) glm::vec2 uv;
	};

	union Vertex { PolyVertex poly; TextVertex text; };

	struct Instance {
		alignas(4 * sizeof(float)) glm::vec4 color;
		alignas(4 * sizeof(float)) glm::mat4 transform;
	};


	struct PipelineSetCreateInfo {
		VkRenderPass    renderPass;
		uint32_t        subpass;
		VkPipelineCache pipelineCache;
		VkDescriptorSetLayout polyDsetLayout;
		VkDescriptorSetLayout textDsetLayout;
	};


	struct PipelineSet {
		VkPipelineLayout layout;
		VkPipeline polyLine;
		VkPipeline polyFill;
		VkPipeline text;

		static PipelineSet create  (VkDevice, const PipelineSetCreateInfo&);
		static void        destroy (VkDevice, PipelineSet&) noexcept;
	};


	struct RectangleBounds {
		float offset[2];
		float size[2];
	};

	struct CharBounds {
		float offset[2];
		float size[2];
		float baseline[2];
	};


	class Shape : private std::vector<Vertex> {
	public:
		using Sptr = std::shared_ptr<const Shape>;

		Shape() = default;
		Shape(const std::vector<PolyVertex>&);
		Shape(const std::vector<TextVertex>&);
		Shape(std::vector<Vertex>, PipelineType vtxType);

		const std::vector<Vertex>& vertices() const noexcept { return *this; }
		PipelineType vtxType() const noexcept { return shape_type; }

	private:
		PipelineType shape_type;
	};


	struct ShapeReference {
		Shape::Sptr shape;
		glm::vec4   color;
		glm::mat4   transform;

		ShapeReference() = default;

		ShapeReference(Shape::Sptr shape, glm::vec4 color, glm::mat4 transform):
			shape(std::move(shape)),
			color(color),
			transform(transform)
		{ }
	};


	class ShapeSet : public std::vector<ShapeReference> {
	public:
		using vector::vector;
	};


	class DrawableShapeInstance {
	public:
		DrawableShapeInstance() = default;
		DrawableShapeInstance(Shape::Sptr s, geom::Instance i): dr_shape_i_shape(std::move(s)), dr_shape_i_instance(std::move(i)) { }

		const Shape& shape() const { return *dr_shape_i_shape.get(); }
		void setShape(Shape::Sptr newShape) { dr_shape_i_shape = std::move(newShape); }

		/* */ geom::Instance& instance()       { return dr_shape_i_instance; }
		const geom::Instance& instance() const { return dr_shape_i_instance; }

	private:
		Shape::Sptr    dr_shape_i_shape;
		geom::Instance dr_shape_i_instance;
	};


	struct ModifiableShapeInstance {
		glm::vec4& color;
		glm::mat4& transform;
	};


	class DrawableShapeSet {
	public:
		DrawableShapeSet(): dr_shape_set_state(0b000) { }

		static DrawableShapeSet create(VmaAllocator, std::vector<DrawableShapeInstance>);
		static DrawableShapeSet create(VmaAllocator, ShapeSet);
		static void     destroy(VmaAllocator, DrawableShapeSet&) noexcept;

		void forceNextCommit() noexcept;
		void commitVkBuffers(VmaAllocator vma) { if(0 == (dr_shape_set_state & 0b001)) [[unlikely]] dr_shape_set_commitBuffers(vma); }

		ModifiableShapeInstance modifyShapeInstance(unsigned index) noexcept;

		operator bool()  { return State(dr_shape_set_state) != State::eUnitialized; }
		bool operator!() { return State(dr_shape_set_state) == State::eUnitialized; }

		vkutil::Buffer& vertexBuffer       () noexcept { return dr_shape_set_vtxBuffer; }
		vkutil::Buffer& drawIndirectBuffer () noexcept { return dr_shape_set_drawBuffer; }
		unsigned instanceCount () const noexcept { return dr_shape_set_instanceCount; }
		unsigned vertexCount   () const noexcept { return dr_shape_set_vertexCount; }
		unsigned drawCmdCount  () const noexcept { return dr_shape_set_drawCount; }

	private:
		enum class State : unsigned {
			// 100 & shape set is initialized
			// 010 & do destroy buffers
			// 001 & do not flush buffers
			eUnitialized = 0b000,
			eEmpty       = 0b101,
			eOutOfDate   = 0b110,
			eUpToDate    = 0b111
		};

		DrawableShapeSet(State state): dr_shape_set_state(unsigned(state)) { }

		void dr_shape_set_commitBuffers(VmaAllocator);

		std::vector<DrawableShapeInstance> dr_shape_set_shapes;
		vkutil::Buffer dr_shape_set_vtxBuffer;  // [  instances  ][  vertices          ]
		vkutil::Buffer dr_shape_set_drawBuffer; // [  draw_cmds         ]
		void*    dr_shape_set_vtxPtr;
		unsigned dr_shape_set_instanceCount;
		unsigned dr_shape_set_vertexCount;
		unsigned dr_shape_set_drawCount;
		unsigned dr_shape_set_state;
	};


	struct GlyphBitmap {
		unsigned xBaseline;
		unsigned yBaseline;
		unsigned width;
		unsigned height;
		std::unique_ptr<std::byte[]> bytes;

		GlyphBitmap() = default;

		GlyphBitmap(const GlyphBitmap& cp):
			xBaseline (cp.xBaseline),
			yBaseline (cp.yBaseline),
			width     (cp.width),
			height    (cp.height),
			bytes(std::make_unique_for_overwrite<std::byte[]>(cp.byteCount()))
		{
			memcpy(bytes.get(), cp.bytes.get(), byteCount());
		}

		GlyphBitmap(GlyphBitmap&&) = default;

		GlyphBitmap& operator=(const GlyphBitmap& cp) {
			this->~GlyphBitmap();
			return * new (this) GlyphBitmap(cp);
		}

		GlyphBitmap& operator=(GlyphBitmap&&) = default;

		unsigned byteCount() const noexcept { return width * height; }
	};


	class FontFace {
	public:
		static FontFace fromFile(FT_Library, const char* path);

		FontFace(): font_face(nullptr) { }
		~FontFace();

		FontFace(FontFace&& mv) = default;
		FontFace& operator=(FontFace&& mv) = default;

		GlyphBitmap getGlyphBitmap(codepoint_t, unsigned pixelHeight);

		operator FT_Face() const noexcept { return font_face; }

	private:
		FontFace(FT_Face face): font_face(face) { }

		util::Moveable<FT_Face> font_face;
	};


	/// \brief Groups multiple glyphs into a single texture,
	///        and updates it when necessary.
	///
	class TextCache {
	public:

	private:
		std::unordered_map<codepoint_t, CharBounds> txtcache_charmap;
		util::Moveable<VmaAllocator> txtcache_vma;
		VkDescriptorPool txtcache_dpool;
		VkDescriptorSet  txtcache_dset;
		VkImage          txtcache_image;
		VkImageView      txtcache_imageView;
		VkSampler        txtcache_sampler;
		unsigned short   txtcache_pixelHeight;
	};


	class TextLine {
	public:
		static TextLine create(VmaAllocator, FontFace&, const std::vector<codepoint_t>&);
		static void destroy(VmaAllocator, TextLine&);

		template <typename T, size_t N>
		static TextLine create(VmaAllocator vma, FontFace& f, const T (&s)[N]) { auto v = std::vector<codepoint_t>(s, s+N); return create(vma, f, v); }

	private:
		std::vector<VkDrawIndexedIndirectCommand> text_ln_drawCmds;
		vkutil::Image text_ln_image;
		uint_fast32_t text_ln_updateIndex;
	};

}}
