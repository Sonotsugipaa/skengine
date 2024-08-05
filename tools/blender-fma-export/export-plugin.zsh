#!/usr/bin/zsh
src=${0:h}/src/
blender --command extension build --verbose --source-dir $src/ --output-dir=/tmp
