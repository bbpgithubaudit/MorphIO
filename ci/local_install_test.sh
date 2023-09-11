#!/usr/bin/env bash

# This file builds and installs MorphIO to a local directory. 

set -euxo pipefail

BUILD_DIR=build/local_install

if [ $# -ge 1 ]; then
  EXTRA_OPTIONS=$1
else
  EXTRA_OPTIONS=""
fi

rm -rf $BUILD_DIR
cmake -DCMAKE_INSTALL_PREFIX=${BUILD_DIR}/install \
      -DMorphIO_CXX_WARNINGS=ON \
      -G "${CMAKE_GENERATOR:-Unix Makefiles}" \
      -B ${BUILD_DIR} ${EXTRA_OPTIONS}
cmake --build ${BUILD_DIR} --parallel --target install
