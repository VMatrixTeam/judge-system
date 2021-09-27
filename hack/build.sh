#!/bin/bash
mkdir -p build
echo "Building Judge System DEBUG VERSION"
pushd build && cmake -DCPR_BUILD_TESTS=OFF -DCPR_BUILD_TESTS_SSL=OFF -DBUILD_ENTRY=ON -G"Ninja" $@ .. && cmake --build . && popd
