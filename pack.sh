#!/bin/zsh

setopt errexit
setopt errreturn
setopt nullglob

config="${config:-"Debug"}"
generator="${generator:-"Unix Makefiles"}"

dstpath="${dstpath:-/tmp/game-engine-sketch}"
pkgpath=$dstpath/${config}-pack
assetpath="${assetpath:-"$(realpath ./assets)"}"
binpath="$dstpath/$config/main"

if [[ ! -d $dstpath/$config ]]; then
	echo 'Build directory '$dstpath/${config}' does not exist' 1>&2
	exit 1
fi

mkdir -m755 -p $pkgpath
cd $dstpath/${config}


function copy-assets {
	if [[ -e "$binpath/assets" ]]; then
		if [[ ! -d "$binpath/assets" ]]; then
			echo \""$binpath/assets"\"' exists, but is not a directory' >&2
			exit 1
		fi
	else
		mkdir "$binpath/assets" || {
			echo 'Failed to create '\""$binpath/assets/"\" >&2
			exit 1
		}
	fi
	while read file; do
		cp -ut "$binpath/assets" "$file"
	done < <(
		find $assetpath \
			-name '*.gap' \
			-mindepth 1 -maxdepth 1
	)
}
copy-assets


# These files are relative to the destination directory, not the source
movefiles=(
	main/assets/
	main/*.spv
	main/sketch-sim
)

function check_files {
	local file
	for file ($movefiles); do
		if [[ ! -e $file ]]; then
			local err=
			echo "Missing file: $file" 1>&2
		fi
	done

	if [[ -v err ]]
	then return 1
	else return 0
	fi
}

if check_files; then
	mv -ft $pkgpath $movefiles
fi
