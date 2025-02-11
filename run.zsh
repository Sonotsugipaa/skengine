#!/bin/zsh

setopt errexit
setopt errreturn

: ${config:='Debug'}
: ${dstpath:='/tmp/game-engine-sketch'}

cmd=()
if   [[ "$1" = '-g' || "$1" = '--valgrind'   ]]; then cmd+='valgrind'; shift
elif [[ "$1" = '-l' || "$1" = '--leak-check' ]]; then cmd+='valgrind'; cmd+='--leak-check=full'; shift
elif [[ "$1" = '-L' || "$1" = '--leak-kinds' ]]; then cmd+='valgrind'; cmd+='--leak-check=full'; cmd+='--show-leak-kinds='"$2"; shift 2
fi

cd "$(dirname "$0")"
if [[ ! -v skip_build ]]; then
	config="$config" dstpath="$dstpath" ./build.zsh
	config="$config" dstpath="$dstpath" ./pack.zsh
fi


binpath="$dstpath/${config}-pack"

cmd+=($@)

if [[ -v WAYLAND_DISPLAY && ! -v SDL_VIDEODRIVER ]]; then export SDL_VIDEODRIVER=wayland; fi

cd $binpath
$cmd && echo 'Exit status 0' || echo 'Exit status '$?
