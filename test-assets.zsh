#!/bin/zsh
setopt err_return
setopt null_glob

function fmamdl_export {
	local targets=(/tmp/fmamdl-targets/*.obj)
	if [[ ${#targets} == 0 ]]; then return 0; fi
	local target
	for target ($targets); do
		target="$(sed -e 's/\.obj$//' <<< "${target}")"
		echo "Converting model \"${target}\" to \"/tmp/game-engine-sketch/${cfg}-pack/assets/${target:t}.fma\""
		/tmp/fmamdl/Release/conv/fmaconv $target.obj /tmp/game-engine-sketch/${cfg}-pack/assets/${target:t}.fma
	done || exit 1
}

function convert_image {
	local target="$(sed -e 's/\.fmat\.png$//' <<< "$1")"
	echo "Converting image \"${target}\" to \"${target}.fmat.rgba8u\""
	convert $1 -depth 8 ${target}.fmat.rgba
	cat ${target}.size ${target}.fmat.rgba >${target}.fmat.rgba8u
	rm ${target}.fmat.rgba
}

function convert_images {
	local targets=(/tmp/fmamdl-targets/*.fmat.png)
	if [[ ${#targets} == 0 ]]; then return 0; fi
	local target
	for target ($targets); do
		target="$(sed -e 's/\.png$//' <<< "${target}")"
		convert_image $target.png
		mv -t /tmp/game-engine-sketch/${cfg}-pack/assets/ $target.rgba8u
	done
}

function test_assets {
	local cfg=${1:-Release};
	echo -ne "\e[H\e[2J\e[3J";
	config=$cfg ./build.zsh
	config=$cfg ./pack.zsh
	pushd src/cxx/third-party/fmamdl/
		./build-converter.zsh Release
		cfg=$cfg fmamdl_export
	popd
	cfg=$cfg convert_images
	config=$cfg skip_build= ./run.zsh
}

if [[ ! -d /tmp/fmamdl-targets ]]; then mkdir /tmp/fmamdl-targets; fi
test_assets $@ || exit $?
