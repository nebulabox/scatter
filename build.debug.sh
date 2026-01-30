#!/usr/bin/env bash

mkdir -p build
pushd build
cmake -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles" ..
# cmake -DCMAKE_BUILD_TYPE=Debug -G "Sublime Text 2 - Unix Makefiles" ..
echo ">>>>>> start make project >>>>>"
make -j8

popd

echo ".......Running...."
# ./build/k
echo ".......End...."
