#!/bin/zsh

setopt errexit
setopt errreturn
setopt nullglob

config="${config:-"Debug"}"
generator="${generator:-"Ninja"}"

dstpath="${dstpath:-/tmp/game-engine-sketch}"
pkgpath=$dstpath/${config}-pack
srcassetpath="${srcassetpath:-"$(realpath ./assets)"}"
binpath="$dstpath/$config"

if [[ ! -d $binpath ]]; then
	echo 'Build directory '$binpath' does not exist' 1>&2
	exit 1
fi

mkdir -m755 -p $pkgpath
cd $binpath


function create_pkgpath {
	if [[ -e "$pkgpath" ]]; then
		if [[ ! -d "$pkgpath" ]]; then
			echo \""$pkgpath"\"' exists, but is not a directory' >&2
			exit 1
		fi
	else
		mkdir "$pkgpath" || {
			echo 'Failed to create '\""$pkgpath/"\" >&2
			exit 1
		}
	fi
}
create_pkgpath


function copybin {
	# These files are relative to the binary directory, not the source
	local copyfiles=(
		"cxx/main/sketch-sim"
		"cxx/sneka3d/sneka3d"
	)

	function sync_asset_dir {
		local file
		local filename

		[[ -d $pkgpath/assets ]] || mkdir $pkgpath/assets

		# Remove out-of-date files
		while read file; do
			filename=${file:t}
			if [[ ! -e $srcassetpath/$filename ]]; then
				echo Deleting\ \"assets/$filename\"
				rm -r $file
			fi
		done < <(find $pkgpath/assets -mindepth 1)

		# Copy files
		while read file; do
			filename=${file:t}
			{
				[[ ! -e $pkgpath/assets/$filename ]] ||
				[[ $file -nt $pkgpath/assets/$filename ]]
			} && {
				echo Copying\ \"assets/$filename\"
				cp -rut $pkgpath/assets $file
			} || true
		done < <(find $srcassetpath -mindepth 1 -maxdepth 1)
	}

	function check_files {
		local file
		for file ($copyfiles); do
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
		cp -ft $pkgpath $copyfiles
		sync_asset_dir
	fi
}
copybin
