#!/bin/bash -e

TEST_ALL_PWD=$(pwd)

echo "Build and install module"
cd ${TEST_ALL_PWD}/../../module
./mk.sh clean
./mk.sh build
./mk.sh install

cd ${TEST_ALL_PWD}
