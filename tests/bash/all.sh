#!/bin/bash -e

TEST_ALL_PWD=$(pwd)
echo "Current directory ${TEST_ALL_PWD}"
echo "Complete and install module"
cd ${TEST_ALL_PWD}/../../module
./mk.sh build
./mk.sh install
cd ${TEST_ALL_PWD}

echo "Current directory ${TEST_ALL_PWD}"

echo "Execute all tests"
echo "***"

modprobe blk-snap
sleep 2s

for SCRIPT in $(ls ????-*.sh)
do
	. ${SCRIPT}
done

echo 0 > /sys/kernel/livepatch/blk_snap/enabled
sleep 2s
modprobe -r blk-snap

echo "***"
echo "Complete"
