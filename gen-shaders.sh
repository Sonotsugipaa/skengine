#!/bin/zsh -e

setopt errexit

srcpath="realpath ./src/glsl"
dstpath="assets"
wdir="$(pwd)"


function gen_shaders {
	function gen_shader {
		local stage="$1"
		local name="$2"
		local name_no_ext="$(echo "$2" | sed -e 's|\.glsl||')"
		local relname="$(realpath --relative-to=$wdir "$name_no_ext")"
		local basename="$(basename "$name_no_ext")"
		local input="$name"
		local output="$dstpath/$basename.spv"
		if [[ ! -f $output || $input -nt $output ]]; then
			echo "Generating $stage shader  \"$relname.glsl\"  ->  \"$output\""
			glslc -O -x glsl -fshader-stage=$stage $input -o $output
		else
			echo "Skipping up-to-date $stage shader  \"$relname.glsl\""
		fi
	}

	local searchpath="$1"
	local shader

	while read shader; do
		gen_shader vertex $shader
	done < <(find $searchpath -name '*-vtx.glsl')

	while read shader; do
		gen_shader fragment $shader
	done < <(find $searchpath -name '*-frg.glsl')
}
gen_shaders "$wdir"
