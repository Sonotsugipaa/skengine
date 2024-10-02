#!/bin/zsh
setopt errexit

config="${config:-${1:-"Debug"}}"
case "$config" in
	'Debug')          default_generator='Unix Makefiles';;
	'Release')        default_generator='Ninja' ;;
	'RelWithDebInfo') default_generator='Unix Makefiles';;
	'MinSizeRel')     default_generator='Ninja' ;;
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

projname="${0:A:h:t}"
generator="${generator:-$default_generator}"

dstpath="${dstpath:-${XDG_RUNTIME_DIR:-/tmp}/$projname}"
mkdir -m755 -p "$dstpath/$config"
srcpath="$(realpath --relative-to=$dstpath/$config src)"

cd "$dstpath/$config"

cmake -DCMAKE_BUILD_TYPE=$config -DSFLOG_BUILD_TEST=1 $srcpath -G $generator
cmake --build "." --config $config
cxx/sflog_test
