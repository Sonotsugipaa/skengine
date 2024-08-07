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

function convert_images {
	local targets=(/tmp/fmamdl-targets/*.fmat.png)
	if [[ ${#targets} == 0 ]]; then return 0; fi
	/tmp/game-engine-sketch/${cfg}/cxx/png-to-fmat/fmat $targets
	mv -t /tmp/game-engine-sketch/${cfg}-pack/assets/ /tmp/fmamdl-targets/*.fmat.rgba8u
}

function link_assets {
	local targets=(/tmp/fmamdl-targets/*.fma /tmp/fmamdl-targets/*.rgba8u)
	if [[ ${#targets} -gt 0 ]]; then
		ln -srf -t /tmp/game-engine-sketch/${cfg}-pack/assets/ $targets
	fi
}

function test_assets {
	local cfg=${1:-Release};
	echo -ne "\e[H\e[2J\e[3J";
	config=$cfg ./build.zsh
	config=$cfg ./pack.zsh
	pushd src/cxx/vendored-libraries/fmamdl/
		./build-converter.zsh Release
		cfg=$cfg fmamdl_export
	popd
	cfg=$cfg convert_images
	cfg=$cfg link_assets
	config=$cfg skip_build= ./run.zsh
}

if [[ ! -d /tmp/fmamdl-targets ]]; then mkdir /tmp/fmamdl-targets; fi
test_assets $@ || exit $?
