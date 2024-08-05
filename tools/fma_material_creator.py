#### For your convenience, tweak and use the following snippet
#### to quickly stitch together material files.
#
#
#import fma_material_creator as fma
#mi = fma.MaterialInfo()
#mit = mi.textures
#mit.diffuse .set(False, "ground.0.fmat.rgba8u")
#mit.normal  .set(False, "ground.1.fmat.rgba8u")
#mit.specular.set(True , 0xFF)
#mit.emissive.set(True , 0xFF)
#mi.comment = "[Comment]"
#with open("/tmp/AAAA.mtl.fma", "wb") as file: fma.write_material(file, mi)
#
#



import struct


FMA_BYTE_ORDER_I = 'little'
FMA_BYTE_ORDER_F = 'little'
FMA_STR_ENCODING = 'utf-8'


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



class TextureEntry:
	def set(self, inline: bool, value):
		(self.inline, self.value) = (inline, value)

	def __init__(self, inline: bool, default_value):
		self.set(inline, default_value)


class TextureInfo:
	def __init__(self):
		self.diffuse  = TextureEntry(True, 0xCCCCCCFF)
		self.normal   = TextureEntry(True, 0x7F7FFFFF)
		self.specular = TextureEntry(True, 0x303030FF)
		self.emissive = TextureEntry(True, 0x010101FF)


class MaterialInfo:
	def __init__(self):
		self.transparent       = False
		self.textures          = TextureInfo()
		self.specular_exponent = 2.0
		self.comment           = ""



def pad_seq_8(seq):
	padding = bytearray("        ", "ascii")
	s_mod = len(seq) % 8
	seq += padding[:(8-s_mod)%8]
	return seq


def write_material(file, mtl_info):
	buffer = magic_number(4)

	# 8 bytes for each: Magic number, flags, diffuse ptr, normal ptr, specular ptr, emissive ptr
	# 4 bytes for each: specular exponent, padding
	# 8 bytes for each: string storage ptr, SS p+n+l
	header_offset = (8*6) + (4*2) + (8*3)

	string_storage = StringStorage()

	flags = 0
	mtl_textures = mtl_info.textures
	if mtl_info.transparent:     flags |= (1 << 0)
	if    mtl_textures.diffuse.inline:  flags |= (1 << 1)
	else: mtl_textures.diffuse.value  = string_storage.seek_string_ptr(mtl_textures.diffuse.value )
	if    mtl_textures.normal.inline:   flags |= (1 << 2)
	else: mtl_textures.normal.value   = string_storage.seek_string_ptr(mtl_textures.normal.value  )
	if    mtl_textures.specular.inline: flags |= (1 << 3)
	else: mtl_textures.specular.value = string_storage.seek_string_ptr(mtl_textures.specular.value)
	if    mtl_textures.emissive.inline: flags |= (1 << 4)
	else: mtl_textures.emissive.value = string_storage.seek_string_ptr(mtl_textures.emissive.value)
	write_bytes(buffer, 8, flags)

	comment = mtl_info.comment
	comment = comment.encode(FMA_STR_ENCODING)
	comment = pad_seq_8(comment)
	segm_ss = (header_offset + len(comment), len(string_storage.strings), len(string_storage.content))
	print("Comment: {:06x} to {:06x}".format(header_offset, header_offset + len(comment)))
	print("Strings: {:3} @ {:010x}".format(segm_ss[1], segm_ss[0]))

	def color_bytes(col): return (col << 32).to_bytes(8, "big")
	def textr_bytes(txt): return txt.to_bytes(8, FMA_BYTE_ORDER_I)
	def entry_bytes(entry):
		if entry.inline: return color_bytes(entry.value)
		else:            return textr_bytes(entry.value)
	buffer = magic_number(4) # Hardcoded version number
	write_bytes(buffer, 8, flags, endian="big")
	buffer += entry_bytes(mtl_textures.diffuse)
	buffer += entry_bytes(mtl_textures.normal)
	buffer += entry_bytes(mtl_textures.specular)
	buffer += entry_bytes(mtl_textures.emissive)
	write_bytes(buffer, 4, mtl_info.specular_exponent)
	buffer += b'pad4'
	write_bytes(buffer, 8, segm_ss)
	buffer += comment ## Comment (arbitrary data)
	file.write(buffer) ; buffer.clear()

	file.write(string_storage.content)
