#!/bin/bash -e

./build_and_install.sh

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
