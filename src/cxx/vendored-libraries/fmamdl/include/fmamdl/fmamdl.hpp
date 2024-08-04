#pragma once

#include <stdexcept>
#include <span>

#include "fmamdl/binary.hpp"
#include "fmamdl/layout.hpp"



// # FMA 4 model format specification
//
// Pointers are 8-wide word addresses.
// All pointers refer to the beginning of the model; if a file|stream contains
// both the header and the model, then `0` should be a pointer to the magic
// number for most sensible applications; other applications may store the model
// and the header in separate locations, and still have a spec-compliant header
// without storing unused bytes at the beginning of the model file.
// Unicorn applications may even store the header between two model tables,
// although the practicality of doing so is dubious.
//
// Despite the compartmentalized nature of the format, there is one
// standard way of arranging all the segments for a self-contained file:
// the header comes first at the beginning ("first byte" beginning),
// followed by every other segment in the order they appear in the header;
// unspecified data may be inserted between each table, preferentially
// after the header (for easy reading through a hex editor) or at the end
// of the file (in order not to put it between two other types of data).
//
// Pointers with value `UINT64_MAX` are null pointers, since
// 0 may be a valid value.
//
// Except for the header's vertex memory layout, every string is stored in
// the aptly named string storage;
// no two equal strings may exist in it, so that string comparisons can be
// reduced to just comparing the string offsets.
//
// Each mesh points to a sequence of faces;
// for this reason, faces within its table must be grouped by meshes,
// and vertex indices are grouped by faces (although they may be deduplicated, if one
// mesh is a subset of another).
//
// Each group of vertex indices (triangle list or triangle fan) has a 1:1 match
// with a face, and they are equally ordered within their tables.
// For example, in a triangle list model, indices [0-2] are part
// of the first face, indices [3-5] are part of the second face, and so on.
//
// An ellipsis in place of a type marks an unspecified amount of
// repeated elements;
// a question mark following a type indicates that the element may be absent;
// Three question marks as a type indicate that the type is known at runtime.
//
// "Cstr" is a null-terminated sequence of standard ASCII characters;
// "Chrs" is a U2 specifying the string length in bytes, followed by an
// ASCII string without a null terminator;
// "Nstr" is a string with both the string length at the beginning and
// the null terminator.
// All string types are 2-aligned, but the last string is post-padded
// so that the following non-string type is 8-aligned.
//
// "Pad2", "Pad4" and "Pad8" are post-padding types used to override
// the following datum's alignment (or simply the length of the table/type).
// Alignment rules follow those of C: a type's alignment equals the highest
// alignment of its members.
//
// "Bit8" is a bit stored in a U8, along with all the "Bit8" neighbours.
// Bit sequences are big-endian: if a type has 10 "Bit8" values, the first
// one is the least significant bit of the last byte, and the 10th value
// in the second least significant bit of the second-to-last byte.
//
//
// Header:
// U8    | Magic Number + ("##fma" + big-endian version number)
// HDFL  | Flags
// U8    | Pointer to string storage
// U8    | String count
// U8    | String storage size
// U8    | Pointer to Material Table
// U8    | Material count
// U8    | Pointer to Mesh Table
// U8    | Mesh count
// U8    | Pointer to Bone Table
// U8    | Bone count
// U8    | Pointer to Face Table
// U8    | Face count
// U8    | Pointer to Index Table
// U8    | Index count
// U8    | Pointer to Vertex Table
// U8    | Vertex count
// Nstr  | Vertex memory layout (something like "f222s11111111111")
//
// HDFL:
// Bit8  | Triangle Fan
// Bit8  | Triangle List
// Bit8  | External Model (the model tables do not share memory with the header)
// Bit8  | External Strings (the string storage does not share memory with the header)
//
// String storage:
// Nstr  | First String
// ...   | Remaining Strings
//
// Material Table:
// MAT?  | First Material
// ...   | Remaining Materials
//
// MAT:
// U8    | Name String Offset
//
// Mesh Table:
// MSH?  | First Mesh
// ...   | Remaining Meshes
//
// MSH:
// U8    | Material Index
// U8    | First Face
// U4    | Face Count
// U4    | Index Count
// F4    | Center (X)
// F4    | Center (Y)
// F4    | Center (Z)
// F4    | Radius
//
// Bone Table:
// BON   | Main Bone
// BON?  | First Secondary Bone
// ...   | Remaining Secondary Bones
//
// BON:
// U8    | Name String Offset
// U8    | Parent Name String Offset
// U8    | Mesh Offset
// F4    | Relative Position (X)
// F4    | Relative Position (Y)
// F4    | Relative Position (Z)
// F4    | Relative Rotation (yaw)
// F4    | Relative Rotation (pitch)
// F4    | Relative Rotation (roll)
// F4    | Relative Scale (width)
// F4    | Relative Scale (height)
// F4    | Relative Scale (depth)
// Pad4  |
//
// Face Table:
// FAC?  | First Face
// ...   | Remaing Faces
//
// FAC:
// U4    | Index count
// U4    | First Index index
// U4    | Material index
// F4    | Average Normal (X)
// F4    | Average Normal (Y)
// F4    | Average Normal (Z)
//
// Index Table:
// U4?   | First index
// ...   | Remaining indices
//
// Vertex Table:
// VTX?  | First Vertex
// ...   | Remaining Vertices
//
// VTX:
// F4    | Position (X)
// F4    | Position (Y)
// F4    | Position (Z)
// F4    | Texture Coordinate (U)
// F4    | Texture Coordinate (V)
// F4    | Normal (X)
// F4    | Normal (Y)
// F4    | Normal (Z)
// F4    | Tangent (X)
// F4    | Tangent (Y)
// F4    | Tangent (Z)
// F4    | Bi-Tangent (X)
// F4    | Bi-Tangent (Y)
// F4    | Bi-Tangent (Z)



namespace fmamdl {

	struct Nstr {
		const char* base;
		u2_t        size;

		std::string      toString    () const { return std::string     (base, size); }
		std::string_view toStringView() const { return std::string_view(base, size); }
		operator std::string_view() const { return toStringView(); }
	};


	class ParseError : public std::runtime_error {
	public:
		ParseError(std::string msg, std::size_t off):
			std::runtime_error(std::move(msg)),
			pe_char(off)
		{ }

		std::size_t errorOffset() const noexcept { return pe_char; }

	private:
		std::size_t pe_char;
	};

	#define MK_ERROR_SPEC_(NM_, MSG_) class NM_##Error : public ParseError { public: NM_##Error(std::size_t o): ParseError(MSG_, o) { } };
	MK_ERROR_SPEC_(UnexpectedEof,            "Unexpected end of file")
	MK_ERROR_SPEC_(OutOfBounds,              "Access out of bounds")
	MK_ERROR_SPEC_(ExpectedStringTerminator, "Expected a null-terminator")
	MK_ERROR_SPEC_(BadMagicNumber,           "Magic number mismatch")
	#undef MK_ERROR_SPEC_



	using header_flags_e = u8_t;
	enum class HeaderFlags : header_flags_e {
		eTriangleFan     = 1 << 0,
		eTriangleList    = 1 << 1,
		eExternalModel   = 1 << 2,
		eExternalStrings = 1 << 3
	};

	using string_offset_e = u8_t;
	enum class StringOffset : string_offset_e { };


	struct Material {
		StringOffset name;
	};

	struct Mesh {
		u8_t materialIndex;
		u8_t firstFace;
		u4_t faceCount;
		u4_t indexCount;
		f4_t center[3];
		f4_t radius;
	};

	struct Bone {
		StringOffset name;
		StringOffset parent;
		u8_t meshIndex;
		f4_t relPosition[3];
		f4_t relRotation[3];
		f4_t relScale[3];
		f4_t pad4;
	};

	struct Face {
		u4_t indexCount;
		u4_t firstIndex;
		u4_t materialIndex;
		f4_t normal[3];
	};

	enum class Index : u4_t { ePrimitiveRestart = 0xffffffff };

	struct Vertex {
		f4_t position[3];
		f4_t texture[2];
		f4_t normal[3];
		f4_t tangent[3];
		f4_t bitangent[3];
	};



	/// \brief An reference to a model, with utility functions to
	///        retrieve the location of all the elements it contains.
	///
	/// "Offset" functions return offsets in bytes.
	///
	/// "Ptr" functions return pointers based on the element's offset and
	/// the "data" member variable. <br>
	/// While the "Ptr" functions are more convenient to use, the "Offset"
	/// functions
	///
	class HeaderView {
	public:
		/// \brief Non-owning pointer to the model.
		///
		std::byte*  data;

		/// \brief The size of the model, in bytes.
		///
		std::size_t length;

		#define GETTER_REF_(T_, N_)   const T_& N_() const;   T_& N_() { return const_cast<T_&>(const_cast<const HeaderView&>(*this).N_()); }
		#define GETTER_PTR_(T_, N_)   const T_* N_() const;   T_* N_() { return const_cast<T_*>(const_cast<const HeaderView&>(*this).N_()); }
		#define GETTER_SPN_(T_, N_, BP_, BS_)   const std::span<const T_> N_() const { return std::span<const T_>(BP_(), size_t(BS_())); }   std::span<T_> N_() noexcept { return std::span<T_>(BP_(), size_t(BS_())); }

			GETTER_REF_(u8_t,        magicNumber)
			GETTER_REF_(HeaderFlags, flags)

			GETTER_REF_(u8_t, materialTableOffset)
			GETTER_REF_(u8_t, meshTableOffset)
			GETTER_REF_(u8_t, boneTableOffset)
			GETTER_REF_(u8_t, faceTableOffset)
			GETTER_REF_(u8_t, indexTableOffset)
			GETTER_REF_(u8_t, vertexTableOffset)
			GETTER_REF_(u8_t, stringStorageOffset)

			GETTER_REF_(u8_t, materialCount)
			GETTER_REF_(u8_t, meshCount)
			GETTER_REF_(u8_t, boneCount)
			GETTER_REF_(u8_t, faceCount)
			GETTER_REF_(u8_t, indexCount)
			GETTER_REF_(u8_t, vertexCount)
			GETTER_REF_(u8_t, stringCount)
			GETTER_REF_(u8_t, stringStorageSize)

			GETTER_PTR_(Material,  materialPtr)
			GETTER_PTR_(Mesh,      meshPtr)
			GETTER_PTR_(Bone,      bonePtr)
			GETTER_PTR_(Face,      facePtr)
			GETTER_PTR_(Index,     indexPtr)
			GETTER_PTR_(Vertex,    vertexPtr)
			GETTER_PTR_(std::byte, stringPtr)

			GETTER_SPN_(Material,  materials,     materialPtr, materialCount)
			GETTER_SPN_(Mesh,      meshes,        meshPtr,     meshCount)
			GETTER_SPN_(Bone,      bones,         bonePtr,     boneCount)
			GETTER_SPN_(Face,      faces,         facePtr,     faceCount)
			GETTER_SPN_(Index,     indices,       indexPtr,    indexCount)
			GETTER_SPN_(Vertex,    vertices,      vertexPtr,   vertexCount)
			GETTER_SPN_(std::byte, stringStorage, stringPtr,   stringStorageSize)

		#undef GETTER_SPN_
		#undef GETTER_PTR_
		#undef GETTER_REF_

		/// \returns The size in bytes of a header with the given parameters.
		///
		static std::size_t requiredBytesFor(const Layout& layout) noexcept;

		Layout getVertexLayout() const;

		/// Setting the vertex layout requires a variable-length
		/// string to be written, which means data following it
		/// may be overwritten; <br>
		/// this function is only meant for building a new header|model,
		/// NOT for modifying an existing one.
		///
		/// However, if the user knows that the new layout string has the exact
		/// same length as the overwritten one, this function may be called
		/// without size-related side effects.
		///
		void setVertexLayout(const Layout&);

		const char* getCstring(StringOffset) const;
		const std::string_view getStringView(StringOffset) const;
	};

}
