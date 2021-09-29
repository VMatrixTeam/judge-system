#!/bin/bash
apt update
apt install $1 \
  pylint \
  pylint3 \
  gperf \
  bison \
  flex \
  libssl-dev \
  libcurl4-openssl-dev \
  curl \
  make \
  xz-utils \
  python3 \
  python3-pip \
  libboost-all-dev \
  cmake \
  libgtest-dev \
  libprotobuf-dev \
  ninja-build \
  git \
  pkg-config \
  gcc \
  g++
