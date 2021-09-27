#!/bin/bash
mkdir -p build
mkdir -p build/rabbitmqc-build
pushd build/rabbitmqc-build && cmake -DCMAKE_INSTALL_PREFIX=$(pwd)/../librabbitmq -G"Ninja" ../../ext/rabbitmq-c && cmake --build . && ninja install && popd
rm -rf build/rabbitmqc-build
echo "Building Judge System DEBUG VERSION"
pushd build && cmake -DRabbitmqc_DIR=$(pwd)/librabbitmq -DCPR_BUILD_TESTS=OFF -DCPR_BUILD_TESTS_SSL=OFF -DBUILD_ENTRY=ON -G"Ninja" $@ .. && cmake --build . && popd