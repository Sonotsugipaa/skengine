import struct


FMA_BYTE_ORDER_I       = 'little'
FMA_BYTE_ORDER_F       = 'little'
FMA_STR_ENCODING       = 'utf-8'
FMA_VTX_FORMAT         = b'f44444222222222'
FMA_MATERIAL_NULL_NAME = "default"
FMA_PRIMITIVE_RESTART  = 0xFFFFFFFF
FMA_MTL_SIZE           = 0x08
FMA_MESH_SIZE          = 0x28
FMA_BONE_SIZE          = 0x40
FMA_FACE_SIZE          = 0x18
FMA_IDX_SIZE           = 0x04
FMA_VTX_SIZE           = 0x38
FMA_PLACEHOLDER_BONE_PARENT_NAME = "null_bone_parent"

class FmaFlags:
	TRIANGLE_FAN     = 1 << 0
	TRIANGLE_LIST    = 1 << 1
	EXTERNAL_MODEL   = 1 << 2
	EXTERNAL_STRINGS = 1 << 3


def float32_bytes(f): return struct.unpack('I', struct.pack('f', f))[0].to_bytes(4, FMA_BYTE_ORDER_F)

def float64_bytes(f): return struct.unpack('Q', struct.pack('d', f))[0].to_bytes(8, FMA_BYTE_ORDER_F)

def floatn_byte_seq(n, values):
	func = float32_bytes if (n == 4) else float64_bytes
	r = bytearray()
	for value in values: r += func(value)
	return r


def write_bytes(dst, width, values, *, sign=False, endian=FMA_BYTE_ORDER_I):
	if type(dst)    != bytearray: raise TypeError("argument 'dst' must be a bytearray")
	if type(sign)   != bool:      raise TypeError("argument 'sign' must be a bool")
	if type(values) == int: values = ( values, )
	if type(values) != list and type(values) != tuple: values = ( values, )
	if width != 1 and width != 2 and width != 4 and width != 8: raise ValueError("argument 'width' must be 1, 2, 4 or 8")
	def error_n_int_nor_float(index): raise TypeError("argument 'values', index {} is neither an int nor a float".format(index))
	def append_int(dst, val): dst += val.to_bytes(width, endian, signed=sign)
	if width == 4:
		for i in range(0, len(values)):
			val_type = type(values[i])
			if   val_type == int:   append_int(dst, values[i])
			elif val_type == float: dst += float32_bytes(values[i])
			else: error_n_int_nor_float(i)
	elif width == 8:
		for i in range(0, len(values)):
			val_type = type(values[i])
			if   val_type == int:   append_int(dst, values[i])
			elif val_type == float: dst += float64_bytes(values[i])
			else: error_n_int_nor_float(i)
	else:
		for i in range(0, len(values)):
			val_type = type(values[i])
			if val_type == int: append_int(dst, values[i])
			else: raise TypeError("argument 'values', index {}".format(i))


def write_mesh_bytes(dst, mtl_idx, first_face, face_n, idx_n, center_xyz, radius):
	if   type(dst) == bytes: dst = bytearray(dst)
	elif type(dst) != bytearray: raise TypeError("argument 'dst' is neither a 'bytes' or a 'bytearray'")
	write_bytes(dst, 8, ( mtl_idx, first_face ))
	write_bytes(dst, 4, ( face_n, idx_n ))
	for v in (*center_xyz, radius): dst += float32_bytes(center_xyz)


def magic_number(version: int):
	r = bytearray(b'##fma')
	r.append((version & 0xff0000) >> (8*2))
	r.append((version & 0x00ff00) >> (8*1))
	r.append((version & 0x0000ff) >> (8*0))
	return r


#class Vertex:
#	xyz:  (float, float, float)
#	uv:   (float, float)
#	nrm:  (float, float, float)
#	tanu: (float, float, float)
#	tanv: (float, float, float)
#
#	def __init__(xyz, uv, nrm, tanu, tanv):
#		self.xyz  = xyz
#		self.uv   = uv
#		self.nrm  = nrm
#		self.tanu = tanu
#		self.tanv = tanv
#
#	def __eq__(lh, rh):
#		pow_2_8 = 2**8
#		lvalues = (*lh.xyz, *lh.uv, *lh.nrm, *lh.tanu, *lh.tanv)
#		rvalues = (*rh.xyz, *rh.uv, *rh.nrm, *rh.tanu, *rh.tanv)
#		for i in range(0, len(lvalues)):
#			if (lvalues[i] / pow_2_8) != (rvalues[i] / pow_2_8): return False
#		return True
#
#	def __lt__(lh, rh):
#		lvalues = (*lh.xyz, *lh.uv, *lh.nrm, *lh.tanu, *lh.tanv)
#		rvalues = (*rh.xyz, *rh.uv, *rh.nrm, *rh.tanu, *rh.tanv)
#		for i in range(0, len(lvalues)):
#			if lvalues[i] < rvalues[i]: return True
#		return False


class StringStorage:
	content: bytearray
	strings: dict = { }

	def __init__(self):
		self.content = bytearray()
		self.strings = dict()

	def add_string(self, seq, encoding=FMA_STR_ENCODING):
		if   type(seq) == str: seq = seq.encode(encoding)
		elif type(seq) == bytearray or type(seq) == memoryview: seq = bytes(seq)
		elif type(seq) != bytes: raise TypeError("string sequence must be a string or a binary sequence type")
		length = len(seq)
		string_ptr = len(self.content)
		if length > (2<<16): raise ValueError("string sequence is too large (> 65536 bytes)")
		write_bytes(self.content, 2, length)
		self.content += seq
		self.content += b'\00'
		self.pad(2)
		self.strings[seq] = string_ptr
		return string_ptr;

	def get_string(self, string_ptr):
		content = memoryview(self.content)
		content_len = len(content)
		if string_ptr + 2 > content_len: raise IndexError("string pointer out of bounds")
		length = int.from_bytes(content[string_ptr : string_ptr+2], FMA_BYTE_ORDER_I)
		if length + 2+1 > content_len: raise IndexError("bad string pointer: out of bounds by {}".format((length+2+1) - content_len))
		return bytes(content[string_ptr+2 : string_ptr+2 + length])

	def seek_string_ptr(self, seq, *, insert=True, encoding=FMA_STR_ENCODING):
		if   type(seq) == bytearray: seq = bytes(seq)
		elif type(seq) == str:       seq = bytes(seq, encoding)
		elif type(seq) != bytes:     raise TypeError("seq argument must be a string, a bytearray or bytes")
		if seq in self.strings: return self.strings[seq]
		if insert: return self.add_string(seq, encoding=encoding)
		return None

	def pad(self, alignment):
		padding_size = (alignment-(len(self.content)%alignment))%alignment
		for i in range(0, padding_size): self.content += b'\x00'


#if __name__ == "__main__":
#	import os
#	def getenv(key, def_value): return os.environ[key] if key in os.environ else def_value
#	dst = getenv("XDG_RUNTIME_DIR", "/tmp") + "/fma_export_test"
#
#	ss = StringStorage()
#	strings = [ ]
#	strings.append(ss.add_string("a"))
#	strings.append(ss.add_string("abcd"))
#	strings.append(ss.add_string("abcdefg"))
#	strings.append(ss.add_string("ab"))
#	ss.seek_string_ptr("abcd")
#	ss.seek_string_ptr("abc")
#	ss.pad(8)
#	with open(dst, "wb") as file:
#		file.write(magic_number(4))
#		file.write(ss.content)



# - - - - - - - - - - - - - - - - - - -
# LOGIC ABOVE
#---------------------------------------
# BLENDER-SPECIFIC CODE BELOW
# - - - - - - - - - - - - - - - - - - -



# ExportHelper is a helper class, defines filename and
# invoke() function which calls the file selector.
import math
import bpy
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, EnumProperty
from bpy.types import Operator



def compute_mesh_soi(mesh: bpy.types.Mesh):
	soi = [ 0.0, 0.0, 0.0, -0.0 ]
	soi_i = 0
	for vtx in mesh.vertices:
		soi_i_1 = soi_i + 1
		soi_mul = soi_i / soi_i_1
		soi[0] = ((soi[0] * soi_i) + vtx.co[0]) / soi_i_1
		soi[1] = ((soi[1] * soi_i) + vtx.co[1]) / soi_i_1
		soi[2] = ((soi[2] * soi_i) + vtx.co[2]) / soi_i_1
		soi_i += 1
	for vtx in mesh.vertices:
		soi_dist_sq = ((vtx.co[0]-soi[0]) ** 2) + ((vtx.co[1]-soi[1]) ** 2) + ((vtx.co[2]-soi[2]) ** 2)
		soi[3] = soi_dist_sq if (soi_dist_sq > soi[3]) else soi[3]
	soi[3] = math.sqrt(soi[3])
	return soi


def uv_of_mesh_loop(mesh, loop_index):
	print("Mesh {} has {} UV layers".format(mesh.name_full, len(mesh.uv_layers)))
	match len(mesh.uv_layers):
		case 0: return (0.0, 0.0)
		case 1: return mesh.uv_layers[0].uv[loop_index].vector
		case _: raise ValueError("Multiple UV layers are not supported by FMA")



class Material:
	material:   bpy.types.Material
	own_offset: int

	def __init__(self, material, own_offset):
		self.material   = material
		self.own_offset = own_offset



class FmaExporter(Operator, ExportHelper):
	"""Save the selected objects as a FMA model"""
	bl_idname = "fma.export" # important since its how bpy.ops.import_test.some_data is constructed
	bl_label = "Export As FMA"

	# ExportHelper mix-in class uses this.
	filename_ext = ".fma"

	filter_glob: StringProperty(
		default="*.fma",
		options={'HIDDEN'},
		maxlen=255,  # Max internal buffer length, longer would be clamped.
	)

	# List of operator properties, the attributes will be assigned
	# to the class instance from the operator settings before calling.
	use_setting: BoolProperty(
		name="Example Boolean",
		description="Example Tooltip",
		default=True,
	)

	type: EnumProperty(
		name="Example Enum",
		description="Choose between two items",
		items=(
			('OPT_A', "First Option", "Description one"),
			('OPT_B', "Second Option", "Description two"),
		),
		default='OPT_A',
	)


	class Execution:
		def invalid_geom_type(self): raise ValueError("Invalid geometry type: '{}'".format(self.geometry_type))

		def material_name(*, mesh_name):
			return mesh + ".mtl.fma"

		def material_from_mesh(self, obj_data):
			match len(obj_data.materials):
				case 0: return None
				case 1: return obj_data.materials[0]
				case _: raise ValueError("Mesh \"{}\" has {} materials, but FMA only allows one material per mesh".format(
					obj_data.name_full,
					len(obj_data.materials) ))

		def select_objects(self, ctx: bpy.context):
			self.objects = [ ]
			for obj in ctx.selected_objects:
				if obj.type == "MESH": self.objects.append(obj)

		def scan_materials(self):
			materials = dict()
			own_offset = 0
			for obj in self.objects:
				mtl = self.material_from_mesh(obj.data)
				mtl_name = mtl.name_full if (mtl != None) else FMA_MATERIAL_NULL_NAME
				if not mtl_name in materials:
					materials[mtl_name] = Material(mtl, own_offset)
					own_offset += 1
			return materials

		def scan_meshes(self):
			# Returns number of meshes
			meshes = dict()
			for obj in self.objects:
				name = obj.data.name_full
				if not name in meshes:
					meshes[name] = obj.data
			return meshes

		def count_bones(self):
			return len(self.objects)

		def count_faces(self):
			r = 0
			for mesh_name in self.meshes: r += len(self.meshes[mesh_name].polygons)
			return r

		def count_indices(self, face_count):
			match self.geometry_type:
				case "triangle-list":
					return 3 * face_count
				case "triangle-fan":
					print("Counting triangle fan indices...")
					r = 0
					for obj in self.objects: r += len(obj.data.loops)
					return r + face_count # One primitive restart for every face
				case _:
					self.invalid_geom_type()

		def populate_string_storage(self):
			print("Populating string storage...")
			r = StringStorage()
			i = 0
			r.seek_string_ptr("string_storage_placeholder_garbage")
			r.seek_string_ptr(FMA_PLACEHOLDER_BONE_PARENT_NAME)
			for obj in self.objects:
				r.seek_string_ptr(obj.name)
				r.seek_string_ptr(obj.data.name)
				material = self.material_from_mesh(obj.data)
				if material != None: r.seek_string_ptr(material.name_full)
				else:                r.seek_string_ptr(FMA_MATERIAL_NULL_NAME)
				i += 1
			r.pad(8)
			return r

		def write_materials(self, ss, wr_buffer):
			mtl_array = [ None for i in range(len(self.materials)) ]
			for mtl_name in self.materials:
				offset = self.materials[mtl_name].own_offset
				mtl_array[offset] = ss.seek_string_ptr(mtl_name)
				print("Material [{}] \"{}\" = {}".format(offset, mtl_name, mtl_array[offset]))
			write_bytes(wr_buffer, 8, mtl_array)

		def write_meshes(self, mesh_allocations, wr_buffer):
			face_counter = 0
			idx_counter = 0
			for mesh_name in self.meshes:
				class MeshAlloc:
					def __init__(self, p1, p2):
						self.mesh_offset = p1
						self.face_alloc = p2
				mesh = self.meshes[mesh_name]
				face_alloc = (
					mesh,
					face_counter,
					len(mesh.polygons) )
				face_counter += face_alloc[2]
				mesh_allocations[mesh_name] = MeshAlloc(idx_counter, face_alloc)
				mtl = self.material_from_mesh(mesh)
				if mtl == None: mtl = self.materials[FMA_MATERIAL_NULL_NAME]
				else:           mtl = self.materials[mtl.name_full]
				soi = compute_mesh_soi(mesh)
				print("Mesh \"{}\" @ {:x}: center = ({:.3}, {:.3}, {:.3}), radius = {:.3}".format(mesh_name, self.segm_mesh[0] + idx_counter, *soi))
				write_bytes(wr_buffer, 8, (
					mtl.own_offset,
					face_alloc[1] ))
				write_bytes(wr_buffer, 4, (
					face_alloc[2],
					len(mesh.loops) + face_alloc[2], # Index count: one primitive restart for every face
					*soi ))
				idx_counter += 1

		def write_bones(self, ss, mesh_allocations, wr_buffer):
			placeholder_parent_offset = ss.seek_string_ptr(FMA_PLACEHOLDER_BONE_PARENT_NAME)
			idx_counter = 0
			placeholder_rel_location = b'[        PLACEHOLDER_VEC3_x3       ]pad4'
			for obj in self.objects:
				mesh_name_offset = ss.seek_string_ptr(obj.name_full)
				mesh_offset = mesh_allocations[obj.data.name_full].mesh_offset
				print("Bone {:x}\"{}\" -> {:x}\"{}\": @ {:x}, mesh {} @ {:x}".format(
					self.segm_ss[0] + placeholder_parent_offset,  FMA_PLACEHOLDER_BONE_PARENT_NAME,
					self.segm_ss[0] + mesh_name_offset,           obj.name_full,
					self.segm_bone[0] + (idx_counter * FMA_BONE_SIZE),
					mesh_offset,
					self.segm_mesh[0] + (mesh_offset * FMA_MESH_SIZE) ))
				write_bytes(wr_buffer, 8, (
					mesh_name_offset,
					placeholder_parent_offset,
					mesh_offset ))
				wr_buffer += placeholder_rel_location
				idx_counter += 1

		def write_faces(self, mesh_allocations, meshpoly_idx_allocations, wr_buffer):
			idx_counter = 0
			for face_alloc_key in mesh_allocations:
				face_alloc = mesh_allocations[face_alloc_key].face_alloc
				mesh = face_alloc[0]
				mesh_mtl = self.material_from_mesh(mesh)
				mesh_mtl_name = mesh_mtl.name_full if (mesh_mtl != None) else FMA_MATERIAL_NULL_NAME
				print("Mesh \"{}\": {} faces @ {:x}".format(
					mesh.name_full,
					face_alloc[2],
					self.segm_face[0] + (FMA_FACE_SIZE * face_alloc[1]) ))
				if len(mesh.polygons) != face_alloc[2]: raise ValueError("Bad face count: \"{}\" should have {} faces but has {}".format(mesh.name_full, face_alloc[2], len(mesh.polygons)))
				cur_face_n = 0
				for face in mesh.polygons:
					idx_offset_rel = (4 * idx_counter)
					idx_offset = self.segm_idx[0] + idx_offset_rel
					meshpoly_idx_allocations[idx_offset] = (mesh, face.index)
					print("Mesh \"{}\" face {} @ {:x}: {} indices @ {}:{:x}, material # {}, normal ({:.3}, {:.3}, {:.3})".format(
						mesh.name_full,
						cur_face_n,
						self.segm_face[0] + (FMA_FACE_SIZE * (face_alloc[1] + cur_face_n)),
						face.loop_total,
						idx_offset_rel,
						idx_offset,
						self.materials[mesh_mtl_name].own_offset,
						*face.normal ))
					write_bytes(wr_buffer, 4, (face.loop_total, idx_counter, self.materials[mesh_mtl_name].own_offset, *face.normal))
					idx_counter += face.loop_total
					cur_face_n += 1

		def write_vertex(self, current_idx, uv, loop, vtx, vtx_idx_map, wr_buffer):
			# F4    | Position (X)
			# F4    | Position (Y)
			# F4    | Position (Z)
			# F4    | Texture Coordinate (U)
			# F4    | Texture Coordinate (V)
			# F4    | Normal (X)
			# F4    | Normal (Y)
			# F4    | Normal (Z)
			# F4    | Tangent (X)
			# F4    | Tangent (Y)
			# F4    | Tangent (Z)
			# F4    | Bi-Tangent (X)
			# F4    | Bi-Tangent (Y)
			# F4    | Bi-Tangent (Z)
			loop_bytes = bytes(floatn_byte_seq(4, (*vtx.co, *uv, *loop.normal, *loop.tangent)))
			def rotate_xz(xyz): return (xyz[0], xyz[2], -xyz[1])
			if not loop_bytes in vtx_idx_map:
				vtx_idx_map[loop_bytes] = current_idx
				write_bytes(wr_buffer, 4, (
					*rotate_xz(vtx.co),
					*uv,
					*rotate_xz(loop.normal),
					*rotate_xz(loop.tangent),
					*rotate_xz(loop.bitangent) ))

		def write_indices(self, meshpoly_idx_allocations, vtx_idx_map, wr_buffer):
			print("Writing indices...")
			primitive_restart_bytes = bytes(FMA_PRIMITIVE_RESTART.to_bytes(4, FMA_BYTE_ORDER_I))
			for leading_idx in meshpoly_idx_allocations:
				(mesh, poly_idx) = meshpoly_idx_allocations[leading_idx]
				poly = mesh.polygons[poly_idx]
				for loop_idx in range(poly.loop_start, poly.loop_start + poly.loop_total):
					loop = mesh.loops[loop_idx]
					vtx = mesh.vertices[loop.vertex_index]
					uv = uv_of_mesh_loop(mesh, loop.index)
					loop_bytes = bytes(floatn_byte_seq(4, (*vtx.co, *uv, *loop.normal, *loop.tangent)))
					idx = vtx_idx_map[loop_bytes]
					write_bytes(wr_buffer, 4, idx)
				wr_buffer += primitive_restart_bytes

		def write_model(self, file):
			# https://docs.blender.org/api/current/bpy.types.Mesh.html

			def wr_str(*values):
				for value in values: file.write(value.encode(FMA_STR_ENCODING))

			tri_fan_bit  = (1 << FmaFlags.TRIANGLE_FAN)  if (self.geometry_type == "triangle-fan")  else 0
			tri_list_bit = (1 << FmaFlags.TRIANGLE_LIST) if (self.geometry_type == "triangle-list") else 0

			# 8 bytes for each: Magic number, flags, SS p+n+l, material p+n, mesh p+n, bone p+n, face p+n, index p+n, vertex p+n
			# 2+15+1 bytes for vertex format length + data + terminator
			# 6 8-padding bytes
			header_offset = (8*17) + (2+15+1) + 6

			# Write vertices early on a separate buffer
			print("Assembling vertex table...")
			vertex_bytes = bytearray()
			vtx_idx_map = dict()
			for obj in self.objects:
				obj.data.calc_tangents()
				for loop in obj.data.loops:
					vtx = obj.data.vertices[loop.vertex_index]
					uv = uv_of_mesh_loop(obj.data, loop.index)
					self.write_vertex(len(vtx_idx_map), uv, loop, vtx, vtx_idx_map, vertex_bytes)

			print("Assembling header...")
			self.materials = self.scan_materials() ; mtl_n  = len(self.materials)
			self.meshes    = self.scan_meshes()    ; mesh_n = len(self.meshes)
			self.bone_n = self.count_bones()
			self.face_n = self.count_faces()
			self.vtx_n  = len(vtx_idx_map)
			self.idx_n  = self.count_indices(self.face_n)
			string_storage = self.populate_string_storage()
			comment = "[[[ This model file was generated on Blender by the `fma_export.py` script.  ]]]".encode(FMA_STR_ENCODING)
			self.segm_ss   = (header_offset + len(comment), len(string_storage.strings), len(string_storage.content))
			self.segm_mtl  = (self.segm_ss  [0] + (self.segm_ss  [2]                ), mtl_n)
			self.segm_mesh = (self.segm_mtl [0] + (self.segm_mtl [1] * FMA_MTL_SIZE ), mesh_n)
			self.segm_bone = (self.segm_mesh[0] + (self.segm_mesh[1] * FMA_MESH_SIZE), self.bone_n)
			self.segm_face = (self.segm_bone[0] + (self.segm_bone[1] * FMA_BONE_SIZE), self.face_n)
			self.segm_vtx  = (self.segm_face[0] + (self.segm_face[1] * FMA_FACE_SIZE), self.vtx_n)
			self.segm_idx  = (self.segm_vtx [0] + (self.segm_vtx [1] * FMA_VTX_SIZE ), self.idx_n)
			print("Comment:   {:06x} to {:06x}".format(header_offset, header_offset + len(comment)))
			print("Strings:   {:3} @ {:010x}".format(self.segm_ss[1], self.segm_ss[0]))
			print("Materials: {:3} @ {:010x}".format(mtl_n,  self.segm_mtl[0]))
			print("Meshes:    {:3} @ {:010x}".format(mesh_n, self.segm_mesh[0]))
			print("Bones:     {:3} @ {:010x}".format(self.bone_n, self.segm_bone[0]))
			print("Faces:     {:3} @ {:010x}".format(self.face_n, self.segm_face[0]))
			print("Vertices:  {:3} @ {:010x}".format(self.vtx_n,  self.segm_vtx[0]))
			print("Indices:   {:3} @ {:010x}".format(self.idx_n,  self.segm_idx[0]))

			# Write the header
			wr_buffer = magic_number(4) # Hardcoded version number
			write_bytes(wr_buffer, 8, (tri_fan_bit | tri_list_bit), endian="big")
			write_bytes(wr_buffer, 8, (*self.segm_ss, *self.segm_mtl, *self.segm_mesh, *self.segm_bone, *self.segm_face, *self.segm_idx, *self.segm_vtx))
			write_bytes(wr_buffer, 2, 15)
			wr_buffer += FMA_VTX_FORMAT
			wr_buffer += b'\x00\x00\x00\x00\x00\x00\x00'
			wr_buffer += comment ## Comment (arbitrary data)
			file.write(wr_buffer) ; wr_buffer.clear()

			file.write(string_storage.content)

			self.write_materials(string_storage, wr_buffer)

			mesh_allocations = dict()
			self.write_meshes(mesh_allocations, wr_buffer)

			self.write_bones(string_storage, mesh_allocations, wr_buffer)

			meshpoly_idx_allocations = dict()
			self.write_faces(mesh_allocations, meshpoly_idx_allocations, wr_buffer)

			file.write(wr_buffer) ; wr_buffer.clear()
			file.write(vertex_bytes) ; vertex_bytes.clear()

			self.write_indices(meshpoly_idx_allocations, vtx_idx_map, wr_buffer)

			file.write(wr_buffer) ; wr_buffer.clear()

		def __init__(self, file, ctx: bpy.context, *, geometry_type="triangle-fan"):
			self.geometry_type = geometry_type
			self.select_objects(ctx)
			self.write_model(file)
			self.result = { "FINISHED" }
			print("Done.")


	def execute(self, ctx: bpy.context):
		with open(self.filepath, 'wb') as file:
			x = FmaExporter.Execution(file, ctx)
		return x.result



# Only needed if you want to add into a dynamic menu
def menu_func_export(self, context):
	self.layout.operator(FmaExporter.bl_idname, text="Fast Memory Mappable (.fma)")


# Register and add to the "file selector" menu (required to use F3 search "Fast Memory Mappable (.fma)" for quick access).
def register():
	bpy.utils.register_class(FmaExporter)
	bpy.types.TOPBAR_MT_file_export.append(menu_func_export)


def unregister():
	bpy.utils.unregister_class(FmaExporter)
	bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)


if __name__ == "__main__":
	#try:
	#	unregister()
	#except RuntimeError as e:
	#	pass
	register()
	bpy.ops.fma.export('INVOKE_DEFAULT')
