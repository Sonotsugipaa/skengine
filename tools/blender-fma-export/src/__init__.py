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
import os
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
	match len(mesh.uv_layers):
		case 0:
			return (0.0, 0.0)
		case _:
			r = mesh.uv_layers.active.uv[loop_index].vector
			return (r[0], 1.0 - r[1])


def material_from_mesh(obj_data):
	match len(obj_data.materials):
		case 0: return None
		case 1: return obj_data.materials[0]
		case _: raise ValueError("Mesh \"{}\" has {} materials, but FMA only allows one material per mesh".format(
			obj_data.name_full,
			len(obj_data.materials) ))


def merge_dir_basename(dir, basename):
	dir_end = len(dir)
	while (dir_end > 0) and ((dir[dir_end-1] == '/') or (dir[dir_end-1] == '\\')):
		dir_end -= 1
	if dir_end != len(dir): dir = dir[:dir_end]
	return basename if (len(dir) == 0) else "{}/{}".format(dir, basename)


def compute_tangent_pair(vtx0, vtx1, vtx2, uv0, uv1, uv2):
	edge_0 = vsub3m3(vtx1.co, vtx0.co)
	edge_1 = vsub3m3(vtx2.co, vtx0.co)
	d_uv_0 = vsub2m2(uv1, uv0)
	d_uv_1 = vsub2m2(uv2, uv0)
	inv_det = (d_uv_0[0] * d_uv_1[1]) - (d_uv_0[1] * d_uv_1[0])
	inv_det = 1.0 / inv_det if (inv_det != 0.0) else 1.0
	acc0 = vmul1x3(d_uv_1[1], edge_0)
	acc1 = vmul1x3(d_uv_0[1], edge_1)
	tanu = vsub3m3(acc0, acc1)
	tanu = vmul1x3(inv_det, tanu)
	acc0 = vmul1x3(d_uv_0[0], edge_1)
	acc1 = vmul1x3(d_uv_1[0], edge_0)
	tanv = vsub3m3(acc0, acc1)
	tanv = vmul1x3(inv_det, tanv)
	return (tanu, tanv)


def compute_vertices(meshes):
	class VertexMapEntry:
		pass
	vtx_list = list()
	vtx_map = dict()
	def add_vertex(vtx_list, vtx_map, uv, loop, vtx, tangents):
		# Same-identity vertices need to have their tangents averaged out
		vtx_entry = (
			rotate_xz(vtx.co),
			uv,
			rotate_xz(loop.normal),
			rotate_xz(tangents[0]),
			rotate_xz(tangents[1]) )
		vtx_id = vertex_identity(vtx.co, uv, loop.normal)
		if not vtx_id in vtx_map:
			ins = VertexMapEntry()
			ins.index = len(vtx_list)
			ins.ref_count = 1
			vtx_map[vtx_id] = ins
			vtx_list.append(vtx_entry)
		else:
			# Average the vertex normals
			edit_map_entry = vtx_map[vtx_id]
			edit_vtx = vtx_list[edit_map_entry.index]
			ref_counts = (edit_map_entry.ref_count, edit_map_entry.ref_count + 1)
			edit_map_entry.ref_count = ref_counts[1]
			inv_ref_count_1 = 1.0 / ref_counts[1]
			new_tanu = vmul1x3(ref_counts[0], edit_vtx[3])
			new_tanu = vadd3p3(new_tanu, tangents[0])
			new_tanu = vmul1x3(inv_ref_count_1, new_tanu)
			new_tanv = vmul1x3(ref_counts[0], edit_vtx[4])
			new_tanv = vadd3p3(new_tanv, tangents[1])
			new_tanv = vmul1x3(inv_ref_count_1, new_tanv)
			vtx_list[edit_map_entry.index] = (*edit_vtx[:3], vnormalize3(new_tanu), vnormalize3(new_tanv))
	for i in range(0, len(vtx_list)):
		vtx = vtx_list[i]
		vtx_list[i] = (vtx[0], vtx[1], vtx[2], vnormalize3(vtx[3]), vnormalize3(vtx[4]))
	for mesh_name in meshes:
		mesh = meshes[mesh_name]
		mesh_loops = mesh.data.loops
		for poly in mesh.data.polygons:
			loops_range = range(0, poly.loop_total)
			for i in loops_range:
				tri_indices = (
					poly.loop_start + i,
					poly.loop_start + (i+1) % poly.loop_total,
					poly.loop_start + (i+2) % poly.loop_total)
				loop = mesh_loops[tri_indices[0]]
				uv =       [ uv_of_mesh_loop(mesh.data, i)             for i in tri_indices ]
				vertices = [ mesh.data.vertices[mesh_loops[i].vertex_index] for i in tri_indices ]
				tangents = compute_tangent_pair(*vertices, *uv)
				add_vertex(vtx_list, vtx_map, uv[0], loop, vertices[0], tangents)
	vtx_bytes = bytearray()
	for vtx in vtx_list:
		vtx_bytes += floatn_byte_seq(4, (*vtx[0], *vtx[1], *vtx[2], *vtx[3], *vtx[4]))
	return (vtx_bytes, vtx_map)


def mk_material_filename(*, name): return name + ".mtl.fma"

def rotate_xz(xyz): return (xyz[0], xyz[2], -xyz[1])

def vertex_identity(pos, uv, normal): return bytes(floatn_byte_seq(4, (*pos, *uv, *normal)))

def vnorm3(v): return math.sqrt((v[0]**2) + (v[1]**2) + (v[2]**2))
def vnormalize3(v): n = vnorm3(v); return (v[0] / n, v[1] / n, v[2] / n) if (n != 0.0) else (1.0, 0.0, 0.0)
def vdot3(lho, rho): return (lho[0]*rho[0]) + (lho[1]*rho[1]) + (lho[2]*rho[2])
def vadd3p3(lho, rho): return (lho[0]+rho[0], lho[1]+rho[1], lho[2]+rho[2])
def vsub2m2(lho, rho): return (lho[0]-rho[0], lho[1]-rho[1])
def vsub3m3(lho, rho): return (lho[0]-rho[0], lho[1]-rho[1], lho[2]-rho[2])
def vmul3x3(lho, rho): return (lho[0]*rho[0], lho[1]*rho[1], lho[2]*rho[2])
def vmul1x3(lho, rho): return (   lho*rho[0],    lho*rho[1],    lho*rho[2])


class Material:
	material:   bpy.types.Material
	own_offset: int

	def __init__(self, material, own_offset):
		self.material   = material
		self.own_offset = own_offset



class MaterialOrColor:
	def __init__(self, *, color = None, name = None):
		if color != None:
			self.isColor = True
			self.value = (
				int(color[0] * 255) << 24 |
				int(color[1] * 255) << 16 |
				int(color[2] * 255) <<  8 |
				int(color[3] * 255)       )
		else:
			self.isColor = False
			self.value = name



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

	opt_gen_model: BoolProperty(
		name="Export model file",
		description="Enable to generate the \"*.fma\" file",
		default=True,
	)

	opt_gen_materials: BoolProperty(
		name="Export empty material files",
		description="Enable to generate the \"*.mtl.fma\" files",
		default=False,
	)


	class ModelExecution:
		def invalid_geom_type(self): raise ValueError("Invalid geometry type: '{}'".format(self.geometry_type))

		def select_objects(self, ctx):
			self.objects = [ ]
			for obj in ctx.selected_objects:
				if obj.type == "MESH": self.objects.append(obj)

		def scan_materials(self):
			self.materials = dict()
			own_offset = 0
			for obj in self.objects:
				mtl = material_from_mesh(obj.data)
				mtl_name = mtl.name_full if (mtl != None) else FMA_MATERIAL_NULL_NAME
				if not mtl_name in self.materials:
					self.materials[mtl_name] = Material(mtl, own_offset)
					own_offset += 1

		def scan_meshes(self):
			# Returns number of meshes
			self.object_mesh_name_map = dict()
			self.meshes = dict()
			for obj in self.objects:
				det_ltz = obj.matrix_world.determinant() < 0
				name    = "{}:det_{}tz".format(obj.data.name_full, "l" if det_ltz else "g")
				class MeshInfo:
					data:    bpy.types.Mesh
					det_ltz: bool
				self.object_mesh_name_map[obj.name_full] = name
				if not name in self.meshes:
					mi = MeshInfo
					mi.data    = obj.data
					mi.det_ltz = det_ltz
					self.meshes[name] = mi

		def count_bones(self):
			return len(self.objects)

		def count_faces(self):
			r = 0
			for mesh_name in self.meshes: r += len(self.meshes[mesh_name].data.polygons)
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
			print("Populating model string storage...")
			r = StringStorage()
			r.seek_string_ptr("string_storage_placeholder_garbage")
			r.seek_string_ptr(FMA_PLACEHOLDER_BONE_PARENT_NAME)
			for obj in self.objects:
				r.seek_string_ptr(obj.name)
				r.seek_string_ptr(obj.data.name)
				material = material_from_mesh(obj.data)
				if material != None: r.seek_string_ptr(mk_material_filename(name=material.name_full))
				else:                r.seek_string_ptr(mk_material_filename(name=FMA_MATERIAL_NULL_NAME))
			r.pad(8)
			return r

		def write_materials(self, ss, wr_buffer):
			mtl_array = [ None for i in range(len(self.materials)) ]
			for mtl_name in self.materials:
				mtl_filename = mk_material_filename(name=mtl_name)
				offset = self.materials[mtl_name].own_offset
				mtl_array[offset] = ss.seek_string_ptr(mtl_filename)
				print("Material [{}] \"{}\" = {}".format(offset, mtl_filename, mtl_array[offset]))
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
					len(mesh.data.polygons) )
				face_counter += face_alloc[2]
				mesh_allocations[mesh_name] = MeshAlloc(idx_counter, face_alloc)
				mtl = material_from_mesh(mesh.data)
				if mtl == None: mtl = self.materials[FMA_MATERIAL_NULL_NAME]
				else:           mtl = self.materials[mtl.name_full]
				soi = compute_mesh_soi(mesh.data)
				print("Mesh \"{}\" @ {:x}: center = ({:.3}, {:.3}, {:.3}), radius = {:.3}".format(
					mesh_name,
					self.segm_mesh[0] + idx_counter,
					*soi ))
				write_bytes(wr_buffer, 8, (
					mtl.own_offset,
					face_alloc[1] ))
				write_bytes(wr_buffer, 4, (
					face_alloc[2],
					len(mesh.data.loops) + face_alloc[2], # Index count: one primitive restart for every face
					*soi ))
				idx_counter += 1

		def write_bones(self, ss, mesh_allocations, wr_buffer):
			placeholder_parent_offset = ss.seek_string_ptr(FMA_PLACEHOLDER_BONE_PARENT_NAME)
			idx_counter = 0
			pad4 = b'pad4'
			for obj in self.objects:
				bone_name_offset = ss.seek_string_ptr(obj.name_full)
				mesh_offset = mesh_allocations[self.object_mesh_name_map[obj.name_full]].mesh_offset
				print("Bone {:x}\"{}\" -> {:x}\"{}\": @ {:x}, mesh {} @ {:x}".format(
					self.segm_ss[0] + placeholder_parent_offset,  FMA_PLACEHOLDER_BONE_PARENT_NAME,
					self.segm_ss[0] + bone_name_offset,           obj.name_full,
					self.segm_bone[0] + (idx_counter * FMA_BONE_SIZE),
					mesh_offset,
					self.segm_mesh[0] + (mesh_offset * FMA_MESH_SIZE) ))
				obj_pos = obj.location;       obj_pos = (+obj_pos[0], +obj_pos[2], -obj_pos[1])
				obj_rot = obj.rotation_euler; obj_rot = (+obj_rot[0], +obj_rot[2], +obj_rot[1])
				obj_scl = obj.scale;          obj_scl = (+obj_scl[0], +obj_scl[2], +obj_scl[1])
				print("     position ({:5.3} {:5.3} {:5.3})".format(*obj_pos))
				print("     rotation ({:5.3} {:5.3} {:5.3})".format(*obj_rot))
				print("     scale    ({:5.3} {:5.3} {:5.3})".format(*obj_scl))
				write_bytes(wr_buffer, 8, (
					bone_name_offset,
					placeholder_parent_offset,
					mesh_offset ))
				write_bytes(wr_buffer, 4, (obj_pos[0], obj_pos[1], obj_pos[2]), sign=True)
				write_bytes(wr_buffer, 4, (obj_rot[0], obj_rot[1], obj_rot[2]), sign=True)
				write_bytes(wr_buffer, 4, (obj_scl[0], obj_scl[1], obj_scl[2]), sign=True)
				wr_buffer += pad4
				idx_counter += 1

		def write_faces(self, mesh_allocations, meshpoly_indices, wr_buffer):
			idx_counter = 0
			for face_alloc_key in mesh_allocations:
				face_alloc = mesh_allocations[face_alloc_key].face_alloc
				mesh = face_alloc[0]
				mesh_mtl = material_from_mesh(mesh.data)
				mesh_mtl_name = mesh_mtl.name_full if (mesh_mtl != None) else FMA_MATERIAL_NULL_NAME
				print("Mesh \"{}\": {} faces @ {:x}".format(
					mesh.data.name_full,
					face_alloc[2],
					self.segm_face[0] + (FMA_FACE_SIZE * face_alloc[1]) ))
				if len(mesh.data.polygons) != face_alloc[2]: raise ValueError("Bad face count: \"{}\" should have {} faces but has {}".format(mesh.data.name_full, face_alloc[2], len(mesh.data.polygons)))
				cur_face_n = 0
				for face in mesh.data.polygons:
					meshpoly_indices.append( (mesh, face.index) )
					write_bytes(wr_buffer, 4, (face.loop_total, idx_counter, self.materials[mesh_mtl_name].own_offset, *face.normal))
					idx_counter += face.loop_total + 1
					cur_face_n += 1

		def write_indices(self, meshpoly_indices, vtx_map, wr_buffer):
			print("Writing indices...")
			primitive_restart_bytes = FMA_PRIMITIVE_RESTART.to_bytes(4, FMA_BYTE_ORDER_I)
			indices_written = 0
			def write_mesh_indices(loop_idx, mesh, vtx_map, wr_buffer):
				loop = mesh.data.loops[loop_idx]
				vtx = mesh.data.vertices[loop.vertex_index]
				uv = uv_of_mesh_loop(mesh.data, loop.index)
				loop_bytes = vertex_identity(vtx.co, uv, loop.normal)
				idx = vtx_map[loop_bytes].index
				write_bytes(wr_buffer, 4, idx)
			for idx_alloc in meshpoly_indices:
				(mesh, poly_idx) = idx_alloc
				poly = mesh.data.polygons[poly_idx]
				if not mesh.det_ltz:
					for loop_idx in range(poly.loop_start, poly.loop_start + poly.loop_total):
						write_mesh_indices(loop_idx, mesh, vtx_map, wr_buffer)
				else:
					for loop_idx in range(poly.loop_start + poly.loop_total - 1, poly.loop_start - 1, -1):
						write_mesh_indices(loop_idx, mesh, vtx_map, wr_buffer)
				wr_buffer += primitive_restart_bytes
				indices_written += poly.loop_total + 1
			assert indices_written == self.segm_idx[1]

		def write_model(self, file):
			tri_fan_bit  = (1 << FmaFlags.TRIANGLE_FAN)  if (self.geometry_type == "triangle-fan")  else 0
			tri_list_bit = (1 << FmaFlags.TRIANGLE_LIST) if (self.geometry_type == "triangle-list") else 0

			# 8 bytes for each: Magic number, flags, SS p+n+l, material p+n, mesh p+n, bone p+n, face p+n, index p+n, vertex p+n
			# 2+15+1 bytes for vertex format length + data + terminator
			# 6 8-padding bytes
			header_offset = (8*17) + (2+15+1) + 6

			print("Assembling header...")
			self.scan_materials() ; mtl_n  = len(self.materials)
			self.scan_meshes()    ; mesh_n = len(self.meshes)
			(vertex_bytes, vtx_map) = compute_vertices(self.meshes) # Needs to happen after scan_meshes but before vtx_map is needed
			self.bone_n = self.count_bones()
			self.face_n = self.count_faces()
			self.vtx_n  = len(vtx_map)
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

			meshpoly_indices = list()
			self.write_faces(mesh_allocations, meshpoly_indices, wr_buffer)

			file.write(wr_buffer) ; wr_buffer.clear()
			file.write(vertex_bytes) ; vertex_bytes.clear()

			self.write_indices(meshpoly_indices, vtx_map, wr_buffer)

			file.write(wr_buffer) ; wr_buffer.clear()

		def __init__(self, file, ctx, *, geometry_type="triangle-fan"):
			self.geometry_type = geometry_type
			self.select_objects(ctx)
			self.write_model(file)


	class MaterialExecution:
		def select_materials(self, ctx):
			self.materials = dict()
			for obj in ctx.selected_objects:
				if obj.type == "MESH":
					mtl = material_from_mesh(obj.data)
					mtl_name = mtl.name_full if (mtl != None) else FMA_MATERIAL_NULL_NAME
					if not mtl_name in self.materials:
						mtl_name = mk_material_filename(name=mtl_name)
						self.materials[mtl_name] = mtl
			if None in self.materials:
				self.materials[FMA_MATERIAL_NULL_NAME] = bpy.data.materials.new(name=mtl_name)

		def populate_string_storage(self):
			print("Populating material string storage...")
			r = StringStorage()
			r.pad(8)
			return r

		def write_material(self, file, material):
			# https://docs.blender.org/api/current/bpy.types.Material.html

			# Hardcoded flags
			mtl_flags = 0
			if False: mtl_flags |= (1 << 0) # Transparency
			if True : mtl_flags |= (1 << 1) # Diffuse texture is inline
			if True : mtl_flags |= (1 << 2) # Normal texture is inline
			if True : mtl_flags |= (1 << 3) # Specular texture is inline
			if True : mtl_flags |= (1 << 4) # Emissive texture is inline

			# 8 bytes for each: Magic number, flags, diffuse ptr, normal ptr, specular ptr, emissive ptr
			# 4 bytes for each: specular exponent, padding
			# 8 bytes for each: string storage ptr, SS p+n+l
			header_offset = (8*6) + (4*2) + (8*3)

			string_storage = self.populate_string_storage()
			comment = "[[[        This material file was generated on Blender by the `fma_export.py` script.        ]]]".encode(FMA_STR_ENCODING)
			self.segm_ss = (header_offset + len(comment), len(string_storage.strings), len(string_storage.content))
			print("Comment: {:06x} to {:06x}".format(header_offset, header_offset + len(comment)))
			print("Strings: {:3} @ {:010x}".format(self.segm_ss[1], self.segm_ss[0]))

			def color_bytes(col): return (col << 32).to_bytes(8, "big")

			wr_buffer = magic_number(4) # Hardcoded version number
			write_bytes(wr_buffer, 8, mtl_flags, endian="big")
			diffuseValue = MaterialOrColor(color = material.diffuse_color)
			specularValue = MaterialOrColor(color = (*material.specular_color, 1.0))
			write_bytes(wr_buffer, 8, (0xCC2222FF << 32, 0x8080FFFF << 32, 0x222222FF << 32, 0x000000FF << 32), endian="big") # Hardcoded texture colors
			write_bytes(wr_buffer, 4, material.roughness) # Hardcoded specular exponent
			wr_buffer += b'pad4'
			write_bytes(wr_buffer, 8, self.segm_ss)
			wr_buffer += comment ## Comment (arbitrary data)
			file.write(wr_buffer) ; wr_buffer.clear()

			file.write(string_storage.content)

		def __init__(self, ctx, dst_dir):
			self.select_materials(ctx)
			for material_name in self.materials:
				with open(merge_dir_basename(dst_dir, material_name), 'wb') as file:
					self.write_material(file, self.materials[material_name])


	def execute(self, ctx: bpy.context):
		if not ctx.mode == "OBJECT": raise RuntimeError("Workspace must be in OBJECT mode, but is in {} mode".format(ctx.mode))
		if self.opt_gen_model:
			with open(self.filepath, 'wb') as file:
				FmaExporter.ModelExecution(file, ctx)
		if self.opt_gen_materials:
			FmaExporter.MaterialExecution(ctx, os.path.dirname(self.filepath))
		print("Done.")
		return { "FINISHED" }



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
	register()
	bpy.ops.fma.export('INVOKE_DEFAULT')
