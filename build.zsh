#!/bin/zsh -e

setopt errexit

config="${config:-"Debug"}"
generator="${generator:-"Unix Makefiles"}"

srcpath="${srcpath:-"$(realpath ./src)"}"
dstpath="${dstpath:-/tmp/game-engine-sketch}"
assetpath="${assetpath:-"$(realpath ./assets)"}"

mkdir -m755 -p "$dstpath/$config"


./gen-shaders.sh


cd "$dstpath/$config"

if [[ $generator = "Ninja" ]]; then if tty; then export CMAKE_COLOR_DIAGNOSTICS=ON; fi; fi

cmake -DCMAKE_BUILD_TYPE="$config" "$srcpath" -G "$generator"
cmake --build . --config "$config"
