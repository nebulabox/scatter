#!/usr/bin/env bash
rm -rf build

mkdir build
pushd build
cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ..

echo ">>>>>> start ninja project >>>>>"
make -j8

popd

