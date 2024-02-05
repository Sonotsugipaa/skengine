#!/bin/zsh -e
setopt errexit

config="${config:-${1:-"Debug"}}"
case "${#}$config" in
	'1Debug')          default_generator='Unix Makefiles';;
	'1Release')        default_generator='Ninja' ;;
	'1RelWithDebInfo') default_generator='Unix Makefiles';;
	'1MinSizeRel')     default_generator='Ninja' ;;
	*)
		echo 'Error: the script must be called with one argument, or the "config"' >&2
		echo '       environment  variable set,  which  must  have  one  of the' >&2
		echo '       following values:' >&2
		echo '       - Debug' >&2
		echo '       - Release' >&2
		echo '       - RelWithDebInfo' >&2
		echo '       - MinSizeRel' >&2
		exit 1
		;;
esac

projname="${${$(realpath $0):h}:t}"
generator="${generator:-$default_generator}"

dstpath="${dstpath:-/tmp/$projname}"
mkdir -m755 -p "$dstpath/$config"
srcpath="$(realpath --relative-to=$dstpath/$config .)"

cd "$dstpath/$config"

cmake -DCMAKE_BUILD_TYPE=$config -DFMAMDL_ENABLE_TESTS=true $srcpath -G $generator
cmake --build "." --config $config
