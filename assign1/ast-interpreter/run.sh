#!/bin/bash
set -e

if [ -d "build" ]; then rm -rf build; fi
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DLLVM_DIR=/usr/local/llvm10ra ..
cmake --build .
cmake --install .


ast-interpreter "`cat ../../test/test01.c`"

cd ..

# clang -c -Xclang -ast-dump ../test/test04.c
