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
config="$config" dstpath="$dstpath" ./build.sh
config="$config" dstpath="$dstpath" ./pack.sh


binpath="$dstpath/${config}-pack"

if [[ -n $1 ]]; then
	cp -t $binpath $1
	cmd+=('./sketch-sim' $1)
else
	cmd+=('./sketch-sim')
fi

if [[ -v WAYLAND_DISPLAY ]]; then export SDL_VIDEODRIVER=wayland; fi

cd $binpath
$cmd && echo 'Exit status 0' || echo 'Exit status '$?
