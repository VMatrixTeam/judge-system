#!/bin/bash
mkdir -p build
mkdir -p build/ext/rabbitmq-c
pushd build/ext/rabbitmq-c && cmake -DCMAKE_INSTALL_PREFIX=$(pwd) -G"Ninja" ../../../ext/rabbitmq-c && cmake --build . && ninja install && popd
echo "Building Judge System DEBUG VERSION"
pushd build && cmake -DRabbitmqc_DIR=$(pwd)/ext/rabbitmq-c -DCPR_BUILD_TESTS=OFF -DCPR_BUILD_TESTS_SSL=OFF -DBUILD_ENTRY=ON -G"Ninja" $@ .. && cmake --build . && popd
