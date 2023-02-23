#!/bin/zsh -e

setopt errexit

srcpath="${srcpath:-"$(realpath ./src)"}"
dstpath="${dstpath:-$srcpath/main}"
assetpath="${assetpath:-"$(realpath ./assets)"}"


function gen_shaders {
	function gen_shader {
		local name_no_ext="$(echo "$2" | sed -e 's|\.glsl||')"
		local relname="$(realpath --relative-to=$assetpath "$name_no_ext")"
		local basename="$(basename "$name_no_ext")"
		echo "Generating $1 shader  \"$relname.glsl\"  ->  \"$dstpath/$basename.spv\""
		glslc -O -x glsl -fshader-stage=$1 $name_no_ext.glsl -o $dstpath/$basename.spv
	}

	local shader

	while read shader; do
		gen_shader vertex $shader
	done < <(find $1 -name '*-vtx.glsl')

	while read shader; do
		gen_shader fragment $shader
	done < <(find $1 -name '*-frg.glsl')
}
gen_shaders "$assetpath"
