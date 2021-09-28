#!/bin/bash
set -e
mkdir -p build
mkdir -p build/ext/rabbitmq-c
INSTALL_DIR=${INSTALL_DIR:-"/opt/judge"}
pushd build/ext/rabbitmq-c 
    cmake -DCMAKE_INSTALL_PREFIX=$(pwd) -G"Ninja" ../../../ext/rabbitmq-c 
    cmake --build .
    cmake --build . --target install
popd

echo "Building judge-system"
pushd build
    cmake -DRabbitmqc_DIR=$(pwd)/ext/rabbitmq-c -DBoost_USE_STATIC_LIBS=ON -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR} -DCPR_BUILD_TESTS=OFF -DCPR_BUILD_TESTS_SSL=OFF -DBUILD_ENTRY=ON -DBUILD_SHARED_LIBS=OFF -G"Ninja" $@ .. 
    cmake --build .
popd

echo "Install path is ${INSTALL_DIR}"
echo "To install judge-system, run cmake --build build --target install"
