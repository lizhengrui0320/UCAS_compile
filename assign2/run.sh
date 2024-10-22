#!/bin/bash
set -e

if [ -d "build" ]; then rm -rf build; fi
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DLLVM_DIR=/usr/local/llvm10ra -DCMAKE_CXX_FLAGS="-std=c++14" ..
cmake --build .
cmake --install .
# llvmassignment 

cd ..
