#!/bin/zsh -e

: ${config:='Debug'}
: ${dstpath:='/tmp/tick-regulator'}

run_cmd=()
if   [[ "$1" = '-g' || "$1" = '--valgrind'   ]]; then run_cmd+='valgrind'; shift
elif [[ "$1" = '-l' || "$1" = '--leak-check' ]]; then run_cmd+='valgrind'; run_cmd+='--leak-check=full'; shift
elif [[ "$1" = '-L' || "$1" = '--leak-kinds' ]]; then run_cmd+='valgrind'; run_cmd+='--leak-check=full'; run_cmd+='--show-leak-kinds='"$2"; shift 2
fi



set -e # Terminate the script when an error occurs

config="${config:-"Debug"}"
generator="${generator:-"Unix Makefiles"}"
srcpath="${srcpath:-"$(realpath ./)"}"

mkdir -p "$dstpath/$config"
cd "$dstpath/$config"

cmake -DCMAKE_BUILD_TYPE="$config" "$srcpath" -G "$generator"
cmake --build . --target tick-regulator-test --config "$config"



cd "$(dirname "$0")"
binpath="$dstpath/$config"

run_cmd+=('./tick-regulator-test' $@)

cd $binpath
$run_cmd && echo 'Exit status 0' || echo 'Exit status '$?
