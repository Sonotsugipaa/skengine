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

cmake -DCMAKE_BUILD_TYPE="$config" "$srcpath" -G "$generator"
cmake --build . --config "$config"
