#!/bin/bash -e
#
# SPDX-License-Identifier: GPL-2.0+

TEST_ALL_PWD=$(pwd)

echo "Build and install module"
cd ${TEST_ALL_PWD}/../module
./mk.sh clean
./mk.sh build
./mk.sh install

cd ${TEST_ALL_PWD}
